#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*

Copyright (c) 2026 Misael Díaz-Maldonado
This source file is released under the MIT License.
See LICENSE file in the project root for the full license information.

*/

#include <cstdio>
#include "engine.h"

int main()
{
	void *base = EngineInit();
	struct map *data = (typeof(data)) base;
	fprintf(stdout, "GameWindow: %ld\n", data->GameWindow);
	EngineFree(base);
	return 0;
}
