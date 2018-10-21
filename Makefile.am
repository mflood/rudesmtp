# Makefile.am
#
# Copyright (C) 2003, 2004, 2005 Matthew Flood
# See file AUTHORS for contact information
#
# This file is part of RudeSMTP.
#
# RudeSMTP is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
# 
# RudeSMTP is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with RudeSMTP; (see COPYING) if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
# 02111-1307, USA.
#------------------------------------------------------------------------
lib_LTLIBRARIES = librudesmtp.la

librudesmtp_la_SOURCES = 		src/smtp.cpp \
	src/smtp.h \
	src/smtpimpl.cpp \
	src/smtpimpl.h

librudesmtp_la_LDFLAGS = -version-info 1:0:0
librudesmtp_la_LIBADD = -lrudesocket

pkginclude_HEADERS = src/smtp.h
pkgincludedir = $(includedir)/rude

AUTOMAKE_OPTIONS = gnits dist-bzip2 dist-shar dist-zip
