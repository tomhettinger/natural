/*
Window window
    Layer window_layer
        Layer background_layer
            BitmapLayer(face_bg_white_layer)
            Layer daylight_layer
                GPath daylight_path
            BitmapLayer(face_bg_black_layer)
        Layer sun_layer
            BitmapLayer(black_sun_layer)
            BitmapLayer(white_sun_layer)
        Layer TextLayer(time_text_layer)
        Layer TextLayer(sunrise_text_layer)
        Layer TextLayer(sunset_text_layer)
        Layer TextLayer(loc_text_layer)
*/

#include <pebble.h>
#include "natural.h"


static Window *window;

static Layer *background_layer;
static BitmapLayer *white_face_layer, *black_face_layer;
static GBitmap *white_face_image, *black_face_image;
static Layer *daylight_layer;

static Layer *sun_layer;
static BitmapLayer *black_sun_layer, *white_sun_layer;
static GBitmap *black_sun_image, *white_sun_image;

static TextLayer *time_text_layer, *sunrise_text_layer, *sunset_text_layer, *loc_text_layer;

static char time_buffer[32], sunset_buffer[32], sunrise_buffer[32], loc_buffer[32];

static int timezone_offset = 0;
static time_t sunset_epoch;
static time_t sunrise_epoch;

static bool clean_timezone = false;
static bool clean_sunrise = false;
static bool clean_sunset = false;

// Can probably change these to #define if i want.
enum {
  KEY_TEMPERATURE = 0,
  KEY_LONGITUDE = 1,
  KEY_LATITUDE = 2,
  KEY_SUNRISE = 3,
  KEY_SUNSET = 4,
  KEY_TIMEZONE_OFFSET = 42
};


static GPoint get_point_from_time(time_t epoch) {
  // Given an epoch, return a GPoint on the clock edge.
  struct tm *t = localtime(&epoch);
  int hour = t->tm_hour;
  int min = t->tm_min;
  int32_t angle = TRIG_MAX_ANGLE * (hour + 12.0 + (min / 60.0)) / 24.0;
  GPoint newPoint = {
    .x = (int16_t)(sin_lookup(angle) * (int32_t)RAD / TRIG_MAX_RATIO) + CX,
    .y = (int16_t)(-cos_lookup(angle) * (int32_t)RAD / TRIG_MAX_RATIO) + CY
  };
  return newPoint;
}


static TextLayer* init_text_layer(GRect location, GColor color, GColor background, const char *res_id, GTextAlignment alignment) {
  // Helper function used to initialize any text layer.
  TextLayer *layer = text_layer_create(location);
  text_layer_set_text_color(layer, color);
  text_layer_set_background_color(layer, background);
  text_layer_set_font(layer, fonts_get_system_font(res_id));
  text_layer_set_text_alignment(layer, alignment);
  return layer;
}


/*  COMMUNICATION WITH PHONE
    ------------------------  */
static void process_tuple(Tuple *tup) {
  // Get key
  int key = tup->key;

  // Get int value if present
  int value = tup->value->int32;

  // Get string value if present
  char string_value[32];
  strcpy(string_value, tup->value->cstring);

  // Decide what to do
  switch(key) {
    case KEY_TEMPERATURE: ;
      break;
    case KEY_LONGITUDE: ;
      snprintf(loc_buffer, sizeof("L: -999.99"), "L: %s", string_value);
      text_layer_set_text(loc_text_layer, loc_buffer);
      break;
    case KEY_LATITUDE: ;
      break;
    case KEY_SUNRISE: ;
      sunrise_epoch = (time_t) (value - timezone_offset);   // workaround until localtime() works.
      clean_sunrise = true;  // Use this time to check that the value actually is valid!!
      struct tm *rise_tm = localtime(&sunrise_epoch);       // localtime not yet implemented. Returns GMT.
      strftime(sunrise_buffer, sizeof("00:00"), "%H:%M", rise_tm);
      text_layer_set_text(sunrise_text_layer, (char*) &sunrise_buffer);
      break;
    case KEY_SUNSET: ;
      sunset_epoch = (time_t) (value - timezone_offset);  // workaround until localtime() works.
      clean_sunset = true;   // Use this time to check that the value actually is valid!!
      struct tm *set_tm = localtime(&sunset_epoch);       // localtime not yet implemented. Returns GMT.
      strftime(sunset_buffer, sizeof("00:00"), "%H:%M", set_tm);
      text_layer_set_text(sunset_text_layer, (char*) &sunset_buffer);
      break;
    case KEY_TIMEZONE_OFFSET: ;
      timezone_offset = value;  // update the global variable timezone offset in seconds.
      clean_timezone = true;
  }
}


static void in_received_handler(DictionaryIterator *iter, void *context) {
  // This will handle the information handed to the Pebble from the phone's PebbleApp.
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Message received.");
  // Get data.
  Tuple *tup = dict_read_first(iter);
  if (tup) {
    process_tuple(tup);
  }
  // Get next.
  while (tup != NULL) {
    tup = dict_read_next(iter);
    if (tup) {
      process_tuple(tup);
    }
  }
}


static void in_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Dropped!");
}


static void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Failed to Send!");
}


static void send_int(uint8_t key, uint8_t cmd) {
  // Send a key, cmd tuple of integers to the phone app.
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Message Attempting to send.");
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  Tuplet value = TupletInteger(key, cmd);
  dict_write_tuplet(iter, &value);
  app_message_outbox_send();
}


/*  UPDATE FUNCTIONS
    ----------------  */
static GPath* create_path_with_sun_times() {
  // Calculate a new path for the current daylight hours.
  // Must handle case where there is no time set, or the time is > 24 hours away!
  if (clean_sunrise && clean_sunset) {
    GPoint sunrise_point = get_point_from_time(sunrise_epoch);
    GPoint sunset_point = get_point_from_time(sunset_epoch);  
    
    GPathInfo info = {
      .num_points = 7,
      .points = (GPoint []) {
        {CX, CY},                // center
        sunrise_point,           // sunrise angle (edge of circle)
        {0, sunrise_point.y},    // left edge
        {0, 0},                  // topleft
        {W, 0},                  // top right
        {W, sunset_point.y},     // right edge
        sunset_point             // sunset point  (edge of circle)
      }
    };
    GPath *new_path_ptr = gpath_create(&info);
    return new_path_ptr;

  } else {
    GPathInfo info = {
      .num_points = 4,
      .points = (GPoint []) { {0, 0}, {W, 0}, {W, H}, {0, H} }
    };
    GPath *new_path_ptr = gpath_create(&info);
    return new_path_ptr;
  }
}


static void daylight_update_proc(Layer *layer, GContext *ctx) {
  // Consider merging this function with create_path_with_sun_times().
  GPath *new_path_ptr = create_path_with_sun_times();
  gpath_move_to(new_path_ptr, GPoint(0, 0));
  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, new_path_ptr);
  gpath_destroy(new_path_ptr); // Clear the path from RAM.
}


static void minute_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // This is the handler which executes every minute.
  
  // Update the clock time.
  strftime(time_buffer, sizeof("00:00"), "%H:%M", tick_time);
  text_layer_set_text(time_text_layer, time_buffer);

  // Move the Sun to the correct position.
  GPoint sunLocation = get_point_from_time(time(NULL));
  //GPoint sunLocation = GPoint(60, 60);
  const int16_t sunDiameter = layer_get_bounds(sun_layer).size.w;  // replace with definition?
  sunLocation.x = sunLocation.x - (sunDiameter / 2);
  sunLocation.y = sunLocation.y - (sunDiameter / 2);
  layer_set_frame(sun_layer, GRect(sunLocation.x, sunLocation.y, sunDiameter, sunDiameter));

  // Check online for updates every 15 minutes. (Arbitrary key and value)
  if(tick_time->tm_min % 15 == 0) {
    send_int(5, 5);
  }

  //layer_mark_dirty(window_get_root_layer(window));  // Is this line necessary?
}


/*  LOAD AND UNLOAD FUNCTIONS
    -------------------------  */
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  // Create background clock including the daylight path.
  background_layer = layer_create(bounds);
  layer_add_child(window_layer, background_layer);

  white_face_image = gbitmap_create_with_resource(RESOURCE_ID_FACE_BG_WHITE);
  white_face_layer = bitmap_layer_create(bounds);
  bitmap_layer_set_bitmap(white_face_layer, white_face_image);
  bitmap_layer_set_background_color(white_face_layer, GColorClear);
  bitmap_layer_set_compositing_mode(white_face_layer, GCompOpAssign);
  layer_add_child(background_layer, bitmap_layer_get_layer(white_face_layer));

  daylight_layer = layer_create(bounds);
  layer_set_update_proc(daylight_layer, daylight_update_proc);
  layer_add_child(background_layer, daylight_layer);

  black_face_image = gbitmap_create_with_resource(RESOURCE_ID_FACE_BG_BLACK);
  black_face_layer = bitmap_layer_create(bounds);
  bitmap_layer_set_bitmap(black_face_layer, black_face_image);
  bitmap_layer_set_background_color(black_face_layer, GColorClear);
  bitmap_layer_set_compositing_mode(black_face_layer, GCompOpAnd);
  layer_add_child(background_layer, bitmap_layer_get_layer(black_face_layer));

  // Create the sun layer including both sun images.
  sun_layer = layer_create(GRect(0, 0, 19, 19));
  layer_set_frame(sun_layer, GRect(0, 100, 19, 19));  // Use this to move sun. (x, y, 19, 19)
  //layer_set_update_proc(sun_layer, sun_update_proc);
  layer_add_child(window_layer, sun_layer);

  black_sun_image = gbitmap_create_with_resource(RESOURCE_ID_SUN_HAND_BLACK);
  black_sun_layer = bitmap_layer_create(GRect(0, 0, 19, 19));
  bitmap_layer_set_bitmap(black_sun_layer, black_sun_image);
  bitmap_layer_set_background_color(black_sun_layer, GColorClear);
  bitmap_layer_set_compositing_mode(black_sun_layer, GCompOpAnd);
  layer_add_child(sun_layer, bitmap_layer_get_layer(black_sun_layer));

  white_sun_image = gbitmap_create_with_resource(RESOURCE_ID_SUN_HAND_WHITE);
  white_sun_layer = bitmap_layer_create(GRect(0, 0, 19, 19));
  bitmap_layer_set_bitmap(white_sun_layer, white_sun_image);
  bitmap_layer_set_background_color(white_sun_layer, GColorClear);
  bitmap_layer_set_compositing_mode(white_sun_layer, GCompOpOr);
  layer_add_child(sun_layer, bitmap_layer_get_layer(white_sun_layer));

  // Create the text layer to hold the current time.
  time_text_layer = init_text_layer(GRect(0, 148, 40, 14), GColorWhite, GColorClear, "FONT_KEY_GOTHIC_24_BOLD", GTextAlignmentCenter);
  text_layer_set_text(time_text_layer, "00:00");
  layer_add_child(window_layer, (Layer*) time_text_layer);
  
  // Create the text layers to hold the sunrise and sunset times.
  sunrise_text_layer = init_text_layer(GRect(104, 6, 40, 14), GColorWhite, GColorClear, "FONT_KEY_GOTHIC_24_BOLD", GTextAlignmentCenter);
  text_layer_set_text(sunrise_text_layer, "00:00");
  layer_add_child(window_layer, (Layer*) sunrise_text_layer);
  sunset_text_layer = init_text_layer(GRect(104, 148, 40, 14), GColorWhite, GColorClear, "FONT_KEY_GOTHIC_24_BOLD", GTextAlignmentCenter);
  text_layer_set_text(sunset_text_layer, "00:00");
  layer_add_child(window_layer, (Layer*) sunset_text_layer);

  // Create the text layer for coordinates.
  loc_text_layer = init_text_layer(GRect(0, 6, 100, 18), GColorWhite, GColorClear, "FONT_KEY_GOTHIC_24_BOLD", GTextAlignmentLeft);
  text_layer_set_text(loc_text_layer, "N/A");
  layer_add_child(window_layer, (Layer*) loc_text_layer);

  // Execute the minute handler on window load.
  time_t now = time(NULL);                         // local time_t variable
  struct tm *startup_time = localtime(&now);       // local tm struct that will be destroyed AFTER minute_tick_handler finishes
  minute_tick_handler(startup_time, MINUTE_UNIT);
}


static void window_unload(Window *window) {
  // Destroy TextLayers.
  text_layer_destroy(time_text_layer);
  text_layer_destroy(sunrise_text_layer);
  text_layer_destroy(sunset_text_layer);
  text_layer_destroy(loc_text_layer);

  // Destroy GBitmaps.
  gbitmap_destroy(white_sun_image);
  gbitmap_destroy(black_sun_image);
  gbitmap_destroy(black_face_image);
  gbitmap_destroy(white_face_image);

  // Destroy BitmapLayrs.
  bitmap_layer_destroy(white_sun_layer);
  bitmap_layer_destroy(black_sun_layer);
  bitmap_layer_destroy(black_face_layer);
  bitmap_layer_destroy(white_face_layer);

  // Destroy Layers.
  layer_destroy(sun_layer);
  layer_destroy(background_layer);
  layer_destroy(daylight_layer);
}


static void init(void) {
  // Initialize window.
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

  // Register AppMessage events.
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  app_message_register_outbox_failed(out_failed_handler);
  app_message_open(512, 512);  // Large input and output buffer sizes

  // Subscribe to 'minute' events.
  tick_timer_service_subscribe(MINUTE_UNIT, (TickHandler) minute_tick_handler);

  window_stack_push(window, true);
}


static void deinit(void) {
  window_destroy(window);
  tick_timer_service_unsubscribe();
}


int main(void) {
  init();
  app_event_loop();
  deinit();
}
