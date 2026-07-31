// Tests written from what smtp.h promises, not from what the code does.
//
// This is the approach that found every defect in the other three libraries:
// each one sat on a path the documentation describes, and CI was green
// through all of them.
#include "smtp_server.h"

#include <rude/smtp.h>

#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>

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

	// smtp.h: "The connection is owned by this object. Copying it would give
	// two objects the same connection and close it twice, so copying is
	// disallowed rather than left to fail at runtime."
	//
	// Before 2.0.0 both SMTP and SMTPImpl were copyable while holding a raw
	// owning rude::Socket*, so a copy double-freed it.
	static_assert(!std::is_copy_constructible<rude::SMTP>::value,
				  "rude::SMTP must not be copy constructible");
	static_assert(!std::is_copy_assignable<rude::SMTP>::value,
				  "rude::SMTP must not be copy assignable");

	// "Returns a description of the last error" - never null, even before
	// anything has gone wrong.
	{
		rude::SMTP smtp;
		CHECK(smtp.getError() != 0);
		CHECK(smtp.getResponse() != 0);

		// "or 0 if no reply has been read"
		CHECK(smtp.getResponseCode() == 0);
	}

	// "Returns the library version" - never null, never empty.
	{
		CHECK(rude::SMTP::version() != 0);
		CHECK(std::strlen(rude::SMTP::version()) > 0);
	}

	// connect() is documented to return false on a bad address or port
	// rather than attempting the connection.
	{
		rude::SMTP smtp;
		CHECK(!smtp.connect(0, 25));
		CHECK(std::strlen(smtp.getError()) > 0);

		CHECK(!smtp.connect("127.0.0.1", 0));
		CHECK(std::strlen(smtp.getError()) > 0);

		CHECK(!smtp.connect("127.0.0.1", -1));
		CHECK(std::strlen(smtp.getError()) > 0);
	}

	// "addRecipient() may be called more than once, before sendData()."
	{
		TestSmtpServer server;
		server.addReply("250 Hello\r\n");
		server.addReply("250 Sender ok\r\n");
		server.addReply("250 Recipient 1 ok\r\n");
		server.addReply("250 Recipient 2 ok\r\n");
		server.addReply("250 Recipient 3 ok\r\n");
		server.addReply("354 Go ahead\r\n");
		server.addReply("250 Queued\r\n");
		server.addReply("221 Bye\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		rude::SMTP smtp;
		CHECK(smtp.connect("127.0.0.1", server.port()));
		CHECK(smtp.sayHelo("client.example.com"));
		CHECK(smtp.sayFrom("me@example.com"));
		CHECK(smtp.addRecipient("one@example.com"));
		CHECK(smtp.addRecipient("two@example.com"));
		CHECK(smtp.addRecipient("three@example.com"));
		CHECK(smtp.sendData("Subject: hi\r\n\r\nbody\r\n"));
		CHECK(smtp.disconnect());
		t.join();

		CHECK(server.sawLine("RCPT TO:<one@example.com>"));
		CHECK(server.sawLine("RCPT TO:<two@example.com>"));
		CHECK(server.sawLine("RCPT TO:<three@example.com>"));
		std::printf("three recipients all sent\n");
	}

	// "Sends QUIT and closes the connection. The connection is closed
	// whatever the server replies." Calling disconnect() twice, or on an
	// object that never connected, must not misbehave.
	{
		rude::SMTP smtp;
		CHECK(smtp.disconnect()); // never connected: nothing to do
	}
	{
		TestSmtpServer server;
		server.addReply("221 Bye\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		rude::SMTP smtp;
		CHECK(smtp.connect("127.0.0.1", server.port()));
		CHECK(smtp.disconnect());
		CHECK(smtp.disconnect()); // already closed
		t.join();
		std::printf("disconnect is safe to repeat\n");
	}

	net_cleanup();

	if(failures)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("contract OK\n");
	return 0;
}
