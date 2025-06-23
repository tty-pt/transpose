include env.mk
PREFIX ?= usr

npm-lib := @tty-pt/qdb
-include node_modules/@tty-pt/mk/include.mk
-include ../mk/include.mk

LDFLAGS += -L/usr/local/lib
CFLAGS += -g -I/usr/local/include -Wall -Wextra -pedantic
exe ?= transp

$(exe): $(exe).c
	${CC} -o $@ $(exe).c ${LDFLAGS} ${CFLAGS}

$(DESTDIR)$(PREFIX)/bin/$(exe): $(exe)
	install -m 755 $(exe) $@

install: ${DESTDIR}${PREFIX}/bin/transp

clean:
	rm $(exe) || true

.PHONY: install clean
