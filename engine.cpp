#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*

Copyright (c) 2026 Misael Díaz-Maldonado
This source file is released under the MIT License.
See LICENSE file in the project root for the full license information.

*/

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <X11/cursorfont.h>
#include <sys/mman.h>
#include "engine.hpp"

#define persistent static
#define ENGINE_FPS_TARGET 30.0f

// defines the handmade-hero Assert() macro function for those that know Casey Muratori's legendary game engine development series
#if DEVBUILD
#define Assert(x)\
	if (!(x)) {\
		fprintf(stderr, "assertion failed %s:%d\n", __FILE__, __LINE__);\
		*((volatile int*) 0) = 0;\
	}
#else
#define Assert(x)
#endif

inline void LinuxSetTimeSpec(
	struct timespec * const clock_time,
	int64_t const nsec
) {
	clock_time->tv_sec  = (nsec / 1000000000);
	clock_time->tv_nsec = (nsec % 1000000000);
}

inline void LinuxSetDelayTime(
        struct timespec * const clock_target,
        struct timespec const * const clock_start,
        struct timespec const * const clock_delta
) {
	clock_target->tv_sec = (
		(clock_start->tv_sec + clock_delta->tv_sec) +
		((clock_start->tv_nsec + clock_delta->tv_nsec) / 1000000000)
	);
	clock_target->tv_nsec = (
		((clock_start->tv_nsec + clock_delta->tv_nsec) % 1000000000)
	);
}

inline void LinuxDiffTimeSpec(
	struct timespec * const clock_delta,
	struct timespec const * const clock_start,
	struct timespec const * const clock_end
) {
	int64_t nsec_diff = 0;
	int64_t const nsec_start = 1000000000 * clock_start->tv_sec + clock_start->tv_nsec;
	int64_t const nsec_end   = 1000000000 *   clock_end->tv_sec +   clock_end->tv_nsec;
	if (nsec_end > nsec_start) {
		nsec_diff = (nsec_end - nsec_start);
	} else {
		nsec_diff = (nsec_start - nsec_end);
	}
	clock_delta->tv_sec  = (nsec_diff / 1000000000);
	clock_delta->tv_nsec = (nsec_diff % 1000000000);
}

inline void LinuxDelay(
        clockid_t clock_id,
        struct timespec const * const clock_target
) {
        int rc = 0;
        Assert(CLOCK_MONOTONIC == clock_id);
        do {
                rc = clock_nanosleep(clock_id, TIMER_ABSTIME, clock_target, NULL);
                Assert(EFAULT != rc);
                Assert(EINVAL != rc);
        } while (EINTR == rc);
}

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

	int64_t const pagesz = rc;
	int64_t const mask_page = (pagesz - 1);

	Display *display = XOpenDisplay(NULL);
	if (!display) {
		fprintf(stderr, "%s\n", "error: failed to open display");
		_exit(1);
	}

	Screen *screen = DefaultScreenOfDisplay(display);
	int32_t const width_screen = WidthOfScreen(screen);
	int32_t const height_screen = HeightOfScreen(screen);
	int64_t const pixels_screen = (width_screen * height_screen);
	// NOTE: assuming 32-bit depth for a pixel, even if the visual depth is 24-bits the XImage data can still have a 32-bit depth and this is what we are counting on
	int32_t const depth_pixel = 32;
	// NOTE: assuming that the scanline bytes are exactly width_screen x depth_pixel, at least that has been my experience so far with XImages; if not the case, we can know that after getting the XImage data
	int64_t const bytes_screen = depth_pixel * pixels_screen;
	int64_t bytes_partition = bytes_screen;
	struct cluster stud = {};
	struct cluster *clustep = &stud;
	int64_t bytes_clusters = pixels_screen * sizeof(*clustep);
	int64_t bytes_cluster_list = pixels_screen * sizeof(CID);
	int64_t bytes_required = (
		bytes_screen +
		bytes_partition +
		bytes_clusters +
		bytes_cluster_list +
		0
        );
        int64_t bytes_aligned = ((bytes_required + mask_page) & (~mask_page));
        int64_t bytes_mmap = (bytes_aligned << 1);

        errno = 0;
        void *base = mmap(NULL, bytes_mmap, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (MAP_FAILED == base) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XCloseDisplay(display);
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
	float constexpr FPSFloat = ENGINE_FPS_TARGET;
        float constexpr FPSInvFloat = 1.0e9f / FPSFloat;
        int64_t constexpr FrameDurationTargetNanoSec = FPSInvFloat;
	LinuxSetTimeSpec(&data->time_target, FrameDurationTargetNanoSec);
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

extern "C" void EngineTime(void *base)
{
	struct map *data = (typeof(data)) base;
	clock_gettime(CLOCK_MONOTONIC, &data->time_start);
}

#if DEVBUILD
extern "C" void EngineDelay(void *base)
{
	persistent int64_t frameno = 0;
	struct map *data = (typeof(data)) base;
	LinuxSetDelayTime(&data->time_iddle, &data->time_start, &data->time_target);
	LinuxDelay(CLOCK_MONOTONIC, &data->time_iddle);
	if (64 == frameno) {
		frameno = 0;
		struct timespec time_delta = {};
		struct timespec time_end = {};
		clock_gettime(CLOCK_MONOTONIC, &time_end);
		LinuxDiffTimeSpec(&time_delta, &data->time_start, &time_end);
		float const etime = (
			1.0e+3 * time_delta.tv_sec +
			1.0e-6 * time_delta.tv_nsec
		);
		float const FPS = 1.0e+3f / etime;
		fprintf(stdout, "\nFPS: %.1f\netime (ms): %.1f\n", FPS, etime);
	}
	else {
		++frameno;
	}
}
#else
extern "C" void EngineDelay(void *base)
{
	struct map *data = (typeof(data)) base;
	LinuxSetDelayTime(&data->time_iddle, &data->time_start, &data->time_target);
	LinuxDelay(CLOCK_MONOTONIC, &data->time_iddle);
}
#endif
