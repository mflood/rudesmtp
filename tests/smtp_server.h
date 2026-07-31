// smtp_server.h - a scriptable in-process SMTP server for the tests.
//
// The point is to drive the client against replies a real server is allowed
// to send but a happy-path test never produces: multiline replies, 4xx and
// 5xx codes, a greeting that never arrives, a connection that drops mid
// conversation. Those are exactly the cases the old response handling got
// wrong, and none of them are reachable by pointing the client at a working
// mail server.
//
// The server records every command it received, so a test can assert on what
// actually went onto the wire -- the bracketing of MAIL FROM, the dot-stuffing
// of the message body -- rather than only on what the client returned.
#ifndef RUDESMTP_TESTS_SMTP_SERVER_H
#define RUDESMTP_TESTS_SMTP_SERVER_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET net_socket_t;
#define NET_INVALID_SOCKET INVALID_SOCKET
inline void net_close(net_socket_t s)
{
	closesocket(s);
}
inline void net_init()
{
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
}
inline void net_cleanup()
{
	WSACleanup();
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int net_socket_t;
#define NET_INVALID_SOCKET (-1)
inline void net_close(net_socket_t s)
{
	::close(s);
}
inline void net_init() {}
inline void net_cleanup() {}
#endif

#include <cstring>
#include <string>
#include <vector>

class TestSmtpServer
{
	net_socket_t d_listen;
	int d_port;

	// Reply sent on connect, before any command. Empty means say nothing,
	// which is how a silent server is simulated.
	std::string d_greeting;

	// Replies handed out in order, one per command received. When they run
	// out the server closes, which exercises a mid-conversation drop.
	std::vector<std::string> d_replies;
	size_t d_next;

	// Everything the client sent, one entry per line.
	std::vector<std::string> d_received;

	// Set when the server is meant to hang up rather than reply.
	bool d_dropAfterGreeting;

	static bool sendAll(net_socket_t s, const std::string &text)
	{
		size_t off = 0;
		while(off < text.size())
		{
			const int sent = (int) ::send(s, text.data() + off, (int) (text.size() - off), 0);
			if(sent <= 0)
			{
				return false;
			}
			off += (size_t) sent;
		}
		return true;
	}

	// Reads one CRLF-terminated line. Returns false at EOF.
	static bool readLine(net_socket_t s, std::string &out)
	{
		out.clear();
		for(;;)
		{
			char c;
			const int rc = (int) ::recv(s, &c, 1, 0);
			if(rc <= 0)
			{
				return !out.empty();
			}
			if(c == '\n')
			{
				return true;
			}
			if(c != '\r')
			{
				out += c;
			}
		}
	}

  public:
	TestSmtpServer()
		: d_listen(NET_INVALID_SOCKET), d_port(0), d_greeting("220 test.example.com ESMTP\r\n"), d_next(0), d_dropAfterGreeting(false)
	{
		d_listen = ::socket(AF_INET, SOCK_STREAM, 0);
		sockaddr_in addr;
		std::memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr.sin_port = 0; // ephemeral
		::bind(d_listen, (sockaddr *) &addr, sizeof(addr));
		::listen(d_listen, 1);
		socklen_t len = sizeof(addr);
		::getsockname(d_listen, (sockaddr *) &addr, &len);
		d_port = ntohs(addr.sin_port);
	}

	~TestSmtpServer()
	{
		if(d_listen != NET_INVALID_SOCKET)
		{
			net_close(d_listen);
		}
	}

	int port() const
	{
		return d_port;
	}

	// Replaces the greeting. Pass "" for a server that accepts the
	// connection and then says nothing at all.
	void setGreeting(const std::string &greeting)
	{
		d_greeting = greeting;
	}

	// Queues one reply. Include the trailing CRLF; multiline replies are
	// just several lines in one string, with '-' after the code on every
	// line but the last.
	void addReply(const std::string &reply)
	{
		d_replies.push_back(reply);
	}

	// Hang up immediately after greeting, without answering anything.
	void dropAfterGreeting()
	{
		d_dropAfterGreeting = true;
	}

	// Every line the client sent, in order.
	const std::vector<std::string> &received() const
	{
		return d_received;
	}

	// True if the client sent this exact line at any point.
	bool sawLine(const std::string &line) const
	{
		for(size_t i = 0; i < d_received.size(); i++)
		{
			if(d_received[i] == line)
			{
				return true;
			}
		}
		return false;
	}

	void run()
	{
		net_socket_t c = ::accept(d_listen, 0, 0);
		if(c == NET_INVALID_SOCKET)
		{
			return;
		}

		if(!d_greeting.empty())
		{
			sendAll(c, d_greeting);
		}

		if(d_dropAfterGreeting)
		{
			net_close(c);
			return;
		}

		// One reply per command, until the script is exhausted.
		//
		// DATA is the exception: after a 354 the client sends message lines
		// rather than commands, and they must not consume replies. Those
		// are collected until the "." terminator.
		bool inData = false;
		for(;;)
		{
			std::string line;
			if(!readLine(c, line))
			{
				break;
			}
			d_received.push_back(line);

			if(inData)
			{
				if(line == ".")
				{
					inData = false;
				}
				else
				{
					continue; // message content, not a command
				}
			}
			else if(line.size() >= 4 && (line.compare(0, 4, "DATA") == 0))
			{
				// The reply to DATA decides whether content follows.
				if(d_next < d_replies.size() && d_replies[d_next].compare(0, 3, "354") == 0)
				{
					inData = true;
				}
			}

			if(d_next >= d_replies.size())
			{
				break; // script exhausted: drop the connection
			}
			if(!sendAll(c, d_replies[d_next++]))
			{
				break;
			}
		}

		net_close(c);
	}
};

#endif
