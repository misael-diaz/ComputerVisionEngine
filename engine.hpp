#ifndef ENGINE_CVE_H
#define ENGINE_CVE_H

#include <X11/Xlib.h>

extern "C" struct map {
        Display *display;
        int64_t GameWindow;
};

static_assert(16 == sizeof(struct map));

extern "C" void *EngineInit(void);
extern "C" void EngineFree(void *base);

#endif
