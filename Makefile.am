AUTOMAKE_OPTIONS = foreign
SUBDIRS = src tests bench

CFLAGS ?= -O2 -Wall -Wshadow -pipe

test: all
	tests/test.sh
