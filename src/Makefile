# Surgical Strike Free Software.
# Copyright (C) 2008, 2014 Rob Myers
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option, and if the Coin3D library supports it) any later
# version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

all: lex.yy.c y.tab.cpp surgical_strike

lex.yy.c: surgical_strike.l
	flex surgical_strike.l

y.tab.cpp: surgical_strike.y
	bison --verbose --debug --defines surgical_strike.y -o y.tab.cpp

# Suppress unused-function as lex.yy.c has a generated static one

surgical_strike: lex.yy.o y.tab.cpp surgical_strike.cpp
	c++ -Wall -Wno-unused-function -g \
	lex.yy.c y.tab.cpp surgical_strike.cpp \
	-lOpenThreads \
	-losg -losgDB -losgUtil -losgGA -losgText -losgViewer \
	-o surgical_strike

install:
	install surgical_strike /usr/local/bin/

clean:
	rm -f *.o
	rm -f lex.yy.c
	rm -f y.tab.cpp
	rm -f surgical_strike
	rm -f y.output
	rm -f y.tab.hpp
