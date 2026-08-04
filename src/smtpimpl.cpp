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

//
// SASL carries credentials as base64 (RFC 4648).  Encoding only: the two
// mechanisms implemented here have server challenges that are either empty or
// human-readable prompts this client does not need to interpret.
//
static std::string base64Encode(const std::string &input)
{
	static const char *ALPHABET =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string out;
	out.reserve(((input.size() + 2) / 3) * 4);

	size_t i = 0;
	while(i + 2 < input.size())
	{
		const unsigned int triple = ((unsigned char) input[i] << 16) |
									((unsigned char) input[i + 1] << 8) |
									((unsigned char) input[i + 2]);
		out += ALPHABET[(triple >> 18) & 0x3F];
		out += ALPHABET[(triple >> 12) & 0x3F];
		out += ALPHABET[(triple >> 6) & 0x3F];
		out += ALPHABET[triple & 0x3F];
		i += 3;
	}

	// One or two bytes left: pad the group out with '='.
	//
	if(i < input.size())
	{
		unsigned int triple = (unsigned char) input[i] << 16;
		const bool two = (i + 1 < input.size());
		if(two)
		{
			triple |= (unsigned char) input[i + 1] << 8;
		}
		out += ALPHABET[(triple >> 18) & 0x3F];
		out += ALPHABET[(triple >> 12) & 0x3F];
		out += two ? ALPHABET[(triple >> 6) & 0x3F] : '=';
		out += '=';
	}

	return out;
}

//
// Overwrites a string's buffer before releasing it.
//
// This is best-effort, not a guarantee: std::string may have copied itself
// during any earlier reallocation, and those copies are gone.  It costs
// nothing and narrows the window in which a credential sits in freed memory.
//
static void scrub(std::string &s)
{
	for(size_t i = 0; i < s.size(); i++)
	{
		s[i] = 0;
	}
	s.clear();
}

static std::string upperCase(const std::string &s)
{
	std::string out = s;
	for(size_t i = 0; i < out.size(); i++)
	{
		out[i] = (char) toupper((unsigned char) out[i]);
	}
	return out;
}

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
	: d_socket(0), d_error(""), d_lastcode(0), d_lastresponse(""), d_connected(false), d_timeoutsecs(DEFAULT_TIMEOUT_SECONDS), d_secure(false), d_allowplaintextauth(false), d_esmtp(false)
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


void SMTPImpl::allowPlaintextAuth(bool allow)
{
	d_allowplaintextauth = allow;
}


void SMTPImpl::setSSLVerify(bool verify)
{
	d_socket->setSSLVerify(verify);
}


bool SMTPImpl::isSecure() const
{
	return d_secure;
}


//
// Rebuilds the capability lists from the EHLO reply.
//
// The first line is the server's greeting text, not a capability.  Each line
// after it names one extension, optionally followed by parameters:
//
//     250-mail.example.com Hello
//     250-SIZE 35882577
//     250-STARTTLS
//     250-AUTH LOGIN PLAIN
//     250 8BITMIME
//
void SMTPImpl::parseCapabilities()
{
	d_extensions.clear();
	d_authmechanisms.clear();

	size_t pos = 0;
	bool first = true;

	while(pos <= d_lastresponse.size())
	{
		size_t end = d_lastresponse.find('\n', pos);
		if(end == std::string::npos)
		{
			end = d_lastresponse.size();
		}
		std::string line = d_lastresponse.substr(pos, end - pos);
		pos = end + 1;

		if(first)
		{
			// The greeting line, e.g. "250-mail.example.com Hello".
			//
			first = false;
			continue;
		}

		// Strip the "250-" or "250 " prefix.
		//
		if(line.size() > 4)
		{
			line = line.substr(4);
		}
		else
		{
			continue;
		}

		// Trim, since a stray CR or trailing space would otherwise become
		// part of the extension name.
		//
		while(!line.empty() && isspace((unsigned char) line[line.size() - 1]))
		{
			line.erase(line.size() - 1, 1);
		}
		while(!line.empty() && isspace((unsigned char) line[0]))
		{
			line.erase(0, 1);
		}
		if(line.empty())
		{
			continue;
		}

		const std::string upper = upperCase(line);
		d_extensions.push_back(upper);

		// AUTH's parameters are the mechanism names.  Two spellings are in
		// the wild: "AUTH LOGIN PLAIN" and the older "AUTH=LOGIN PLAIN".
		//
		if(upper.compare(0, 4, "AUTH") == 0 && (upper.size() == 4 || upper[4] == ' ' || upper[4] == '='))
		{
			size_t at = 4;
			while(at < upper.size())
			{
				while(at < upper.size() && (upper[at] == ' ' || upper[at] == '='))
				{
					at++;
				}
				size_t stop = at;
				while(stop < upper.size() && upper[stop] != ' ')
				{
					stop++;
				}
				if(stop > at)
				{
					d_authmechanisms.push_back(upper.substr(at, stop - at));
				}
				at = stop;
			}
		}
	}
}


bool SMTPImpl::supportsExtension(const char *name) const
{
	if(!name || !*name || !d_esmtp)
	{
		return false;
	}
	const std::string want = upperCase(name);
	for(size_t i = 0; i < d_extensions.size(); i++)
	{
		const std::string &have = d_extensions[i];
		// Match the name, ignoring any parameters that follow it.
		//
		if(have.compare(0, want.size(), want) == 0 && (have.size() == want.size() || have[want.size()] == ' ' || have[want.size()] == '='))
		{
			return true;
		}
	}
	return false;
}


bool SMTPImpl::supportsAuth(const char *mechanism) const
{
	if(!mechanism || !*mechanism || !d_esmtp)
	{
		return false;
	}
	const std::string want = upperCase(mechanism);
	for(size_t i = 0; i < d_authmechanisms.size(); i++)
	{
		if(d_authmechanisms[i] == want)
		{
			return true;
		}
	}
	return false;
}


bool SMTPImpl::sayEhlo(const char *heloname)
{
	std::string ehlo = "EHLO ";
	ehlo += heloname ? heloname : "";
	ehlo += "\r\n";

	d_esmtp = false;
	d_extensions.clear();
	d_authmechanisms.clear();

	if(!command(ehlo.c_str()))
	{
		setProtocolError("SMTP error - EHLO failed");
		return false;
	}
	if(d_lastcode != 250)
	{
		// A server that predates ESMTP answers 500 or 502.  That is a
		// legitimate answer, not a transport failure, so report it and let
		// the caller fall back to sayHelo().
		//
		setProtocolError("SMTP error - EHLO rejected");
		return false;
	}

	d_esmtp = true;
	parseCapabilities();
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


bool SMTPImpl::connectSSL(const char *address, int port)
{
#ifdef RUDESOCKET_WITH_SSL
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

	d_socket->setTimeout(d_timeoutsecs, 0);

	// Implicit TLS: the session is encrypted from the first byte, before
	// the greeting.  This is what port 465 expects.
	//
	if(!d_socket->connectSSL(address, port))
	{
		setError(d_socket->getError());
		return false;
	}

	d_connected = true;
	d_secure = true;

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
#else
	(void) address;
	(void) port;
	setError("SMTP error - built without SSL support; rebuild rudesocket with OpenSSL (RUDESOCKET_WITH_SSL=ON)");
	return false;
#endif
}


//
// Negotiates TLS on the open connection (RFC 3207).
//
bool SMTPImpl::startTLS()
{
#ifdef RUDESOCKET_WITH_SSL
	if(!d_connected)
	{
		setError("SMTP error - not connected");
		return false;
	}
	if(d_secure)
	{
		setError("SMTP error - the connection is already encrypted");
		return false;
	}
	if(!supportsExtension("STARTTLS"))
	{
		// Either the server does not offer it, or sayEhlo() has not run.
		// Issuing it blind would send a command the server may answer with
		// something this client then has to guess about.
		//
		setError("SMTP error - server does not advertise STARTTLS (call sayEhlo() first)");
		return false;
	}

	if(!command("STARTTLS\r\n"))
	{
		setProtocolError("SMTP error - STARTTLS failed");
		return false;
	}
	if(d_lastcode != 220)
	{
		setProtocolError("SMTP error - server refused STARTTLS");
		return false;
	}

	if(!d_socket->startSSL())
	{
		// rudesocket closes the connection on a failed handshake, since a
		// half-negotiated session cannot be handed back.  Reflect that
		// rather than leaving this object claiming to be connected.
		//
		std::string message = "SMTP error - TLS handshake failed: ";
		message += d_socket->getError();
		setError(message.c_str());
		d_connected = false;
		return false;
	}

	d_secure = true;

	// RFC 3207 section 4.2: the client MUST discard everything it learned
	// from the earlier EHLO and ask again.  That exchange happened in the
	// clear, so an attacker in the middle could have written it - hiding
	// AUTH mechanisms to force a weaker one, or advertising extensions the
	// server does not have.  Only what arrives inside TLS can be trusted.
	//
	d_esmtp = false;
	d_extensions.clear();
	d_authmechanisms.clear();

	return true;
#else
	setError("SMTP error - built without SSL support; rebuild rudesocket with OpenSSL (RUDESOCKET_WITH_SSL=ON)");
	return false;
#endif
}


//
// Sends one line and reads the reply.
//
// Identical to command() today - both record only what the server said, via
// readResponse(), and neither ever puts the outgoing line anywhere getError()
// or getResponse() can surface it.  This exists anyway because the AUTH
// exchange carries credentials, and the two call sites are worth keeping
// structurally separate: if command() is later changed to echo the outgoing
// text into a richer error message, that change cannot silently start
// leaking credentials through the paths that use this one instead.
//
bool SMTPImpl::secretCommand(const std::string &text)
{
	if(!d_socket->sends(text.c_str()))
	{
		d_lastcode = 0;
		d_lastresponse = "";
		return false;
	}
	return readResponse();
}


//
// Authenticates with AUTH PLAIN or AUTH LOGIN, preferring PLAIN.
//
// PLAIN is one round trip and one encoding; LOGIN is three round trips and is
// not in any RFC, but plenty of servers offer only it.  Neither protects the
// password in any way - both are base64, which is an encoding and not
// encryption - so both depend entirely on the connection being encrypted.
// That is why this refuses to run in the clear by default.
//
bool SMTPImpl::authenticate(const char *user, const char *password)
{
	if(!d_connected)
	{
		setError("SMTP error - not connected");
		return false;
	}
	if(!user || !password)
	{
		setError("SMTP error - username and password are required");
		return false;
	}
	if(!d_esmtp)
	{
		setError("SMTP error - call sayEhlo() before authenticating");
		return false;
	}

	// AUTH PLAIN and AUTH LOGIN both put the password on the wire in a form
	// anyone watching can read.  On an unencrypted connection an attacker
	// who can see the traffic simply has the password; one who can modify
	// it can also strip STARTTLS from the greeting and wait for the client
	// to hand the credentials over anyway.  Refusing by default is what
	// makes that downgrade fail loudly instead of silently.
	//
	if(!d_secure && !d_allowplaintextauth)
	{
		setError("SMTP error - refusing to send credentials over an unencrypted connection; use startTLS() or connectSSL(), or call allowPlaintextAuth(true) if you accept the risk");
		return false;
	}

	const bool hasPlain = supportsAuth("PLAIN");
	const bool hasLogin = supportsAuth("LOGIN");

	if(!hasPlain && !hasLogin)
	{
		setError("SMTP error - server offers no supported AUTH mechanism (PLAIN or LOGIN)");
		return false;
	}

	bool ok = false;

	if(hasPlain)
	{
		// RFC 4616: authorization identity, NUL, authentication identity,
		// NUL, password.  The first field is empty, meaning "the same as
		// the one authenticating".
		//
		std::string plain;
		plain += '\0';
		plain += user;
		plain += '\0';
		plain += password;

		std::string line = "AUTH PLAIN ";
		line += base64Encode(plain);
		line += "\r\n";

		scrub(plain);

		ok = secretCommand(line);
		scrub(line);

		if(!ok)
		{
			setProtocolError("SMTP error - AUTH PLAIN failed");
			return false;
		}
		if(d_lastcode == 235)
		{
			return true;
		}

		// Fall through to LOGIN if the server has it: some advertise PLAIN
		// but only really accept LOGIN.
		//
		if(!hasLogin)
		{
			setProtocolError("SMTP error - authentication failed");
			return false;
		}
	}

	// AUTH LOGIN: the server prompts twice with 334, once for each half.
	//
	ok = secretCommand("AUTH LOGIN\r\n");
	if(!ok)
	{
		setProtocolError("SMTP error - AUTH LOGIN failed");
		return false;
	}
	if(d_lastcode != 334)
	{
		setProtocolError("SMTP error - server refused AUTH LOGIN");
		return false;
	}

	std::string line = base64Encode(user);
	line += "\r\n";
	ok = secretCommand(line);
	scrub(line);

	if(!ok)
	{
		setProtocolError("SMTP error - AUTH LOGIN failed sending the username");
		return false;
	}
	if(d_lastcode != 334)
	{
		setProtocolError("SMTP error - server rejected the username");
		return false;
	}

	line = base64Encode(password);
	line += "\r\n";
	ok = secretCommand(line);
	scrub(line);

	if(!ok)
	{
		setProtocolError("SMTP error - AUTH LOGIN failed sending the password");
		return false;
	}
	if(d_lastcode != 235)
	{
		setProtocolError("SMTP error - authentication failed");
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
	d_secure = false;
	d_esmtp = false;
	d_extensions.clear();
	d_authmechanisms.clear();

	return accepted;
}

} // namespace smtp
} // namespace rude
