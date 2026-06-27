# ComputerVisionEngine
Python C/C++ Interoperable Computer Vision Engine

## Week 1 Querying the Game Window Resource ID

**Gets the Game Window**: gets the X11 Window Resource ID of the Game Window. The implementation is based on Xorg's `xwininfo` but tailored for the demo that only has the bare minimum code to retrieve the Window ID by clicking the Game Window with the mouse.

[![QueryingWindowResourceID](https://img.youtube.com/vi/oOI-gqFBDTg/hqdefault.jpg)](https://youtu.be/oOI-gqFBDTg)

**Tracking-Player from Python**: from the onset I have verified that the engine can be called from Python with little ceremony.

[![QueryingWindowResourceID](https://img.youtube.com/vi/t6irqflaQ8s/hqdefault.jpg)](https://youtu.be/t6irqflaQ8s)

## Build

To build the standalone C/C++ code:

```sh
g++ -Wall -Wformat -O0 -gdwarf-4 -g engine.cpp main.cpp  -o engine.bin -lX11
```

And to build the interoperable library:

```sh
g++ -fPIC -Wall -Wformat -O0 -gdwarf-4 -g -shared engine.cpp -o engine.so -lX11
```
