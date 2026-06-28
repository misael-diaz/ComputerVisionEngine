#ifndef ENGINE_CVE_H
#define ENGINE_CVE_H

#include <cstdint>
#include <X11/Xlib.h>
#include <time.h>

typedef int32_t CID;

extern "C" struct cluster {
	int32_t root;
	int32_t node;
	int32_t prev;
	int32_t next;
	int32_t size;
	int32_t super;
	int32_t total;
	int32_t id;
	int32_t mask;
	int32_t x;
	int32_t y;
	int32_t x_min;
	int32_t x_max;
	int32_t y_min;
	int32_t y_max;
	int32_t __pad;
};

extern "C" struct map {
        Display *display;
        int64_t GameWindow;
        struct timespec time_start;
        struct timespec time_target;
        struct timespec time_iddle;
};

static_assert(64 == sizeof(struct map));

extern "C" void *EngineInit(void);
extern "C" void EngineFree(void *base);
extern "C" void EngineTime(void *base);
extern "C" void EngineDelay(void *base);

#endif
