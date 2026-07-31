// sendmail.cpp - minimal RudeSMTP example.
//
// Sends one message through a local relay:
//
//     ./sendmail localhost 25 me@example.com you@example.com
//
// Port 25 because this library has no authentication and no encryption, so
// it can only talk to a server that accepts unauthenticated relay. Ports 465
// and 587 will refuse it.
#include <rude/smtp.h>

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char **argv)
{
	if(argc != 5)
	{
		std::printf("usage: %s <server> <port> <from> <to>\n", argv[0]);
		return 1;
	}
	const char *server = argv[1];
	const int port = std::atoi(argv[2]);
	const char *from = argv[3];
	const char *to = argv[4];

	rude::SMTP smtp;
	std::printf("RudeSMTP %s\n", rude::SMTP::version());

	if(!smtp.connect(server, port))
	{
		std::fprintf(stderr, "connect: %s\n", smtp.getError());
		return 1;
	}

	std::string message;
	message += "From: ";
	message += from;
	message += "\r\nTo: ";
	message += to;
	message += "\r\nSubject: RudeSMTP test\r\n\r\n";
	message += "Sent by the RudeSMTP example program.\r\n";

	if(!smtp.sayHelo("localhost") || !smtp.sayFrom(from) ||
	   !smtp.addRecipient(to) || !smtp.sendData(message.c_str()))
	{
		// 4xx is worth retrying later; 5xx is not.
		std::fprintf(stderr, "failed (%d): %s\n", smtp.getResponseCode(), smtp.getError());
		smtp.disconnect();
		return 1;
	}

	smtp.disconnect();
	std::printf("sent\n");
	return 0;
}
