// SPDX-License-Identifier: MPL-2.0
/*
 *	zviewer
 *	A simple utility to monitor and read changes
 *	Copyright (c) 2024 Yao Zi.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/wait.h>

#include <curses.h>

#define fail_if(cond, _msg) do { \
	if (cond) {							\
		G.err = errno;					\
		G.msg = _msg;						\
		exit(-1);						\
	}								\
} while (0)

struct {
	const char **renderCmd;
	int cmdLen;

	char **contents;
	size_t nlines;

	/* we must put off error messages until curses cleans up */
	int err;
	char *msg;

	int cursesEnabled;
	WINDOW *pad;
	int rowoff;
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

		const char *msg;
		if (!WIFEXITED(wstatus)) {
			msg = "render terminated";
		} else if (WEXITSTATUS(wstatus) != 0) {
			msg = "render failed";
		} else {
			*rlines = nlines;

			return newContents;
		}

		if (nlines)
			asprintf(&G.msg, "%s: %s", msg, newContents[0]);
		else
			asprintf(&G.msg, "%s\n", msg);

		G.err = -1;
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
set_rowoff(int y)
{
	if (y < 0)
		y = 0;
	else if (G.nlines <= (size_t)LINES)
		y = 0;
	else if ((size_t)y >= G.nlines - (size_t)LINES)
		y = G.nlines - LINES;
	G.rowoff = y;
}

static void
do_reload(void)
{
	size_t nlines;
	char **newContents = do_render(&nlines);

	delwin(G.pad);
	G.pad = newpad(nlines + 1, COLS);

	for (size_t i = 0; i < nlines; i++)
		waddstr(G.pad, newContents[i]);

	/* looking for the changed part and move the focus to it */
	int rowoff = -1;

	/* check for changes in the prefix */
	for (size_t i = 0; i < G.nlines && i < nlines; i++) {
		if (strcmp(G.contents[i], newContents[i])) {
			rowoff = i;
			break;
		}
	}

	if (rowoff < 0) {
		if (!G.contents)		// first load
			rowoff = 0;
		else if (G.nlines != nlines)	// append/delete at tail
			rowoff = nlines;
		else
			rowoff = G.rowoff;
	}

	for (size_t i = 0; i < G.nlines; i++)
		free(G.contents[i]);
	free(G.contents);

	G.contents = newContents;
	G.nlines = nlines;

	set_rowoff(rowoff);
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

static void
curses_init(void)
{
	initscr();
	cbreak();
	noecho();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	curs_set(0);

	G.cursesEnabled = 1;
}

static void
curses_cleanup(void)
{
	if (G.cursesEnabled) {
		delwin(G.pad);
		endwin();
	}

	if (G.err > 0)
		fprintf(stderr, "%s: %s\n", G.msg, strerror(G.err));
	else if (G.err < 0)
		fputs(G.msg, stderr);
}

static void
draw_screen(void)
{
	erase();
	refresh();
	prefresh(G.pad, G.rowoff, 0, 0, 0, LINES - 1, COLS);
}

static void
handle_key(int key)
{
	static int last_key;
	switch (key) {
	case 'j':
	case KEY_DOWN:
		set_rowoff(G.rowoff + 1);
		break;
	case 'k':
	case KEY_UP:
	case KEY_ENTER:
		set_rowoff(G.rowoff - 1);
		break;
	case 'u':
	case KEY_NPAGE:
		set_rowoff(G.rowoff - LINES / 2);
		break;
	case 'd':
	case KEY_PPAGE:
		set_rowoff(G.rowoff + LINES / 2);
		break;
	case 'g':
		if (last_key == 'g') {
			set_rowoff(0);
			last_key = 0;
		} else {
			last_key = key;
		}
		break;
	case 'G':
		set_rowoff(G.nlines);
		break;
	case 'q':
		exit(0);
	default:
		last_key = key;
		break;
	}
}

int
main(int argc, const char *argv[])
{
	if (argc < 3) {
		usage(argv[0]);
		return -1;
	}

	setlocale(LC_ALL, "");

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

	curses_init();
	atexit(curses_cleanup);

	do_reload();
	draw_screen();

	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(watchfd, &fds);
	FD_SET(STDIN_FILENO, &fds);

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
		} else if (FD_ISSET(STDIN_FILENO, &fds)) {
			handle_key(getch());
		} else {
			abort();	// never reaches here
		}

		draw_screen();

		FD_ZERO(&fds);
		FD_SET(watchfd, &fds);
		FD_SET(STDIN_FILENO, &fds);
	}

	fail_if(ret < 0, "failed to wait for changes");
do_exit:

	return 0;
}
