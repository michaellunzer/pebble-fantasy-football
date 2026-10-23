#include <pebble.h>

#define MAX_LEAGUES    10
#define NAME_LEN       22
#define PTS_LEN         9
#define REC_LEN         9
#define FIELD_SEP    '\x1F'
#define PERSIST_ROWS    1
#define PERSIST_COUNT   2
#define PERSIST_WEEK    3
#define PERSIST_SCHEMA  4

typedef struct {
  char my_name[NAME_LEN];
  char opp_name[NAME_LEN];
  char my_pts[PTS_LEN];
  char opp_pts[PTS_LEN];
  char my_rec[REC_LEN];
  char opp_rec[REC_LEN];
  char my_proj[PTS_LEN];     // projected final, incl. starters yet to play
  char opp_proj[PTS_LEN];
  uint8_t my_win;            // 0-100
  uint8_t my_left;           // starters who have not played yet
  uint8_t opp_left;
} LeagueRow;

// Bump whenever LeagueRow changes shape: persisted rows from an older build
// would otherwise be read back misaligned and drawn as garbage.
#define ROW_SCHEMA 2

static Window *s_window;
static ScrollLayer *s_scroll;
static Layer *s_content;
static StatusBarLayer *s_status_bar;

static LeagueRow s_rows[MAX_LEAGUES];
static int s_count;
static int s_week;
static int s_expected;             // LEAGUE_COUNT announced by the phone
static int16_t s_pan_base;         // content offset committed between pans
static char s_status[64];
static bool s_launched_by_wakeup;   // woke to refresh, so leave again promptly

// ---------------------------------------------------------------- layout tiers

typedef struct {
  GFont name_font;
  GFont score_font;
  GFont foot_font;
  int16_t name_y, score_y, bar_y, bar_h, foot_y;
  int16_t name_h, score_h, foot_h;
} RowStyle;

static RowStyle row_style(int16_t row_h) {
  RowStyle s;
  if (row_h >= 200) {                       // one league: fills the screen
    s.name_font  = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    s.score_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    s.foot_font  = fonts_get_system_font(FONT_KEY_GOTHIC_24);
    s.name_y = 24;  s.name_h  = 30;
    s.score_y = 76; s.score_h = 46;
    s.bar_y = 142;  s.bar_h   = 10;
    s.foot_y = 160; s.foot_h  = 30;
  } else if (row_h >= 100) {                // two leagues: 50% each
    s.name_font  = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    s.score_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    s.foot_font  = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    s.name_y = 6;   s.name_h  = 22;
    s.score_y = 30; s.score_h = 32;
    s.bar_y = 70;   s.bar_h   = 8;
    s.foot_y = 82;  s.foot_h  = 22;
  } else {                                  // three or more: compact, scrolls past 3
    s.name_font  = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    s.score_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    s.foot_font  = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    s.name_y = 1;   s.name_h  = 17;
    s.score_y = 17; s.score_h = 21;
    s.bar_y = 41;   s.bar_h   = 5;
    s.foot_y = 48;  s.foot_h  = 17;
  }
  return s;
}

static void draw_text(GContext *ctx, const char *txt, GFont font, GRect box,
                      GTextAlignment align) {
  graphics_draw_text(ctx, txt, font, box, GTextOverflowModeTrailingEllipsis,
                     align, NULL);
}

// True for any textual zero the phone might send: "0", "0.0", "0.00".
static bool is_zero_score(const char *s) {
  bool seen_digit = false;
  for (const char *p = s; *p; p++) {
    if (*p == '.') continue;
    if (*p != '0') return false;
    seen_digit = true;
  }
  return seen_digit;
}

static bool is_pregame(const LeagueRow *r) {
  // Both sides still on zero: the week has not kicked off yet.
  return is_zero_score(r->my_pts) && is_zero_score(r->opp_pts);
}

static void draw_row(GContext *ctx, const LeagueRow *r, GRect row) {
  RowStyle st = row_style(row.size.h);

  // Card
  GRect card = GRect(row.origin.x + 3, row.origin.y + 2,
                     row.size.w - 6, row.size.h - 4);
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, card, 6, GCornersAll);

  const int16_t pad = 6;
  const int16_t inner_x = card.origin.x + pad;
  const int16_t inner_w = card.size.w - 2 * pad;
  const int16_t half = inner_w / 2 - 2;

  // Team names, mine left and the opponent right
  graphics_context_set_text_color(ctx, GColorWhite);
  draw_text(ctx, r->my_name, st.name_font,
            GRect(inner_x, row.origin.y + st.name_y, half, st.name_h),
            GTextAlignmentLeft);
  draw_text(ctx, r->opp_name, st.name_font,
            GRect(inner_x + inner_w - half, row.origin.y + st.name_y, half,
                  st.name_h),
            GTextAlignmentRight);

  // Scores, with VS between them
  GRect score_box = GRect(inner_x, row.origin.y + st.score_y, inner_w,
                          st.score_h);
  bool pregame = is_pregame(r);
  if (pregame) {
    // Nothing scored yet, so the projection is the only real information.
    // Dimmed and labelled PROJ so it cannot be mistaken for a live score.
    graphics_context_set_text_color(ctx, GColorLightGray);
    draw_text(ctx, r->my_proj, st.score_font, score_box, GTextAlignmentLeft);
    draw_text(ctx, r->opp_proj, st.score_font, score_box, GTextAlignmentRight);
  } else {
    draw_text(ctx, r->my_pts, st.score_font, score_box, GTextAlignmentLeft);
    draw_text(ctx, r->opp_pts, st.score_font, score_box, GTextAlignmentRight);
  }
  graphics_context_set_text_color(ctx, GColorLightGray);
  draw_text(ctx, pregame ? "PROJ" : "VS",
            fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GRect(score_box.origin.x, score_box.origin.y + st.score_h / 4,
                  score_box.size.w, st.score_h),
            GTextAlignmentCenter);

  // Win-probability bar: leader green, trailing side red, split at my_win%
  int my_w = (inner_w * r->my_win) / 100;
  if (my_w < 0) my_w = 0;
  if (my_w > inner_w) my_w = inner_w;
  bool i_lead = (r->my_win >= 50);
  GRect bar_l = GRect(inner_x, row.origin.y + st.bar_y, my_w, st.bar_h);
  GRect bar_r = GRect(inner_x + my_w, row.origin.y + st.bar_y, inner_w - my_w,
                      st.bar_h);
  graphics_context_set_fill_color(ctx, i_lead ? GColorMediumSpringGreen
                                              : GColorFolly);
  graphics_fill_rect(ctx, bar_l, 2, GCornersLeft);
  graphics_context_set_fill_color(ctx, i_lead ? GColorFolly
                                              : GColorMediumSpringGreen);
  graphics_fill_rect(ctx, bar_r, 2, GCornersRight);

  // Footer: win% and record on each side
  // While anyone is still to play, the count of players left says far more
  // about a deficit than the season record does. Swap back once they are done.
  char left[22], right[22];
  if (r->my_left > 0 || r->opp_left > 0) {
    snprintf(left, sizeof(left), "%d%%  %d left", r->my_win, r->my_left);
    snprintf(right, sizeof(right), "%d left  %d%%", r->opp_left, 100 - r->my_win);
  } else {
    snprintf(left, sizeof(left), "%d%%  %s", r->my_win, r->my_rec);
    snprintf(right, sizeof(right), "%s  %d%%", r->opp_rec, 100 - r->my_win);
  }
  graphics_context_set_text_color(ctx, GColorLightGray);
  draw_text(ctx, left, st.foot_font,
            GRect(inner_x, row.origin.y + st.foot_y, half, st.foot_h),
            GTextAlignmentLeft);
  draw_text(ctx, right, st.foot_font,
            GRect(inner_x + inner_w - half, row.origin.y + st.foot_y, half,
                  st.foot_h),
            GTextAlignmentRight);
}

static void content_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (s_count == 0) {
    graphics_context_set_text_color(ctx, GColorWhite);
    draw_text(ctx, s_status[0] ? s_status
                               : "Open settings to add your Sleeper leagues",
              fonts_get_system_font(FONT_KEY_GOTHIC_18),
              GRect(10, 60, b.size.w - 20, 120), GTextAlignmentCenter);
    return;
  }

  int16_t row_h = b.size.h / s_count;
  for (int i = 0; i < s_count; i++) {
    draw_row(ctx, &s_rows[i], GRect(0, i * row_h, b.size.w, row_h));
  }
}

// ---------------------------------------------------------------- scroll sizing

static int16_t visible_height(void) {
  GRect b = layer_get_bounds(window_get_root_layer(s_window));
  return b.size.h - STATUS_BAR_LAYER_HEIGHT;
}

static void relayout(void) {
  int16_t vis = visible_height();
  int16_t w = layer_get_bounds(window_get_root_layer(s_window)).size.w;
  int n = s_count > 0 ? s_count : 1;
  // 1 league fills the screen, 2 take half each, 3 a third each. Past 3 the row
  // height stays at the 3-league size and the content scrolls.
  int16_t row_h = (n <= 3) ? (vis / n) : (vis / 3);
  int16_t content_h = (s_count > 0) ? row_h * s_count : vis;
  if (content_h < vis) content_h = vis;

  layer_set_frame(s_content, GRect(0, 0, w, content_h));
  layer_set_bounds(s_content, GRect(0, 0, w, content_h));
  scroll_layer_set_content_size(s_scroll, GSize(w, content_h));
  scroll_layer_set_content_offset(s_scroll, GPoint(0, 0), false);
  s_pan_base = 0;
  layer_mark_dirty(s_content);
}

// ---------------------------------------------------------------- touch (pan)

// SDK 4.33 added gesture recognizers, which are the right way to scroll a
// ScrollLayer by finger. Older SDKs still expose the raw TouchService, so keep
// a hand-rolled equivalent for them: the 4.33 firmware does not currently boot
// in the QEMU emulator, and the fallback is what lets the app be demonstrated
// there. Both paths drive the same scroll offset.
#ifdef _PBL_API_EXISTS_pan_recognizer_create
#define HAVE_RECOGNIZERS 1
#else
#define HAVE_RECOGNIZERS 0
#endif

#if HAVE_RECOGNIZERS

static void pan_handler(const Recognizer *recognizer, RecognizerEvent event) {
  switch (event) {
    case RecognizerEvent_Updated: {
      // delta_since_start is (0,0) at Started, so the content never jumps.
      GPoint d = pan_recognizer_get_delta_since_start(recognizer);
      scroll_layer_set_content_offset(s_scroll, GPoint(0, s_pan_base + d.y),
                                      false);
      break;
    }
    case RecognizerEvent_Completed:
      s_pan_base = scroll_layer_get_content_offset(s_scroll).y;
      break;
    case RecognizerEvent_Cancelled:
      scroll_layer_set_content_offset(s_scroll, GPoint(0, s_pan_base), true);
      break;
    default:
      break;
  }
}

static void touch_attach(Window *window) {
  // Take the touch stream off the system button bridge so the pan recognizer
  // sees it, then drive the ScrollLayer from the pan.
  window_set_touch_bridge_disabled(window, true);
  window_attach_recognizer(window, pan_recognizer_create(pan_handler, NULL,
                                                         PanAxis_Vertical));
}

static void touch_enable_app(void) { app_touch_navigation_enable(true); }

#else   /* pre-4.33: drive the scroll from raw touch events */

static int16_t s_touch_origin;
static bool s_touch_active;

static void touch_handler(const TouchEvent *event, void *context) {
  switch (event->type) {
    case TouchEvent_Touchdown:
      s_touch_origin = event->y;
      s_touch_active = true;
      break;
    case TouchEvent_PositionUpdate:
      if (s_touch_active) {
        scroll_layer_set_content_offset(
            s_scroll, GPoint(0, s_pan_base + (event->y - s_touch_origin)),
            false);
      }
      break;
    case TouchEvent_Liftoff:
      s_touch_active = false;
      s_pan_base = scroll_layer_get_content_offset(s_scroll).y;
      break;
    default:
      break;
  }
}

static void touch_attach(Window *window) { (void)window; }

static void touch_enable_app(void) {
  touch_service_subscribe(touch_handler, NULL);
}

#endif

// ---------------------------------------------------------------- persistence

static void save_rows(void) {
  persist_write_data(PERSIST_ROWS, s_rows, sizeof(LeagueRow) * MAX_LEAGUES);
  persist_write_int(PERSIST_COUNT, s_count);
  persist_write_int(PERSIST_WEEK, s_week);
  persist_write_int(PERSIST_SCHEMA, ROW_SCHEMA);
}

static void load_rows(void) {
  // Rows written by a build with a different LeagueRow layout are unreadable.
  int schema = persist_exists(PERSIST_SCHEMA) ? persist_read_int(PERSIST_SCHEMA) : 0;
  if (schema != ROW_SCHEMA) {
    s_count = 0;
    return;
  }
  if (persist_exists(PERSIST_COUNT)) {
    s_count = persist_read_int(PERSIST_COUNT);
    if (s_count < 0) s_count = 0;
    if (s_count > MAX_LEAGUES) s_count = MAX_LEAGUES;
    s_week = persist_exists(PERSIST_WEEK) ? persist_read_int(PERSIST_WEEK) : 0;
    if (persist_exists(PERSIST_ROWS)) {
      persist_read_data(PERSIST_ROWS, s_rows, sizeof(LeagueRow) * MAX_LEAGUES);
    }
  }
}

// ---------------------------------------------------------------- app message

// Copy one FIELD_SEP-delimited field out of src into dest.
static const char *take_field(const char *src, char *dest, size_t cap) {
  size_t i = 0;
  while (*src && *src != FIELD_SEP) {
    if (i + 1 < cap) dest[i++] = *src;
    src++;
  }
  dest[i] = '\0';
  return (*src == FIELD_SEP) ? src + 1 : src;
}

static void parse_league(int idx, const char *data) {
  if (idx < 0 || idx >= MAX_LEAGUES) return;
  LeagueRow *r = &s_rows[idx];
  char win[8];
  const char *p = data;
  p = take_field(p, r->my_name,  NAME_LEN);
  p = take_field(p, r->opp_name, NAME_LEN);
  p = take_field(p, r->my_pts,   PTS_LEN);
  p = take_field(p, r->opp_pts,  PTS_LEN);
  p = take_field(p, win,         sizeof(win));
  p = take_field(p, r->my_rec,   REC_LEN);
  p = take_field(p, r->opp_rec,  REC_LEN);

  char num[8];
  p = take_field(p, num, sizeof(num));
  r->my_left = (uint8_t)atoi(num);
  p = take_field(p, num, sizeof(num));
  r->opp_left = (uint8_t)atoi(num);
  p = take_field(p, r->my_proj,  PTS_LEN);
  p = take_field(p, r->opp_proj, PTS_LEN);

  int w = atoi(win);
  if (w < 0) w = 0;
  if (w > 100) w = 100;
  r->my_win = (uint8_t)w;
}

// window_stack_pop_all takes a bool, not the void* an AppTimerCallback is handed,
// so it needs a wrapper rather than a cast.
static void exit_after_wakeup(void *data) {
  window_stack_pop_all(false);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t_status = dict_find(iter, MESSAGE_KEY_STATUS);
  if (t_status) {
    strncpy(s_status, t_status->value->cstring, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
  }

  Tuple *t_week = dict_find(iter, MESSAGE_KEY_WEEK);
  if (t_week) s_week = t_week->value->int32;

  // Exactly one wakeup is ever pending: cancel whatever was armed and re-arm
  // from the time the phone just gave us. 0 means "nothing live, do not wake".
  Tuple *t_wake = dict_find(iter, MESSAGE_KEY_WAKEUP_AT);
  if (t_wake) {
    wakeup_cancel_all();
    time_t when = (time_t)t_wake->value->int32;
    if (when > time(NULL)) {
      wakeup_schedule(when, 0, true);
      APP_LOG(APP_LOG_LEVEL_INFO, "next wakeup in %d min",
              (int)((when - time(NULL)) / 60));
    }
    // We only woke to refresh the glance; do not sit in front of the user.
    if (s_launched_by_wakeup) {
      app_timer_register(2000, exit_after_wakeup, NULL);
    }
  }

  Tuple *t_count = dict_find(iter, MESSAGE_KEY_LEAGUE_COUNT);
  if (t_count) {
    s_expected = t_count->value->int32;
    if (s_expected < 0) s_expected = 0;
    if (s_expected > MAX_LEAGUES) s_expected = MAX_LEAGUES;
    if (s_expected == 0) {
      s_count = 0;
      relayout();
    }
  }

  Tuple *t_idx  = dict_find(iter, MESSAGE_KEY_LEAGUE_IDX);
  Tuple *t_data = dict_find(iter, MESSAGE_KEY_LEAGUE_DATA);
  if (t_idx && t_data) {
    int idx = t_idx->value->int32;
    parse_league(idx, t_data->value->cstring);
    if (idx + 1 > s_count) s_count = idx + 1;
    if (s_count > MAX_LEAGUES) s_count = MAX_LEAGUES;
    APP_LOG(APP_LOG_LEVEL_INFO, "league %d of %d: %s vs %s", idx, s_expected,
            s_rows[idx].my_name, s_rows[idx].opp_name);
    // Re-layout once the final league of the batch lands, so row heights are
    // computed against the real league count rather than a partial one.
    if (s_expected > 0 && idx == s_expected - 1) {
      s_count = s_expected;
      save_rows();
      relayout();
    } else {
      layer_mark_dirty(s_content);
    }
  } else if (t_status) {
    layer_mark_dirty(s_content);
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "inbox dropped: %d", (int)reason);
}

static void request_refresh(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_REFRESH, 1);
    app_message_outbox_send();
  }
}

// ---------------------------------------------------------------- window

static void select_click(ClickRecognizerRef recognizer, void *context) {
  request_refresh();
}

// Called by the ScrollLayer after it has claimed Up/Down, so Select is ours
// without disturbing scrolling.
static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_status_bar = status_bar_layer_create();
  status_bar_layer_set_colors(s_status_bar, GColorBlack, GColorWhite);
  status_bar_layer_set_separator_mode(s_status_bar,
                                      StatusBarLayerSeparatorModeNone);
  layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

  GRect scroll_frame = GRect(0, STATUS_BAR_LAYER_HEIGHT, b.size.w,
                             b.size.h - STATUS_BAR_LAYER_HEIGHT);
  s_scroll = scroll_layer_create(scroll_frame);
  scroll_layer_set_shadow_hidden(s_scroll, true);
  scroll_layer_set_callbacks(s_scroll, (ScrollLayerCallbacks) {
    .click_config_provider = click_config,
  });
  scroll_layer_set_click_config_onto_window(s_scroll, window);

  s_content = layer_create(GRect(0, 0, scroll_frame.size.w,
                                 scroll_frame.size.h));
  layer_set_update_proc(s_content, content_update);
  scroll_layer_add_child(s_scroll, s_content);
  layer_add_child(root, scroll_layer_get_layer(s_scroll));

  // A bare ScrollLayer does not scroll by touch on its own.
  touch_attach(window);

  relayout();
}

static void window_unload(Window *window) {
  layer_destroy(s_content);
  scroll_layer_destroy(s_scroll);
  status_bar_layer_destroy(s_status_bar);
}

static void init(void) {
  load_rows();
  s_launched_by_wakeup = (launch_reason() == APP_LAUNCH_WAKEUP);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

  // Make the app touch-aware (opt-in on 4.33+, a raw subscription before).
  touch_enable_app();

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_open(2048, 256);

  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
