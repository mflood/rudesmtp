// Real TLS: connectSSL() (implicit TLS, port 465) and startTLS() (RFC 3207,
// port 587), against an in-process server that actually performs the
// handshake -- see smtp_tls_server.h for why a scripted plaintext server
// cannot stand in for this.
//
// Needs the openssl CLI to generate a self-signed certificate; skips cleanly
// without it, matching the pattern rudesocket's own SSL tests use.

#include "smtp_tls_server.h"

#include <rude/smtp.h>

#include <cstdio>
#include <cstdlib>
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

	if(std::system("openssl version > /dev/null 2>&1") != 0)
	{
		std::printf("SKIP: openssl CLI not available\n");
		return 0;
	}
	if(std::system("openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem"
				   " -out cert.pem -days 1 -subj /CN=127.0.0.1"
				   " > /dev/null 2>&1") != 0)
	{
		std::printf("SKIP: could not generate a test certificate\n");
		return 0;
	}

	net_init();
	SSL_library_init();

	// ---- Implicit TLS: connectSSL() -----------------------------------
	{
		SmtpTlsServer server(/* immediateTls */ true);
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH PLAIN\r\n");
		server.addReply("235 Authentication successful\r\n");
		server.addReply("221 Bye\r\n");
		std::thread t(&SmtpTlsServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.setSSLVerify(false); // self-signed cert
			CHECK(smtp.connectSSL("127.0.0.1", server.port()));
			CHECK(smtp.isSecure());
			CHECK(smtp.getResponseCode() == 220);

			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.authenticate("user@example.com", "secret"));
			CHECK(smtp.disconnect());
		}
		t.join();

		CHECK(server.handshook());
		std::printf("connectSSL(): greeting, EHLO and AUTH all inside TLS\n");
	}

	// ---- Implicit TLS verifies the certificate by default ---------------
	{
		SmtpTlsServer server(/* immediateTls */ true);
		std::thread t(&SmtpTlsServer::run, &server);

		{
			rude::SMTP smtp; // verification ON, the default
			CHECK(!smtp.connectSSL("127.0.0.1", server.port()));
			CHECK(std::strlen(smtp.getError()) > 0);
			std::printf("connectSSL() with verification on rejects the "
						"self-signed cert: %s\n",
						smtp.getError());
		}
		t.join();
	}

	// ---- STARTTLS: EHLO in the clear, upgrade, EHLO again over TLS ------
	{
		SmtpTlsServer server(/* immediateTls */ false);
		server.addReply("250-mail.example.com Hello\r\n"
						"250-STARTTLS\r\n"
						"250 AUTH PLAIN\r\n"); // first EHLO: in the clear
		server.addReply("220 Go ahead\r\n");   // STARTTLS ack: in the clear
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH PLAIN\r\n");				  // second EHLO: over TLS
		server.addReply("235 Authentication successful\r\n"); // AUTH: over TLS
		server.addReply("221 Bye\r\n");
		std::thread t(&SmtpTlsServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.setSSLVerify(false);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(!smtp.isSecure());

			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.supportsExtension("STARTTLS"));

			CHECK(smtp.startTLS());
			CHECK(smtp.isSecure());

			// RFC 3207: capabilities from the pre-TLS EHLO must be
			// discarded. This is enforced structurally, not just by
			// convention -- authenticate() requires sayEhlo() to have run
			// with d_esmtp true, and startTLS() clears that flag.
			CHECK(!smtp.supportsExtension("STARTTLS"));
			CHECK(!smtp.authenticate("user@example.com", "secret"));

			// The second EHLO, run inside TLS, restores it.
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.authenticate("user@example.com", "secret"));

			CHECK(smtp.disconnect());
		}
		t.join();

		CHECK(server.handshook());

		// The pre-upgrade exchange happened in the clear: EHLO and STARTTLS
		// are both among the lines the server received before the handshake.
		// AUTH is not, because it only ran after the upgrade -- if the
		// client had sent it before, or if the upgrade never actually took
		// hold and everything after it silently stayed in the clear, it
		// would show up here too.
		CHECK(server.sawInClearWithPrefix("EHLO"));
		CHECK(server.sawInClearWithPrefix("STARTTLS"));
		CHECK(!server.sawInClearWithPrefix("AUTH"));
		std::printf("startTLS(): capabilities reset after upgrade, "
					"AUTH only reachable after a fresh EHLO over TLS\n");
	}

	// ---- STARTTLS refused when the server does not advertise it ---------
	//
	// This does not need a real handshake: the refusal happens before any
	// TLS record is sent, so a plain (non-upgrading) server can drive it.
	// Reusing SmtpTlsServer with immediateTls=false and no STARTTLS in the
	// capability list is sufficient -- it just never gets asked to upgrade.
	{
		SmtpTlsServer server(/* immediateTls */ false);
		server.addReply("250 mail.example.com Hello\r\n"); // no STARTTLS
		server.addReply("221 Bye\r\n");
		std::thread t(&SmtpTlsServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(!smtp.supportsExtension("STARTTLS"));
			CHECK(!smtp.startTLS());
			CHECK(std::strlen(smtp.getError()) > 0);
			CHECK(smtp.disconnect());
		}
		t.join();

		CHECK(!server.handshook());
		bool sentStartTls = false;
		for(size_t i = 0; i < server.received().size(); i++)
		{
			if(server.received()[i] == "STARTTLS")
			{
				sentStartTls = true;
			}
		}
		CHECK(!sentStartTls);
		std::printf("STARTTLS refused locally when not advertised; "
					"nothing sent, no handshake attempted\n");
	}

	// ---- A second startTLS() is refused, not nested ----------------------
	{
		SmtpTlsServer server(/* immediateTls */ false);
		server.addReply("250-mail.example.com Hello\r\n"
						"250 STARTTLS\r\n");
		server.addReply("220 Go ahead\r\n");
		server.addReply("221 Bye\r\n"); // over TLS, once startTLS() succeeds
		std::thread t(&SmtpTlsServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.setSSLVerify(false);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.startTLS());
			CHECK(!smtp.startTLS());
			CHECK(smtp.disconnect());
		}
		t.join();
		std::printf("a second startTLS() on the same connection is refused\n");
	}

	if(failures)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("tls OK\n");
	return 0;
}
