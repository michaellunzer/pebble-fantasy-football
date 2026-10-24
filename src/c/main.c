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

#define MAX_PLAYERS 16
#define PLAYER_NAME 18

// One starter in the detail view.
typedef struct {
  char pos[6];
  char name[PLAYER_NAME];
  char actual[PTS_LEN];      // points banked, or "-" before their game
  char proj[PTS_LEN];        // what they are projected to finish on
  char matchup[14];          // "SF \u00b7 ATL"
  uint8_t state;             // 0 done, 1 still to play, 2 no game (bye/inactive)
} PlayerRow;

static Window *s_window;
static MenuLayer *s_menu;
static StatusBarLayer *s_status_bar;

static Window *s_detail_window;
static MenuLayer *s_detail_menu;
static PlayerRow s_players[MAX_PLAYERS];
static int s_player_count;
static int s_player_expected;
static char s_detail_title[NAME_LEN];   // whose lineup is on screen
static char s_detail_other[NAME_LEN];   // whose lineup Select switches to
static int s_detail_league = -1;        // which league the detail view is for
static int s_detail_side;               // 0 = my team, 1 = the opponent's

static LeagueRow s_rows[MAX_LEAGUES];
static int s_count;
static int s_week;
static int s_expected;             // LEAGUE_COUNT announced by the phone
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

static void draw_row(GContext *ctx, const LeagueRow *r, GRect row,
                     bool selected) {
  RowStyle st = row_style(row.size.h);

  // Card
  GRect card = GRect(row.origin.x + 3, row.origin.y + 2,
                     row.size.w - 6, row.size.h - 4);
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, card, 6, GCornersAll);
  if (selected) {
    // Select now opens the detail view, so the focused card has to be obvious.
    graphics_context_set_stroke_color(ctx, GColorMediumSpringGreen);
    graphics_draw_round_rect(ctx, card, 6);
  }

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

static void request_refresh(void);

// ------------------------------------------------------------------ menu

// 1 league fills the screen, 2 take half each, 3 a third each. Past 3 the row
// height stays at the 3-league size and the list scrolls.
static int16_t row_height(void) {
  GRect b = layer_get_bounds(window_get_root_layer(s_window));
  int16_t vis = b.size.h - STATUS_BAR_LAYER_HEIGHT;
  int n = s_count > 0 ? s_count : 1;
  return (n <= 3) ? (vis / n) : (vis / 3);
}

static uint16_t menu_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  return s_count > 0 ? s_count : 1;      // one row carries the empty state
}

static int16_t menu_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (s_count == 0) {
    GRect b = layer_get_bounds(window_get_root_layer(s_window));
    return b.size.h - STATUS_BAR_LAYER_HEIGHT;
  }
  return row_height();
}

static void menu_draw_row(GContext *gctx, const Layer *cell, MenuIndex *idx,
                          void *ctx) {
  GRect b = layer_get_bounds(cell);
  graphics_context_set_fill_color(gctx, GColorBlack);
  graphics_fill_rect(gctx, b, 0, GCornerNone);

  if (s_count == 0) {
    graphics_context_set_text_color(gctx, GColorWhite);
    draw_text(gctx, s_status[0] ? s_status
                                : "Open settings to add your Sleeper leagues",
              fonts_get_system_font(FONT_KEY_GOTHIC_18),
              GRect(10, 50, b.size.w - 20, 120), GTextAlignmentCenter);
    return;
  }

  MenuIndex sel = menu_layer_get_selected_index(s_menu);
  draw_row(gctx, &s_rows[idx->row], b, menu_index_compare(&sel, idx) == 0);
}

static void request_detail(int league_idx);

static void menu_select(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (s_count == 0) return;
  request_detail(idx->row);
}

// Long-press Select still forces a refresh, since Select itself now opens the
// detail view.
static void menu_select_long(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  request_refresh();
}

static void menu_reload(void) {
  if (s_menu) menu_layer_reload_data(s_menu);
}

// --------------------------------------------------------- detail view menu

static uint16_t detail_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  return s_player_count > 0 ? s_player_count : 1;
}

static int16_t detail_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  return 44;        // two stacked lines, as the Sleeper app lays a starter out
}

static int16_t detail_header_height(MenuLayer *ml, uint16_t section, void *ctx) {
  return 36;        // team name, then the side's running and projected totals
}

static void detail_draw_header(GContext *gctx, const Layer *cell,
                               uint16_t section, void *ctx) {
  GRect b = layer_get_bounds(cell);
  graphics_context_set_fill_color(gctx, GColorOxfordBlue);
  graphics_fill_rect(gctx, b, 0, GCornerNone);

  int16_t x = b.origin.x + 4;
  int16_t w = b.size.w - 8;

  // Top line: whose lineup this is, and whose Select switches to. With the
  // projection moved down, both names get back the room they were losing.
  graphics_context_set_text_color(gctx, GColorWhite);
  draw_text(gctx, s_detail_title[0] ? s_detail_title : "Starters",
            fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GRect(x, b.origin.y - 2, (w / 2) + 6, 18), GTextAlignmentLeft);

  if (s_detail_other[0]) {
    char hint[NAME_LEN + 4];
    snprintf(hint, sizeof(hint), "%s \u203a", s_detail_other);
    graphics_context_set_text_color(gctx, GColorLightGray);
    draw_text(gctx, hint, fonts_get_system_font(FONT_KEY_GOTHIC_14),
              GRect(x + (w / 2) + 6, b.origin.y - 2, (w / 2) - 6, 18),
              GTextAlignmentRight);
  }

  // Second line pairs the two totals the way every row below pairs them:
  // what is banked, then what it is projected to become.
  if (s_detail_league >= 0 && s_detail_league < s_count) {
    const LeagueRow *r = &s_rows[s_detail_league];
    bool mine = (s_detail_side == 0);

    graphics_context_set_text_color(gctx, GColorWhite);
    draw_text(gctx, mine ? r->my_pts : r->opp_pts,
              fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
              GRect(x, b.origin.y + 14, w, 22), GTextAlignmentLeft);

    char p[PTS_LEN + 6];
    snprintf(p, sizeof(p), "proj %s", mine ? r->my_proj : r->opp_proj);
    graphics_context_set_text_color(gctx, GColorLightGray);
    draw_text(gctx, p, fonts_get_system_font(FONT_KEY_GOTHIC_18),
              GRect(x, b.origin.y + 14, w, 22), GTextAlignmentCenter);
  }
}

// Sleeper colour-codes the position chip, and that colour is how you find a
// slot without reading. Keep the same associations: QB pink, RB green, WR blue,
// TE orange, K purple, DEF sand.
static GColor position_color(const char *pos) {
  if (strcmp(pos, "QB") == 0)  return GColorMelon;
  if (strcmp(pos, "RB") == 0)  return GColorMintGreen;
  if (strcmp(pos, "WR") == 0)  return GColorPictonBlue;
  if (strcmp(pos, "TE") == 0)  return GColorRajah;
  if (strcmp(pos, "K") == 0)   return GColorLavenderIndigo;
  if (strcmp(pos, "DEF") == 0) return GColorPastelYellow;
  return GColorCeleste;                      // FLEX and anything unexpected
}

static void detail_draw_row(GContext *gctx, const Layer *cell, MenuIndex *idx,
                            void *ctx) {
  GRect b = layer_get_bounds(cell);
  graphics_context_set_fill_color(gctx, GColorBlack);
  graphics_fill_rect(gctx, b, 0, GCornerNone);

  if (s_player_count == 0) {
    graphics_context_set_text_color(gctx, GColorLightGray);
    draw_text(gctx, "Loading\u2026", fonts_get_system_font(FONT_KEY_GOTHIC_18),
              GRect(8, b.origin.y + 8, b.size.w - 16, 24), GTextAlignmentCenter);
    return;
  }

  const PlayerRow *pl = &s_players[idx->row];
  int16_t y = b.origin.y;

  // Two columns: identity on the left, numbers on the right. The matchup is
  // right-aligned under the name so it meets the numbers instead of leaving a
  // band of dead space across the middle of every row.
  const int16_t num_w = 64;
  int16_t nx = b.origin.x + b.size.w - num_w - 3;
  int16_t tx = b.origin.x + 42;
  int16_t tw = nx - tx - 4;

  // Position chip
  GRect chip = GRect(b.origin.x + 3, y + 6, 35, 23);
  graphics_context_set_fill_color(gctx, position_color(pl->pos));
  graphics_fill_rect(gctx, chip, 4, GCornersAll);
  graphics_context_set_text_color(gctx, GColorBlack);
  draw_text(gctx, pl->pos, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GRect(chip.origin.x, chip.origin.y - 3, chip.size.w, chip.size.h + 6),
            GTextAlignmentCenter);

  graphics_context_set_text_color(gctx, GColorWhite);
  draw_text(gctx, pl->name, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(tx, y + 1, tw, 27), GTextAlignmentLeft);
  graphics_context_set_text_color(gctx, GColorLightGray);
  draw_text(gctx, pl->matchup, fonts_get_system_font(FONT_KEY_GOTHIC_18),
            GRect(tx, y + 24, tw, 21), GTextAlignmentRight);

  // Points banked above points projected.
  bool played = (pl->state == 0);
  graphics_context_set_text_color(gctx, played ? GColorWhite : GColorLightGray);
  draw_text(gctx, pl->state == 2 ? "\u2013" : pl->actual,
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            GRect(nx, y + 1, num_w, 27), GTextAlignmentRight);
  graphics_context_set_text_color(gctx, GColorDarkGray);
  draw_text(gctx, pl->proj, fonts_get_system_font(FONT_KEY_GOTHIC_18),
            GRect(nx, y + 24, num_w, 21), GTextAlignmentRight);
}

static void request_detail_side(int league_idx, int side);

static void detail_select(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (s_detail_league < 0) return;
  s_detail_side = s_detail_side ? 0 : 1;
  s_player_count = 0;                       // show Loading while it arrives
  menu_layer_reload_data(s_detail_menu);
  // Back to the top: the other lineup is a different list, and keeping the old
  // scroll position drops you into the middle of it with the header off screen.
  menu_layer_set_selected_index(s_detail_menu, (MenuIndex) { 0, 0 },
                                MenuRowAlignTop, false);
  request_detail_side(s_detail_league, s_detail_side);
}

static void detail_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  s_detail_menu = menu_layer_create(b);
  menu_layer_set_callbacks(s_detail_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = detail_num_rows,
    .get_cell_height = detail_cell_height,
    .get_header_height = detail_header_height,
    .draw_header = detail_draw_header,
    .draw_row = detail_draw_row,
    .select_click = detail_select,
  });
  menu_layer_set_normal_colors(s_detail_menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_detail_menu, GColorOxfordBlue, GColorWhite);
  menu_layer_set_click_config_onto_window(s_detail_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_detail_menu));
}

static void detail_window_unload(Window *window) {
  menu_layer_destroy(s_detail_menu);
  s_detail_menu = NULL;
  window_destroy(s_detail_window);
  s_detail_window = NULL;
}

static void show_detail_window(void) {
  s_player_count = 0;
  s_player_expected = 0;
  s_detail_window = window_create();
  window_set_background_color(s_detail_window, GColorBlack);
  window_set_window_handlers(s_detail_window, (WindowHandlers) {
    .load = detail_window_load,
    .unload = detail_window_unload,
  });
  window_stack_push(s_detail_window, true);
}

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

static void parse_player(int idx, const char *data) {
  if (idx < 0 || idx >= MAX_PLAYERS) return;
  PlayerRow *pl = &s_players[idx];
  char st[4];
  const char *p = data;
  p = take_field(p, pl->pos,     sizeof(pl->pos));
  p = take_field(p, pl->name,    PLAYER_NAME);
  p = take_field(p, pl->actual,  PTS_LEN);
  p = take_field(p, pl->proj,    PTS_LEN);
  p = take_field(p, pl->matchup, sizeof(pl->matchup));
  p = take_field(p, st,          sizeof(st));
  pl->state = (uint8_t)atoi(st);
}

static void request_detail_side(int league_idx, int side) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_int32(iter, MESSAGE_KEY_REQUEST_DETAIL, league_idx);
    dict_write_int32(iter, MESSAGE_KEY_DETAIL_SIDE, side);
    app_message_outbox_send();
  }
}

static void request_detail(int league_idx) {
  s_detail_league = league_idx;
  s_detail_side = 0;
  s_detail_other[0] = '\0';
  request_detail_side(league_idx, 0);
  show_detail_window();
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

  Tuple *t_title = dict_find(iter, MESSAGE_KEY_DETAIL_TITLE);
  if (t_title) {
    const char *rest = take_field(t_title->value->cstring, s_detail_title,
                                  NAME_LEN);
    take_field(rest, s_detail_other, NAME_LEN);
  }

  Tuple *t_dcount = dict_find(iter, MESSAGE_KEY_DETAIL_COUNT);
  if (t_dcount) {
    s_player_expected = t_dcount->value->int32;
    if (s_player_expected > MAX_PLAYERS) s_player_expected = MAX_PLAYERS;
    s_player_count = 0;
  }

  Tuple *t_didx  = dict_find(iter, MESSAGE_KEY_DETAIL_IDX);
  Tuple *t_ddata = dict_find(iter, MESSAGE_KEY_DETAIL_DATA);
  if (t_didx && t_ddata) {
    int di = t_didx->value->int32;
    parse_player(di, t_ddata->value->cstring);
    if (di + 1 > s_player_count) s_player_count = di + 1;
    if (s_player_count > MAX_PLAYERS) s_player_count = MAX_PLAYERS;
    if (s_detail_menu) {
      menu_layer_reload_data(s_detail_menu);
      if (di == 0) {
        menu_layer_set_selected_index(s_detail_menu, (MenuIndex) { 0, 0 },
                                      MenuRowAlignTop, false);
      }
    }
    return;      // detail traffic never touches the league list
  }

  Tuple *t_count = dict_find(iter, MESSAGE_KEY_LEAGUE_COUNT);
  if (t_count) {
    s_expected = t_count->value->int32;
    if (s_expected < 0) s_expected = 0;
    if (s_expected > MAX_LEAGUES) s_expected = MAX_LEAGUES;
    if (s_expected == 0) {
      s_count = 0;
      menu_reload();
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
    }
    menu_reload();
  } else if (t_status) {
    menu_reload();
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

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_status_bar = status_bar_layer_create();
  status_bar_layer_set_colors(s_status_bar, GColorBlack, GColorWhite);
  status_bar_layer_set_separator_mode(s_status_bar,
                                      StatusBarLayerSeparatorModeNone);
  layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

  // MenuLayer rather than a ScrollLayer: on 4.33 it scrolls by touch on its
  // own, and it brings row selection and Select handling with it, which the
  // detail view needs. That replaces the hand-driven pan recognizer entirely.
  s_menu = menu_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, b.size.w,
                                   b.size.h - STATUS_BAR_LAYER_HEIGHT));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = menu_num_rows,
    .get_cell_height = menu_cell_height,
    .draw_row = menu_draw_row,
    .select_click = menu_select,
    .select_long_click = menu_select_long,
  });
  // The rows paint their own cards, so the menu's own highlight stays out of
  // the way and selection is shown by the card border instead.
  menu_layer_set_normal_colors(s_menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_menu, GColorBlack, GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu);
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

  // MenuLayer scrolls by touch on 4.33, but only for apps that opt in to the
  // system touch-navigation bridge; third-party apps are opted out by default.
  // Older SDKs have no such call, and MenuLayer there is buttons-only.
#ifdef _PBL_API_EXISTS_app_touch_navigation_enable
  app_touch_navigation_enable(true);
#endif

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
