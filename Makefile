INCLUDEDIR:=include
LIBS:= -lpthread

DEFINES:=-D_REENTRANT 
CFLAGS:=-g -O2 -Wall -Wunused-result
LDFLAGS=:
CC:=gcc
PROG:=ulogd
INCLUDES:= -I${INCLUDEDIR}
DEPDIR:=deps

SOURCES:=$(wildcard *.c)
OBJECTS:=${SOURCES:.c=.o}

PREFIX=/usr/local/ulogd
DESTDIR=

.PHONY: clean install check depend

all: check depend ${PROG}

$(PROG): ${OBJECTS}
	${CC} ${CFLAGS} $^ -o $@ ${LIBS}

${OBJECTS}: ${SOURCES} 
	${CC} ${CFLAGS} ${INCLUDES} ${DEFINES} $^ -c

depend: 
	${CC} ${INCLUDES} -M -MM -MD ${SOURCES}
	@mv *.d ${DEPDIR}

clean:
	rm -f ${DEPDIR}/*.d
	rm -f *.o ${PROG}

check:
	@if [ "`uname`" != "Linux" ]; then \
		echo "Sorry, linux required, not `uname`"; \
		exit 1; \
	fi

install:
	@mkdir -p ${DESTDIR}/${PREFIX}/sbin
	install -m 750 ulogd ${DESTDIR}/${PREFIX}/sbin
	install -m 755 ulogd.init ${DESTDIR}/etc/init.d/ulogd

-include ${DEPDIR}/*.d
