#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*

Copyright (c) 2026 Misael Díaz-Maldonado
This source file is released under the MIT License.
See LICENSE file in the project root for the full license information.

*/

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <X11/cursorfont.h>
#include <sys/mman.h>
#include "engine.hpp"

extern "C" void* EngineInit(void)
{
	errno = 0;
	int rc = 0;
	rc = sysconf(_SC_PAGESIZE);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	int64_t pagesz = rc;

        errno = 0;
	int64_t bytes_mmap = (pagesz << 1);
        void *base = mmap(NULL, bytes_mmap, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (MAP_FAILED == base) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	Display *display = XOpenDisplay(NULL);
	if (!display) {
		fprintf(stderr, "%s\n", "error: failed to open display");
		_exit(1);
	}

	Window root = DefaultRootWindow(display);
	Cursor cursor = XCreateFontCursor(display, XC_crosshair);
	rc = XGrabPointer(
			display,
			root,
			False,
			ButtonPressMask | ButtonReleaseMask,
			GrabModeSync,
			GrabModeAsync,
			root,
			cursor,
			CurrentTime
	);

	if (GrabSuccess != rc) {
		fprintf(stderr, "%s\n", "error: failed to grab pointer");
		XFreeCursor(display, cursor);
		XCloseDisplay(display);
		_exit(1);
	}

	XEvent ev = {};
	Window subwindow = 0;
	// NOTE: the game is not played with a mouse so that we know that we don't need to worry about previous button events unlike `xwininfo` because of its general purpose
	while (0 == subwindow) {
		XAllowEvents(display, SyncPointer, CurrentTime);
		while (XPending(display)) {
			XNextEvent(display, &ev);
			if (ButtonPress == ev.type) {
				if (0 == subwindow) {
					subwindow = ev.xbutton.subwindow;
					if (0 == subwindow) {
						subwindow = root;
					}
				}
				break;
			}
		}
	}

	XUngrabPointer(display, CurrentTime);

	int unsigned nchildren_return = 0;
	Window root_return = 0;
	Window parent_return = 0;
	Window *children_return = NULL;
	rc = XQueryTree(
		display,
		subwindow,
		&root_return,
		&parent_return,
		&children_return,
		&nchildren_return
	);

	if (!rc) {
		fprintf(stderr, "%s\n", "error: failed to query tree");
		XFreeCursor(display, cursor);
		XCloseDisplay(display);
		_exit(1);
	}
	else if (!children_return) {
		fprintf(stderr, "%s\n", "error: picked window with no children");
		XFreeCursor(display, cursor);
		XCloseDisplay(display);
		_exit(1);
	}

	Window GameWindow = 0;
	if (1 == nchildren_return) {
		GameWindow = children_return[0];
	}

	if (!GameWindow) {
		fprintf(stderr, "%s\n", "error: failed to get window");
		XFree(children_return);
		XFreeCursor(display, cursor);
		XCloseDisplay(display);
		_exit(1);
	}

	XFree(children_return);
	XFreeCursor(display, cursor);
	struct map *data = (typeof(data)) base;
	data->display = display;
	data->GameWindow = GameWindow;
	fprintf(stdout, "GameWindow: %ld\n", data->GameWindow);
	return base;
}

extern "C" void EngineFree(void* base)
{
	if (!base) {
		fprintf(stderr, "%s\n", "error: NULL pointer error");
		_exit(1);
	}	
	struct map *data = (typeof(data)) base;
	XCloseDisplay(data->display);
}
