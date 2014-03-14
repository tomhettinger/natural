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
#include <time.h>
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

static char time_buffer[32], sunset_buffer[32], sunrise_buffer[32];

static int timezone_offset = 0;
static bool clean_timezone = false;

static char latitude[32], longitude[32];

static const time_t INF = (time_t)  1489362018;
static const time_t INVALID = (time_t)  666;
static time_t prev_sunrise_epoch, next_sunrise_epoch, prev_sunset_epoch, next_sunset_epoch;

enum {
  KEY_TEMPERATURE = 0,
  KEY_LONGITUDE = 1,
  KEY_LATITUDE = 2,
  KEY_SUNRISE = 3,
  KEY_SUNSET = 4,
  KEY_TIMEZONE_OFFSET = 42
};


static GPoint get_point_from_time(time_t epoch) {  //int tzone_offset
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


static void assign_rise_or_set_epoch(time_t incoming_epoch, char *motion, time_t now) {
  /* See where incoming rise/set times fit in.
  A)   incoming < prev < now < next  Shouldn't happen.  If it does, don't update any times.
  B)   prev < incoming < now < next  Incoming time in past, but more recent than prev. Update prev time.
  C)   prev < now < incoming < next  Incoming time is in the future, but sooner than next. Use incoming.
  D)   prev < now < next < incoming  Incoming time is in the future after next.  Don't update. */
  time_t *prev_epoch;
  time_t *next_epoch;
  char log_buffer[264];

  if (strcmp(motion, "rise") == 0 ) {
    if (!(difftime(now, prev_sunrise_epoch)>0 && difftime(next_sunrise_epoch, now)>0)) {  // 
      snprintf(log_buffer, 264, "ERROR: (prev_sunrise <= now <= next_sunrise) failed assertion:  %d, %d, %d", (int) prev_sunrise_epoch, (int) now, (int) next_sunrise_epoch);
      APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
    }
    prev_epoch = &prev_sunrise_epoch;
    next_epoch = &next_sunrise_epoch;
  }
  else if (strcmp(motion, "set") == 0 ) {
    if (!(difftime(now, prev_sunset_epoch)>0 && difftime(next_sunset_epoch, now)>0)) {
      snprintf(log_buffer, 264, "ERROR: (prev_sunset <= now <= next_sunset) failed assertion  %d, %d, %d", (int) prev_sunset_epoch, (int) now, (int) next_sunset_epoch);
      APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
    }
    prev_epoch = &prev_sunset_epoch;
    next_epoch = &next_sunset_epoch;
  }
  else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Warning: Didn't specify rise or set!");
    return;
  }

  if (difftime(*prev_epoch, incoming_epoch)>0) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Incoming time is before all other times.");
  }
  else if (difftime(incoming_epoch, *prev_epoch)>0 && difftime(now, incoming_epoch)>0) {
    snprintf(log_buffer, 264, "Setting previous_%s to %d.", motion, (int) incoming_epoch);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
    *prev_epoch = incoming_epoch;
  } 
  else if (difftime(incoming_epoch, now)>0 && difftime(*next_epoch, incoming_epoch)>0) {
    snprintf(log_buffer, 264, "Setting next_%s to %d.", motion, (int) incoming_epoch);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
    *next_epoch = incoming_epoch;
  } 
  else if (difftime(incoming_epoch, *next_epoch)>0) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Incoming time is in future, but after current 'next'. Not updating.");
  }
}


/*  COMMUNICATION WITH PHONE
    ------------------------  */
static void process_tuple(Tuple *tup, time_t now) {
  // Get key
  int key = tup->key;
  // Get int value if present
  int value = tup->value->int32;
  // Get string value if present
  char string_value[32];
  strcpy(string_value, tup->value->cstring);

  switch(key) {
    case KEY_TEMPERATURE:
      break;
    case KEY_LONGITUDE:
      //sscanf(string_value, "%lf", &longitude);   // pebble cant handle float->string or string->float
      strcpy(longitude, string_value);
      break;
    case KEY_LATITUDE:
      //sscanf(string_value, "%lf", &latitude);
      strcpy(latitude, string_value);
      break;
    case KEY_SUNRISE:
      if (clean_timezone) {
        assign_rise_or_set_epoch((time_t) value-timezone_offset, "rise", now);
      }
      break;
    case KEY_SUNSET:
      if (clean_timezone) {
        assign_rise_or_set_epoch((time_t) value-timezone_offset, "set", now);
      }
      break;
    case KEY_TIMEZONE_OFFSET:
      timezone_offset = value;
      clean_timezone = true;
      break;
  }
}


static void in_received_handler(DictionaryIterator *iter, void *context) {
  // This will handle the information handed to the Pebble from the phone's PebbleApp.
  time_t now = time(NULL);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Message received.");
  // Get data.
  Tuple *tup = dict_read_first(iter);
  if (tup) {
    process_tuple(tup, now);
  }
  // Get next.
  while (tup != NULL) {
    tup = dict_read_next(iter);
    if (tup) {
      process_tuple(tup, now);
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
static void daylight_update_proc(Layer *layer, GContext *ctx) {
  /* Update the daylight path if both epochs are valid.  If not, draw either a line
  or fill in the face with a solid color. */
  char log_buffer[264];

  snprintf(log_buffer, 264, "daylight_update_proc()  prev_sunrise=%d,  prev_sunset=%d", 
           (int) prev_sunrise_epoch, (int) prev_sunset_epoch);
  APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
  snprintf(log_buffer, 264, "daylight_update_proc()  next_sunrise=%d, next_sunset=%d", 
           (int) next_sunrise_epoch, (int) next_sunset_epoch);
  APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);

  time_t now = time(NULL);
  time_t this_sunrise_epoch;
  time_t this_sunset_epoch;

  // Decided which times to use.
  if (difftime(next_sunrise_epoch, now) < 86400) {
    this_sunrise_epoch = next_sunrise_epoch;
  } else if (difftime(now, prev_sunrise_epoch) < 86400) {
    this_sunrise_epoch = prev_sunrise_epoch;
  }
  else {
    this_sunrise_epoch = INVALID;
  }
  if (difftime(next_sunset_epoch, now) < 86400) {
    this_sunset_epoch = next_sunset_epoch;
  } else if (difftime(now, prev_sunset_epoch) < 86400) {
    this_sunset_epoch = prev_sunset_epoch;
  }
  else {
    this_sunset_epoch = INVALID;
  }

  if (this_sunrise_epoch != INVALID && this_sunset_epoch != INVALID) {  
    /* If we have a valid rise and set within 24 hours, draw
    both sunrise and sunset, creating a day and night side. */
    GPoint sunrise_point = get_point_from_time(this_sunrise_epoch);
    GPoint sunset_point = get_point_from_time(this_sunset_epoch);
    
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

    snprintf(log_buffer, 264, "Making Path (%d,%d), (%d,%d), (%d,%d), (%d,%d), (%d,%d), (%d,%d), (%d,%d)", 
      info.points[0].x, info.points[0].y, info.points[1].x, info.points[1].y, info.points[2].x, info.points[2].y,
      info.points[3].x, info.points[3].y, info.points[4].x, info.points[4].y, info.points[5].x, info.points[5].y,
      info.points[6].x, info.points[6].y);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);

    GPath *new_path_ptr = gpath_create(&info);
    gpath_move_to(new_path_ptr, GPoint(0, 0));
    graphics_context_set_fill_color(ctx, GColorWhite);
    gpath_draw_filled(ctx, new_path_ptr);
    gpath_destroy(new_path_ptr); // Clear the path from RAM.

  } else if (difftime(next_sunset_epoch, now)>86400 && next_sunset_epoch != INF && 
             difftime(next_sunrise_epoch, now)>86400 && next_sunrise_epoch != INF) { 
    // It is perpetual daylight or nighttime if both rise and set are more than 24h in future.
    snprintf(log_buffer, 264, "all day or all night: next_sunrise=%d, next_sunset=%d, INF=%d", 
             (int) next_sunrise_epoch, (int) next_sunset_epoch, (int) INF);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);

    if (difftime(next_sunset_epoch, next_sunrise_epoch)>0) {
      // It is perpetual night time.  Don't draw a path.

      //GPathInfo info = {
      //  .num_points = 3,
      //  .points = (GPoint []) { {-1, -1}, {-4, -1}, {-4, -5} }
      //};
      //GPath *new_path_ptr = gpath_create(&info);
      //gpath_move_to(new_path_ptr, GPoint(0, 0));
      //graphics_context_set_fill_color(ctx, GColorWhite);
      //gpath_draw_filled(ctx, new_path_ptr);
      //gpath_destroy(new_path_ptr);
      if (difftime(now, prev_sunset_epoch)<86400) {
        // Sunset happened within the last 24 hours, draw a line if you wish.
        // DRAW SUNSET LINE HERE
      }
    } else if (difftime(next_sunrise_epoch, next_sunset_epoch)>0) {
      // It is perpetual day time. Draw a full path.
      GPath *new_path_ptr = gpath_create(&FULL_DAY_PATH_INFO);
      gpath_move_to(new_path_ptr, GPoint(0, 0));
      graphics_context_set_fill_color(ctx, GColorWhite);
      gpath_draw_filled(ctx, new_path_ptr);
      gpath_destroy(new_path_ptr);
      if (difftime(now, prev_sunrise_epoch)<86400) {
        // Sunrise happened within the last 24 hours, draw a line if you wish.
        // DRAW A SUNRISE LINE HERE
      }
    }

  } else if (difftime(now, prev_sunset_epoch)<86400 && prev_sunset_epoch != (time_t) 0 &&
            difftime(next_sunrise_epoch, now)>86400 && next_sunrise_epoch != INF) {
    /* It is perpetual nighttime since a recent set means the next set must be after next rise, and 
    the next rise will not happen for a long time. Don't draw the day path.*/
    // DRAW SUNSET LINE HERE

  } else if (difftime(now, prev_sunrise_epoch)<86400 && prev_sunrise_epoch != (time_t) 0 &&
            difftime(next_sunset_epoch, now)>86400 && next_sunset_epoch != INF) {
    /* It is perpetual daytime since a recent rise means the next rise must be after the next set,
    and the next set will not happen for a long time. Draw full day path.*/
    GPath *new_path_ptr = gpath_create(&FULL_DAY_PATH_INFO);
    gpath_move_to(new_path_ptr, GPoint(0, 0));
    graphics_context_set_fill_color(ctx, GColorWhite);
    gpath_draw_filled(ctx, new_path_ptr);
    gpath_destroy(new_path_ptr);
    // DRAW SUNRISE LINE HERE

  } else {
    // Insufficient information.
    snprintf(log_buffer, 264, "insufficient info: next_sunrise=%d, next_sunset=%d, INF=%d", 
             (int) next_sunrise_epoch, (int) next_sunset_epoch, (int) INF);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
    GPath *new_path_ptr = gpath_create(&FULL_DAY_PATH_INFO);
    gpath_move_to(new_path_ptr, GPoint(0, 0));
    graphics_context_set_fill_color(ctx, GColorWhite);
    gpath_draw_filled(ctx, new_path_ptr);
    gpath_destroy(new_path_ptr);
  }
}


static void update_rise_and_set_epochs(time_t now) {
  char log_buffer[164];

  // Assertions
  if ( difftime(now, prev_sunrise_epoch) < 0 ) {  // prev_sunrise_epoch <= now
    snprintf(log_buffer, 164, "ERROR: (prev_sunrise <= now) failed assertion  %d, %d", (int) prev_sunrise_epoch, (int) now);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
  }
  if ( difftime(next_sunrise_epoch, prev_sunrise_epoch) < 0 ) {  // prev_sunrise_epoch <= next_sunrise_epoch
    snprintf(log_buffer, 164, "ERROR: (prev_sunrise <= next_sunrise) failed assertion:  %d, %d", (int) prev_sunrise_epoch, (int) next_sunrise_epoch);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
  }
  if ( difftime(now, prev_sunset_epoch) < 0 ) {  //prev_sunset_epoch <= now
    snprintf(log_buffer, 164, "ERROR: (prev_sunset <= now) failed assertion  %d, %d", (int) prev_sunset_epoch, (int) now);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
  }
  if ( difftime(next_sunset_epoch, prev_sunset_epoch) < 0 ) {  // prev_sunset_epoch <= next_sunset_epoch
    snprintf(log_buffer, 164, "ERROR: (prev_sunset <= next_sunset) failed assertion  %d, %d", (int) prev_sunset_epoch, (int) next_sunset_epoch);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
  }

  // If the current time has passed the 'next' rise, set it as the 'prev' rise.
  if ( difftime(now, next_sunrise_epoch) > 0 ) {
    snprintf(log_buffer, 164, "Current time passed next_sunrise. Moving to prev_sunrise.  %d", (int) next_sunrise_epoch);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
    prev_sunrise_epoch = next_sunrise_epoch;
    next_sunrise_epoch = (time_t) INF;
  }
  if ( difftime(now, next_sunset_epoch) > 0 ) {
    snprintf(log_buffer, 164, "Current time passed next_sunset. Moving to prev_sunrise.  %d", (int) next_sunset_epoch);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
    prev_sunset_epoch = next_sunset_epoch;
    next_sunset_epoch = (time_t) INF;
  }
}


static void refresh_rise_and_set_text() {
  // Update the current 'next_epoch' text layers.
  if (next_sunrise_epoch != INF) {
    struct tm *rise_tm = localtime(&next_sunrise_epoch);
    strftime(sunrise_buffer, sizeof("00:00"), "%H:%M", rise_tm);
    text_layer_set_text(sunrise_text_layer, (char*) &sunrise_buffer);
  } else {
    snprintf(sunrise_buffer, sizeof("N/A"), "N/A");
    text_layer_set_text(sunrise_text_layer, (char*) &sunrise_buffer);
  }
    if (next_sunset_epoch != INF) {
    struct tm *set_tm = localtime(&next_sunset_epoch);
    strftime(sunset_buffer, sizeof("00:00"), "%H:%M", set_tm);
    text_layer_set_text(sunset_text_layer, (char*) &sunset_buffer);
  } else {
    snprintf(sunset_buffer, sizeof("N/A"), "N/A");
    text_layer_set_text(sunset_text_layer, (char*) &sunset_buffer);
  }
}


static void reframe_sun_layer(time_t now) {
  // Reframe the Sun layer to the correct position.
  GPoint sunLocation = get_point_from_time(now);
  const int16_t sunDiameter = layer_get_bounds(sun_layer).size.w;  // replace with definition?
  sunLocation.x = sunLocation.x - (sunDiameter / 2);
  sunLocation.y = sunLocation.y - (sunDiameter / 2);
  layer_set_frame(sun_layer, GRect(sunLocation.x, sunLocation.y, sunDiameter, sunDiameter));
}


static void minute_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  time_t now = time(NULL);

  // Update the time on the clock text layer.
  strftime(time_buffer, sizeof("00:00"), "%H:%M", tick_time);
  text_layer_set_text(time_text_layer, time_buffer);

  // Move the Sun to the correct position.
  reframe_sun_layer(now);

  // Update the moon position and image.
  //update_moon_image();  // only updates if necessary
  //reframe_moon_layer(now);

  // Update the current location coordinates. AND timezone!
  //send_int(88, 1);  // 88 is the key that tells the .js file to only check for location.
  //get_location_and_timezone_from_phone()
  //if (succesful and location is changed) {
  if (false) {
    prev_sunrise_epoch = (time_t) 0;
    next_sunrise_epoch = INF;
    prev_sunset_epoch = (time_t) 0;
    next_sunset_epoch = INF;
  } 
  
  // Get new sunset / rise times every 15 minutes.
  if(tick_time->tm_min % 15 == 0) {
    send_int(66, 5);  // 66 is the key that tells the .js file to get sunrise/sunset times (and location).
  }

  // Based on current time, move any next_xx to prev_xx as necessary.
  update_rise_and_set_epochs(now);

  // Update the current 'next_epoch' text layers.
  refresh_rise_and_set_text();

  // Update the current lat/long text layers.
  text_layer_set_text(loc_text_layer, longitude);

  // Tell the daylight layer to update itself.
  layer_mark_dirty(daylight_layer);
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
  time_text_layer = init_text_layer(GRect(0, 144, 40, 18), GColorWhite, GColorClear, "FONT_KEY_GOTHIC_24_BOLD", GTextAlignmentCenter);
  text_layer_set_text(time_text_layer, "N/A");
  layer_add_child(window_layer, (Layer*) time_text_layer);
  
  // Create the text layers to hold the sunrise and sunset times.
  sunrise_text_layer = init_text_layer(GRect(104, 6, 40, 18), GColorWhite, GColorClear, "FONT_KEY_GOTHIC_24_BOLD", GTextAlignmentCenter);
  text_layer_set_text(sunrise_text_layer, "N/A");
  layer_add_child(window_layer, (Layer*) sunrise_text_layer);
  sunset_text_layer = init_text_layer(GRect(104, 144, 40, 18), GColorWhite, GColorClear, "FONT_KEY_GOTHIC_24_BOLD", GTextAlignmentCenter);
  text_layer_set_text(sunset_text_layer, "N/A");
  layer_add_child(window_layer, (Layer*) sunset_text_layer);

  // Create the text layer for coordinates.
  loc_text_layer = init_text_layer(GRect(0, 6, 100, 18), GColorWhite, GColorClear, "FONT_KEY_GOTHIC_24_BOLD", GTextAlignmentLeft);
  text_layer_set_text(loc_text_layer, "N/A");
  layer_add_child(window_layer, (Layer*) loc_text_layer);

  // Initialize times and location variables.
  prev_sunrise_epoch = (time_t) 0;
  next_sunrise_epoch = INF;
  prev_sunset_epoch = (time_t) 0;
  next_sunset_epoch = INF;
  snprintf(longitude, 32, "N/A");
  snprintf(latitude, 32, "N/A");

  // Execute the minute handler on window load.
  time_t now = time(NULL);
  struct tm *startup_time = gmtime(&now);
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
