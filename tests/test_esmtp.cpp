// EHLO and capability parsing.
//
// This is what had to be fixed before AUTH or TLS could exist at all: EHLO's
// reply is multiline by design (RFC 5321 section 4.1.1.1 gives an example
// with half a dozen lines), and the reply reader that shipped in 2.0.0 could
// not read a multiline reply without desynchronizing the session. That fix
// is what test_protocol.cpp covers; this file covers what sits on top of it -
// turning the EHLO reply into a queryable capability list.

#include "smtp_server.h"

#include <rude/smtp.h>

#include <cstdio>
#include <cstring>
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
	setvbuf(stdout, 0, _IONBF, 0);
	net_init();

	// ---- A realistic EHLO reply is parsed into extensions and mechanisms -
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250-SIZE 35882577\r\n"
						"250-8BITMIME\r\n"
						"250-STARTTLS\r\n"
						"250 AUTH LOGIN PLAIN\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.getResponseCode() == 250);

			CHECK(smtp.supportsExtension("SIZE"));
			CHECK(smtp.supportsExtension("8BITMIME"));
			CHECK(smtp.supportsExtension("STARTTLS"));
			CHECK(smtp.supportsExtension("AUTH"));

			// Case-insensitive, since the wire form is not.
			CHECK(smtp.supportsExtension("starttls"));

			// Something the server did not mention.
			CHECK(!smtp.supportsExtension("PIPELINING"));
			CHECK(!smtp.supportsExtension("VRFY"));

			CHECK(smtp.supportsAuth("LOGIN"));
			CHECK(smtp.supportsAuth("PLAIN"));
			CHECK(smtp.supportsAuth("login")); // case-insensitive
			CHECK(!smtp.supportsAuth("CRAM-MD5"));
			CHECK(!smtp.supportsAuth("GSSAPI"));

			std::printf("parsed: SIZE, 8BITMIME, STARTTLS, AUTH LOGIN PLAIN\n");
		}
		t.join();
	}

	// ---- The older "AUTH=" spelling is understood too --------------------
	//
	// RFC 4954 uses "AUTH LOGIN PLAIN"; some deployed servers still answer
	// with the pre-standardization "AUTH=LOGIN PLAIN" from early Exchange
	// and Sendmail patches. Both are real traffic.
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH=PLAIN\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.supportsAuth("PLAIN"));
			std::printf("AUTH=PLAIN (legacy spelling) parsed\n");
		}
		t.join();
	}

	// ---- No extensions offered is not the same as EHLO never running -----
	{
		rude::SMTP smtp;
		CHECK(!smtp.supportsExtension("STARTTLS"));
		CHECK(!smtp.supportsAuth("PLAIN"));
		std::printf("before EHLO, nothing is supported\n");
	}
	{
		TestSmtpServer server;
		server.addReply("250 mail.example.com Hello\r\n"); // no extensions
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(!smtp.supportsExtension("STARTTLS"));
			CHECK(!smtp.supportsAuth("PLAIN"));
			std::printf("EHLO with a bare Hello: nothing supported, as it should be\n");
		}
		t.join();
	}

	// ---- A pre-ESMTP server answers 500/502 to EHLO -----------------------
	//
	// Essentially unseen in 2026, but a real and legal response: RFC 1869
	// grandfathers HELO-only servers, and they answer EHLO with an error
	// rather than silently ignoring it.
	{
		TestSmtpServer server;
		server.addReply("500 Command not recognized\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(!smtp.sayEhlo("client.example.com"));
			CHECK(smtp.getResponseCode() == 500);
			CHECK(std::strlen(smtp.getError()) > 0);

			// The caller's fallback path.
			CHECK(!smtp.supportsExtension("STARTTLS"));
			std::printf("pre-ESMTP server: EHLO rejected as documented, fallback available\n");
		}
		t.join();
	}

	// ---- A fresh EHLO reply replaces the old capability list, not merges -
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250 STARTTLS\r\n");			   // first EHLO
		server.addReply("250 mail.example.com Hello\r\n"); // second EHLO: bare
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.supportsExtension("STARTTLS"));

			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(!smtp.supportsExtension("STARTTLS"));
			std::printf("second EHLO replaced the capability list rather than adding to it\n");
		}
		t.join();
	}

	net_cleanup();

	if(failures)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("esmtp OK\n");
	return 0;
}
