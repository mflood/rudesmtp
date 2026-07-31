// Reply handling: the parts of SMTP a happy-path test never reaches.
//
// Before 2.0.0 a reply was judged by comparing character zero to a digit, and
// exactly one line was read per command. That made three things wrong at once,
// and all three are checked here:
//
//   1. Multiline replies desynchronized the session. RFC 5321 section 4.2
//      lets a reply span lines, each carrying the code with '-' after it on
//      every line but the last. Only the first was read, so the rest sat in
//      the buffer and were consumed as the reply to the NEXT command, putting
//      every later exchange one reply behind.
//
//   2. 4xx and 5xx were indistinguishable. 421 (shutting down, retry later)
//      and 550 (rejected, never retry) both produced a bare false. That is
//      the difference between requeueing a message and bouncing it, and the
//      caller could not tell.
//
//   3. Codes within a class were indistinguishable: 250 and 251 alike, and a
//      354 where a 250 belonged.
//
// Plus the hang: no timeout was ever set, and rudesocket blocks by default,
// so a server that accepted the connection and then said nothing pinned the
// calling thread with no way for the caller to intervene.

#include "smtp_server.h"

#include <rude/smtp.h>

#include <chrono>
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

int main()
{
	// Unbuffered, so the trace survives if the process dies from a signal
	// rather than returning -- which is exactly what a SIGPIPE from writing
	// to a departed peer looks like, and losing the output makes it much
	// harder to see where.
	setvbuf(stdout, 0, _IONBF, 0);

	net_init();

	// Each block below scopes its client and joins the server thread
	// afterwards. The server sits in recv() until the client closes, and the
	// client closes when it is destroyed, so joining while the client is
	// still alive deadlocks. That the destructor closes at all is new in
	// rudesocket 1.7.1 -- writing these tests is what turned up the leak.

	// ---- 1. A multiline greeting must be consumed whole ------------------
	//
	// If continuation lines are left behind, the reply to HELO is really the
	// tail of the greeting, and everything after is off by one.
	{
		TestSmtpServer server;
		server.setGreeting("220-test.example.com ESMTP\r\n"
						   "220-still the greeting\r\n"
						   "220 ready\r\n");
		server.addReply("250 Hello\r\n");	  // HELO
		server.addReply("250 Sender ok\r\n"); // MAIL FROM
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.getResponseCode() == 220);

			// If the greeting had leaked, this would read "220-still the
			// greeting" and see 220 rather than 250.
			CHECK(smtp.sayHelo("client.example.com"));
			CHECK(smtp.getResponseCode() == 250);

			CHECK(smtp.sayFrom("me@example.com"));
			CHECK(smtp.getResponseCode() == 250);

			std::printf("multiline greeting: HELO saw %d, MAIL FROM saw %d\n",
						250, smtp.getResponseCode());
		}
		t.join();
	}

	// ---- 2. A multiline reply to a command, likewise ---------------------
	{
		TestSmtpServer server;
		server.addReply("250-first line\r\n"
						"250-second line\r\n"
						"250 last line\r\n"); // HELO
		server.addReply("250 Sender ok\r\n"); // MAIL FROM
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayHelo("client.example.com"));
			CHECK(smtp.getResponseCode() == 250);

			// All three lines are reported, not just the first.
			const std::string text = smtp.getResponse();
			CHECK(text.find("first line") != std::string::npos);
			CHECK(text.find("second line") != std::string::npos);
			CHECK(text.find("last line") != std::string::npos);

			CHECK(smtp.sayFrom("me@example.com"));
			CHECK(smtp.getResponseCode() == 250);
			std::printf("multiline reply: kept %zu bytes of reply text\n", text.size());
		}
		t.join();
	}

	// ---- 3. Transient vs permanent ---------------------------------------
	{
		struct Case
		{
			const char *reply;
			int code;
			const char *meaning;
		};
		const Case cases[] = {
			{"421 Service not available, closing\r\n", 421, "transient"},
			{"450 Mailbox busy\r\n", 450, "transient"},
			{"550 No such user\r\n", 550, "permanent"},
			{"552 Storage exceeded\r\n", 552, "permanent"},
		};

		for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		{
			TestSmtpServer server;
			server.addReply("250 Hello\r\n");
			server.addReply(cases[i].reply); // MAIL FROM
			std::thread t(&TestSmtpServer::run, &server);

			{
				rude::SMTP smtp;
				CHECK(smtp.connect("127.0.0.1", server.port()));
				CHECK(smtp.sayHelo("client.example.com"));

				CHECK(!smtp.sayFrom("me@example.com"));
				CHECK(smtp.getResponseCode() == cases[i].code);

				// The caller can act on the class, which is the whole point.
				const bool transient = smtp.getResponseCode() / 100 == 4;
				CHECK(transient == (std::strcmp(cases[i].meaning, "transient") == 0));

				// And the server's own words survive into the error.
				CHECK(std::strstr(smtp.getError(), "MAIL FROM") != 0);

				std::printf("%d -> %s, error: %s\n", cases[i].code,
							transient ? "retry" : "bounce", smtp.getError());
			}
			t.join();
		}
	}

	// ---- 4. Codes within a class are distinguished -----------------------
	//
	// 251 is an acceptance (not local, will forward). 551 is not. Both begin
	// with the same digit as codes on the other side of that line.
	{
		TestSmtpServer server;
		server.addReply("250 Hello\r\n");
		server.addReply("250 Sender ok\r\n");
		server.addReply("251 User not local; will forward\r\n"); // RCPT TO
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayHelo("client.example.com"));
			CHECK(smtp.sayFrom("me@example.com"));
			CHECK(smtp.addRecipient("you@elsewhere.example.com"));
			CHECK(smtp.getResponseCode() == 251);
			std::printf("251 accepted as a forward\n");
		}
		t.join();
	}
	{
		TestSmtpServer server;
		server.addReply("250 Hello\r\n");
		server.addReply("250 Sender ok\r\n");
		server.addReply("551 User not local; try elsewhere\r\n"); // RCPT TO
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayHelo("client.example.com"));
			CHECK(smtp.sayFrom("me@example.com"));
			CHECK(!smtp.addRecipient("you@elsewhere.example.com"));
			CHECK(smtp.getResponseCode() == 551);
			std::printf("551 refused\n");
		}
		t.join();
	}

	// ---- 5. A greeting that is not 220 is not an SMTP server -------------
	{
		TestSmtpServer server;
		server.setGreeting("+OK POP3 server ready\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(!smtp.connect("127.0.0.1", server.port()));
			CHECK(std::strlen(smtp.getError()) > 0);
			std::printf("non-SMTP greeting refused: %s\n", smtp.getError());
		}
		t.join();
	}

	// ---- 6. A silent server must not hang the caller ---------------------
	{
		TestSmtpServer server;
		server.setGreeting(""); // accept, then say nothing
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.setTimeout(2);

			const std::chrono::steady_clock::time_point start =
				std::chrono::steady_clock::now();
			const bool connected = smtp.connect("127.0.0.1", server.port());
			const long ms = (long) std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::steady_clock::now() - start)
								.count();

			CHECK(!connected);
			CHECK(ms >= 1500); // it waited
			CHECK(ms < 20000); // but gave up
			std::printf("silent server abandoned after %ld ms: %s\n", ms, smtp.getError());
		}
		t.join();
	}

	// ---- 7. A connection dropped mid-conversation is an error, not a hang -
	{
		TestSmtpServer server;
		server.dropAfterGreeting();
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.setTimeout(5);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(!smtp.sayHelo("client.example.com"));
			CHECK(std::strlen(smtp.getError()) > 0);
			std::printf("dropped connection reported: %s\n", smtp.getError());
		}
		t.join();
	}

	net_cleanup();

	if(failures)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("protocol OK\n");
	return 0;
}
