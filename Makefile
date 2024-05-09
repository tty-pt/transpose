LDFLAGS := -L/usr/lib -L/usr/local/lib
CFLAGS := -I/usr/include -I/usr/local/include
LDLIBS := -ldb
UNAME != uname
LD-Linux := gcc
LD-OpenBSD := clang
LD := ${LD-${UNAME}}

transp: transpose.c
	${LD} -o $@ transpose.c ${LDFLAGS} ${LDLIBS} ${CFLAGS}
