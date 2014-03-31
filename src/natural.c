/*
  Window window
    Layer window_layer
        Layer background_layer
            BitmapLayer(face_bg_white_layer)
            Layer daylight_layer
                GPath daylight_path
            Layer TextLayer(time_text_layer)
            BitmapLayer(face_bg_black_layer)
        Layer sun_layer
            BitmapLayer(b_sun_layer)
            BitmapLayer(w_sun_layer)
        Layer moon_layer
            BitmapLayer(b_moon_layer)
            BitmapLayer(w_moon_layer)
        Layer TextLayer(next_sunrise_text_layer)
        Layer TextLayer(next_sunset_text_layer)
        Layer TextLayer(prev_sunrise_text_layer)
        Layer TextLayer(prev_sunset_text_layer)
        Layer BitmapLayer(noti_layer)
        Layer BitmapLayer(battery_layer)
*/

#include <pebble.h>
#include <time.h>
#include "natural.h"

static const time_t INF = (time_t) 2147483640;      // 7 seconds before 2038 event.
static const time_t ZERO = (time_t) 0;
static const time_t INVALID = (time_t)  666;
static const time_t NEW_MOON = (time_t) 1393678800; // A recent new moon at March 1, 2014 13:00 UT
static const double LUNAR_CYCLE = 2551442.98;       // In seconds.
static const time_t TIMEOUT = 900;                  // Seconds between weather checks
static const time_t ERROR_TIMEOUT = 120;            // Wait time after error before retrying get_weather
static const bool DEBUG_MODE = false;

static Window *window;

static Layer *background_layer;
static BitmapLayer *b_clockface_layer, *w_clockface_layer;
static GBitmap *b_clockface_image, *w_clockface_image;
static Layer *daylight_layer;

static Layer *sun_layer;
static BitmapLayer *b_sun_layer, *w_sun_layer;
static GBitmap *b_sun_image, *w_sun_image;

static Layer *moon_layer;
static BitmapLayer *b_moon_layer, *w_moon_layer;
static GBitmap *b_moon_image, *w_moon_image;

static BitmapLayer *noti_layer;
static GBitmap *refresh_image, *error_image, *empty_image, *no_bluetooth_image;
static BitmapLayer *battery_layer;
static GBitmap *batt_100_image, *batt_80_image, *batt_60_image, *batt_40_image, *batt_20_image, *batt_10_image, *batt_charge_image;

static TextLayer *time_text_layer, *next_sunrise_text_layer, *next_sunset_text_layer, *prev_sunrise_text_layer, *prev_sunset_text_layer;

static char time_buffer[32], prev_sunset_buffer[32], prev_sunrise_buffer[32], next_sunset_buffer[32], next_sunrise_buffer[32], log_buffer[256];

static int current_image_index[2] = {99, 99};       // points to nothing
static int timezone_offset = 0;                     // actual epoch - time(NULL)
static bool timezone_missing = true;                // necessary? for moon_update maybe
static bool getting_weather = false;                // prevent calling get_weather() twice
static bool js_ready = false;                       // js ready to receive requests
static bool bluetooth_connected = false;            // whether or not bluetooth is connected
static time_t time_stamp = 0;                       // time of last weather check
static time_t prev_sunrise_epoch, next_sunrise_epoch, prev_sunset_epoch, next_sunset_epoch;

enum {
  KEY_STATUS = 0,
  KEY_TZOFFSET = 1,
  KEY_SUNRISE = 2,
  KEY_SUNSET = 3,
  KEY_PREV_SUNRISE = 4,
  KEY_PREV_SUNSET = 5,
  KEY_NEXT_SUNRISE = 6,
  KEY_NEXT_SUNSET = 7,
  KEY_TIME_STAMP = 8
};


static TextLayer* init_text_layer(GRect location, GColor color, GColor background, const char *res_id, GTextAlignment alignment) {
  /* Helper function used to initialize any text layer. */
  TextLayer *layer = text_layer_create(location);
  text_layer_set_text_color(layer, color);
  text_layer_set_background_color(layer, background);
  text_layer_set_font(layer, fonts_get_system_font(res_id));
  text_layer_set_text_alignment(layer, alignment);
  return layer;
}


static bool time_to_refresh() {
  /* Check the current time with time of last check.  Return
  true if greater than 15 minutes. */
  time_t time_passed = time(NULL) - time_stamp;
  if(time_stamp == 0 || time_passed >= TIMEOUT) {
    return true;
  } else {
    return false;
  }
}


static void battery_handler(BatteryChargeState charge_state) {
  if (charge_state.is_charging) {
    snprintf(log_buffer, 32, "PEBBLE: charging");
    bitmap_layer_set_bitmap(battery_layer, batt_charge_image);
  } else {
    int percentage = charge_state.charge_percent;
    snprintf(log_buffer, 32, "PEBBLE: %d charged", percentage);
    if (percentage > 80) bitmap_layer_set_bitmap(battery_layer, batt_100_image);
    else if (percentage <= 80 && percentage > 60) bitmap_layer_set_bitmap(battery_layer, batt_80_image);
    else if (percentage <= 60 && percentage > 40) bitmap_layer_set_bitmap(battery_layer, batt_60_image);
    else if (percentage <= 40 && percentage > 20) bitmap_layer_set_bitmap(battery_layer, batt_40_image);
    else if (percentage <= 20 && percentage > 10) bitmap_layer_set_bitmap(battery_layer, batt_20_image);
    else if (percentage <= 10) bitmap_layer_set_bitmap(battery_layer, batt_10_image);
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
}


static void bluetooth_handler(bool connected) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "bluetooth connected=%d", (int) connected);
  bluetooth_connected = connected;
  if (!connected) {
    getting_weather = false;
    bitmap_layer_set_bitmap(noti_layer, no_bluetooth_image);
  } 
  else if (connected) {
    bitmap_layer_set_bitmap(noti_layer, empty_image);
  }
}


/*  UPDATE FUNCTIONS
    ----------------  */
static GPoint get_point_from_time(time_t epoch, int radius) {
  /* Given an epoch, return a GPoint on the clock edge 
  at the location of the corresponding time. */
  struct tm *t = localtime(&epoch);
  int hour = t->tm_hour;
  int min = t->tm_min;
  int32_t angle = TRIG_MAX_ANGLE * (hour + 12.0 + (min / 60.0)) / 24.0;
  GPoint newPoint = {
    .x = (int16_t)(sin_lookup(angle) * (int32_t)radius / TRIG_MAX_RATIO) + CX,
    .y = (int16_t)(-cos_lookup(angle) * (int32_t)radius / TRIG_MAX_RATIO) + CY
  };
  return newPoint;
}


static void reframe_sun_layer(time_t now) {
  /* Reframe the Sun layer to the correct position. */
  const int16_t sunDiameter = layer_get_bounds(sun_layer).size.w;  // replace with definition?
  int sunRingRadius = CLOCK_RAD - 10;
  GPoint sunLocation = get_point_from_time(now, sunRingRadius);
  sunLocation.x = sunLocation.x - (sunDiameter / 2);
  sunLocation.y = sunLocation.y - (sunDiameter / 2);
  layer_set_frame(sun_layer, GRect(sunLocation.x, sunLocation.y, sunDiameter, sunDiameter));
}


static void assign_rise_or_set_epoch(time_t incoming_epoch, char *motion, time_t now) {
  /* Given a rise or set time, determine if it should replace either 'next' or 'prev' values.
  A)   incoming < prev < now < next  Shouldn't happen.  If it does, don't update any times.
  B)   prev < incoming < now < next  Incoming time in past, but more recent than prev. Update prev time.
  C)   prev < now < incoming < next  Incoming time is in the future, but sooner than next. Use incoming.
  D)   prev < now < next < incoming  Incoming time is in the future after next.  Don't update. */
  time_t *prev_epoch;
  time_t *next_epoch;

  if (strcmp(motion, "rise") == 0 ) {
    prev_epoch = &prev_sunrise_epoch;
    next_epoch = &next_sunrise_epoch;
  }
  else if (strcmp(motion, "set") == 0 ) {
    prev_epoch = &prev_sunset_epoch;
    next_epoch = &next_sunset_epoch;
  }
  else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: Didn't specify rise or set!");
    return;
  }

  if (difftime(*prev_epoch, incoming_epoch)>0) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: 'incoming_epoch' < 'prev'; not using.");
  }
  else if (difftime(incoming_epoch, *prev_epoch)>0 && difftime(now, incoming_epoch)>0) {
    *prev_epoch = incoming_epoch;
  } 
  else if (difftime(incoming_epoch, now)>0 && difftime(*next_epoch, incoming_epoch)>0) {
    *next_epoch = incoming_epoch;
  } 
  else if (difftime(incoming_epoch, *next_epoch)>0) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: 'incoming_epoch' > 'next'; not using.");
  }
}


static void update_rise_and_set_epochs(time_t now) {
  /* Check to see that 'next' times are still in the future. If not,
  set them to 'prev' and set 'next' to INF. */
  if (difftime(now, next_sunrise_epoch)>0) {
    prev_sunrise_epoch = next_sunrise_epoch;
    next_sunrise_epoch = (time_t) INF;
  }
  if (difftime(now, next_sunset_epoch)>0) {
    prev_sunset_epoch = next_sunset_epoch;
    next_sunset_epoch = (time_t) INF;
  }

  /* Update the rise/set epoch text layers. */
  if (next_sunrise_epoch != INF) {
    struct tm *rise_tm = localtime(&next_sunrise_epoch);
    strftime(next_sunrise_buffer, sizeof("00:00"), "%H:%M", rise_tm);
    text_layer_set_text(next_sunrise_text_layer, (char*) &next_sunrise_buffer);
  } else {
    snprintf(next_sunrise_buffer, sizeof("N/A"), "N/A");
    text_layer_set_text(next_sunrise_text_layer, (char*) &next_sunrise_buffer);
  }

  if (next_sunset_epoch != INF) {
    struct tm *set_tm = localtime(&next_sunset_epoch);
    strftime(next_sunset_buffer, sizeof("00:00"), "%H:%M", set_tm);
    text_layer_set_text(next_sunset_text_layer, (char*) &next_sunset_buffer);
  } else {
    snprintf(next_sunset_buffer, sizeof("N/A"), "N/A");
    text_layer_set_text(next_sunset_text_layer, (char*) &next_sunset_buffer);
  }

  if (prev_sunrise_epoch != ZERO) {
    struct tm *rise_tm = localtime(&prev_sunrise_epoch);
    strftime(prev_sunrise_buffer, sizeof("00:00"), "%H:%M", rise_tm);
    text_layer_set_text(prev_sunrise_text_layer, (char*) &prev_sunrise_buffer);
  } else {
    snprintf(prev_sunrise_buffer, sizeof("N/A"), "N/A");
    text_layer_set_text(prev_sunrise_text_layer, (char*) &prev_sunrise_buffer);
  }
  
  if (prev_sunset_epoch != ZERO) {
    struct tm *set_tm = localtime(&prev_sunset_epoch);
    strftime(prev_sunset_buffer, sizeof("00:00"), "%H:%M", set_tm);
    text_layer_set_text(prev_sunset_text_layer, (char*) &prev_sunset_buffer);
  } else {
    snprintf(prev_sunset_buffer, sizeof("N/A"), "N/A");
    text_layer_set_text(prev_sunset_text_layer, (char*) &prev_sunset_buffer);
  }
}


static void daylight_update_proc(Layer *layer, GContext *ctx) {
  /* Update the daylight path if both epochs are valid.  If not, draw either a line
  or fill in the face with a solid color. */
  time_t now = time(NULL);
  time_t this_sunrise_epoch;
  time_t this_sunset_epoch;

  // Decided which rise/set epochs to use (prev or next).
  if (difftime(next_sunrise_epoch, now)<86400) {
    this_sunrise_epoch = next_sunrise_epoch;
  } else if (difftime(now, prev_sunrise_epoch)<86400) {
    this_sunrise_epoch = prev_sunrise_epoch;
  } else {
    this_sunrise_epoch = INVALID;
  }
  if (difftime(next_sunset_epoch, now)<86400) {
    this_sunset_epoch = next_sunset_epoch;
  } else if (difftime(now, prev_sunset_epoch)<86400) {
    this_sunset_epoch = prev_sunset_epoch;
  } else {
    this_sunset_epoch = INVALID;
  }

  /* If we have a valid rise and set within 24 hours, draw
    both sunrise and sunset, creating a day and night side. */
  if (this_sunrise_epoch != INVALID && this_sunset_epoch != INVALID) {  
    GPoint sunrise_point = get_point_from_time(this_sunrise_epoch, CLOCK_RAD);
    GPoint sunset_point = get_point_from_time(this_sunset_epoch, CLOCK_RAD);
    
    // Assumes 00:00 < sunrise < 12:00 and 12:00 < sunset < 24:00 
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
    gpath_move_to(new_path_ptr, GPoint(0, 0));
    graphics_context_set_fill_color(ctx, GColorWhite);
    gpath_draw_filled(ctx, new_path_ptr);
    gpath_destroy(new_path_ptr); // Clear the path from RAM.
  } 

  /* It is perpetual daylight or nighttime if both rise and set are more than 24h in future. */
  else if (difftime(next_sunset_epoch, now)>86400 && next_sunset_epoch != INF && difftime(next_sunrise_epoch, now)>86400 && next_sunrise_epoch != INF) { 
    snprintf(log_buffer, 256, "PEBBLE: 24h day/night. next_rise=%d, next_set=%d, INF=%d", (int) next_sunrise_epoch, (int) next_sunset_epoch, (int) INF);
    APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);

    // Perpetual night time.  Don't draw a path.
    if (difftime(next_sunset_epoch, next_sunrise_epoch)>0) {
      if (difftime(now, prev_sunset_epoch)<86400) {
        // Sunset happened within the last 24 hours, draw a line if you wish.
        // DRAW SUNSET LINE HERE
      }
    } 

    // Perpetual day time. Draw a full path.
    else if (difftime(next_sunrise_epoch, next_sunset_epoch)>0) {
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
  } 

  /* It is perpetual nighttime since a recent set means the next set must be after next rise, and 
  the next rise will not happen for a long time. Don't draw the day path.*/
  else if (difftime(now, prev_sunset_epoch)<86400 && prev_sunset_epoch != ZERO && difftime(next_sunrise_epoch, now)>86400 && next_sunrise_epoch != INF) {
    // DRAW SUNSET LINE HERE
  } 

  /* It is perpetual daytime since a recent rise means the next rise must be after the next set,
  and the next set will not happen for a long time. Draw full day path.*/
  else if (difftime(now, prev_sunrise_epoch)<86400 && prev_sunrise_epoch != ZERO && difftime(next_sunset_epoch, now)>86400 && next_sunset_epoch != INF) {
    GPath *new_path_ptr = gpath_create(&FULL_DAY_PATH_INFO);
    gpath_move_to(new_path_ptr, GPoint(0, 0));
    graphics_context_set_fill_color(ctx, GColorWhite);
    gpath_draw_filled(ctx, new_path_ptr);
    gpath_destroy(new_path_ptr);
    // DRAW SUNRISE LINE HERE
  } 

  /* Insufficient information to determine sky.  Draw day path.*/
  else {
    GPath *new_path_ptr = gpath_create(&FULL_DAY_PATH_INFO);
    gpath_move_to(new_path_ptr, GPoint(0, 0));
    graphics_context_set_fill_color(ctx, GColorWhite);
    gpath_draw_filled(ctx, new_path_ptr);
    gpath_destroy(new_path_ptr);
  }
}


static double calc_moon_phase(time_t now) {
  /* Calculate the current moon phase from 0 to 1.  0=new, 0.25=first quarter, and 0.5=full. */
  double diff = difftime(now, NEW_MOON) + timezone_offset;
  double phase = diff / LUNAR_CYCLE;
  phase = phase - (int)phase;
  return phase;
}


static void update_moon_image(time_t now) {
  /* Determine the image_type and image_rotation to use. Phase must be in range [0,1].
  Image types:  {0:new, 1:wax_cresc, 2:first_quarter, 3:wax_gibb, 4:full, ..., 7:wan_cresc}
  Rotations are {0:sun_at_00, 1:sun_at_03, 2:sun_at_06, 3:sun_at_09, ...}  */

  // Determine which image_type to use.
  double phase = calc_moon_phase(now);
  int img_type = (int) ((phase + 0.0625) / 0.125);
  if (img_type == 8) {img_type = 0;}

  // Determine which image_rotation to use.
  struct tm *now_cal = localtime(&now);
  int h = now_cal->tm_hour;
  int m = now_cal->tm_min;
  double hour = h + (m / 60.0);
  double rotation = hour / 24.0;
  rotation = rotation - (int)rotation;
  int img_rotation = (int) ((rotation+0.0625) / 0.125);
  if (img_rotation == 8) {img_rotation = 0;}
 
  // If we need it, load in a new image.
  int new_image_index[2] = {img_type, img_rotation};
  if (new_image_index[0] != current_image_index[0] || new_image_index[1] != current_image_index[1]) {
    current_image_index[0] = new_image_index[0];
    current_image_index[1] = new_image_index[1];
    int resource_id_b = MOON_IDS[img_type][img_rotation][0];
    int resource_id_w = MOON_IDS[img_type][img_rotation][1];

    layer_remove_from_parent(bitmap_layer_get_layer(b_moon_layer));
    bitmap_layer_destroy(b_moon_layer);
    gbitmap_destroy(b_moon_image);
    b_moon_image = gbitmap_create_with_resource(resource_id_b);
    b_moon_layer = bitmap_layer_create(GRect(0, 0, MOON_DIAMETER, MOON_DIAMETER));
    bitmap_layer_set_bitmap(b_moon_layer, b_moon_image);
    bitmap_layer_set_background_color(b_moon_layer, GColorClear);
    bitmap_layer_set_compositing_mode(b_moon_layer, GCompOpAnd);
    layer_add_child(moon_layer, bitmap_layer_get_layer(b_moon_layer));

    layer_remove_from_parent(bitmap_layer_get_layer(w_moon_layer));
    bitmap_layer_destroy(w_moon_layer);
    gbitmap_destroy(w_moon_image);
    w_moon_image = gbitmap_create_with_resource(resource_id_w);
    w_moon_layer = bitmap_layer_create(GRect(0, 0, MOON_DIAMETER, MOON_DIAMETER));
    bitmap_layer_set_bitmap(w_moon_layer, w_moon_image);
    bitmap_layer_set_background_color(w_moon_layer, GColorClear);
    bitmap_layer_set_compositing_mode(w_moon_layer, GCompOpOr);
    layer_add_child(moon_layer, bitmap_layer_get_layer(w_moon_layer));
  }
}


static void reframe_moon_layer(time_t now) {
  /* Reframe the moon layer to the correct position and make it visible.*/
  const int16_t moonDiameter = layer_get_bounds(moon_layer).size.w;  // replace with definition?
  int moonRingRadius = CLOCK_RAD - 10;
  double phase = calc_moon_phase(now);
  double seconds_behind = (phase * 24.0 * 3600);
  time_t moontime = (time_t) ((long)now - seconds_behind);
  GPoint moonLocation = get_point_from_time(moontime, moonRingRadius);
  moonLocation.x = moonLocation.x - (moonDiameter / 2);
  moonLocation.y = moonLocation.y - (moonDiameter / 2);
  layer_set_frame(moon_layer, GRect(moonLocation.x, moonLocation.y, moonDiameter, moonDiameter));
  layer_set_hidden(moon_layer, false);
}


/*  COMMUNICATION WITH PHONE
    ------------------------  */
static void get_weather() {
  if(!getting_weather) {
    getting_weather = true;
    bitmap_layer_set_bitmap(noti_layer, refresh_image);
    time_stamp = time(NULL) - TIMEOUT;  // Reset time_stamp back to interval to avoid simultanious runs of get_weather
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    Tuplet value = TupletCString(KEY_STATUS, "retrieve");
    dict_write_tuplet(iter, &value);
    app_message_outbox_send();
    }
}


static void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
  snprintf(log_buffer, 64, "PEBBLE: Failed to Send: reason %d", (int) reason);
  APP_LOG(APP_LOG_LEVEL_DEBUG, log_buffer);
}


static void out_sent_handler(DictionaryIterator *sent, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: Message sent successfully.");
}


static void in_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: App Message Dropped!");
}


static void in_received_handler(DictionaryIterator *message, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: call to in_received_handler");
  time_t now = time(NULL);
  char *status = (char*)dict_find(message, KEY_STATUS)->value;

  if(strcmp(status, "ready") == 0) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: Recieved status \"ready\"");
    js_ready = true;
    bitmap_layer_set_bitmap(noti_layer, empty_image);
    get_weather();
  } 

  else if(strcmp(status, "reporting") == 0) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: Recieved status \"reporting\"");
    getting_weather = false;
    bitmap_layer_set_bitmap(noti_layer, empty_image);

    // Timezone offset and moon
    timezone_offset = dict_find(message, KEY_TZOFFSET)->value->int32;
    timezone_missing = false;
    update_moon_image(now);
    reframe_moon_layer(now);

    // Weather and daylight path
    int incoming_sunrise = dict_find(message, KEY_SUNRISE)->value->int32;
    int incoming_sunset = dict_find(message, KEY_SUNSET)->value->int32;
    assign_rise_or_set_epoch((time_t) incoming_sunrise - timezone_offset, "rise", now);
    assign_rise_or_set_epoch((time_t) incoming_sunset - timezone_offset, "set", now);
    update_rise_and_set_epochs(now);
    layer_mark_dirty(daylight_layer);

    time_stamp = time(NULL);
  } 

  else if(strcmp(status, "failed") == 0) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: Recieved status \"failed\"");
    getting_weather = false;
    bitmap_layer_set_bitmap(noti_layer, error_image);

    timezone_offset = dict_find(message, KEY_TZOFFSET)->value->int32;
    timezone_missing = false;
    update_moon_image(now);
    reframe_moon_layer(now);

    time_stamp = time(NULL) - (TIMEOUT - ERROR_TIMEOUT);
  }
}


/*  OTHER
    -----  */
static bool data_to_load() {
  return (
    persist_exists(KEY_PREV_SUNRISE) &&
    persist_exists(KEY_NEXT_SUNRISE) &&
    persist_exists(KEY_PREV_SUNSET) &&
    persist_exists(KEY_NEXT_SUNSET) &&
    persist_exists(KEY_TIME_STAMP) &&
    persist_exists(KEY_TZOFFSET)
  );
}


static void save_data() {
  /* Save data to persistent storage if we have it.*/
  if(!timezone_missing) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: Saving data to persistent storage.");
    persist_write_int(KEY_PREV_SUNRISE, prev_sunrise_epoch);
    persist_write_int(KEY_NEXT_SUNRISE, next_sunrise_epoch);
    persist_write_int(KEY_PREV_SUNSET, prev_sunset_epoch);
    persist_write_int(KEY_NEXT_SUNSET, next_sunset_epoch);
    persist_write_int(KEY_TIME_STAMP, time_stamp);
    persist_write_int(KEY_TZOFFSET, timezone_offset);
  } else
    APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: Some values are empty, not saving.");
}


static void load_data() {
  /* If there is data saved, then load it.
  persist_read_string(KEY_SOMETHING, something_buffer, 100); */
  if(data_to_load()) {
    time_t now = time(NULL);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: Loading data from persistent storage.");

    // Get timezone and update moon.
    timezone_offset = (int)persist_read_int(KEY_TZOFFSET);
    timezone_missing = false;
    update_moon_image(now);
    reframe_moon_layer(now);

    time_stamp  =   (time_t)persist_read_int(KEY_TIME_STAMP);

    // Get rise/set information and update daypath.
    prev_sunrise_epoch = (time_t)persist_read_int(KEY_PREV_SUNRISE);
    next_sunrise_epoch = (time_t)persist_read_int(KEY_NEXT_SUNRISE);
    prev_sunset_epoch = (time_t)persist_read_int(KEY_PREV_SUNSET);
    next_sunset_epoch = (time_t)persist_read_int(KEY_NEXT_SUNSET);
    update_rise_and_set_epochs(now);
    layer_mark_dirty(daylight_layer);
  }
}


static void minute_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  /* Each minute: update clock, move the sun, check weather (if time to), 
  update moon, update epochs, redraw day path. */
  APP_LOG(APP_LOG_LEVEL_DEBUG, "PEBBLE: Tick");
  time_t now = time(NULL);
  strftime(time_buffer, sizeof("00:00"), "%H:%M", tick_time);
  text_layer_set_text(time_text_layer, time_buffer);

  reframe_sun_layer(now);

  if (time_to_refresh() && js_ready) {
    get_weather();
  }
  
  if (!timezone_missing) {
    update_moon_image(now);
    reframe_moon_layer(now);
  } else {
    layer_set_hidden(moon_layer, true);
  }

  update_rise_and_set_epochs(now);
  layer_mark_dirty(daylight_layer);
}


static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  // Create background clock including the daylight path.
  background_layer = layer_create(bounds);
  layer_add_child(window_layer, background_layer);

  w_clockface_image = gbitmap_create_with_resource(RESOURCE_ID_CLOCKFACE_W);
  w_clockface_layer = bitmap_layer_create(bounds);
  bitmap_layer_set_bitmap(w_clockface_layer, w_clockface_image);
  bitmap_layer_set_background_color(w_clockface_layer, GColorClear);
  bitmap_layer_set_compositing_mode(w_clockface_layer, GCompOpAssign);
  layer_add_child(background_layer, bitmap_layer_get_layer(w_clockface_layer));

  daylight_layer = layer_create(bounds);
  layer_set_update_proc(daylight_layer, daylight_update_proc);
  layer_add_child(background_layer, daylight_layer);

  // Create the text layer to hold the current time.
  time_text_layer = init_text_layer(GRect(44, 42, 60, 29), GColorBlack, GColorWhite, FONT_KEY_GOTHIC_28_BOLD, GTextAlignmentCenter);
  text_layer_set_text(time_text_layer, "N/A");
  layer_add_child(background_layer, (Layer*) time_text_layer);

  b_clockface_image = gbitmap_create_with_resource(RESOURCE_ID_CLOCKFACE_B);
  b_clockface_layer = bitmap_layer_create(bounds);
  bitmap_layer_set_bitmap(b_clockface_layer, b_clockface_image);
  bitmap_layer_set_background_color(b_clockface_layer, GColorClear);
  bitmap_layer_set_compositing_mode(b_clockface_layer, GCompOpAnd);
  layer_add_child(background_layer, bitmap_layer_get_layer(b_clockface_layer));

  // Create the sun layer
  sun_layer = layer_create(GRect(0, 0, SUN_DIAMETER, SUN_DIAMETER));
  layer_set_frame(sun_layer, GRect(0, 100, SUN_DIAMETER, SUN_DIAMETER));
  layer_add_child(window_layer, sun_layer);

  b_sun_image = gbitmap_create_with_resource(RESOURCE_ID_SUN_B);
  b_sun_layer = bitmap_layer_create(GRect(0, 0, SUN_DIAMETER, SUN_DIAMETER));
  bitmap_layer_set_bitmap(b_sun_layer, b_sun_image);
  bitmap_layer_set_background_color(b_sun_layer, GColorClear);
  bitmap_layer_set_compositing_mode(b_sun_layer, GCompOpAnd);
  layer_add_child(sun_layer, bitmap_layer_get_layer(b_sun_layer));

  w_sun_image = gbitmap_create_with_resource(RESOURCE_ID_SUN_W);
  w_sun_layer = bitmap_layer_create(GRect(0, 0, SUN_DIAMETER, SUN_DIAMETER));
  bitmap_layer_set_bitmap(w_sun_layer, w_sun_image);
  bitmap_layer_set_background_color(w_sun_layer, GColorClear);
  bitmap_layer_set_compositing_mode(w_sun_layer, GCompOpOr);
  layer_add_child(sun_layer, bitmap_layer_get_layer(w_sun_layer));

  // Create the moon layer
  moon_layer = layer_create(GRect(0, 0, MOON_DIAMETER, MOON_DIAMETER));
  layer_set_frame(moon_layer, GRect(80, 100, MOON_DIAMETER, MOON_DIAMETER));
  layer_set_hidden(moon_layer, true);
  layer_add_child(window_layer, moon_layer);
  
  // Create the text layers to hold the sunrise and sunset times.
  next_sunrise_text_layer = init_text_layer(GRect(104, 6, 40, 18), GColorWhite, GColorClear, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  text_layer_set_text(next_sunrise_text_layer, "N/A");
  layer_add_child(window_layer, (Layer*) next_sunrise_text_layer);
  next_sunset_text_layer = init_text_layer(GRect(104, 148, 40, 18), GColorWhite, GColorClear, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  text_layer_set_text(next_sunset_text_layer, "N/A");
  layer_add_child(window_layer, (Layer*) next_sunset_text_layer);
  prev_sunrise_text_layer = init_text_layer(GRect(0, 6, 40, 18), GColorWhite, GColorClear, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  text_layer_set_text(prev_sunrise_text_layer, "N/A");
  layer_add_child(window_layer, (Layer*) prev_sunrise_text_layer);
  prev_sunset_text_layer = init_text_layer(GRect(0, 148, 40, 18), GColorWhite, GColorClear, FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  text_layer_set_text(prev_sunset_text_layer, "N/A");
  layer_add_child(window_layer, (Layer*) prev_sunset_text_layer);

  // Create the notification layer.
  refresh_image = gbitmap_create_with_resource(RESOURCE_ID_REFRESH);
  error_image = gbitmap_create_with_resource(RESOURCE_ID_ERROR);
  empty_image = gbitmap_create_with_resource(RESOURCE_ID_EMPTY);
  no_bluetooth_image = gbitmap_create_with_resource(RESOURCE_ID_NO_BLUETOOTH);
  noti_layer = bitmap_layer_create(layer_get_frame(window_layer));
  bitmap_layer_set_background_color(noti_layer, GColorClear);
  layer_add_child(window_layer, bitmap_layer_get_layer(noti_layer));
  layer_set_frame(bitmap_layer_get_layer(noti_layer), GRect(4, 4, NOTI_W, NOTI_H));
  layer_set_bounds(bitmap_layer_get_layer(noti_layer), GRect(0, 0, NOTI_W, NOTI_H));
  bluetooth_handler(bluetooth_connection_service_peek());

  // Create the layer for battery status.
  batt_10_image = gbitmap_create_with_resource(RESOURCE_ID_BATT_10);
  batt_20_image = gbitmap_create_with_resource(RESOURCE_ID_BATT_20);
  batt_40_image = gbitmap_create_with_resource(RESOURCE_ID_BATT_40);
  batt_60_image = gbitmap_create_with_resource(RESOURCE_ID_BATT_60);
  batt_80_image = gbitmap_create_with_resource(RESOURCE_ID_BATT_80);
  batt_100_image = gbitmap_create_with_resource(RESOURCE_ID_BATT_100);
  batt_charge_image = gbitmap_create_with_resource(RESOURCE_ID_BATT_CHARGE);
  battery_layer = bitmap_layer_create(layer_get_frame(window_layer));
  bitmap_layer_set_background_color(battery_layer, GColorClear);
  layer_add_child(window_layer, bitmap_layer_get_layer(battery_layer));
  layer_set_frame(bitmap_layer_get_layer(battery_layer), GRect(120, 152, BATT_W, BATT_H));
  layer_set_bounds(bitmap_layer_get_layer(battery_layer), GRect(0, 0, BATT_W, BATT_H));
  battery_handler(battery_state_service_peek());
  
  // Initialize times
  prev_sunrise_epoch = ZERO;
  next_sunrise_epoch = INF;
  prev_sunset_epoch = ZERO;
  next_sunset_epoch = INF;

  // Hide the debug info if not in debug mode.
  if (!DEBUG_MODE) {
    layer_set_hidden((Layer*)next_sunrise_text_layer, true);
    layer_set_hidden((Layer*)prev_sunrise_text_layer, true);
    layer_set_hidden((Layer*)next_sunset_text_layer, true);
    layer_set_hidden((Layer*)prev_sunset_text_layer, true);
  }

  // Load data from persistent storage
  load_data();

  // Execute the minute handler on window load.
  time_t now = time(NULL);
  struct tm *startup_time = gmtime(&now);
  minute_tick_handler(startup_time, MINUTE_UNIT);
}


static void window_unload(Window *window) {
  // Save data to persistent storage
  save_data();

  // Destroy TextLayers.
  text_layer_destroy(time_text_layer);
  text_layer_destroy(next_sunrise_text_layer);
  text_layer_destroy(next_sunset_text_layer);
  text_layer_destroy(prev_sunrise_text_layer);
  text_layer_destroy(prev_sunset_text_layer);

  // Destroy GBitmaps.
  gbitmap_destroy(b_sun_image);
  gbitmap_destroy(w_sun_image);
  gbitmap_destroy(b_moon_image);
  gbitmap_destroy(w_moon_image);
  gbitmap_destroy(b_clockface_image);
  gbitmap_destroy(w_clockface_image);
  gbitmap_destroy(refresh_image);
  gbitmap_destroy(error_image);
  gbitmap_destroy(empty_image);
  gbitmap_destroy(no_bluetooth_image);
  gbitmap_destroy(batt_10_image);
  gbitmap_destroy(batt_20_image);
  gbitmap_destroy(batt_40_image);
  gbitmap_destroy(batt_60_image);
  gbitmap_destroy(batt_80_image);
  gbitmap_destroy(batt_100_image);
  gbitmap_destroy(batt_charge_image);

  // Destroy BitmapLayrs.
  bitmap_layer_destroy(b_sun_layer);
  bitmap_layer_destroy(w_sun_layer);
  bitmap_layer_destroy(b_moon_layer);
  bitmap_layer_destroy(w_moon_layer);
  bitmap_layer_destroy(b_clockface_layer);
  bitmap_layer_destroy(w_clockface_layer);
  bitmap_layer_destroy(noti_layer);
  bitmap_layer_destroy(battery_layer);

  // Destroy Layers.
  layer_destroy(sun_layer);
  layer_destroy(moon_layer);
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
  app_message_register_outbox_sent(out_sent_handler);
  app_message_open(256, 256);  // Large input and output buffer sizes

  // Subscribe to 'minute' events and 'bluetooth' events.
  tick_timer_service_subscribe(MINUTE_UNIT, (TickHandler) minute_tick_handler);
  bluetooth_connection_service_subscribe(bluetooth_handler);
  battery_state_service_subscribe(battery_handler);

  window_stack_push(window, true);
}


static void deinit(void) {
  window_destroy(window);
  tick_timer_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
}


int main(void) {
  init();
  app_event_loop();
  deinit();
}
