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

namespace rude{

namespace smtp{
class SMTPImpl;
} // end namespace smtp



class SMTP{

	rude::smtp::SMTPImpl *d_implementation;

public:

	SMTP();
	static const char *version();
	const char *getError();
	bool connect(const char *address, int port);
	bool sayHelo(const char *heloname);
	bool sayFrom(const char *from);
	bool addRecipient(const char *recipient);
	bool sendData(const char *message);
	bool disconnect();
	~SMTP();
};

} // end namespace rude

#endif

