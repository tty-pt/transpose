include env.mk
PREFIX ?= /var/www/usr
INSTALL-BIN := transp
LDLIBS := -lqmap
CFLAGS := -g

npm-lib := @tty-pt/qmap
-include node_modules/@tty-pt/mk/include.mk
-include ../mk/include.mk
