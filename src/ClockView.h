#ifndef CLOCKVIEW_H
#define CLOCKVIEW_H

#include <Arduino_GFX_Library.h>

class ClockView {
public:
    static void draw(Arduino_GFX *gfx, int cx, int cy, int radius);
};

#endif
