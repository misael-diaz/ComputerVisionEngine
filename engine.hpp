#ifndef ENGINE_CVE_H
#define ENGINE_CVE_H

#include <X11/Xlib.h>
#include <time.h>

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
