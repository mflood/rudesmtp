// smtpimpl.cpp
//
// Copyright (C) 2003, 2004, 2005 Matthew Flood
// See file AUTHORS for contact information
//
// This file is part of RudeSMTP.
//
// RudeSMTP is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// RudeSMTP is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with RudeSMTP; (see COPYING) if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//------------------------------------------------------------------------


#include "smtpimpl.h"

#include "rudesmtp_version.h"

#include <rude/socket.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace rude
{
namespace smtp
{

// A server that answered with continuation lines forever would keep us
// reading forever. No legitimate reply comes close to this; EHLO from a
// well-stocked server runs to a dozen or so.
//
static const int MAX_RESPONSE_LINES = 128;

// Default seconds to wait for the server. Applied per read, so it bounds
// silence rather than the whole exchange.
//
// RFC 5321 section 4.5.3.2 sets per-command minimums that are far longer --
// five minutes for the greeting, MAIL and RCPT, ten for the reply after
// end-of-data. Those are the limits below which a client must not give up
// on a *busy but working* server. Waiting five minutes on a socket that has
// gone silent is a different thing, and the caller can raise this with
// setTimeout() when talking to a server that earns it.
//
static const int DEFAULT_TIMEOUT_SECONDS = 30;

const char *SMTPImpl::version()
{
	return RUDESMTP_VERSION_STRING;
}

SMTPImpl::SMTPImpl()
	: d_socket(0), d_error(""), d_lastcode(0), d_lastresponse(""), d_connected(false), d_timeoutsecs(DEFAULT_TIMEOUT_SECONDS)
{
	d_socket = new rude::Socket();
}


SMTPImpl::~SMTPImpl()
{
	delete d_socket;
}


void SMTPImpl::setError(const char *error)
{
	d_error = error ? error : "";
}


const char *SMTPImpl::getError()
{
	return d_error.c_str();
}


int SMTPImpl::getResponseCode() const
{
	return d_lastcode;
}


const char *SMTPImpl::getResponse() const
{
	return d_lastresponse.c_str();
}


void SMTPImpl::setTimeout(int seconds)
{
	d_timeoutsecs = seconds > 0 ? seconds : 0;
}


//
// Builds "<context>: <what the server said>", or the socket error when the
// server said nothing.
//
void SMTPImpl::setProtocolError(const char *context)
{
	std::string message = context ? context : "SMTP error";
	message += ": ";
	if(!d_lastresponse.empty())
	{
		message += d_lastresponse;
	}
	else
	{
		message += d_socket->getError();
	}
	setError(message.c_str());
}


//
// Reads one complete reply.
//
// RFC 5321 section 4.2 allows a reply to span several lines: each carries the
// same three-digit code, and every line but the last has a '-' immediately
// after it instead of a space. Only the first line was ever read here, so
// every continuation line stayed in the socket buffer and was picked up as
// the reply to whatever command came next -- each subsequent exchange reading
// one reply further behind. Nothing exposed the fault while the library only
// spoke HELO, whose reply is usually one line, but it is legal there too, and
// EHLO is multiline by design.
//
bool SMTPImpl::readResponse()
{
	d_lastcode = 0;
	d_lastresponse = "";

	for(int lines = 0; lines < MAX_RESPONSE_LINES; lines++)
	{
		const char *line = d_socket->readline();
		if(!line)
		{
			// Connection dropped or timed out. Anything already collected
			// stays in d_lastresponse as context for the error message.
			//
			return false;
		}

		if(!d_lastresponse.empty())
		{
			d_lastresponse += "\n";
		}
		d_lastresponse += line;

		// A reply line must begin with three digits.
		//
		if(strlen(line) < 3 || !isdigit((unsigned char) line[0]) || !isdigit((unsigned char) line[1]) || !isdigit((unsigned char) line[2]))
		{
			setError("SMTP protocol error - malformed reply from server");
			return false;
		}

		const int code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');

		if(d_lastcode == 0)
		{
			d_lastcode = code;
		}

		// '-' in the fourth column means another line follows. A line of
		// exactly three digits is a complete reply.
		//
		if(strlen(line) < 4 || line[3] != '-')
		{
			return true;
		}
	}

	setError("SMTP protocol error - reply exceeded the continuation-line limit");
	return false;
}


//
// Sends one command and reads its reply.
//
// The send is checked. addRecipient(), sendData() and disconnect() used to
// discard the result of sends() and then block reading a reply that a broken
// connection was never going to produce.
//
bool SMTPImpl::command(const char *text)
{
	if(!d_socket->sends(text))
	{
		d_lastcode = 0;
		d_lastresponse = "";
		return false;
	}
	return readResponse();
}


bool SMTPImpl::connect(const char *address, int port)
{
	if(!address || !*address)
	{
		setError("SMTP error - no server address given");
		return false;
	}
	if(port <= 0)
	{
		setError("SMTP error - invalid port");
		return false;
	}

	// Before 2.0.0 no timeout was ever set, and rudesocket blocks by
	// default, so a server that accepted the connection and then said
	// nothing hung the calling thread for good. There was no way for a
	// caller to fix that: the socket is not exposed.
	//
	d_socket->setTimeout(d_timeoutsecs, 0);

	if(!d_socket->connect(address, port))
	{
		setError(d_socket->getError());
		return false;
	}

	d_connected = true;

	// The server speaks first.
	//
	if(!readResponse())
	{
		setProtocolError("SMTP error - could not read the server greeting");
		disconnect();
		return false;
	}

	if(d_lastcode != 220)
	{
		setProtocolError("SMTP error - server did not greet with 220");
		disconnect();
		return false;
	}

	return true;
}


bool SMTPImpl::sayHelo(const char *heloname)
{
	std::string hello = "HELO ";
	hello += heloname ? heloname : "";
	hello += "\r\n";

	if(!command(hello.c_str()))
	{
		setProtocolError("SMTP error - HELO failed");
		return false;
	}
	if(d_lastcode != 250)
	{
		setProtocolError("SMTP error - HELO rejected");
		return false;
	}
	return true;
}


//
// Wraps a bare address in angle brackets.
//
// RFC 5321 requires the reverse-path and forward-path to be bracketed, and
// callers were left to remember that themselves. An unbracketed address is
// rejected outright by strict servers, so it worked or did not depending on
// whose mail server you happened to be talking to.
//
static std::string bracketed(const char *address)
{
	std::string out = address ? address : "";

	// Trim, so " foo@bar " does not defeat the check below.
	//
	while(!out.empty() && isspace((unsigned char) out[0]))
	{
		out.erase(0, 1);
	}
	while(!out.empty() && isspace((unsigned char) out[out.size() - 1]))
	{
		out.erase(out.size() - 1, 1);
	}

	if(out.empty())
	{
		// An empty reverse-path is legal and meaningful: it is how a bounce
		// is sent, so that a bounce of a bounce has nowhere to go.
		//
		return "<>";
	}
	if(out[0] == '<' && out[out.size() - 1] == '>')
	{
		return out;
	}
	return "<" + out + ">";
}


bool SMTPImpl::sayFrom(const char *from)
{
	// "MAIL FROM: <addr>" with a space after the colon is not the grammar in
	// RFC 5321 section 4.1.1.2, which is "MAIL FROM:<reverse-path>". Most
	// servers tolerate the space; some do not, and there is no reason to
	// send it.
	//
	std::string mailfrom = "MAIL FROM:";
	mailfrom += bracketed(from);
	mailfrom += "\r\n";

	if(!command(mailfrom.c_str()))
	{
		setProtocolError("SMTP error - MAIL FROM failed");
		return false;
	}
	if(d_lastcode != 250)
	{
		setProtocolError("SMTP error - MAIL FROM rejected");
		return false;
	}
	return true;
}


bool SMTPImpl::addRecipient(const char *recipient)
{
	std::string recip = "RCPT TO:";
	recip += bracketed(recipient);
	recip += "\r\n";

	if(!command(recip.c_str()))
	{
		setProtocolError("SMTP error - RCPT TO failed");
		return false;
	}

	// 251 means the address is not local and will be forwarded; it is an
	// acceptance. Comparing only the leading digit accepted it too, but by
	// accident rather than intent.
	//
	if(d_lastcode != 250 && d_lastcode != 251)
	{
		setProtocolError("SMTP error - recipient rejected");
		return false;
	}
	return true;
}


//
// Applies the dot-stuffing of RFC 5321 section 4.5.2: a line of message data
// that begins with '.' gets a second one, so it cannot be mistaken for the
// end-of-data marker.
//
// The previous implementation tracked the preceding character and doubled a
// '.' that followed CR, LF or FF. That misses the first line of the message,
// where there is no preceding character: a message starting with "." had it
// stripped by the receiving server, silently losing the first character of
// the mail.
//
static std::string dotStuffed(const char *message)
{
	std::string out;
	const size_t length = message ? strlen(message) : 0;
	bool atLineStart = true; // the message begins a line

	for(size_t x = 0; x < length; x++)
	{
		const char c = message[x];
		if(atLineStart && c == '.')
		{
			out += '.';
		}
		out += c;
		atLineStart = (c == '\n' || c == '\r' || c == '\f');
	}
	return out;
}


bool SMTPImpl::sendData(const char *message)
{
	if(!command("DATA\r\n"))
	{
		setProtocolError("SMTP error - DATA failed");
		return false;
	}
	if(d_lastcode != 354)
	{
		setProtocolError("SMTP error - server refused DATA");
		return false;
	}

	std::string payload = dotStuffed(message);

	// The terminator is CRLF ".", CRLF. The CRLF that opens it belongs to
	// the message's last line, so sending it unconditionally appended a
	// blank line to every message that already ended in one -- which is
	// every message assembled correctly.
	//
	const bool endsWithNewline =
		payload.size() >= 2 && payload[payload.size() - 2] == '\r' && payload[payload.size() - 1] == '\n';

	if(!payload.empty() && !endsWithNewline)
	{
		payload += "\r\n";
	}
	payload += ".\r\n";

	if(!d_socket->sends(payload.c_str()))
	{
		setError(d_socket->getError());
		return false;
	}

	if(!readResponse())
	{
		setProtocolError("SMTP error - no reply after message data");
		return false;
	}
	if(d_lastcode != 250)
	{
		setProtocolError("SMTP error - message rejected");
		return false;
	}
	return true;
}


bool SMTPImpl::disconnect()
{
	if(!d_connected)
	{
		return true;
	}

	// Whatever the server makes of QUIT, the socket has to be closed;
	// returning early on a bad reply used to leak the connection.
	//
	const bool replied = command("QUIT\r\n");
	const bool accepted = replied && d_lastcode == 221;

	if(!accepted && replied)
	{
		setProtocolError("SMTP error - QUIT rejected");
	}

	d_socket->close();
	d_connected = false;

	return accepted;
}

} // namespace smtp
} // namespace rude
