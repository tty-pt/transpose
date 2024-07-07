DESTDIR ?= ../../../../
INSTALL_DEP ?= ${DESTDIR}make_dep.sh
PREFIX ?= usr

LDFLAGS := -L/usr/lib -L/usr/local/lib
CFLAGS := -I/usr/include -I/usr/local/include
LDLIBS := -ldb
UNAME != uname
LD-Linux := gcc
LD-OpenBSD := clang
LD := ${LD-${UNAME}}

transp: transpose.c
	${LD} -o $@ transpose.c ${LDFLAGS} ${LDLIBS} ${CFLAGS}

$(DESTDIR)$(PREFIX)/bin/transp: transp
	install -m 755 transp $@
	${INSTALL_DEP} ${@:${DESTDIR}%=%}

install: ${DESTDIR}${PREFIX}/bin/transp

clean:
	rm transp || true

.PHONY: install clean
