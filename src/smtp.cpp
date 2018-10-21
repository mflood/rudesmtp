// smtp.cpp
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


#include "smtp.h"
#include "smtpimpl.h"

using namespace rude::smtp;

namespace rude{


SMTP::SMTP()
{
	d_implementation = new SMTPImpl();
}

SMTP::~SMTP()
{
	delete d_implementation;
}

const char *SMTP::version()
{
	return SMTPImpl::version();
}

const char *SMTP::getError()
{
	return d_implementation->getError();
}

bool SMTP::connect(const char *address, int port)
{
	return d_implementation->connect(address,port);
}

bool SMTP::sayHelo(const char *heloname)
{
	return d_implementation->sayHelo(heloname);
}

bool SMTP::sayFrom(const char *from)
{
	return d_implementation->sayFrom(from);
}

bool SMTP::addRecipient(const char *recipient)
{
	return d_implementation->addRecipient(recipient);
}

bool SMTP::sendData(const char *message)
{
	return d_implementation->sendData(message);
}

bool SMTP::disconnect()
{
	return d_implementation->disconnect();
}


} // end namespace rude


