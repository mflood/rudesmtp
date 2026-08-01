// AUTH PLAIN / AUTH LOGIN (RFC 4954) and the plaintext-authentication gate.
//
// The base64 decoder here is deliberately independent of anything in
// smtpimpl.cpp: checking the client's encoder against itself would only prove
// it is consistent, not correct. Decoding what actually went over the wire
// and comparing it to the credentials that were passed in is what makes these
// checks mean something.

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

static int decodeChar(char c)
{
	if(c >= 'A' && c <= 'Z')
		return c - 'A';
	if(c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if(c >= '0' && c <= '9')
		return c - '0' + 52;
	if(c == '+')
		return 62;
	if(c == '/')
		return 63;
	return -1;
}

static std::string base64Decode(const std::string &input)
{
	std::string out;
	int buf = 0, bits = 0;
	for(size_t i = 0; i < input.size(); i++)
	{
		if(input[i] == '=')
		{
			break;
		}
		const int v = decodeChar(input[i]);
		if(v < 0)
		{
			continue;
		}
		buf = (buf << 6) | v;
		bits += 6;
		if(bits >= 8)
		{
			bits -= 8;
			out += (char) ((buf >> bits) & 0xFF);
		}
	}
	return out;
}

int main()
{
	setvbuf(stdout, 0, _IONBF, 0);
	net_init();

	// ---- Refuses by default over an unencrypted connection ---------------
	//
	// This is the security gate, and it is a REFUSAL TO ASK, not "ask and
	// let the server reject it": nothing resembling AUTH should reach the
	// wire at all.
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH PLAIN LOGIN\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(!smtp.isSecure());

			CHECK(!smtp.authenticate("user@example.com", "secret"));
			CHECK(std::strlen(smtp.getError()) > 0);
			CHECK(std::strstr(smtp.getError(), "unencrypted") != 0);

			smtp.disconnect();
		}
		t.join();

		bool sentAuth = false;
		for(size_t i = 0; i < server.received().size(); i++)
		{
			if(server.received()[i].compare(0, 4, "AUTH") == 0)
			{
				sentAuth = true;
			}
		}
		CHECK(!sentAuth);
		std::printf("plaintext AUTH refused before anything reached the wire\n");
	}

	// ---- allowPlaintextAuth(true) unlocks it ------------------------------
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH PLAIN\r\n");
		server.addReply("235 Authentication successful\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.allowPlaintextAuth(true);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.authenticate("user@example.com", "secret"));
			smtp.disconnect();
		}
		t.join();
		std::printf("allowPlaintextAuth(true) permits it\n");
	}

	// ---- AUTH PLAIN wire format, decoded independently --------------------
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH PLAIN\r\n");
		server.addReply("235 Authentication successful\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.allowPlaintextAuth(true);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.authenticate("user@example.com", "secret"));
			smtp.disconnect();
		}
		t.join();

		// RFC 4954/4616: "AUTH PLAIN " followed by base64 of
		// authzid \0 authcid \0 password, with an empty authzid.
		std::string authLine;
		for(size_t i = 0; i < server.received().size(); i++)
		{
			if(server.received()[i].compare(0, 11, "AUTH PLAIN ") == 0)
			{
				authLine = server.received()[i];
			}
		}
		CHECK(!authLine.empty());

		const std::string decoded = base64Decode(authLine.substr(11));
		const std::string expected = std::string("\0user@example.com\0secret", 24);
		CHECK(decoded == expected);
		std::printf("AUTH PLAIN decoded to %zu bytes, matches authzid=\"\" "
					"authcid=\"user@example.com\" password=\"secret\"\n",
					decoded.size());
	}

	// ---- AUTH LOGIN wire format, decoded independently --------------------
	//
	// Only LOGIN offered, so PLAIN (preferred when both are present) cannot
	// be what ran.
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH LOGIN\r\n");
		server.addReply("334 VXNlcm5hbWU6\r\n"); // "Username:"
		server.addReply("334 UGFzc3dvcmQ6\r\n"); // "Password:"
		server.addReply("235 Authentication successful\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.allowPlaintextAuth(true);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.authenticate("user@example.com", "secret"));
			smtp.disconnect();
		}
		t.join();

		// Find "AUTH LOGIN" and decode the two lines that follow it, rather
		// than encoding the expected values and comparing strings -- that
		// would just be checking the encoder against itself a second time.
		int authAt = -1;
		const std::vector<std::string> &received = server.received();
		for(size_t i = 0; i < received.size(); i++)
		{
			if(received[i] == "AUTH LOGIN")
			{
				authAt = (int) i;
			}
		}
		CHECK(authAt >= 0);
		CHECK(authAt >= 0 && (size_t) authAt + 2 < received.size());
		if(authAt >= 0 && (size_t) authAt + 2 < received.size())
		{
			CHECK(base64Decode(received[authAt + 1]) == "user@example.com");
			CHECK(base64Decode(received[authAt + 2]) == "secret");
		}
		std::printf("AUTH LOGIN sent username and password as separate "
					"base64 lines, decoded independently\n");
	}

	// ---- PLAIN preferred when both are offered -----------------------------
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH PLAIN LOGIN\r\n");
		server.addReply("235 Authentication successful\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.allowPlaintextAuth(true);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.authenticate("user@example.com", "secret"));
			smtp.disconnect();
		}
		t.join();

		bool usedPlain = false;
		bool usedLogin = false;
		for(size_t i = 0; i < server.received().size(); i++)
		{
			if(server.received()[i].compare(0, 11, "AUTH PLAIN ") == 0)
			{
				usedPlain = true;
			}
			if(server.received()[i] == "AUTH LOGIN")
			{
				usedLogin = true;
			}
		}
		CHECK(usedPlain);
		CHECK(!usedLogin);
		std::printf("PLAIN used when both PLAIN and LOGIN are offered\n");
	}

	// ---- PLAIN offered but rejected falls through to LOGIN -----------------
	//
	// Some servers advertise PLAIN and only really accept LOGIN.
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH PLAIN LOGIN\r\n");
		server.addReply("535 5.7.8 Authentication failed\r\n"); // PLAIN rejected
		server.addReply("334 VXNlcm5hbWU6\r\n");
		server.addReply("334 UGFzc3dvcmQ6\r\n");
		server.addReply("235 Authentication successful\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.allowPlaintextAuth(true);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(smtp.authenticate("user@example.com", "secret"));
			smtp.disconnect();
		}
		t.join();

		CHECK(server.sawLine("AUTH LOGIN"));
		std::printf("fell through to LOGIN after PLAIN was rejected\n");
	}

	// ---- Wrong credentials fail cleanly ------------------------------------
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH PLAIN\r\n");
		server.addReply("535 5.7.8 Authentication failed\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.allowPlaintextAuth(true);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(!smtp.authenticate("user@example.com", "wrong"));
			CHECK(smtp.getResponseCode() == 535);
			smtp.disconnect();
		}
		t.join();
		std::printf("wrong credentials: 535, reported through getResponseCode()\n");
	}

	// ---- No usable mechanism refuses without sending anything -------------
	{
		TestSmtpServer server;
		server.addReply("250-mail.example.com Hello\r\n"
						"250 AUTH CRAM-MD5\r\n");
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.allowPlaintextAuth(true);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(smtp.sayEhlo("client.example.com"));
			CHECK(!smtp.authenticate("user@example.com", "secret"));
			CHECK(std::strstr(smtp.getError(), "PLAIN") != 0 ||
				  std::strstr(smtp.getError(), "LOGIN") != 0);
			smtp.disconnect();
		}
		t.join();

		bool sentAuth = false;
		for(size_t i = 0; i < server.received().size(); i++)
		{
			if(server.received()[i].compare(0, 4, "AUTH") == 0)
			{
				sentAuth = true;
			}
		}
		CHECK(!sentAuth);
		std::printf("CRAM-MD5-only server: refused locally, nothing sent\n");
	}

	// ---- authenticate() before EHLO is refused -----------------------------
	{
		TestSmtpServer server;
		std::thread t(&TestSmtpServer::run, &server);

		{
			rude::SMTP smtp;
			smtp.allowPlaintextAuth(true);
			CHECK(smtp.connect("127.0.0.1", server.port()));
			CHECK(!smtp.authenticate("user@example.com", "secret"));
			smtp.disconnect();
		}
		t.join();
		std::printf("authenticate() before sayEhlo() refused\n");
	}

	net_cleanup();

	if(failures)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("auth OK\n");
	return 0;
}
