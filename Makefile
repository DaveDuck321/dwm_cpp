# dwm - dynamic window manager
# See LICENSE file for copyright and license details.

include config.mk

SRC = drw.cpp dwm.cpp util.cpp
OBJ = ${SRC:.cpp=.o}

all: release

debug: CXXFLAGS += ${DEBUG_CXXFLAGS}
debug: LDFLAGS += ${DEBUG_LDFLAGS}
debug: options dwm

release: CXXFLAGS += ${RELEASE_CXXFLAGS}
release: LDFLAGS += ${RELEASE_LDFLAGS}
release: options dwm

options:
	@echo dwm build options:
	@echo "CXXFLAGS = ${CXXFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CXX      = ${CXX}"

.c.o:
	${CXX} -c ${CXXFLAGS} $<

${OBJ}: config.hpp config.mk

config.hpp:
	cp config.def.hpp $@

dwm: ${OBJ}
	${CXX} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f dwm ${OBJ} dwm-${VERSION}.tar.gz

dist: clean
	mkdir -p dwm-${VERSION}
	cp -R LICENSE Makefile README config.def.hpp config.mk\
		dwm.1 drw.hpp util.hpp ${SRC} dwm.png transient.cpp dwm-${VERSION}
	tar -cf dwm-${VERSION}.tar dwm-${VERSION}
	gzip dwm-${VERSION}.tar
	rm -rf dwm-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f dwm ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/dwm
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < dwm.1 > ${DESTDIR}${MANPREFIX}/man1/dwm.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/dwm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dwm\
		${DESTDIR}${MANPREFIX}/man1/dwm.1

.PHONY: all options clean dist install uninstall
