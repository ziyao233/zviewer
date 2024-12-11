#!/usr/bin/env sh

if [ "$1" = release ]; then
	BUILD_FLAGS="-O2"
else
	DEBUG_FLAGS="-O0 -DDEBUG -Werror"
fi

cc zviewer.c -o zviewer $BUILD_FLAGS -g -Wall -Wextra -pedantic -lncursesw \
	$CFLAGS $LDFLAGS
