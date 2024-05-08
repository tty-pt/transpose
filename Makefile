LDFLAGS := -L/usr/lib -L/usr/local/lib
CFLAGS := -I/usr/include -I/usr/local/include
LDLIBS := -ldb
LD := clang

transp: transpose.c
	${LD} -o $@ transpose.c ${LDFLAGS} ${LDLIBS} ${CFLAGS}
