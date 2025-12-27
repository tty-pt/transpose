include env.mk
PREFIX ?= /var/www/usr
all := transp
LDLIBS := -lqmap
CFLAGS := -g

-include ../mk/include.mk
-include ../../../../mk/include.mk
