# ComputerVisionEngine
Python C/C++ Interoperable Computer Vision Engine

## Week 1 Querying the Game Window Resource ID

**Gets the Game Window**: gets the X11 Window Resource ID of the Game Window. The implementation is based on Xorg's `xwininfo` but tailored for the demo that only has the bare minimum code to retrieve the Window ID by clicking the Game Window with the mouse.

[![QueryingWindowResourceID](https://img.youtube.com/vi/oOI-gqFBDTg/hqdefault.jpg)](https://youtu.be/oOI-gqFBDTg)

## Build

```sh
g++ -Wall -Wformat -O0 -g main.cpp -o engine.bin -lX11
```
