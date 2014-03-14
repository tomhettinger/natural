#define CX 72
#define CY 84
#define W 144
#define H 168
#define RAD 72

//#define INF int32_t 2972590818  //148204965966  // a long time from now.

const GPathInfo FULL_DAY_PATH_INFO = {
  .num_points = 4,
  .points = (GPoint []) { {0, 0}, {W, 0}, {W, H}, {0, H} }
};

//const GPathInfo FULL_NIGHT_PATH_INFO = {
//  .num_points = 3,
//  .points = (GPoint []) { {-1, -1}, {-4, -1}, {-4, -5} }
//};

/*
image_types:
  0 new
  1 waxing crescent
  2 first quarter
  3 waxing gibbous
  4 full
  5 waning gibbous
  6 third quarter
  7 waning crescent

image_rotations:
  0 sun at 00
  1 sun at 03
  2 sun at 06
  3 sun at 09
  4 sun at 12
  5 sun at 15
  6 sun at 18
  7 sun at 21
*/