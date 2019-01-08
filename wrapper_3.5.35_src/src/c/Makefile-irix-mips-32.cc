# Copyright (c) 1999, 2018 Tanuki Software, Ltd.
# http://www.tanukisoftware.com
# All rights reserved.
#
# This software is the proprietary information of Tanuki Software.
# You shall use it only in accordance with the terms of the
# license agreement you entered into with Tanuki Software.
# http://wrapper.tanukisoftware.com/doc/english/licenseOverview.html

# Makefile for SGI IRIX 6.5 (may work on other versions as well but not tested)
# MIPSpro Compilers: Version 7.3.1.3m
COMPILE = cc -DIRIX -KPIC  -DUNICODE -D_UNICODE

INCLUDE=$(JAVA_HOME)/include

DEFS = -I$(INCLUDE) -I$(INCLUDE)/irix

wrapper_OBJECTS = wrapper.o wrapperinfo.o wrappereventloop.o wrapper_unix.o property.o logger.o logger_file.o wrapper_file.o wrapper_i18n.o wrapper_hashmap.o wrapper_ulimit.o wrapper_encoding.o wrapper_jvminfo.o

libwrapper_so_OBJECTS = wrapper_i18n.o wrapperjni_unix.o wrapperinfo.o wrapperjni.o loggerjni.o

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

wrapper: $(wrapper_OBJECTS)
	$(COMPILE) $(wrapper_OBJECTS) -o $(BIN)/wrapper -lm -lpthread

libwrapper.so: $(libwrapper_so_OBJECTS)
	${COMPILE} -shared -no_unresolved -n32 -all $(libwrapper_so_OBJECTS) -o $(LIB)/libwrapper.so

%.o: %.c
	@echo '$(COMPILE) -c $<'; \
	$(COMPILE) $(DEFS) -c $<

