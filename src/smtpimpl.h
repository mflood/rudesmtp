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

namespace rude{

class Socket;

namespace smtp{




class SMTPImpl{

	rude::Socket *d_socket;
	std::string d_error;

protected:
	void setError(const char *error);
	

public:

	SMTPImpl();
	static const char *version();
	const char *getError();
	bool connect(const char *address, int port);
	bool sayHelo(const char *heloname);
	bool sayFrom(const char *from);
	bool addRecipient(const char *recipient);
	bool sendData(const char *message);
	bool disconnect();
	~SMTPImpl();
	
};

}} // end namespace rude::smtp

#endif
