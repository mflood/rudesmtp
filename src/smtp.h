// smtp.h
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


#ifndef INCLUDED_SMTP_H
#define INCLUDED_SMTP_H

namespace rude
{

namespace smtp
{
class SMTPImpl;
} // namespace smtp


//=
// A client for sending mail over SMTP.
//
// The calls below are the SMTP conversation itself, in order:
//
//   connect() -> sayHelo() -> sayFrom() -> addRecipient() -> sendData()
//   -> disconnect()
//
// addRecipient() may be called more than once, before sendData().  Nothing
// enforces the order; calling out of sequence gets you the server's
// complaint through getError().
//
// sendData() takes the complete message - headers, blank line, body - and
// does not generate any of it.  Line endings should be CRLF.
//
// <b>Example:</b>
// <p>
// <code>
// rude::SMTP smtp;<br>
// if(!smtp.connect("localhost", 25)) { cerr << smtp.getError(); return; }<br>
// smtp.sayHelo("myhost.example.com");<br>
// smtp.sayFrom("me@example.com");<br>
// smtp.addRecipient("you@example.com");<br>
// smtp.sendData("From: me@example.com\r\nTo: you@example.com\r\n"<br>
// &nbsp;&nbsp;"Subject: hi\r\n\r\nThe body.\r\n");<br>
// smtp.disconnect();<br>
// </code>
//
// <b>Note:</b> this client speaks HELO and sends in the clear.  It has no
// authentication and no encryption, so it can only be used against a server
// that accepts unauthenticated relay - in practice a local one.
//=
class SMTP
{

	rude::smtp::SMTPImpl *d_implementation;

  public:
	SMTP();
	~SMTP();

	//=
	// The connection is owned by this object.  Copying it would give two
	// objects the same connection and close it twice, so copying is
	// disallowed rather than left to fail at runtime.
	//=
	SMTP(const SMTP &) = delete;
	SMTP &operator=(const SMTP &) = delete;


	//=
	// Returns the library version, e.g. "2.0.0".
	//=
	static const char *version();


	//=
	// Returns a description of the last error, including the server's own
	// words where there were any.
	//=
	const char *getError();


	//=
	// Returns the three-digit code from the server's last reply, or 0 if
	// no reply has been read.
	//
	// This is what tells a temporary failure from a permanent one, and it
	// is the difference between requeueing a message and bouncing it:
	// 4xx means try again later, 5xx means never.  A failed call reports
	// through getError() either way, so without this a caller cannot
	// choose.
	//=
	int getResponseCode();


	//=
	// Returns the full text of the server's last reply, continuation lines
	// included, separated by newlines.
	//=
	const char *getResponse();


	//=
	// Sets how long to wait for the server, in seconds.  Applied per read,
	// so it bounds silence rather than the whole exchange.  Must be called
	// before connect().  Pass 0 to wait indefinitely.
	//
	// The default is 30 seconds.  Before 2.0.0 there was no timeout at all
	// and no way to add one, so a server that accepted the connection and
	// then went quiet hung the calling thread for good.
	//
	// RFC 5321 section 4.5.3.2 sets much longer per-command minimums - five
	// minutes for the greeting, MAIL and RCPT - but those govern how long
	// to wait on a busy server that is still working, which is not the same
	// as waiting on a silent socket.  Raise this for a server that needs
	// the room.
	//=
	void setTimeout(int seconds);


	//=
	// Opens the connection and reads the server's greeting.
	// Returns false unless the greeting is a 220.
	//
	// Port 25 is the usual relay port.  Ports 465 and 587 require
	// encryption and authentication, which this library does not have.
	//=
	bool connect(const char *address, int port);


	//=
	// Sends HELO with the given hostname.
	//=
	bool sayHelo(const char *heloname);


	//=
	// Sends MAIL FROM with the sender's address.
	//
	// Angle brackets are added if you leave them off.  An empty address
	// becomes "<>", the null reverse-path used when sending a bounce.
	//=
	bool sayFrom(const char *from);


	//=
	// Sends RCPT TO with a recipient's address.  Call once per recipient,
	// before sendData().  Angle brackets are added if you leave them off.
	//=
	bool addRecipient(const char *recipient);


	//=
	// Sends the message.
	//
	// Takes the complete message: headers, a blank line, then the body.
	// Lines beginning with '.' are escaped as the protocol requires, and
	// the end-of-data marker is appended - do not add it yourself.
	//=
	bool sendData(const char *message);


	//=
	// Sends QUIT and closes the connection.  The connection is closed
	// whatever the server replies.
	//=
	bool disconnect();
};

} // namespace rude

#endif
