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
// <b>To use encryption and authentication</b> - required by every hosted
// provider (Gmail, Microsoft 365, SES, ...) - call sayEhlo() instead of
// sayHelo(), after connect() (port 587) or connectSSL() (port 465):
// <p>
// <code>
// rude::SMTP smtp;<br>
// smtp.connect("smtp.example.com", 587);<br>
// smtp.sayEhlo("myhost.example.com");<br>
// smtp.startTLS();<br>
// smtp.sayEhlo("myhost.example.com");&nbsp;// again: see startTLS()<br>
// smtp.authenticate("user@example.com", "app-password");<br>
// smtp.sayFrom("user@example.com");<br>
// &nbsp;&nbsp;// ... as before ...<br>
// </code>
// <p>
// Or, for implicit TLS on port 465, replace connect() with connectSSL() and
// drop the startTLS() call - the connection is already encrypted, so one
// sayEhlo() is enough.
// <p>
// <b>Note:</b> plain sayHelo() still sends in the clear with no
// authentication, and can only be used against a server that accepts
// unauthenticated relay - in practice a local one.
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
	// Enables or disables certificate verification for connectSSL() and
	// startTLS() (enabled by default).  See rude::Socket::setSSLVerify()
	// for what verification checks.  Call before connect() /
	// connectSSL().
	//=
	void setSSLVerify(bool verify);


	//=
	// Permits authenticate() to run on a connection that is not
	// encrypted (disabled by default).
	//
	// AUTH PLAIN and AUTH LOGIN both put the password on the wire as
	// base64, which is an encoding and not encryption: anyone who can
	// observe the connection can read it, and anyone who can modify it
	// can strip STARTTLS from the server's greeting and let the client
	// authenticate anyway.  Refusing by default makes that downgrade fail
	// loudly.  Call this only if you have another way of trusting the
	// network - a loopback connection, a private VPN - not to make
	// unencrypted authentication acceptable in general.
	//=
	void allowPlaintextAuth(bool allow);


	//=
	// True once the connection is carrying TLS, from connectSSL() or a
	// successful startTLS().
	//=
	bool isSecure();


	//=
	// True if the server's EHLO reply advertised the named extension,
	// e.g. "STARTTLS", "SIZE", "8BITMIME".  Always false before sayEhlo()
	// succeeds, and reset by startTLS() - see its documentation for why.
	//=
	bool supportsExtension(const char *name);


	//=
	// True if the server's EHLO reply advertised the named SASL
	// mechanism, e.g. "PLAIN" or "LOGIN".
	//=
	bool supportsAuth(const char *mechanism);


	//=
	// Opens the connection and reads the server's greeting.
	// Returns false unless the greeting is a 220.
	//
	// Port 25 is the traditional relay port and rarely requires either
	// encryption or authentication.  Port 587 (submission) expects
	// sayEhlo() followed by startTLS().
	//=
	bool connect(const char *address, int port);


	//=
	// Opens the connection already wrapped in TLS - "implicit TLS", what
	// port 465 expects - and reads the greeting.  The certificate is
	// verified against \a address by default; see setSSLVerify().
	//
	// Requires a build with OpenSSL; without it this returns false and
	// getError() says so.
	//=
	bool connectSSL(const char *address, int port);


	//=
	// Negotiates TLS on a connection that is already open (RFC 3207) -
	// the "STARTTLS" pattern port 587 expects.  Call after sayEhlo() has
	// shown the server advertises STARTTLS.
	//
	// <b>You must call sayEhlo() again after this succeeds.</b> The
	// capabilities from the first EHLO were read before encryption
	// started, so a network attacker could have altered that reply -
	// hiding AUTH to force a weaker mechanism, for instance - and RFC
	// 3207 requires discarding it.  Only what arrives inside TLS can be
	// trusted; this call clears the capability list to make sure nothing
	// stale gets used by accident.
	//
	// A failed handshake ends the connection - a half-negotiated TLS
	// session is not usable - so do not call disconnect() first and do
	// not retry on the same object.
	//=
	bool startTLS();


	//=
	// Authenticates with AUTH PLAIN or AUTH LOGIN (RFC 4954), preferring
	// PLAIN.  Call after sayEhlo(), and after startTLS() or
	// connectSSL() unless you have called allowPlaintextAuth(true).
	//=
	bool authenticate(const char *user, const char *password);


	//=
	// Sends HELO with the given hostname.  Advertises no extensions and
	// cannot be followed by startTLS() or authenticate() - use sayEhlo()
	// for those.
	//=
	bool sayHelo(const char *heloname);


	//=
	// Sends EHLO with the given hostname and records the extensions and
	// AUTH mechanisms the server advertises.  Required before startTLS()
	// or authenticate().
	//
	// Returns false if the server does not understand EHLO at all (a
	// pre-2001 server, essentially never seen today); fall back to
	// sayHelo() in that case.
	//=
	bool sayEhlo(const char *heloname);


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
