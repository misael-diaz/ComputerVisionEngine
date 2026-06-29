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

/* TODO: add the frame counter `frameno` to struct map to remove local static variable from EngineDelay */
extern "C" struct map {
        Display *display;
        int32_t GameWindow;
        int32_t OutputWindow;
        int32_t running;
        int32_t frameno;
        struct timespec time_start;
        struct timespec time_target;
        struct timespec time_iddle;
	int64_t bytes_partition;
	int64_t bytes_clusters;
	int64_t bytes_cluster_list;
	int64_t bytes_framebuffer;
	int64_t bytes_backbuffer;
	int64_t offset_partition;
	int64_t offset_clusters;
	int64_t offset_cluster_list;
	int64_t offset_framebuffer;
	int64_t offset_backbuffer;
	int64_t _pad[13];
};

static_assert(256 == sizeof(struct map));

extern "C" void *EngineInit(void);
extern "C" void EngineFree(void *base);
extern "C" int EngineUpdateAndRender(void *base);
extern "C" void EngineTime(void *base);
extern "C" void EngineDelay(void *base);

#endif
