#pragma once
#include <pebble.h>
#define CLOCK_RADIUS 71.5

//static GPoint CLOCK_CENTER = GPoint(72, 84);
//static const GSize SUN_SIZE = GSize(19, 19);

static const GPathInfo DAYLIGHT_POINTS = {
  5, (GPoint []) {
    {0, 0},  // center
    {+71, -14},  // sunset angle  (edge of circle)  (fill with dummy values)
    {+73, -14}, // right edge
    {+73, -84},  // top right
    {-73, -84},  // topleft
    {-73, -24}, //left edge
    {-71, -24},  // sunrise angle (edge of circle)  (fill with dummy values)
  }
};