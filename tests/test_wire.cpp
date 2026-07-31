// What actually goes onto the wire.
//
// These assert on the bytes the server received, not on what the client
// returned. A client can return true having sent something a stricter server
// would have rejected, and that is precisely how the defects below survived:
// they only show up against a server that enforces the grammar, and the
// library was only ever pointed at ones that did not.
//
//   - "MAIL FROM: <addr>" carries a space after the colon. RFC 5321 section
//     4.1.1.2 has no space there. Most servers tolerate it; some do not.
//
//   - Angle brackets were the caller's problem. An unbracketed address is
//     not the documented grammar either, so whether a program worked came
//     down to whose mail server it was talking to.
//
//   - Dot-stuffing (RFC 5321 section 4.5.2) missed the first line of the
//     message. The old code doubled a '.' that followed CR, LF or FF, and
//     the start of the message follows nothing -- so a message beginning
//     "." had that character silently eaten by the receiving server.
//
//   - The end-of-data marker was sent as "\r\n.\r\n" unconditionally, so a
//     message already ending in CRLF -- which is every correctly assembled
//     message -- gained a trailing blank line.

#include "smtp_server.h"

#include <rude/smtp.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

static int failures = 0;

#define CHECK(cond)                                                              \
	do                                                                           \
	{                                                                            \
		if(!(cond))                                                              \
		{                                                                        \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++failures;                                                          \
		}                                                                        \
	} while(0)

// Runs a full send and hands back everything the server saw.
static std::vector<std::string> deliver(const char *from, const char *to,
										const char *message)
{
	TestSmtpServer server;
	server.addReply("250 Hello\r\n");		 // HELO
	server.addReply("250 Sender ok\r\n");	 // MAIL FROM
	server.addReply("250 Recipient ok\r\n"); // RCPT TO
	server.addReply("354 Go ahead\r\n");	 // DATA
	server.addReply("250 Queued\r\n");		 // end of data
	server.addReply("221 Bye\r\n");			 // QUIT
	std::thread t(&TestSmtpServer::run, &server);

	{
		rude::SMTP smtp;
		if(!smtp.connect("127.0.0.1", server.port()))
		{
			std::fprintf(stderr, "connect failed: %s\n", smtp.getError());
		}
		else
		{
			smtp.sayHelo("client.example.com");
			smtp.sayFrom(from);
			smtp.addRecipient(to);
			smtp.sendData(message);
			smtp.disconnect();
		}
	}
	t.join();
	return server.received();
}

static bool contains(const std::vector<std::string> &lines, const std::string &want)
{
	for(size_t i = 0; i < lines.size(); i++)
	{
		if(lines[i] == want)
		{
			return true;
		}
	}
	return false;
}

static void dump(const char *label, const std::vector<std::string> &lines)
{
	std::printf("%s:\n", label);
	for(size_t i = 0; i < lines.size(); i++)
	{
		std::printf("  C: %s\n", lines[i].c_str());
	}
}

int main()
{
	// Unbuffered, so the trace survives if the process dies from a signal
	// rather than returning -- which is exactly what a SIGPIPE from writing
	// to a departed peer looks like, and losing the output makes it much
	// harder to see where.
	setvbuf(stdout, 0, _IONBF, 0);

	net_init();

	// ---- Command grammar -------------------------------------------------
	{
		const std::vector<std::string> sent =
			deliver("me@example.com", "you@example.com",
					"Subject: hi\r\n\r\nbody\r\n");
		dump("bare addresses", sent);

		// Bracketed, and no space after the colon.
		CHECK(contains(sent, "MAIL FROM:<me@example.com>"));
		CHECK(contains(sent, "RCPT TO:<you@example.com>"));

		// Specifically not the old spellings.
		CHECK(!contains(sent, "MAIL FROM: me@example.com"));
		CHECK(!contains(sent, "MAIL FROM: <me@example.com>"));
	}

	// Addresses the caller already bracketed must not end up double-wrapped.
	{
		const std::vector<std::string> sent =
			deliver("<me@example.com>", "<you@example.com>", "body\r\n");
		CHECK(contains(sent, "MAIL FROM:<me@example.com>"));
		CHECK(contains(sent, "RCPT TO:<you@example.com>"));
		CHECK(!contains(sent, "MAIL FROM:<<me@example.com>>"));
		std::printf("pre-bracketed addresses left alone\n");
	}

	// The null reverse-path, which is how a bounce is sent.
	{
		const std::vector<std::string> sent =
			deliver("", "you@example.com", "body\r\n");
		CHECK(contains(sent, "MAIL FROM:<>"));
		std::printf("empty sender becomes the null reverse-path\n");
	}

	// ---- Dot-stuffing ----------------------------------------------------

	// A message whose FIRST line starts with '.'. This is the case the old
	// implementation could not see.
	{
		const std::vector<std::string> sent =
			deliver("me@example.com", "you@example.com",
					".leading dot\r\nsecond line\r\n");
		dump("message starting with a dot", sent);

		// Stuffed, so the server strips one back off and gets the original.
		CHECK(contains(sent, "..leading dot"));
		CHECK(!contains(sent, ".leading dot"));
	}

	// A '.' at the start of a later line, which it did handle.
	{
		const std::vector<std::string> sent =
			deliver("me@example.com", "you@example.com",
					"first line\r\n.second line\r\n");
		CHECK(contains(sent, "..second line"));
		std::printf("dot on a later line stuffed too\n");
	}

	// A lone "." line must not be able to end the message early.
	{
		const std::vector<std::string> sent =
			deliver("me@example.com", "you@example.com",
					"before\r\n.\r\nafter\r\n");
		CHECK(contains(sent, ".."));
		CHECK(contains(sent, "after")); // survived; the message did not stop
		std::printf("a lone dot line cannot terminate the message early\n");
	}

	// ---- End-of-data marker ---------------------------------------------

	// A message already ending in CRLF must not gain a blank line.
	{
		const std::vector<std::string> sent =
			deliver("me@example.com", "you@example.com",
					"Subject: hi\r\n\r\nbody\r\n");

		// Find the terminator and look at what came directly before it.
		size_t dot = sent.size();
		for(size_t i = 0; i < sent.size(); i++)
		{
			if(sent[i] == ".")
			{
				dot = i;
			}
		}
		CHECK(dot != sent.size());
		if(dot > 0)
		{
			std::printf("line before the terminator: \"%s\"\n", sent[dot - 1].c_str());
			CHECK(sent[dot - 1] == "body"); // not an empty line
		}
	}

	// A message NOT ending in CRLF still needs one before the terminator,
	// or the marker would be appended to the last line of the body.
	{
		const std::vector<std::string> sent =
			deliver("me@example.com", "you@example.com",
					"Subject: hi\r\n\r\nno trailing newline");

		CHECK(contains(sent, "no trailing newline"));
		CHECK(!contains(sent, "no trailing newline."));
		std::printf("terminator kept off the last body line\n");
	}

	net_cleanup();

	if(failures)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("wire format OK\n");
	return 0;
}
