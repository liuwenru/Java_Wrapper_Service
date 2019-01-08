# Copyright (c) 1999, 2018 Tanuki Software, Ltd.
# http://www.tanukisoftware.com
# All rights reserved.
#
# This software is the proprietary information of Tanuki Software.
# You shall use it only in accordance with the terms of the
# license agreement you entered into with Tanuki Software.
# http://wrapper.tanukisoftware.com/doc/english/licenseOverview.html


COMPILE = gcc -O3 -fpic -Wall --pedantic -mlp64 -DHPUX_IA -DHPUX -DJSW64 -D_INCLUDE__STDC_A1_SOURCE -DUNICODE -D_UNICODE

INCLUDE=$(JAVA_HOME)/include

DEFS = -I$(INCLUDE) -I$(INCLUDE)/hp-ux

wrapper_SOURCE = wrapper.c wrapperinfo.c wrappereventloop.c wrapper_unix.c property.c logger.c logger_file.c wrapper_file.c wrapper_i18n.c wrapper_hashmap.c wrapper_ulimit.c wrapper_encoding.c wrapper_jvminfo.c

libwrapper_so_SOURCE = wrapper_i18n.c wrapperjni_unix.c wrapperinfo.c wrapperjni.c loggerjni.c

BIN = ../../bin
LIB = ../../lib

all: init wrapper libwrapper.so

clean:
	rm -f *.o

cleanall: clean
	rm -rf *~ .deps
	rm -f $(BIN)/wrapper $(LIB)/libwrapper.so

init:
	if test ! -d .deps; then mkdir .deps; fi

wrapper: $(wrapper_SOURCE)
	$(COMPILE) $(wrapper_SOURCE) -lm -lpthread -o $(BIN)/wrapper

libwrapper.so: $(libwrapper_so_SOURCE)
	${COMPILE} ${DEFS} $(libwrapper_so_SOURCE) -shared -lm -lpthread -o $(LIB)/libwrapper.so

%.o: %.c
	${COMPILE} -c ${DEFS} $<
