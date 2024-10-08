include env.mk
PREFIX ?= usr

LDFLAGS += -L/usr/local/lib
CFLAGS += -I/usr/local/include

$(exe): $(exe).c
	${CC} -o $@ $(exe).c ${LDFLAGS} ${CFLAGS}

$(DESTDIR)$(PREFIX)/bin/$(exe): $(exe)
	install -m 755 $(exe) $@

install: ${DESTDIR}${PREFIX}/bin/transp

clean:
	rm $(exe) || true

.PHONY: install clean
