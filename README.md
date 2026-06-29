# ComputerVisionEngine
Python C/C++ Interoperable Computer Vision Engine

## Week 1 Querying the Game Window Resource ID

**Gets the Game Window**: gets the X11 Window Resource ID of the Game Window. The implementation is based on Xorg's `xwininfo` but tailored for the demo that only has the bare minimum code to retrieve the Window ID by clicking the Game Window with the mouse.

[![QueryingWindowResourceID](https://img.youtube.com/vi/oOI-gqFBDTg/hqdefault.jpg)](https://youtu.be/oOI-gqFBDTg)

**Tracking-Player from Python**: from the onset I have verified that the engine can be called from Python with little ceremony.

[![QueryingWindowResourceID](https://img.youtube.com/vi/t6irqflaQ8s/hqdefault.jpg)](https://youtu.be/t6irqflaQ8s)

**Heartbeat**: The python orchestrator now has a steady heartbeat running at 30 FPS by leveraging the time utilities that the C/C++ engine provides. The point here is to keep the Python code simple and unaffected by implementation changes of the core engine.

[![SteadyHeartbeatPython](https://img.youtube.com/vi/42Rohg8A_lE/hqdefault.jpg)](https://youtu.be/42Rohg8A_lE)

**memory-leaks**: addresses memory leaks by storing the heap allocated data that Xlib requires in the engine memory map structure (commit hash: 72dc36461db1088b604d6476e32a51aa1e5a0b1d).

[![NoMemoryLeaks](https://img.youtube.com/vi/CWouIx97tEo/hqdefault.jpg)](https://youtu.be/CWouIx97tEo)

## Build

To build the standalone C/C++ code:

```sh
g++ -DDEVBUILD=1 -Wall -Wextra -Wformat -O0 -gdwarf-4 -g engine.cpp main.cpp -o engine.bin -lX11 -lXext
```

And to build the interoperable library:

```sh
g++ -DDEVBUILD=1 -fPIC -Wall -Wextra -Wformat -O0 -gdwarf-4 -g -shared engine.cpp -o engine.so -lX11 -lXext
```

if you wish to compile the production code set `DEVBUILD` to zero or simply omit it from the command-line string.

## Run

To run from Python, copy the source code

```py
import ctypes

engine = ctypes.cdll.LoadLibrary("./engine.so")

# initializes engine
engine.EngineInit.restype = ctypes.c_void_p
base = engine.EngineInit()

# engine-loop fixed framerate
while True:
    engine.EngineTime(ctypes.c_void_p(base))
    if engine.EngineUpdateAndRender(ctypes.c_void_p(base)) == 0:
        break
    engine.EngineDelay(ctypes.c_void_p(base))

engine.EngineFree(ctypes.c_void_p(base))
```

and store it in the file `track-player.py`.

Here you see my philosophy for writing interoperable code. If you own the library you are free to keep all the complexities in the library code and only use Python as the orchestrator. The advantage is that you don't need to change your script if you change the engine's implementation as long you return the base memory address to the script so that it can pass it to engine calls. When would you appreciate this architecture? If you have your django backend already and it's a big codebase (you are already invested) and you just want an endpoint to process data at speeds close to the bare metal.

To run the script:

```sh
python3 track-player.py
```
