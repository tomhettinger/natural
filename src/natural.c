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
*/

#include <pebble.h>
#include "config.h"
#include "natural.h"


static Window *window;

static Layer *background_layer;
static BitmapLayer *white_face_layer, *black_face_layer;
static GBitmap *white_face_image, *black_face_image;
static Layer *daylight_layer;
static GPath *daylight_path;

static Layer *sun_layer;
static BitmapLayer *black_sun_layer, *white_sun_layer;
static GBitmap *black_sun_image, *white_sun_image;

static TextLayer *time_text_layer, *sunrise_text_layer, *sunset_text_layer;
char buffer[] = "00:00";
char sunset_buffer[32], sunrise_buffer[32], time_buffer[32];

enum {
  KEY_SUNRISE = 0,
  KEY_SUNSET = 1,
};


//static void get_daypath_info(double lat, double lon) {
  // calculate the daypath points
  // day_path_info[1] = {rcos(sunset), rsin(sunset)}
  // day_path_info[2] = {+73, rsin(sunset)}
  // day_path_info[5] = {-73, rsin(sunrise)}
  // day_path_info[6] = {rcos(sunrise), rsin(sunrise)}
  //time_t now = time(NULL);  //Gets the current time and returns it as a time_t object
  //struct tm *local_time = localtime(&now);
  //int32_t sunset_angle = TRIG_MAX_ANGLE * sunset_time->tm_hour / 24;
//}


static TextLayer* init_text_layer(GRect location, GColor color, GColor background, const char *res_id, GTextAlignment alignment) {
  // Helper function used to initialize any text layer.
  TextLayer *layer = text_layer_create(location);
  text_layer_set_text_color(layer, color);
  text_layer_set_background_color(layer, background);
  text_layer_set_font(layer, fonts_get_system_font(res_id));
  text_layer_set_text_alignment(layer, alignment);
  return layer;
}


static void daylight_update_proc(Layer *layer, GContext *ctx) {
  // Ask for sunrise, sunset times.
  // Calculate new GPath shape.
  gpath_move_to(daylight_path, GPoint(72, 84));
  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, daylight_path);
}


static void minute_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const GPoint center = grect_center_point(&bounds);


  // Handler which executes every minute.
  strftime(buffer, sizeof("00:00"), "%H:%M", tick_time);
  text_layer_set_text(time_text_layer, buffer);

  // Move the Sun to the correct position.
  const int16_t sunDistance = bounds.size.w / 2;
  const int16_t sunDiameter = layer_get_bounds(sun_layer).size.w;
  GPoint sunLocation;
  int32_t hour_angle = TRIG_MAX_ANGLE * (tick_time->tm_hour + 12.0 + (tick_time->tm_min / 60.0)) / 24.0;
  //int32_t hour_angle = TRIG_MAX_ANGLE * (tick_time->tm_hour + 12) / 24;
  //TRIG_MAX_ANGLE * (((t->tm_hour + 12) * 6) + (t->tm_min / 10)))   /   (12*6);    GO ON THIS

  sunLocation.y = (int16_t)(-cos_lookup(hour_angle) * (int32_t)sunDistance / TRIG_MAX_RATIO) + center.y - (sunDiameter / 2);
  sunLocation.x = (int16_t)(sin_lookup(hour_angle) * (int32_t)sunDistance / TRIG_MAX_RATIO) + center.x - (sunDiameter / 2);
  layer_set_frame(sun_layer, GRect(sunLocation.x, sunLocation.y, sunDiameter, sunDiameter));

  layer_mark_dirty(window_get_root_layer(window));
}


void process_tuple(Tuple *t) {
  // Get key
  int key = t->key;

  // Get int value if present
  int value = t->value->int32;

  // Get string value if present
  char string_value[32];
  strcpy(string_value, t->value->cstring);

  // Decide what to do
  switch(key) {
    case KEY_SUNRISE: ;
      time_t tempA = (time_t) value + (TIMEZONE * 3600);  // workaround until localtime() works.
      struct tm *sunrise_time = localtime(&tempA);  // localtime not yet implemented. Returns GMT.
      strftime(sunrise_buffer, sizeof("00:00"), "%H:%M", sunrise_time);
      //snprintf(sunrise_buffer, sizeof("0123456789"), "%d", value);
      text_layer_set_text(sunrise_text_layer, (char*) &sunrise_buffer);
      break;
    case KEY_SUNSET: ;
      time_t tempB = (time_t) value + (TIMEZONE * 3600);  // workaround until localtime() works.
      struct tm *sunset_time = localtime(&tempB);  // localtime not yet implemented. Returns GMT.
      strftime(sunset_buffer, sizeof("00:00"), "%H:%M", sunset_time);
      //snprintf(sunset_buffer, sizeof("0123456789"), "%d", value);
      text_layer_set_text(sunset_text_layer, (char*) &sunset_buffer);
      break;
  }
}


static void in_received_handler(DictionaryIterator *iter, void *context) {
  // This will handle the information handed to the Pebble from the phone's PebbleApp.
  // Get data.
  Tuple *t = dict_read_first(iter);
  if(t) {
    process_tuple(t);
  }
  // Get next.
  while (t != NULL) {
    t = dict_read_next(iter);
    if(t) {
      process_tuple(t);
    }
  }
}


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

  // Execute the minute handler on window load.
  time_t temp = time(NULL);
  struct tm *t = localtime(&temp);
  minute_tick_handler(t, MINUTE_UNIT);
}


static void window_unload(Window *window) {
  // Destroy TextLayers.
  text_layer_destroy(time_text_layer);
  text_layer_destroy(sunrise_text_layer);
  text_layer_destroy(sunset_text_layer);

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

  // Initialize GPath for daylight (currently a constant shape).
  daylight_path = gpath_create(&DAYLIGHT_POINTS);

  // Register AppMessage events.
  app_message_register_inbox_received(in_received_handler);
  app_message_open(512, 512);  // Large input and output buffer sizes

  // Subscribe to 'minute' events.
  tick_timer_service_subscribe(MINUTE_UNIT, (TickHandler) minute_tick_handler);

  window_stack_push(window, true);
}


static void deinit(void) {
  gpath_destroy(daylight_path);
  window_destroy(window);
  tick_timer_service_unsubscribe();
}


int main(void) {
  init();
  app_event_loop();
  deinit();
}
