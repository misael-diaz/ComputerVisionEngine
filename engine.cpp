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
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include "engine.hpp"

#define persistent static
#define ENGINE_FPS_TARGET 30.0f
#define KBD_ESC XKeysymToKeycode(display, XK_Escape)

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
		pagesz +
		bytes_screen +
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

        errno = 0;
	rc = madvise(base, bytes_mmap, MADV_WILLNEED);
	if (-1 == rc) {
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

	XWindowAttributes attributes = {};
	XGetWindowAttributes(display, GameWindow, &attributes);
	int64_t const width = attributes.width;
	int64_t const height = attributes.height;
	int64_t const depth_window = attributes.depth;
	Visual *visual = attributes.visual;

	int32_t iters = 0;
	int32_t red_shift = 0;
	int32_t green_shift = 0;
	int32_t blue_shift = 0;
	int32_t const rgb_mask = 0xff;
	int32_t const red_mask = visual->red_mask;
	int32_t const green_mask = visual->green_mask;
	int32_t const blue_mask = visual->blue_mask;
	while ((rgb_mask << red_shift) != red_mask) {
		red_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	iters = 0;
	while ((rgb_mask << green_shift) != green_mask) {
		green_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	iters = 0;
	while ((rgb_mask << blue_shift) != blue_mask) {
		blue_shift += 8LU;
		if (iters > 2) {
			fprintf(stderr, "%s\n", "error: unexpected visual endianess");
			XCloseDisplay(display);
			_exit(1);
		}
		++iters;
	}

	// TODO: disable fullscreen toggling because the client might support this but we are enforcing a fixed sized window
	XSizeHints *SizeHintsGameWindow = XAllocSizeHints();
	if (!SizeHintsGameWindow) {
		XCloseDisplay(display);
		_exit(1);
	}

	// NOTES: fixes the game window dimensions so that we can do our work without defensive programming for handling dimension changes; in practice we don't want to change the game dimensions when we call this engine and so this guarantees that
	SizeHintsGameWindow->flags = (PMinSize | PMaxSize);
	SizeHintsGameWindow->min_width = width;
	SizeHintsGameWindow->max_width = width;
	SizeHintsGameWindow->min_height = height;
	SizeHintsGameWindow->max_height = height;
	XSetWMNormalHints(display, GameWindow, SizeHintsGameWindow);
	XSync(display, False);

	XShmSegmentInfo shminfo = {};
	XImage *GameImage = XShmCreateImage(
		display,
		visual,
		depth_pixel,
		ZPixmap,
		NULL,
		&shminfo,
		width,
		height
	);

	if (!GameImage) {
		XFree(SizeHintsGameWindow);
		XCloseDisplay(display);
		fprintf(stderr, "%s\n", "error: XShmCreateImage failed");
		_exit(1);
	}

	int64_t const bytes_per_pixel = (depth_pixel >> 3);
	int64_t const pitch = bytes_per_pixel * width;
	int64_t const pixels = (width * height);
	int64_t const bytes_backbuffer = (bytes_per_pixel * pixels);
	int64_t const bytes_framebuffer = (bytes_per_pixel * pixels);
	if (GameImage->bytes_per_line != pitch) {
		fprintf(stderr, "%s\n", "error: scanline length mismatch");
		XFree(SizeHintsGameWindow);
		XDestroyImage(GameImage);
		XCloseDisplay(display);
		_exit(1);
	}
	else if ((GameImage->bytes_per_line * GameImage->height) != bytes_framebuffer) {
		fprintf(stderr, "%s\n", "error: framebuffer size mismatch");
		XFree(SizeHintsGameWindow);
		XDestroyImage(GameImage);
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	rc = shmget(
		IPC_PRIVATE,
		bytes_framebuffer,
		IPC_CREAT | 0777
	);
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "error: failed to get shared-memory identifier");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XFree(SizeHintsGameWindow);
		XDestroyImage(GameImage);
		XCloseDisplay(display);
		_exit(1);
	}

	shminfo.shmid = rc;
	bytes_partition = bytes_framebuffer;
	bytes_clusters = pixels * sizeof(*clustep);
	bytes_cluster_list = pixels * sizeof(CID);
	// NOTE: the first page is reserved for the `struct map` after that we can do whatever we want but we opted to ensure 64-byte alignment of the clusters and cluster_list arrays
	int64_t const offset_partition = pagesz;
	int64_t const offset_clusters = (
		(((offset_partition + bytes_partition) + 0x3fL) & (~0x3fL))
	);
	int64_t const offset_cluster_list = (
		(((offset_clusters + bytes_clusters) + 0x3fL) & (~0x3fL))
	);
	int64_t const offset_backbuffer = (
		(((offset_cluster_list + bytes_cluster_list) + 0x3fL) & (~0x3fL))
	);
	// NOTE: `shmat` requires the framebuffer address to be paged aligned
	int64_t const offset_framebuffer = (
		(((offset_backbuffer + bytes_backbuffer) + mask_page) & (~mask_page))
	);

	void *framebuffer = ((char*) base) + offset_framebuffer;
	if (((uintptr_t) framebuffer) & mask_page) {
		fprintf(stderr, "%s\n", "error: framebuffer not paged aligned");
		XFree(SizeHintsGameWindow);
		XDestroyImage(GameImage);
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	shminfo.shmaddr = GameImage->data = ((char*) shmat(shminfo.shmid, framebuffer, SHM_REMAP));
	if (shminfo.shmaddr != framebuffer) {
		fprintf(stderr, "%s\n", "error: shmat changed the framebuffer address");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XFree(SizeHintsGameWindow);
		XDestroyImage(GameImage);
		XCloseDisplay(display);
		_exit(1);
	}

	shminfo.readOnly = False;
	if (!XShmAttach(display, &shminfo)) {
		fprintf(stderr, "%s\n", "error: XShmAttach failed");
		shmdt(shminfo.shmaddr);
		shmctl(shminfo.shmid, IPC_RMID, 0);
		// NOTE: framebuffer data is not heap allocated and so we must nullify it for XDestroyImage otherwise it will attempt to free a memory mapped region
		GameImage->data = NULL;
		XDestroyImage(GameImage);
		XFree(SizeHintsGameWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	errno = 0;
	uint64_t const plane_mask = 0xffffff;
	if (!XShmGetImage(display, GameWindow, GameImage, 0, 0, plane_mask)) {
		fprintf(stderr, "%s\n", "error: XShmGetImage failed");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		XShmDetach(display, &shminfo);
		shmdt(shminfo.shmaddr);
		shmctl(shminfo.shmid, IPC_RMID, 0);
		GameImage->data = NULL;
		XDestroyImage(GameImage);
		XFree(SizeHintsGameWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	XSetWindowAttributes OutputWindowAttributes = {};
	OutputWindowAttributes.background_pixel = BlackPixelOfScreen(screen);
	OutputWindowAttributes.event_mask = (
		ExposureMask |
		KeyPressMask |
		0
	);

	Window OutputWindow = XCreateWindow(
		display,
		DefaultRootWindow(display),
		0,
		0,
		width,
		height,
		0,
		depth_window,
		InputOutput,
		DefaultVisualOfScreen(DefaultScreenOfDisplay(display)),
		CWBackPixel | CWEventMask,
		&OutputWindowAttributes
	);

	// TODO: you may want to disable full screen toggling for the output window
	XSizeHints *SizeHints = XAllocSizeHints();
	if (!SizeHints) {
		fprintf(stderr, "%s\n", "error; XSizeHints allocation failed");
		XShmDetach(display, &shminfo);
		shmdt(shminfo.shmaddr);
		shmctl(shminfo.shmid, IPC_RMID, 0);
		GameImage->data = NULL;
		XDestroyImage(GameImage);
		XFree(SizeHintsGameWindow);
		XDestroyWindow(display, OutputWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	SizeHints->flags = (PMinSize | PMaxSize);
	SizeHints->min_width = width;
	SizeHints->max_width = width;
	SizeHints->min_height = height;
	SizeHints->max_height = height;
	XSetWMNormalHints(display, OutputWindow, SizeHints);
	XStoreName(display, OutputWindow, "Handcrafted Blue Computer Vision Engine");

	XMapWindow(display, OutputWindow);
	XWindowEvent(display, OutputWindow, ExposureMask, &ev);

	char *backbuffer = (((char*) base) + offset_backbuffer);
	XImage *OutputImage = XCreateImage(
		display,
		DefaultVisualOfScreen(DefaultScreenOfDisplay(display)),
		depth_pixel,
		ZPixmap,
		0,
		backbuffer,
		width,
		height,
		depth_pixel,
		0
	);

	if (!OutputImage) {
		fprintf(stderr, "%s\n", "error: XCreateImage failed");
		XShmDetach(display, &shminfo);
		shmdt(shminfo.shmaddr);
		shmctl(shminfo.shmid, IPC_RMID, 0);
		GameImage->data = NULL;
		XDestroyImage(GameImage);
		XFree(SizeHintsGameWindow);
		XFree(SizeHints);
		XDestroyWindow(display, OutputWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	if (
		(GameImage->width != OutputImage->width) ||
		(GameImage->height != OutputImage->height) ||
		(GameImage->format != OutputImage->format) ||
		(GameImage->depth != OutputImage->depth) ||
		(GameImage->red_mask != OutputImage->red_mask) ||
		(GameImage->green_mask != OutputImage->green_mask) ||
		(GameImage->blue_mask != OutputImage->blue_mask) ||
		(GameImage->bitmap_pad != OutputImage->bitmap_pad) ||
		(GameImage->bitmap_bit_order != OutputImage->bitmap_bit_order) ||
		(GameImage->bytes_per_line != OutputImage->bytes_per_line) ||
		(GameImage->bits_per_pixel != OutputImage->bits_per_pixel) ||
		0
	   ) {
		fprintf(stderr, "%s\n", "error: surprising XImage mistmatch");
		XShmDetach(display, &shminfo);
		shmdt(shminfo.shmaddr);
		shmctl(shminfo.shmid, IPC_RMID, 0);
		GameImage->data = NULL;
		XDestroyImage(GameImage);
		OutputImage->data = NULL;
		XDestroyImage(OutputImage);
		XFree(SizeHintsGameWindow);
		XFree(SizeHints);
		XDestroyWindow(display, OutputWindow);
		XCloseDisplay(display);
		_exit(1);
	}

	// TODO: store the pointers to the heap allocated resources that were obtained via Xlib calls so that you can free them later
	// TODO: add the EngineUpdateAndRender() function

	int32_t running = 1;
	int32_t frameno = 0;
	struct map *data = (typeof(data)) base;
	data->display = display;
	data->GameWindow = GameWindow;
	data->OutputWindow = OutputWindow;
	data->running = running;
	data->frameno = frameno;
	data->shminfo = shminfo;
	data->bytes_partition = bytes_partition;
	data->bytes_clusters = bytes_clusters;
	data->bytes_cluster_list = bytes_cluster_list;
	data->bytes_backbuffer = bytes_backbuffer;
	data->bytes_framebuffer = bytes_framebuffer;
	data->offset_partition = offset_partition;
	data->offset_clusters = offset_clusters;
	data->offset_cluster_list = offset_cluster_list;
	data->offset_backbuffer = offset_backbuffer;
	data->offset_framebuffer = offset_framebuffer;
	float constexpr FPSFloat = ENGINE_FPS_TARGET;
	float constexpr FPSInvFloat = 1.0e9f / FPSFloat;
	int64_t constexpr FrameDurationTargetNanoSec = FPSInvFloat;
	LinuxSetTimeSpec(&data->time_target, FrameDurationTargetNanoSec);
	fprintf(stdout, "GameWindow: %d\n", data->GameWindow);
	return base;
}

extern "C" void EngineFree(void *base)
{
	// TODO: don't forget to nullify the data member of XImages because it's not heap allocated
	if (!base) {
		fprintf(stderr, "%s\n", "error: NULL pointer error");
		_exit(1);
	}	
	struct map *data = (typeof(data)) base;
	XShmDetach(data->display, &data->shminfo);
	shmdt(data->shminfo.shmaddr);
	shmctl(data->shminfo.shmid, IPC_RMID, 0);
	XCloseDisplay(data->display);
}

extern "C" int EngineUpdateAndRender(void *base)
{
	int rc = 1;
	XEvent ev = {};
	struct map *data = (typeof(data)) base;
	Display *display = data->display;
	Window OutputWindow = data->OutputWindow;
	if (XCheckTypedWindowEvent(display, OutputWindow, KeyPress, &ev)) {
		if ((KBD_ESC == ev.xkey.keycode)) {
			rc = data->running = 0;
			fprintf(stdout, "%s\n", "quitting upon user request");
			return rc;
		}
	}

	return rc;
}

extern "C" void EngineTime(void *base)
{
	struct map *data = (typeof(data)) base;
	clock_gettime(CLOCK_MONOTONIC, &data->time_start);
}

#if DEVBUILD
extern "C" void EngineDelay(void *base)
{
	struct map *data = (typeof(data)) base;
	LinuxSetDelayTime(&data->time_iddle, &data->time_start, &data->time_target);
	LinuxDelay(CLOCK_MONOTONIC, &data->time_iddle);
	if (64 == data->frameno) {
		data->frameno = 0;
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
		data->frameno++;
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
