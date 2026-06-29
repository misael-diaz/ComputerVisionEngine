#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*

Copyright (c) 2026 Misael Díaz-Maldonado
This source file is released under the MIT License.
See LICENSE file in the project root for the full license information.

*/

#include <cstdio>
#include "engine.hpp"

int main()
{
	void *base = EngineInit();
	struct map *data = (typeof(data)) base;
	fprintf(stdout, "GameWindow: %d\n", data->GameWindow);
	while (1) {
		EngineTime(base);
		if (!EngineUpdateAndRender(base)) {
			break;
		}
		EngineDelay(base);
	}
	EngineFree(base);
	return 0;
}
