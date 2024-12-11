# zviewer

zviewer pipes output of a program to the terminal and automatically reloads it
on change of the source file.

This allows you to view the latest changes in time, which is quite helpful
when writing documentation in non-WYSIWYG markup languages.

## Build

### Prerequisites

- Linux platform (zviewer relies on inotify API to work)
- A C99 compatible compiler
- The wide-character variant of ncurses library (`libncursesw`)

```
	$ ./build.sh		# generates executable zviewer, or
	$ ./build.sh release	# for a release build.
```

## Usage

```
	$ zviewer <file> <render-program> [arg1] [arg2] ...
```

For example,

```
	$ zviewer README.md ./md2txt README.md
```
