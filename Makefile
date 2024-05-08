LDFLAGS := -L/usr/lib
CFLAGS := -I/usr/include
LDLIBS := -ldb
LD := gcc

transp: transpose.c
	${LD} -o $@ $^ ${LDFLAGS} ${LDLIBS} ${CFLAGS}
