// SPDX-License-Identifier: MPL-2.0
/*
 *	zviewer
 *	A simple utility to monitor and read changes
 *	Copyright (c) 2024 Yao Zi.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <unistd.h>
#include <sys/inotify.h>
#include <sys/select.h>

#define fail_if(cond, msg) do { \
	if (cond) {							\
		perror(msg);						\
		exit(-1);						\
	}								\
} while (0)

static void
usage(const char *progname)
{
	fprintf(stderr, "USAGE:\n\t%s <FILE> <RENDER_PROG>\n", progname);
}

/*
 *	TODO: Vim renames the old file with a tlide suffix if "writebackup"
 *	is enabled. Don't consider the original file is lost in this case.
 */
static int
handle_event(struct inotify_event *ep)
{
	if (ep->mask & IN_DELETE_SELF)
		return 1;
	printf("should reload\n");
	return 0;
}

int
main(int argc, const char *argv[])
{
	if (argc != 3) {
		usage(argv[0]);
		return -1;
	}

	int watchfd = inotify_init1(IN_NONBLOCK);
	if (watchfd < 0) {
		perror("failed to create inotify instance");
		return -1;
	}

	const char *file = argv[1];
	int watchid = inotify_add_watch(watchfd, file, IN_CLOSE_WRITE	|
						       IN_MODIFY	|
						       IN_DELETE_SELF);
	if (watchid < 0) {
		fprintf(stderr, "failed to watch %s: %s\n", file,
							    strerror(errno));
		return -1;
	}

	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(watchfd, &fds);

	int ret = 0;
	while ((ret = select(FD_SETSIZE + 1, &fds, NULL, NULL, NULL)) >= 0) {
		if (FD_ISSET(watchfd, &fds)) {
			char buf[sizeof(struct inotify_event) + NAME_MAX + 1];

			ssize_t len = read(watchfd, buf, sizeof(buf));
			fail_if(len <= 0, "failed to read inotify event");

			for (char *p = buf; len;) {
				struct inotify_event *ep =
					(struct inotify_event *)p;

				if (handle_event(ep))
					goto do_exit;

				len -= sizeof(*ep) + ep->len;
				p += sizeof(*ep) + ep->len;
			}
		}

		FD_ZERO(&fds);
		FD_SET(watchfd, &fds);
	}

	fail_if(ret < 0, "failed to wait for changes");
do_exit:

	return 0;
}
