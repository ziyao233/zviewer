// SPDX-License-Identifier: MPL-2.0
/*
 *	zviewer
 *	A simple utility to monitor and read changes
 *	Copyright (c) 2024 Yao Zi.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <errno.h>

#include <curses.h>

static void
usage(const char *progname)
{
	fprintf(stderr, "USAGE:\n\t%s <FILE> <RENDER_PROG>\n", progname);
}

int
main(int argc, const char *argv[])
{
	if (argc != 2) {
		usage(argv[0]);
		return -1;
	}

	return 0;
}
