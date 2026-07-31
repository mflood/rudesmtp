// smtpimpl.h
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


#ifndef INCLUDED_SMTPIMPL_H
#define INCLUDED_SMTPIMPL_H

#include <string>

namespace rude
{

class Socket;

namespace smtp
{


class SMTPImpl
{

	rude::Socket *d_socket;
	std::string d_error;

	// The reply to the last command: its numeric code, and its full text
	// including any continuation lines.
	//
	// Replies used to be judged by comparing character zero against a
	// digit, so 250 and 251 were indistinguishable, and so were 421
	// (server shutting down, try later) and 550 (rejected, never retry).
	// A caller had no way to tell a transient failure from a permanent
	// one, which is the difference between requeueing a message and
	// bouncing it.
	//
	int d_lastcode;
	std::string d_lastresponse;

	bool d_connected;

	// Applied to the socket at connect() time. Zero blocks forever, which
	// is what this library did unconditionally before 2.0.0.
	//
	int d_timeoutsecs;

	// Reads one complete reply, honouring RFC 5321 continuation syntax,
	// and records its code and text. Returns false only when the reply
	// could not be read at all; a 4xx or 5xx reply is read successfully
	// and reported through the code.
	//
	bool readResponse();

	// Sends one command and reads the reply. The send is checked -- several
	// commands used to ignore whether the write succeeded and then wait for
	// a reply that could not be coming.
	//
	bool command(const char *text);

	// Sets d_error to "<context>: <server reply, or the socket error>".
	//
	void setProtocolError(const char *context);

  protected:
	void setError(const char *error);


  public:
	SMTPImpl();
	~SMTPImpl();

	// The socket is owned, and copying the handle would give two objects
	// the same connection and free it twice.
	//
	SMTPImpl(const SMTPImpl &) = delete;
	SMTPImpl &operator=(const SMTPImpl &) = delete;

	static const char *version();
	const char *getError();

	int getResponseCode() const;
	const char *getResponse() const;

	void setTimeout(int seconds);

	bool connect(const char *address, int port);
	bool sayHelo(const char *heloname);
	bool sayFrom(const char *from);
	bool addRecipient(const char *recipient);
	bool sendData(const char *message);
	bool disconnect();
};

} // namespace smtp
} // namespace rude

#endif
