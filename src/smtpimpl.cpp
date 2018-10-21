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

#include <rude/socket.h>

#include <stdio.h>
#include <string.h>

#include <iostream>

namespace rude{
namespace smtp{

const char *SMTPImpl::version()
{
	return "rudeserver.com SMTP version 1.0";
}
	
SMTPImpl::SMTPImpl()
{
	d_socket = new rude::Socket();
	d_error="";
}


SMTPImpl::~SMTPImpl()
{
	delete d_socket;
}


void SMTPImpl::setError(const char *error)
{
	d_error = error? error : "";
}
	

const char *SMTPImpl::getError()
{
	return d_error.c_str();
}

bool SMTPImpl::connect(const char *address, int port)
{
	if(d_socket->connect(address, port))
	{
		const char *server_response=d_socket->readline();
		if(server_response)
		{
			if(server_response[0] == '2')
			{
				return true;
			}
			else
			{
				// POP server's greeting line doesn't start with '2'
				//
				std::string errorstring="Not an SMTP Server: '";
				errorstring += server_response;
				errorstring += "'";
				setError(errorstring.c_str());
				d_socket->close();
			}
		}
		else
		{
			// error reading first line
			//
			setError(d_socket->getError());
			d_socket->close();
		}
	}
	else
	{
		// could not connect
		//
		setError(d_socket->getError());	
	}
	return false;
}




bool SMTPImpl::sayHelo(const char *heloname)
{

	std::string hello = "HELO ";
	hello += heloname;
	hello += "\r\n";

	if(d_socket->sends(hello.c_str()))
	{
		const char *retval=d_socket->readline();
		if(retval)
		{
			if( retval[0] == '2')
			{
				return true;
			}
			else
			{
				std::string error= "SMTP Error - Error with HELO: ";
				error +=  retval;
				setError(error.c_str());
			}
		}
		else
		{
				std::string error= "SMTP Error - Error with HELO: ";
				error +=  d_socket->getError();
				setError(error.c_str());
		}
	}
	else
	{
		std::string error= "SMTP Error - Error with HELO: ";
		error +=  d_socket->getError();
		setError(error.c_str());
	}
	return false;
}


bool SMTPImpl::sayFrom(const char *from)
{
	std::string mailfrom = "MAIL FROM: ";
	mailfrom += from;
	mailfrom += "\r\n";
	
	if(d_socket->sends(mailfrom.c_str()))
	{
		const char *retval=d_socket->readline();
		if(retval)
		{
			if( retval[0] == '2')
			{
				return true;
			}
			else
			{
				std::string error= "SMTP Error - Error with FROM: ";
				error +=  retval;
				error += "(";
				error += mailfrom;
				error += ")";			
				setError(error.c_str());
			}
		}
		else
		{
			std::string error= "SMTP Error - Error with FROM: ";
			error +=  d_socket->getError();
			setError(error.c_str());
		}
	}
	else
	{
			std::string error= "SMTP Error - Error with FROM: ";
			error +=  d_socket->getError();
			setError(error.c_str());
	}
	return false;
}

bool SMTPImpl::addRecipient(const char *recipient)
{
	
	std::string recip = "RCPT TO: ";;
	recip += recipient;
	recip += "\r\n";
	
	d_socket->sends(recip.c_str());
	const char *retval=d_socket->readline();
	if(retval)
	{
		if( retval[0] == '2')
		{
			return true;
		}
		else
		{
			std::string error= "SMTP Error - Error with RCPT: ";
			error +=  retval;
			error +=  "(";
			error += recip;
			error += ")";
			setError(error.c_str());
		}
	}
	else
	{
		std::string error= "SMTP Error - Error with RCPT: ";
		error +=  d_socket->getError();
		setError(error.c_str());

	}
	
	return false;

}
bool SMTPImpl::sendData(const char *message)
{
	
	d_socket->sends("DATA\r\n");
	const char *retval=d_socket->readline();
	if(retval)
	{
		if( retval[0] != '3')
		{
			setError(retval);
			return false;
		}
	}
	else
	{
		setError(d_socket->getError());
		return false;
	}

		int length = strlen(message);

		std::string escaped="";
		char prev=0;
		for(int x=0; x< length ; x++)
		{
			switch(prev)
			{
				case '\r':
					if(message[x] == '.')
					{
						escaped += ".";
					}
					break;
				case '\n':
					if(message[x] == '.')
					{
						escaped += ".";
					}
					break;
				case '\f':
					if(message[x] == '.')
					{
						escaped += ".";
					}
					break;
				default:
					break;
			}
			escaped += message[x];
			prev=message[x];
			
		}
		d_socket->sends(escaped.c_str());
		d_socket->sends("\r\n.\r\n");
		
		retval=d_socket->readline();
		if(retval)
		{
			if( retval[0] == '2')
			{
				return true;
			}
			else
			{
				setError(retval);
				return false;
			}
		}
		else
		{
			setError(d_socket->getError());
			return false;
		}
	//////

}


bool SMTPImpl::disconnect()
{

	std::string quit = "QUIT\r\n";

	
	d_socket->sends(quit.c_str());
	const char *retval=d_socket->readline();
	if(retval)
	{
		if( retval[0] == '2')
		{
			return true;
		}
		else
		{
			setError(retval);
		}
	}
	else
	{
		setError(d_socket->getError());
	}
	
	return false;
}

}}

