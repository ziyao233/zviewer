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
#include <sys/wait.h>

#define fail_if(cond, msg) do { \
	if (cond) {							\
		perror(msg);						\
		exit(-1);						\
	}								\
} while (0)

struct {
	const char **renderCmd;
	int cmdLen;
	char **contents;
	size_t nlines;
} G;

static void
usage(const char *progname)
{
	fprintf(stderr, "USAGE:\n\t%s <FILE> <RENDER_PROG>\n", progname);
}

static char **
do_render(size_t *rlines)
{
	int pipefds[2];
	fail_if(pipe(pipefds) < 0, "failed to create pipe");

	int pid = fork();

	fail_if(pid < 0, "failed to run the render");

	if (pid) {
		/* parent */
		close(pipefds[1]);

		FILE *rfp = fdopen(pipefds[0], "rb");
		fail_if(!rfp, "cannot read from the render");

		char **newContents = NULL;
		char *line = NULL;
		size_t len = 0;
		size_t nlines = 0;
		while (getline(&line, &len, rfp) != -1) {
			nlines++;
			newContents = realloc(newContents,
					      sizeof(char *) * nlines);
			fail_if(!newContents, "failed to read from the render");

			newContents[nlines - 1] = line;

			len = 0;
			line = NULL;
		}

		fclose(rfp);

		int wstatus = 0;
		fail_if(waitpid(pid, &wstatus, 0) < 0,
			"failed to read from the render");

		if (!WIFEXITED(wstatus)) {
			fprintf(stderr, "render terminated");
		} else if (WEXITSTATUS(wstatus) != 0) {
			fprintf(stderr, "render failed");
		} else {
			*rlines = nlines;

			return newContents;
		}

		if (nlines)
			fprintf(stderr, ": %s", newContents[0]);
		else
			fprintf(stderr, "\n");

		exit(-1);
	} else {
		/*
		 *	child (the render)
		 *	XXX: should we redirect stdin to /dev/null?
		 */
		close(STDOUT_FILENO);
		close(STDERR_FILENO);

		close(pipefds[0]);

		if (dup2(pipefds[1], STDOUT_FILENO) < 0)
			exit(-1);
		if (dup2(pipefds[1], STDERR_FILENO) < 0)
			exit(-1);

		fail_if(execvp(G.renderCmd[0], (char **)G.renderCmd),
			"failed to execute render");
	}

	return 0;	// never reaches here
}

static void
do_reload(void)
{
	size_t nlines;
	char **newContents = do_render(&nlines);

	puts("==========================");
	for (size_t i = 0; i < nlines; i++) {
		char *line = newContents[i];
		fwrite(line, 1, strlen(line), stdout);
	}

	for (size_t i = 0; i < G.nlines; i++)
		free(G.contents[i]);
	free(G.contents);

	G.contents = newContents;
	G.nlines = nlines;
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

	do_reload();
	return 0;
}

int
main(int argc, const char *argv[])
{
	if (argc < 3) {
		usage(argv[0]);
		return -1;
	}

	G.cmdLen = argc - 2;
	G.renderCmd = argv + 2;

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

	do_reload();

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
