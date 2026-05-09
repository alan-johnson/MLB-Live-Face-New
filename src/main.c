/*

MLB Live Face v2

----------------------

The MIT License (MIT)

Copyright © 2016 Chris Schlitt

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

--------------------

*/

#include <pebble.h>
#include "sliding-text-layer.h"

// Set to 1 when running in the emulator to capture App Store screenshots.
// The watch face loads fixture game data directly — no JS / AppMessage needed.
// Set back to 0 before building for production.
#define DEMO_MODE 1

// Score format: other platforms use trailing spaces + right-align to park the digit;
// gabbro uses left-align at the layer x, so just the digit is needed.
#ifdef PBL_PLATFORM_GABBRO
  #define SCORE_FMT "%d"
#else
  #define SCORE_FMT "%d                     "
#endif

Window *window;	
static TextLayer *s_time_layer;	
static SlidingTextLayer *s_home_team_layer;
static SlidingTextLayer *s_away_team_layer;
static SlidingTextLayer *s_game_time_layer;
static SlidingTextLayer *s_inning_layer;
static SlidingTextLayer* s_away_data_layer;
static SlidingTextLayer* s_home_data_layer;
static SlidingTextLayer* s_loading_layer;
static GFont s_font_mlb_40;
static GFont s_font_mlb_18;
static GFont s_font_capital_20;
static GFont s_font_phillies_22;
static GBitmap *s_team_logo;
static BitmapLayer *s_team_logo_layer;
static Layer *s_bso_layer;
static Layer *s_inning_state_layer;
static Layer *s_bases_layer;
static char inning[15];
static char home_score[35];
static char away_score[35];
static char s_buffer_prev[8];
uint32_t logo[33] = {RESOURCE_ID_PHILLIES, RESOURCE_ID_ANGELS, RESOURCE_ID_ASTROS, RESOURCE_ID_ATHLETICS, RESOURCE_ID_BLUEJAYS, RESOURCE_ID_BRAVES, RESOURCE_ID_BREWERS, RESOURCE_ID_CARDINALS, RESOURCE_ID_CUBS, RESOURCE_ID_DIAMONDBACKS, RESOURCE_ID_DODGERS, RESOURCE_ID_GIANTS, RESOURCE_ID_INDIANS, RESOURCE_ID_MARINERS, RESOURCE_ID_MARLINS, RESOURCE_ID_METS, RESOURCE_ID_NATIONALS, RESOURCE_ID_ORIOLES, RESOURCE_ID_PADRES, RESOURCE_ID_PHILLIES, RESOURCE_ID_PIRATES, RESOURCE_ID_RANGERS, RESOURCE_ID_RAYS, RESOURCE_ID_REDSOX, RESOURCE_ID_REDS, RESOURCE_ID_ROCKIES, RESOURCE_ID_ROYALS, RESOURCE_ID_TIGERS, RESOURCE_ID_TWINS, RESOURCE_ID_WHITESOX, RESOURCE_ID_YANKEES, RESOURCE_ID_NL, RESOURCE_ID_AL};
static int logo_uodate_i = 0;
int color_update = 0;

// Color Resources
#define ASCII_0_VALU 48
#define ASCII_9_VALU 57
#define ASCII_A_VALU 65
#define ASCII_F_VALU 70
unsigned int HexStringToUInt(char const* hexstring)
{
    unsigned int result = 0;
    char const *c = hexstring;
    char thisC;
    while( (thisC = *c) != 0 )
    {
        unsigned int add;

        result <<= 4;
        if( thisC >= ASCII_0_VALU &&  thisC <= ASCII_9_VALU )
            add = thisC - ASCII_0_VALU;
        else if( thisC >= ASCII_A_VALU && thisC <= ASCII_F_VALU)
            add = thisC - ASCII_A_VALU + 10;
        else
        {
            printf("Unrecognised hex character \"%c\"\n", thisC);
            return 0;
        }
        result += add;
        ++c;
    }
    return result;  
}
GColor GColorFromStringBW(char* color){
  if(strcmp(color, "FFFFFF") == 0){
    return GColorWhite;
  } else {
    return GColorBlack;
  }
}

// Game Data data type
typedef struct GameDataSets {
  char home_team[4];
  char away_team[4];
  int num_games;
  int status;
  char home_pitcher[20];
  char away_pitcher[20];
  char game_time[6];
  char home_broadcast[20];
  char away_broadcast[20];
  int first;
  int second;
  int third;
  int home_score;
  int away_score;
  int inning;
  char inning_half[10];
  int balls;
  int strikes;
  int outs;
} GameData;

// Settings data type
typedef struct Settings {
  int favorite_team;
  int shake_enabeled;
  int shake_time;
  int refresh_time_off;
  int refresh_time_on;
  int bases_display;
  GColor primary_color;
  GColor secondary_color;
  GColor background_color;
} Settings;

static GameData currentGameData;
static GameData previousGameData;
static Settings userSettings;

static void initialize_settings(){
  userSettings.favorite_team = 8;
  userSettings.shake_enabeled = 1;
  userSettings.shake_time = 5;
  userSettings.bases_display = 1;
  userSettings.refresh_time_off = 3600;
  userSettings.refresh_time_on = 180;
  #ifdef PBL_COLOR
    userSettings.primary_color = GColorFromHEX(HexStringToUInt("FFFFFF"));
    userSettings.secondary_color = GColorFromHEX(HexStringToUInt("FFFFFF"));
    userSettings.background_color = GColorFromHEX(HexStringToUInt("000000"));
  #else
    userSettings.primary_color = GColorWhite;
    userSettings.secondary_color = GColorWhite;
    userSettings.background_color = GColorBlack;
  #endif
}

// Global Settings
static AppTimer *s_broadcast_timer = NULL;
static AppTimer *s_retry_timer = NULL;
int update_number = 0;
// Counts how many consecutive fast-rate refreshes have confirmed Final status.
// We require 3 consecutive Final responses before switching to the slow rate,
// guarding against a transient Final from the API mid-game.
static int final_confirm_count = 0;
// Watchdog: counts minutes since the last successful data receipt from the phone.
// If this exceeds WATCHDOG_LIMIT during an active game period, the AppMessage
// channel is silently stuck and must be reset. Resets to 0 on any incoming message.
static int ticks_since_last_data = 0;
#define WATCHDOG_LIMIT 10
int showing_loading_screen = 1;
int showing_no_game = 0;
static int bt_connected = 1;
	
// Key values for AppMessage Dictionary
enum {
	TYPE = 0,
  NUM_GAMES = 1,
  STATUS = 2,
  HOME_TEAM = 3,
  AWAY_TEAM = 4,
  HOME_PITCHER = 5,
  AWAY_PITCHER = 6,
  GAME_TIME = 7,
  HOME_BROADCAST = 8,
  AWAY_BROADCAST = 9,
  FIRST = 10,
  SECOND = 11,
  THIRD = 12,
  HOME_SCORE = 13,
  AWAY_SCORE = 14,
  INNING = 15,
  INNING_HALF = 16,
  BALLS = 17,
  STRIKES = 18,
  OUTS = 19,
  PREF_FAVORITE_TEAM = 20,
  PREF_SHAKE_ENABELED = 21,
  PREF_SHAKE_TIME = 22,
  PREF_REFRESH_TIME_OFF = 23,
  PREF_REFRESH_TIME_ON = 24,
  PREF_PRIMARY_COLOR = 25,
  PREF_SECONDARY_COLOR = 26,
  PREF_BACKGROUND_COLOR = 27,
  PREF_BASES_DISPLAY = 28
};

// Key values for graphic instructions Dictionary
enum {
	BASES_CHANGE = 0,
  HOME_SCORE_CHANGE = 1,
  AWAY_SCORE_CHANGE = 2,
  INNING_CHANGE = 3,
  INNING_HALF_CHANGE = 4,
  BSO_CHANGE = 5
};

// On Wrist Shake Functions
static void show_pitchers(){
  if(currentGameData.num_games > 0){
    sliding_text_layer_set_next_text(s_away_data_layer, currentGameData.away_pitcher);
    sliding_text_layer_animate_up(s_away_data_layer);
    sliding_text_layer_set_next_text(s_home_data_layer, currentGameData.home_pitcher);
    sliding_text_layer_animate_up(s_home_data_layer);
  }
}
static void broadcast_timer_callback(void *data) {
  s_broadcast_timer = NULL;
  show_pitchers();
}
static void show_broadcasts(){
  if(currentGameData.num_games > 0){
    if (s_broadcast_timer) {
      app_timer_cancel(s_broadcast_timer);
    }
    sliding_text_layer_set_next_text(s_away_data_layer, currentGameData.away_broadcast);
    sliding_text_layer_animate_down(s_away_data_layer);
    sliding_text_layer_set_next_text(s_home_data_layer, currentGameData.home_broadcast);
    sliding_text_layer_animate_down(s_home_data_layer);
    s_broadcast_timer = app_timer_register(userSettings.shake_time * 1000, broadcast_timer_callback, NULL);
  }
}

static void rotate_clear(SlidingTextLayer* layer_to_clear, int direction){
  sliding_text_layer_set_next_text(layer_to_clear, "");
  if(direction == 0){
    sliding_text_layer_animate_up(layer_to_clear);
  } else {
    sliding_text_layer_animate_down(layer_to_clear);
  }
}
static void hide_loading_screen(){
  sliding_text_layer_set_next_text(s_loading_layer, "");
  sliding_text_layer_animate_down(s_loading_layer);
  showing_loading_screen = 0;
}
static void change_teams(){
  if ((strcmp(currentGameData.home_team, previousGameData.home_team) != 0) || (strcmp(currentGameData.away_team, previousGameData.away_team) != 0)){
    sliding_text_layer_set_next_text(s_away_team_layer, currentGameData.away_team);
    sliding_text_layer_animate_down(s_away_team_layer);
    sliding_text_layer_set_next_text(s_home_team_layer, currentGameData.home_team);
    sliding_text_layer_animate_down(s_home_team_layer);
  }
}

static void schedule_retry();  // forward declaration

static bool request_update(){
  #if DEMO_MODE
    return false;
  #endif
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK || iter == NULL) {
    schedule_retry();
    return false;
  }
  dict_write_uint32(iter, TYPE, 1);
  if (app_message_outbox_send() != APP_MSG_OK) {
    schedule_retry();
    return false;
  }
  return true;
}

static void request_color_update(){
  #if DEMO_MODE
    return;
  #endif
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK || iter == NULL) {
    return;  // Settings will arrive proactively from JS 'ready'; no retry needed.
  }
  dict_write_uint32(iter, TYPE, 2);
  app_message_outbox_send();  // Failure is acceptable; JS 'ready' covers this path.
}


// Graphics Trigger Functions
static void update_bso(){
  layer_mark_dirty(s_bso_layer);
}
static void update_inning_state(){
  layer_mark_dirty(s_inning_state_layer);
}
static void update_bases(){
  layer_mark_dirty(s_bases_layer);
}

// Graphic Functions
static void startGame(){
  // Set Teams
  change_teams();
  // Animate Scores
  #ifndef PBL_PLATFORM_GABBRO
  sliding_text_layer_set_text_alignment(s_away_data_layer, GTextAlignmentRight);
  sliding_text_layer_set_text_alignment(s_home_data_layer, GTextAlignmentRight);
  #endif
  snprintf(away_score, sizeof(away_score), SCORE_FMT, currentGameData.away_score);
  sliding_text_layer_set_next_text(s_away_data_layer, away_score);
  sliding_text_layer_animate_down(s_away_data_layer);
  snprintf(home_score, sizeof(home_score), SCORE_FMT, currentGameData.home_score);
  sliding_text_layer_set_next_text(s_home_data_layer, home_score);
  sliding_text_layer_animate_down(s_home_data_layer);
  // Animate Inning
  sliding_text_layer_set_font(s_inning_layer, s_font_phillies_22);
  #if defined(PBL_PLATFORM_EMERY)
    snprintf(inning, sizeof(inning), " %d", currentGameData.inning);
  #else
    #ifdef PBL_RECT
    if(userSettings.bases_display == 1) {
      #endif
      snprintf(inning, sizeof(inning), "      %d", currentGameData.inning);
      #ifdef PBL_RECT
    } else if(userSettings.bases_display == 2) {
      snprintf(inning, sizeof(inning), "%d", currentGameData.inning);
    }
    #endif
  #endif
  sliding_text_layer_set_next_text(s_inning_layer, inning);
  sliding_text_layer_animate_down(s_inning_layer);
  // Animate Game Time
  rotate_clear(s_game_time_layer, 1);
  // Redraw Inning State
  update_inning_state();
  // Redraw Balls, Strikes, Outs
  update_bso();
  // Redraw bases
  update_bases();
}
static void endGame(){
  // Set Teams
  change_teams();
  // Set Scores
  #ifndef PBL_PLATFORM_GABBRO
  sliding_text_layer_set_text_alignment(s_away_data_layer, GTextAlignmentRight);
  sliding_text_layer_set_text_alignment(s_home_data_layer, GTextAlignmentRight);
  #endif
  snprintf(away_score, sizeof(away_score), SCORE_FMT, currentGameData.away_score);
  sliding_text_layer_set_next_text(s_away_data_layer, away_score);
  sliding_text_layer_animate_down(s_away_data_layer);
  snprintf(home_score, sizeof(home_score), SCORE_FMT, currentGameData.home_score);
  sliding_text_layer_set_next_text(s_home_data_layer, home_score);
  sliding_text_layer_animate_down(s_home_data_layer);
  // Animate Inning to "Final"
  sliding_text_layer_set_font(s_inning_layer, s_font_capital_20);
  sliding_text_layer_set_next_text(s_inning_layer, "Final");
  sliding_text_layer_animate_down(s_inning_layer);
  // Redraw Inning State
  update_inning_state();
  // Redraw Balls, Strikes, Outs
  update_bso();
  // Redraw bases
  update_bases();
}
static void newGame(){
  // Change teams
  change_teams();
  // Animate inning to ""
  rotate_clear(s_inning_layer, 1);
  // Animate Data to Pitchers
  sliding_text_layer_set_text_alignment(s_away_data_layer, GTextAlignmentLeft);
  sliding_text_layer_set_text_alignment(s_home_data_layer, GTextAlignmentLeft);
  sliding_text_layer_set_next_text(s_away_data_layer, currentGameData.away_pitcher);
  sliding_text_layer_animate_down(s_away_data_layer);
  sliding_text_layer_set_next_text(s_home_data_layer, currentGameData.home_pitcher);
  sliding_text_layer_animate_down(s_home_data_layer);
  // Show Game Time
  sliding_text_layer_set_next_text(s_game_time_layer, currentGameData.game_time);
  sliding_text_layer_animate_down(s_game_time_layer);
}
static void updateGame(int *instructions){
  // Update game stats based on instructions
  if (instructions[0] == 1){
    update_bases();
  }
  if (instructions[1] == 1){
    snprintf(home_score, sizeof(home_score), SCORE_FMT, currentGameData.home_score);
    sliding_text_layer_set_next_text(s_home_data_layer, home_score);
    sliding_text_layer_animate_down(s_home_data_layer);
  }
  if (instructions[2] == 1){
    snprintf(away_score, sizeof(away_score), SCORE_FMT, currentGameData.away_score);
    sliding_text_layer_set_next_text(s_away_data_layer, away_score);
    sliding_text_layer_animate_down(s_away_data_layer);
  }
  if (instructions[3] == 1){
    #if defined(PBL_PLATFORM_EMERY)
      snprintf(inning, sizeof(inning), " %d", currentGameData.inning);
    #else
      #ifdef PBL_RECT
        if(userSettings.bases_display == 1) {
      #endif
      snprintf(inning, sizeof(inning), "      %d", currentGameData.inning);
      #ifdef PBL_RECT
        } else if(userSettings.bases_display == 2) {
          snprintf(inning, sizeof(inning), "%d", currentGameData.inning);
        }
      #endif
    #endif
    sliding_text_layer_set_next_text(s_inning_layer, inning);
    sliding_text_layer_animate_down(s_inning_layer);
  }
  if (instructions[4] == 1){
    update_bso();
  }
  if (instructions[5] == 1){
    update_inning_state();
  }
}
static void showNoGame(){
  if(showing_no_game == 0){
    // Hide All Text layers
    rotate_clear(s_away_data_layer, 1);
    rotate_clear(s_home_data_layer, 1);
    rotate_clear(s_inning_layer, 1);
    rotate_clear(s_away_team_layer, 1);
    rotate_clear(s_home_team_layer, 1);
    rotate_clear(s_game_time_layer, 1);
    // Clear game status and team data so canvas layers stop drawing and the
    // next game's route_graphic_updates() detects a team change even if the
    // same teams play again the following day.
    currentGameData.status = 0;
    currentGameData.home_team[0] = '\0';
    currentGameData.away_team[0] = '\0';
    update_bases();
    update_bso();
    update_inning_state();
    // Show No Game
    sliding_text_layer_set_next_text(s_loading_layer, "No Game Today");
    sliding_text_layer_animate_down(s_loading_layer);
    showing_loading_screen = 1;
    showing_no_game = 1;
  }
}
// On BW watches some logos have a white background in the time display area.
// Team indices with confirmed white backgrounds (>50% white pixels in time region):
//   7 = STL (Cardinals), 26 = KC (Royals), 29 = CWS (White Sox)
// For those teams use black text so it reads against the white logo; white otherwise.
static void apply_time_text_color() {
  #ifdef PBL_COLOR
    text_layer_set_text_color(s_time_layer, userSettings.secondary_color);
  #else
    static const int white_bg_teams[] = { 7, 26, 29 };
    GColor c = GColorWhite;
    for (int i = 0; i < (int)(sizeof(white_bg_teams) / sizeof(white_bg_teams[0])); i++) {
      if (userSettings.favorite_team == white_bg_teams[i]) {
        c = GColorBlack;
        break;
      }
    }
    text_layer_set_text_color(s_time_layer, c);
  #endif
}

static void change_colors(){
  color_update = 1;
  // Set Primary Color
  sliding_text_layer_set_text_color(s_home_team_layer, userSettings.primary_color);
  sliding_text_layer_set_text_color(s_away_team_layer, userSettings.primary_color);
  sliding_text_layer_set_text_color(s_home_data_layer, userSettings.primary_color);
  sliding_text_layer_set_text_color(s_away_data_layer, userSettings.primary_color);
  sliding_text_layer_set_text_color(s_inning_layer, userSettings.primary_color);
  sliding_text_layer_set_text_color(s_loading_layer, userSettings.primary_color);
  sliding_text_layer_set_text_color(s_game_time_layer, userSettings.primary_color);
  update_bso();
  update_inning_state();
  // Set Secondary Color
  apply_time_text_color();
  update_bases();
  // Set Background Color
  window_set_background_color(window, userSettings.background_color);
  // Set Favorite Team
  if(logo_uodate_i > 0){
    gbitmap_destroy(s_team_logo);
  }
  logo_uodate_i = 1;
  s_team_logo = gbitmap_create_with_resource(logo[userSettings.favorite_team]);
  bitmap_layer_set_bitmap(s_team_logo_layer, s_team_logo);
  layer_mark_dirty(bitmap_layer_get_layer(s_team_logo_layer));
  #if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
    bitmap_layer_set_background_color(s_team_logo_layer, userSettings.background_color);
  #endif
}

// Function to determine which graphics need to be updated
static void route_graphic_updates(){
  if ((currentGameData.status != previousGameData.status) || (strcmp(currentGameData.home_team, previousGameData.home_team) != 0) || (strcmp(currentGameData.away_team, previousGameData.away_team) != 0)){
    if (currentGameData.status == 2){
      // Game Started
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Routing to Start Game.");
      startGame();
    } else if (previousGameData.status == 2){
      // Game Ended
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Routing to End Game status = 2.");
      endGame();
    } else if (previousGameData.status == 3){
      // Show a New Game
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Routing to New Game.");
      newGame();
    } else if (currentGameData.status == 3){
      // End the Game
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Routing to End Game, status = 3");
      endGame();
    } else {
      // New Game
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Routing to New Game, default.");
      newGame();
    }
  } else if (currentGameData.status == 0 && previousGameData.status != 0){
    // New Game Fallback
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Routing to New Game, fallback.");
    newGame();
  } else if (currentGameData.status == 0 || currentGameData.status == 1){
    // Preview/Warmup/Pre-Game steady state — refresh display if pitchers or
    // game time changed (e.g. pitchers announced after the first overnight refresh)
    if ((strcmp(currentGameData.home_pitcher, previousGameData.home_pitcher) != 0) ||
        (strcmp(currentGameData.away_pitcher, previousGameData.away_pitcher) != 0) ||
        (strcmp(currentGameData.game_time, previousGameData.game_time) != 0)){
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Routing to New Game (preview data changed).");
      newGame();
    }
  } else if (currentGameData.status == 2){
    // Declare the dictionary's iterator
    int instructions[6] = { 0, 0, 0, 0, 0, 0 };
    if ((currentGameData.first != previousGameData.first) || (currentGameData.second != previousGameData.second) || (currentGameData.third != previousGameData.third)){
      // New Bases
      instructions[0] = 1;
    }
    if (currentGameData.home_score != previousGameData.home_score){
      // New Home Score
      instructions[1] = 1;
    }
    if (currentGameData.away_score != previousGameData.away_score){
      // New Away Score
      instructions[2] = 1;
    }
    if (currentGameData.inning != previousGameData.inning){
      // New Inning
      instructions[3] = 1;
    }
    if ((currentGameData.balls != previousGameData.balls) || (currentGameData.strikes != previousGameData.strikes) || (currentGameData.outs != previousGameData.outs)){
      // New Outs
      instructions[4] = 1;
    }
    if (strcmp(currentGameData.inning_half, previousGameData.inning_half) != 0) {
      // New Inning Half
      instructions[5] = 1;
    }
    // Update graphics based on instructions
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Routing to Update Game. instructions: %d, %d, %d, %d, %d, %d", instructions[0], instructions[1], instructions[2], instructions[3], instructions[4], instructions[5]);
    updateGame(instructions);
  }
}

// Called when a message is received from PebbleKitJS
static void in_received_handler(DictionaryIterator *received, void *context) {
  // Any message from the phone proves the channel is alive — reset watchdog.
  ticks_since_last_data = 0;
  // Does this message contain a change in type
  Tuple *type_tuple = dict_find(received, TYPE);
  int type = 1;
  if(type_tuple) {
    // This value was stored as JS Number, which is stored here as int32_t
    type = (int)type_tuple->value->int32;
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Message Received, Type: %d", type);
  if (type == 0){
    Tuple *t = dict_read_first(received);
    while(t != NULL) {
      switch(t->key) {
        case PREF_FAVORITE_TEAM:
          userSettings.favorite_team = (int)t->value->int32;
          APP_LOG(APP_LOG_LEVEL_DEBUG, "Favorite Team: %d", userSettings.favorite_team);
          break;
        case PREF_SHAKE_ENABELED:
          userSettings.shake_enabeled = (int)t->value->int32;
          break;
        case PREF_SHAKE_TIME:
          userSettings.shake_time = (int)t->value->int32;
          break;
        case PREF_REFRESH_TIME_OFF:
          userSettings.refresh_time_off = (int)t->value->int32;
          break;
        case PREF_REFRESH_TIME_ON:
          userSettings.refresh_time_on = (int)t->value->int32;
          break;
        case PREF_BASES_DISPLAY:
          userSettings.bases_display = (int)t->value->int32;
          if(currentGameData.status == 2){
            int instructions[6] = { 0, 0, 0, 1, 0, 0 };
            updateGame(instructions);
          }
          break;
        case PREF_PRIMARY_COLOR:
          #ifdef PBL_COLOR
            userSettings.primary_color = GColorFromHEX(HexStringToUInt(t->value->cstring));
          #else
            userSettings.primary_color = GColorFromStringBW(t->value->cstring);
          #endif
          break;
        case PREF_SECONDARY_COLOR:
          #ifdef PBL_COLOR
            userSettings.secondary_color = GColorFromHEX(HexStringToUInt(t->value->cstring));
          #else
            userSettings.secondary_color = GColorFromStringBW(t->value->cstring);
          #endif
          break;
        case PREF_BACKGROUND_COLOR:
          #ifdef PBL_COLOR
            userSettings.background_color = GColorFromHEX(HexStringToUInt(t->value->cstring));
          #else
            userSettings.background_color = GColorFromStringBW(t->value->cstring);
          #endif
          break;
        default:
          break;
      }
      t = dict_read_next(received);
    }
    // Update the colors
    change_colors();
    // Phone is responsive — pull fresh game data immediately rather than
    // waiting up to refresh_time_off minutes for the tick counter to expire.
    // This covers BT reconnects and app restarts where the JS 'ready' push
    // may not fire again.
    request_update();

  } else {
    
    Tuple *type_tuple = dict_find(received, NUM_GAMES);
    int num_games = 0;
    if(type_tuple) {
      // This value was stored as JS Number, which is stored here as int32_t
      num_games = (int)type_tuple->value->int32;
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Number of Games: %d", num_games);
    if (num_games == 0){
      // Show No Game Today
      showNoGame();
    } else {
      // Reset the refresh counter so the next update fires at the correct
      // interval from *this* received update, not from the last sent request.
      // Without this, a brief status change (e.g. transient showNoGame) would
      // leave update_number at an arbitrary value, causing unpredictable timing.
      update_number = 0;

      // Process the data set
      // Copy the old data set
      previousGameData = currentGameData;
      
      Tuple *t = dict_read_first(received);
      while(t != NULL) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Key: %d", (int)t->key);
        switch(t->key) {
          case HOME_TEAM:
            strcpy(currentGameData.home_team,t->value->cstring);
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Home Team: %s", currentGameData.home_team);
            break;
          case AWAY_TEAM:
            strcpy(currentGameData.away_team,t->value->cstring);
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Away Team: %s", currentGameData.away_team);
            break;
          case NUM_GAMES:
            currentGameData.num_games = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Number of Games: %d", currentGameData.num_games);
            break;
          case STATUS:
            currentGameData.status = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Game Status: %d", currentGameData.status);
            break;
          case HOME_PITCHER:
            strcpy(currentGameData.home_pitcher,t->value->cstring);
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Home Pitcher: %s", currentGameData.home_pitcher);
            break;
          case AWAY_PITCHER:
            strcpy(currentGameData.away_pitcher,t->value->cstring);
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Away Pitcher: %s", currentGameData.away_pitcher);
            break;
          case GAME_TIME:
            strcpy(currentGameData.game_time,t->value->cstring);
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Game Time: %s", currentGameData.game_time);
            break;
          case HOME_BROADCAST:
            strcpy(currentGameData.home_broadcast,t->value->cstring);
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Home Broadcast: %s", currentGameData.home_broadcast);
            break;
          case AWAY_BROADCAST:
            strcpy(currentGameData.away_broadcast,t->value->cstring);
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Away Broadcast: %s", currentGameData.away_broadcast);
            break;
          case FIRST:
            currentGameData.first = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, First Base: %d", currentGameData.first);
            break;
          case SECOND:
            currentGameData.second = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Second Base: %d", currentGameData.second);
            break;
          case THIRD:
            currentGameData.third = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Third Base: %d", currentGameData.third);
            break;
          case HOME_SCORE:
            currentGameData.home_score = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Home Score: %d", currentGameData.home_score);
            break;
          case AWAY_SCORE:
            currentGameData.away_score = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Away Score: %d", currentGameData.away_score);
            break;
          case INNING:
            currentGameData.inning = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Inning: %d", currentGameData.inning);
            break;
          case INNING_HALF:
            strcpy(currentGameData.inning_half,t->value->cstring);
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Inning Half: %s", currentGameData.inning_half);
            break;
          case BALLS:
            currentGameData.balls = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Balls: %d", currentGameData.balls);
            break;
          case STRIKES:
            currentGameData.strikes = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Strikes: %d", currentGameData.strikes);
            break;
          case OUTS:
            currentGameData.outs = (int)t->value->int32;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Outs: %d", currentGameData.outs);
            break;
          default:
            APP_LOG(APP_LOG_LEVEL_DEBUG, "From Message, Unhandled Key: %d", (int)t->key);
            break;
        }
        t = dict_read_next(received);
      }
      
      // Update Final confirmation counter.
      // Increment on each consecutive Final; reset on any other status so a
      // recovered In Progress after a transient Final restarts the count.
      if (currentGameData.status == 3) {
        if (final_confirm_count < 3) { final_confirm_count++; }
      } else {
        final_confirm_count = 0;
      }

      // Data is fully loaded — hide loading/no-game overlays before updating display
      if(showing_loading_screen == 1){
        hide_loading_screen();
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Hiding Loading Screen.");
      }
      if(showing_no_game == 1){
        showing_no_game = 0;
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Hiding No Game Message.");
      }

      // After processing data, route graphic updates
      route_graphic_updates();
    }
    
    
  }
}

// Retry callback — fires after a short delay to avoid rapid-fire retries that
// could flood the AppMessage event queue when the outbox is temporarily busy.
static void retry_update_callback(void *data) {
  s_retry_timer = NULL;
  request_update();
}

static void schedule_retry() {
  if (s_retry_timer == NULL) {
    s_retry_timer = app_timer_register(2000, retry_update_callback, NULL);
  }
}

// Called when an incoming message from PebbleKitJS is dropped
static void in_dropped_handler(AppMessageResult reason, void *context) {
  schedule_retry();
}

// Called when PebbleKitJS does not acknowledge receipt of a message
static void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
  schedule_retry();
}

// Graphics Drawing Functions
static void bso_update_proc(Layer *layer, GContext *ctx) {
  if (currentGameData.status == 2 && bt_connected) {
    GRect bounds = layer_get_bounds(layer);
    #ifdef PBL_RECT 
      if(userSettings.bases_display == 1) {
    #endif
    graphics_context_set_fill_color(ctx, userSettings.primary_color);
		#ifdef PBL_COLOR
    graphics_context_set_stroke_width(ctx, 4);
		#endif
    #ifdef PBL_ROUND
      if(currentGameData.outs == 2){
        graphics_fill_circle(ctx, GPoint(((bounds.size.w) / 2) + 11, (bounds.size.h - 15)), 6);
        graphics_fill_circle(ctx, GPoint(((bounds.size.w) / 2) - 11, (bounds.size.h - 15)), 6);
      } else if(currentGameData.outs > 0) {
        graphics_fill_circle(ctx, GPoint((bounds.size.w) / 2, (bounds.size.h - 14)), 6);
        if ((strcmp(currentGameData.inning_half, "Middle") == 0) || (strcmp(currentGameData.inning_half, "End") == 0)){
          graphics_fill_circle(ctx, GPoint(((bounds.size.w) / 2) - 21, (bounds.size.h - 15)), 6);
          graphics_fill_circle(ctx, GPoint(((bounds.size.w) / 2) + 21, (bounds.size.h - 15)), 6);
        }
      }
    #else
      if(currentGameData.outs == 2){
        graphics_fill_circle(ctx, GPoint(((bounds.size.w) / 20) * 19, ((bounds.size.h / 4) * 3 + 21)), 6);
        graphics_fill_circle(ctx, GPoint(((bounds.size.w) / 20) * 19, ((bounds.size.h / 4) * 3 + 3)), 6);
      } else if(currentGameData.outs > 0) {
        graphics_fill_circle(ctx, GPoint(((bounds.size.w) / 20) * 19, (((bounds.size.h / 4) * 3 + 12))), 6);
        if ((strcmp(currentGameData.inning_half, "Middle") == 0) || (strcmp(currentGameData.inning_half, "End") == 0)){
          graphics_fill_circle(ctx, GPoint(((bounds.size.w) / 20) * 19, ((bounds.size.h / 4) * 3 - 7)), 6);
          graphics_fill_circle(ctx, GPoint(((bounds.size.w) / 20) * 19, ((bounds.size.h / 4) * 3 + 31)), 6);
        }
      }
    #endif
    #ifdef PBL_RECT 
      } else {
        graphics_context_set_fill_color(ctx, userSettings.primary_color);
        if(currentGameData.outs > 0){
          graphics_fill_circle(ctx, GPoint(116, 149), 2);
        }
        if(currentGameData.outs > 1){
          graphics_fill_circle(ctx, GPoint(124, 149), 2);
        }
        if((strcmp(currentGameData.inning_half, "Middle") == 0) || (strcmp(currentGameData.inning_half, "End") == 0) || (currentGameData.outs > 2)){
          graphics_fill_circle(ctx, GPoint(120, 154), 2);
        }
      }
    #endif
  }
}
static void inning_state_update_proc(Layer *layer, GContext *ctx) {
  if (currentGameData.status == 2 && bt_connected) {
    GRect bounds = layer_get_bounds(layer);
    #ifdef PBL_RECT 
      if(userSettings.bases_display == 1) {
    #endif
    if ((strcmp(currentGameData.inning_half, "Top") == 0) || (strcmp(currentGameData.inning_half, "Middle") == 0)) {
      #ifdef PBL_ROUND
        #ifdef PBL_PLATFORM_GABBRO
        GPoint inning_up_arrow[4] = {
          { .x = ((bounds.size.w / 3) * 2) - 7,  .y = ((bounds.size.h / 10) * 6) + 30 },
          { .x = ((bounds.size.w / 3) * 2) - 17, .y = ((bounds.size.h / 10) * 6) + 48 },
          { .x = ((bounds.size.w / 3) * 2) + 3,  .y = ((bounds.size.h / 10) * 6) + 48 },
          { .x = ((bounds.size.w / 3) * 2) - 7,  .y = ((bounds.size.h / 10) * 6) + 30 }
        };
        #else
        GPoint inning_up_arrow[4] = {
          { .x = ((bounds.size.w / 3) * 2) - 5, .y = ((bounds.size.h / 10) * 6) + 21 },
          { .x = ((bounds.size.w / 3) * 2) - 12, .y = ((bounds.size.h / 10) * 6) + 33 },
          { .x = ((bounds.size.w / 3) * 2) + 2, .y = ((bounds.size.h / 10) * 6) + 33 },
          { .x = ((bounds.size.w / 3) * 2) - 5, .y = ((bounds.size.h / 10) * 6) + 21 }
        };
        #endif
      #elif defined(PBL_PLATFORM_EMERY)
        GPoint inning_up_arrow[4] = {
          { .x = ((bounds.size.w / 3) * 2) + 12, .y = ((bounds.size.h / 10) * 7) + 24 },
          { .x = ((bounds.size.w / 3) * 2) + 5, .y = ((bounds.size.h / 10) * 7) + 36 },
          { .x = ((bounds.size.w / 3) * 2) + 19, .y = ((bounds.size.h / 10) * 7) + 36 },
          { .x = ((bounds.size.w / 3) * 2) + 12, .y = ((bounds.size.h / 10) * 7) + 24 }
        };
      #else
        GPoint inning_up_arrow[4] = {
          { .x = ((bounds.size.w / 3) * 2) - 5, .y = ((bounds.size.h / 10) * 7) + 22 },
          { .x = ((bounds.size.w / 3) * 2) - 12, .y = ((bounds.size.h / 10) * 7) + 34 },
          { .x = ((bounds.size.w / 3) * 2) + 2, .y = ((bounds.size.h / 10) * 7) + 34 },
          { .x = ((bounds.size.w / 3) * 2) - 5, .y = ((bounds.size.h / 10) * 7) + 22 }
        };
      #endif
      GPathInfo inning_up_arrowinfo = { .num_points = 4, .points = inning_up_arrow };
      GPath *inning_up_arrow_path = gpath_create(&inning_up_arrowinfo);
      graphics_context_set_fill_color(ctx, userSettings.primary_color);
      gpath_draw_filled(ctx, inning_up_arrow_path);
      gpath_destroy(inning_up_arrow_path);
    } else if ((strcmp(currentGameData.inning_half, "Bottom") == 0) || (strcmp(currentGameData.inning_half, "End") == 0)) {
      #ifdef PBL_ROUND
        #ifdef PBL_PLATFORM_GABBRO
        GPoint inning_down_arrow[4] = {
          { .x = ((bounds.size.w / 3) * 2) - 7,  .y = ((bounds.size.h / 10) * 6) + 48 },
          { .x = ((bounds.size.w / 3) * 2) - 17, .y = ((bounds.size.h / 10) * 6) + 30 },
          { .x = ((bounds.size.w / 3) * 2) + 3,  .y = ((bounds.size.h / 10) * 6) + 30 },
          { .x = ((bounds.size.w / 3) * 2) - 7,  .y = ((bounds.size.h / 10) * 6) + 48 }
        };
        #else
        GPoint inning_down_arrow[4] = {
          { .x = ((bounds.size.w / 3) * 2) - 5, .y = ((bounds.size.h / 10) * 6) + 33 },
          { .x = ((bounds.size.w / 3) * 2) - 12, .y = ((bounds.size.h / 10) * 6) + 21 },
          { .x = ((bounds.size.w / 3) * 2) + 2, .y = ((bounds.size.h / 10) * 6) + 21 },
          { .x = ((bounds.size.w / 3) * 2) - 5, .y = ((bounds.size.h / 10) * 6) + 33 }
        };
        #endif
      #elif defined(PBL_PLATFORM_EMERY)
        GPoint inning_down_arrow[4] = {
          { .x = ((bounds.size.w / 3) * 2) + 12, .y = ((bounds.size.h / 10) * 7) + 36 },
          { .x = ((bounds.size.w / 3) * 2) + 5, .y = ((bounds.size.h / 10) * 7) + 24 },
          { .x = ((bounds.size.w / 3) * 2) + 19, .y = ((bounds.size.h / 10) * 7) + 24 },
          { .x = ((bounds.size.w / 3) * 2) + 12, .y = ((bounds.size.h / 10) * 7) + 36 }
        };
      #else
        GPoint inning_down_arrow[4] = {
          { .x = ((bounds.size.w / 3) * 2) - 5, .y = ((bounds.size.h / 10) * 7) + 34 },
          { .x = ((bounds.size.w / 3) * 2) - 12, .y = ((bounds.size.h / 10) * 7) + 22 },
          { .x = ((bounds.size.w / 3) * 2) + 2, .y = ((bounds.size.h / 10) * 7) + 22 },
          { .x = ((bounds.size.w / 3) * 2) - 5, .y = ((bounds.size.h / 10) * 7) + 34 }
        };
      #endif
      GPathInfo inning_down_arrowinfo = { .num_points = 4, .points = inning_down_arrow };
      GPath *inning_down_arrow_path = gpath_create(&inning_down_arrowinfo);\
      graphics_context_set_fill_color(ctx, userSettings.primary_color);
      gpath_draw_filled(ctx, inning_down_arrow_path);
      gpath_destroy(inning_down_arrow_path);
    }
    #ifdef PBL_RECT 
      } else {
        graphics_context_set_fill_color(ctx, userSettings.primary_color);
        if ((strcmp(currentGameData.inning_half, "Top") == 0) || (strcmp(currentGameData.inning_half, "Middle") == 0)) {
          graphics_fill_circle(ctx, GPoint(88, 125), 3);
         } else if ((strcmp(currentGameData.inning_half, "Bottom") == 0) || (strcmp(currentGameData.inning_half, "End") == 0)) {
          graphics_fill_circle(ctx, GPoint(88, 155), 3);
        }
      }
    #endif
  }
}

#ifdef PBL_RECT
static void bases_legacy_helper(Layer *layer, GContext *ctx, GPath *path, int filled){
  // Translate by (5, 5):
  gpath_move_to(path, GPoint(5, 0));
  graphics_context_set_fill_color(ctx, userSettings.primary_color);
  graphics_context_set_stroke_color(ctx, userSettings.primary_color);
  if (filled == 1) {
    gpath_draw_filled(ctx, path);
  } else {
    gpath_draw_outline(ctx, path);
  }
  gpath_destroy(path);
}
#endif

static void bases_update_proc(Layer *layer, GContext *ctx) {
  if (currentGameData.status == 2 && bt_connected) {
    GRect bounds = layer_get_bounds(layer);
    #ifdef PBL_RECT 
      if(userSettings.bases_display == 1) {
    #endif
    // Custom drawing happens here!
    graphics_context_set_fill_color(ctx, userSettings.secondary_color);
    graphics_context_set_stroke_color(ctx, userSettings.secondary_color);
    #ifdef PBL_COLOR
    graphics_context_set_stroke_width(ctx, 3);
		#endif
    GPoint first_points[4] = {
      { .x = (bounds.size.w - 20), .y = bounds.size.h / 2 },
      { .x = bounds.size.w, .y = (bounds.size.h / 2) + 20 },
      { .x = bounds.size.w, .y = (bounds.size.h / 2) - 20 },
      { .x = (bounds.size.w - 20), .y = bounds.size.h / 2 }
    };
    GPoint second_points[4] = {
      { .x = (bounds.size.w / 2), .y = 20 },
      { .x = ((bounds.size.w / 2) - 20), .y = 0 },
      { .x = ((bounds.size.w / 2) + 20), .y = 0 },
      { .x = (bounds.size.w / 2), .y = 20 }
    };
    GPoint third_points[4] = {
      { .x = 20, .y = bounds.size.h / 2 },
      { .x = 0, .y = (bounds.size.h / 2) + 20 },
      { .x = 0, .y = (bounds.size.h / 2) - 20 },
      { .x = 20, .y = bounds.size.h / 2 }
    };
    GPathInfo first_pathinfo = { .num_points = 4, .points = first_points };
    GPath *first_path = gpath_create(&first_pathinfo);
    GPathInfo second_pathinfo = { .num_points = 4, .points = second_points };
    GPath *second_path = gpath_create(&second_pathinfo);
    GPathInfo third_pathinfo = { .num_points = 4, .points = third_points };
    GPath *third_path = gpath_create(&third_pathinfo);
    
    graphics_context_set_fill_color(ctx, userSettings.secondary_color);
    if (currentGameData.first == 1) {
      gpath_draw_filled(ctx, first_path);
    } else if (currentGameData.status == 2) {
      gpath_draw_outline(ctx, first_path);
    }
    gpath_destroy(first_path);
    if (currentGameData.second == 1) {
      gpath_draw_filled(ctx, second_path);
    } else if (currentGameData.status == 2) {
      gpath_draw_outline(ctx, second_path);
    }
    gpath_destroy(second_path);
    if (currentGameData.third == 1) {
      gpath_draw_filled(ctx, third_path);
    } else if (currentGameData.status == 2) {
      gpath_draw_outline(ctx, third_path);
    }
    gpath_destroy(third_path);
    #ifdef PBL_RECT 
      } else {
        static GPoint first_points[5] = {
          { .x = 127, .y = 132 },
          { .x = 137, .y = 142 },
          { .x = 127, .y = 152 },
          { .x = 117, .y = 142 },
          { .x = 127, .y = 132 }
        };
        GPathInfo first_pathinfo = { .num_points = 5, .points = first_points };
        GPath *first_path = gpath_create(&first_pathinfo);
        bases_legacy_helper(layer, ctx, first_path, currentGameData.first);
        static GPoint second_points[5] = {
          { .x = 115, .y = 120 },
          { .x = 125, .y = 130 },
          { .x = 115, .y = 140 },
          { .x = 105, .y = 130 },
          { .x = 115, .y = 120 }
        };
        GPathInfo second_pathinfo = { .num_points = 5, .points = second_points };
        GPath *second_path = gpath_create(&second_pathinfo);
        bases_legacy_helper(layer, ctx, second_path, currentGameData.second);
        static GPoint third_points[5] = {
          { .x = 103, .y = 132 },
          { .x = 113, .y = 142 },
          { .x = 103, .y = 152 },
          { .x = 93, .y = 142 },
          { .x = 103, .y = 132 }
        };
        GPathInfo third_pathinfo = { .num_points = 5, .points = third_points };
        GPath *third_path = gpath_create(&third_pathinfo);
        bases_legacy_helper(layer, ctx, third_path, currentGameData.third);
      }
    #endif
  }
}

// Window Handlers
static void window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  window_set_background_color(window, userSettings.background_color);
  
  // Load team logo
  // Each platform gets a layer rect sized to its logo image dimensions.
  // Emery: 200x128, Gabbro: 260x163, Round (Chalk): 180x113, Rect: 180x113 (with -18 offset trick)
  #ifdef PBL_PLATFORM_GABBRO
    s_team_logo_layer = bitmap_layer_create(GRect(0, 0, 260, 163));
  #elif defined(PBL_PLATFORM_EMERY)
    s_team_logo_layer = bitmap_layer_create(GRect(0, -6, 200, 128));
  #elif defined(PBL_ROUND)
    s_team_logo_layer = bitmap_layer_create(GRect(0, 0, bounds.size.w, 113));
  #else
    s_team_logo_layer = bitmap_layer_create(GRect(-18, -6, bounds.size.w + 18, 119));
  #endif
  bitmap_layer_set_compositing_mode(s_team_logo_layer, GCompOpSet);
  #if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
    bitmap_layer_set_background_color(s_team_logo_layer, userSettings.background_color);
  #endif
  
  // Load custom fonts
  s_font_mlb_40 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_MLB_40));
  s_font_mlb_18 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_MLB_18));
  s_font_capital_20 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CAPITAL_20));
  s_font_phillies_22 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_PHILLIES_22));
  
  // Create the TextLayer with specific bounds
  s_time_layer = text_layer_create(GRect(0, ((bounds.size.h / 10) * 4) - 6, bounds.size.w, 54));
  #ifdef PBL_ROUND
    #ifdef PBL_PLATFORM_GABBRO
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Using Gabbro Layout");
    s_away_team_layer = sliding_text_layer_create(GRect((bounds.size.w / 5), ((bounds.size.h / 10) * 6) + 7, 72, 36));
    s_home_team_layer = sliding_text_layer_create(GRect((bounds.size.w / 5), ((bounds.size.h / 10) * 7) + 13, 72, 36));
    s_game_time_layer = sliding_text_layer_create(GRect((bounds.size.w / 5) * 2, ((bounds.size.h / 10) * 8) + 16, 72, 36));
    s_away_data_layer = sliding_text_layer_create(GRect(((bounds.size.w / 5) * 2) + 7, ((bounds.size.h / 10) * 6) + 7, ((bounds.size.w / 5) * 3) - 7, 36));
    s_home_data_layer = sliding_text_layer_create(GRect(((bounds.size.w / 5) * 2) + 7, ((bounds.size.h / 10) * 7) + 12, ((bounds.size.w / 5) * 3) - 7, 36));
    s_inning_layer    = sliding_text_layer_create(GRect(((bounds.size.w / 5) * 3) + 6, ((bounds.size.h / 10) * 6) + 22, ((bounds.size.w / 5) * 3) - 7, 65));
    s_loading_layer   = sliding_text_layer_create(GRect(0, ((bounds.size.h / 10) * 6) + 22, bounds.size.w, 65));
    #else
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Using Round Layout");
    s_away_team_layer = sliding_text_layer_create(GRect((bounds.size.w / 5), ((bounds.size.h / 10) * 6) + 5, 50, 25));
    s_home_team_layer = sliding_text_layer_create(GRect((bounds.size.w / 5), ((bounds.size.h / 10) * 7) + 9, 50, 25));
    s_game_time_layer = sliding_text_layer_create(GRect((bounds.size.w / 5) * 2, ((bounds.size.h / 10) * 8) + 11, 50, 25));
    s_away_data_layer = sliding_text_layer_create(GRect(((bounds.size.w / 5) * 2) + 5, ((bounds.size.h / 10) * 6) + 5, ((bounds.size.w / 5) * 3) - 5, 25));
    s_home_data_layer = sliding_text_layer_create(GRect(((bounds.size.w / 5) * 2) + 5, ((bounds.size.h / 10) * 7) + 8, ((bounds.size.w / 5) * 3) - 5, 25));
    s_inning_layer    = sliding_text_layer_create(GRect(((bounds.size.w / 5) * 3), ((bounds.size.h / 10) * 6) + 15, ((bounds.size.w / 5) * 3) - 5, 45));
    s_loading_layer   = sliding_text_layer_create(GRect(0, ((bounds.size.h / 10) * 6) + 15, bounds.size.w, 45));
    #endif
  #elif PBL_PLATFORM_EMERY
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Using Time 2.0 Layout");
    s_away_team_layer = sliding_text_layer_create(GRect((bounds.size.w / 15), ((bounds.size.h / 10) * 7) + 2, 50, 25));
    s_home_team_layer = sliding_text_layer_create(GRect((bounds.size.w / 15), ((bounds.size.h / 10) * 8) + 14, 50, 25));
    s_game_time_layer = sliding_text_layer_create(GRect(((bounds.size.w / 5) * 4) - 4, ((bounds.size.h / 10) * 7) + 11, 50, 25));
    s_away_data_layer = sliding_text_layer_create(GRect(((bounds.size.w / 15) * 2) + 30, ((bounds.size.h / 10) * 7) + 2, ((bounds.size.w / 5) * 4) - 0, 30));
    s_home_data_layer = sliding_text_layer_create(GRect(((bounds.size.w / 15) * 2) + 30, ((bounds.size.h / 10) * 8) + 14, ((bounds.size.w / 5) * 4) - 0, 30));
    s_inning_layer = sliding_text_layer_create(GRect(((bounds.size.w / 4) * 3) + 6, ((bounds.size.h / 10) * 7) + 14, ((bounds.size.w / 5) * 3) - 5, 45));
    s_loading_layer = sliding_text_layer_create(GRect(0, ((bounds.size.h / 10) * 7) + 14, bounds.size.w, 45));
  #else
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Using Rectangular Layout");
    s_away_team_layer = sliding_text_layer_create(GRect((bounds.size.w / 15), ((bounds.size.h / 10) * 7) + 2, 50, 25));
    s_home_team_layer = sliding_text_layer_create(GRect((bounds.size.w / 15), ((bounds.size.h / 10) * 8) + 10, 50, 25));
    s_game_time_layer = sliding_text_layer_create(GRect(((bounds.size.w / 5) * 4) - 4, ((bounds.size.h / 10) * 7) + 11, 50, 25));
    s_away_data_layer = sliding_text_layer_create(GRect(((bounds.size.w / 15) * 2) + 30, ((bounds.size.h / 10) * 7) + 2, ((bounds.size.w / 5) * 4) - 0, 30));
    s_home_data_layer = sliding_text_layer_create(GRect(((bounds.size.w / 15) * 2) + 30, ((bounds.size.h / 10) * 8) + 10, ((bounds.size.w / 5) * 4) - 0, 30));
    s_inning_layer = sliding_text_layer_create(GRect(((bounds.size.w / 5) * 3), ((bounds.size.h / 10) * 7) + 14, ((bounds.size.w / 5) * 3) - 5, 45));
    s_loading_layer = sliding_text_layer_create(GRect(0, ((bounds.size.h / 10) * 7) + 14, bounds.size.w, 45));
  #endif
  
  // Create the canvas layers
  s_bso_layer = layer_create(bounds);
  s_inning_state_layer = layer_create(bounds);
  s_bases_layer = layer_create(bounds);
  layer_set_update_proc(s_bso_layer, bso_update_proc);
  layer_set_update_proc(s_inning_state_layer, inning_state_update_proc);
  layer_set_update_proc(s_bases_layer, bases_update_proc);
  
  // Improve the layout to be more like a watchface
  apply_time_text_color();
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text(s_time_layer, "00:00");
  text_layer_set_font(s_time_layer, s_font_mlb_40);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  
  sliding_text_layer_set_text_color(s_away_team_layer, userSettings.primary_color);
  sliding_text_layer_set_text(s_away_team_layer, "");
  sliding_text_layer_set_font(s_away_team_layer, s_font_capital_20);
  sliding_text_layer_set_text_alignment(s_away_team_layer, GTextAlignmentLeft);
  sliding_text_layer_set_duration(s_away_team_layer, 500);

  sliding_text_layer_set_text_color(s_home_team_layer, userSettings.primary_color);
  sliding_text_layer_set_text(s_home_team_layer, "");
  sliding_text_layer_set_font(s_home_team_layer, s_font_capital_20);
  sliding_text_layer_set_text_alignment(s_home_team_layer, GTextAlignmentLeft);
  sliding_text_layer_set_duration(s_home_team_layer, 500);
  
  sliding_text_layer_set_text_color(s_game_time_layer, userSettings.primary_color);
  sliding_text_layer_set_text(s_game_time_layer, "");
  sliding_text_layer_set_font(s_game_time_layer, s_font_mlb_18);
  sliding_text_layer_set_text_alignment(s_game_time_layer, GTextAlignmentLeft);
  
  sliding_text_layer_set_text_color(s_away_data_layer, userSettings.primary_color);
  sliding_text_layer_set_text(s_away_data_layer, "");
  sliding_text_layer_set_font(s_away_data_layer, s_font_phillies_22);
  sliding_text_layer_set_text_alignment(s_away_data_layer, GTextAlignmentLeft);
  
  sliding_text_layer_set_text_color(s_home_data_layer, userSettings.primary_color);
  sliding_text_layer_set_text(s_home_data_layer, "");
  sliding_text_layer_set_font(s_home_data_layer, s_font_phillies_22);
  sliding_text_layer_set_text_alignment(s_home_data_layer, GTextAlignmentLeft);
  
  sliding_text_layer_set_text_color(s_inning_layer, userSettings.primary_color);
  sliding_text_layer_set_text(s_inning_layer, "");
  sliding_text_layer_set_font(s_inning_layer, s_font_phillies_22);
  sliding_text_layer_set_text_alignment(s_inning_layer, GTextAlignmentLeft);
  
  sliding_text_layer_set_text_color(s_loading_layer, userSettings.primary_color);
  #if !DEMO_MODE
  sliding_text_layer_set_next_text(s_loading_layer, "LOADING");
  sliding_text_layer_animate_down(s_loading_layer);
  #endif
  sliding_text_layer_set_font(s_loading_layer, s_font_capital_20);
  sliding_text_layer_set_text_alignment(s_loading_layer, GTextAlignmentCenter);
  
  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, bitmap_layer_get_layer(s_team_logo_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
  sliding_text_layer_add_to_window(s_home_team_layer, window);
  sliding_text_layer_add_to_window(s_away_team_layer, window);
  sliding_text_layer_add_to_window(s_game_time_layer, window);
  sliding_text_layer_add_to_window(s_away_data_layer, window);
  sliding_text_layer_add_to_window(s_home_data_layer, window);
  sliding_text_layer_add_to_window(s_inning_layer, window);
  sliding_text_layer_add_to_window(s_loading_layer, window);
  layer_add_child(window_layer, s_bso_layer);
  layer_add_child(window_layer, s_inning_state_layer);
  layer_add_child(window_layer, s_bases_layer);
}

static void window_unload(Window *window) {
  fonts_unload_custom_font(s_font_mlb_40);
  fonts_unload_custom_font(s_font_mlb_18);
  fonts_unload_custom_font(s_font_capital_20);
  fonts_unload_custom_font(s_font_phillies_22);
  text_layer_destroy(s_time_layer);
  sliding_text_layer_destroy(s_home_team_layer);
  sliding_text_layer_destroy(s_away_team_layer);
  sliding_text_layer_destroy(s_game_time_layer);
  sliding_text_layer_destroy(s_away_data_layer);
  sliding_text_layer_destroy(s_home_data_layer);
  sliding_text_layer_destroy(s_inning_layer);
  sliding_text_layer_destroy(s_loading_layer);
  gbitmap_destroy(s_team_logo);
  bitmap_layer_destroy(s_team_logo_layer);
  layer_destroy(s_bso_layer);
  layer_destroy(s_inning_state_layer);
  layer_destroy(s_bases_layer);
}

static void update_time(struct tm *tick_time) {
  // Write the current hours and minutes into a buffer
  static char s_buffer[8];
  if(clock_is_24h_style() == true) {
    //Use 2h hour format
    strftime(s_buffer, sizeof("00:00"), "%H:%M", tick_time);
  } else {
    //Use 12 hour format
    strftime(s_buffer, sizeof("00:00"), "%I:%M", tick_time);
    int n;
    if( ( n = strspn(s_buffer, "0" ) ) != 0 && s_buffer[n] != '\0' ) {
      memmove(s_buffer, &s_buffer[n], strlen(s_buffer) - n + 1);
    }

  }
  // Display this time on the TextLayer if the time changed
  if (strcmp(s_buffer, s_buffer_prev) != 0){
    text_layer_set_text(s_time_layer, s_buffer);
    snprintf(s_buffer_prev, sizeof(s_buffer_prev), "%s", s_buffer);
  }

}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);

  update_number++;

  // refresh_time_on and refresh_time_off are stored in seconds; divide by 60
  // for the minute-based counter.  Minimum effective interval is 1 minute.
  //
  // After receiving Final (status 3), stay on the fast refresh rate until
  // final_confirm_count reaches 3 consecutive confirmations. This prevents a
  // transient API 'Final' mid-game from locking the watch into the slow rate.
  //
  // Also use fast rate when the current time is within the expected game window
  // (30 min before scheduled start through 5 hours after), even if the watch
  // still reports Pre-Game status. This prevents the 60-minute slow-poll gap
  // that occurs when the game starts between two scheduled polls.
  // game_time is stored as "H:MM" in 12-hour format with no AM/PM marker.
  // Only evaluated during PM hours (tm_hour >= 12) to avoid 6 AM falsely
  // matching a 6 PM game time.
  int use_game_time_fast_rate = 0;
  if ((currentGameData.status == 0 || currentGameData.status == 1) &&
      tick_time->tm_hour >= 12 &&
      currentGameData.game_time[0] >= '1' && currentGameData.game_time[0] <= '9') {
    int game_hour = atoi(currentGameData.game_time);
    char *colon = strchr(currentGameData.game_time, ':');
    int game_min = colon ? atoi(colon + 1) : 0;
    // Express everything as minutes past noon so arithmetic stays positive.
    // tm_hour is 24h; for PM hours (>=12) subtract 12 to get hours-past-noon.
    int cur_pm_min = (tick_time->tm_hour - 12) * 60 + tick_time->tm_min;
    // game_hour==12 means noon (0 hours past noon); 1-11 means 1-11 PM.
    int game_pm_hour = (game_hour == 12) ? 0 : game_hour;
    int game_pm_min  = game_pm_hour * 60 + game_min;
    int diff = cur_pm_min - game_pm_min;  // negative = before start, positive = after
    if (diff >= -30 && diff <= 300) {     // 30 min before through 5 hours after start
      use_game_time_fast_rate = 1;
    }
  }

  int use_fast_rate = (showing_loading_screen == 1) ||
                      (currentGameData.status == 2) ||
                      (currentGameData.status == 3 && final_confirm_count < 3) ||
                      use_game_time_fast_rate;

  // Watchdog: count ticks since last successful data receipt. If no response
  // has arrived in WATCHDOG_LIMIT minutes during an active game period, the
  // channel is silently stuck. Re-open AppMessage to reset it — same effect
  // as exiting and re-entering the watchface — then force a fresh request.
  ticks_since_last_data++;
  if (use_fast_rate && ticks_since_last_data >= WATCHDOG_LIMIT) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Watchdog: no data in %d min, resetting AppMessage channel.", ticks_since_last_data);
    ticks_since_last_data = 0;
    update_number = 0;
    #ifdef PBL_COLOR
      app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
    #else
      app_message_open(300, 50);
    #endif
    request_update();
  }

  if (use_fast_rate){
    int threshold = userSettings.refresh_time_on / 60;
    if (threshold < 1) { threshold = 1; }
    if (update_number >= threshold){
      if (request_update()) { update_number = 0; }
    }
  } else {
    int threshold = userSettings.refresh_time_off / 60;
    if (threshold < 1) { threshold = 1; }
    if (update_number >= threshold){
      if (request_update()) { update_number = 0; }
    }
  }
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  // A tap event occured
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Tap detected.");
  if(userSettings.shake_enabeled == 1){
    if(currentGameData.status < 2){
      if (s_broadcast_timer == NULL){
        show_broadcasts();
      }
      else {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Already showing broadcasts, ignoring tap.");
      }
    } else {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Game in progress, ignoring tap.");}
  } else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Shake to show broadcasts disabled, ignoring tap.");
  }
  
}

static void bluetooth_callback(bool connected) {
  bt_connected = connected ? 1 : 0;
  if (!connected) {
    sliding_text_layer_set_text_alignment(s_away_data_layer, GTextAlignmentLeft);
    sliding_text_layer_set_next_text(s_away_data_layer, "BT LOST");
    sliding_text_layer_animate_up(s_away_data_layer);
    rotate_clear(s_home_data_layer, 0);
    rotate_clear(s_inning_layer, 0);
    rotate_clear(s_game_time_layer, 0);
    update_bso();
    update_inning_state();
    update_bases();
  } else {
    if (!showing_loading_screen) {
      if (currentGameData.status == 2) {
        startGame();
      } else if (currentGameData.status == 3) {
        endGame();
      } else {
        newGame();
      }
    } else {
      rotate_clear(s_away_data_layer, 1);
    }
    update_number = 0;
    request_update();
  }
}

#if DEMO_MODE
// Fictional game used for emulator screenshots: CIN (away) @ CHC (home),
// Top of 3rd, Cubs 3 – Reds 1, 1 out, runner on 1st.
static void load_demo_data(void) {
  userSettings.favorite_team = 8;  // CHC (index 8 in teams[])
  #ifdef PBL_COLOR
    userSettings.background_color = GColorFromHEX(0x000055);  // Cubs navy
    userSettings.primary_color    = GColorWhite;
    userSettings.secondary_color  = GColorWhite;
  #else
    userSettings.background_color = GColorBlack;
    userSettings.primary_color    = GColorWhite;
    userSettings.secondary_color  = GColorWhite;
  #endif

  strcpy(currentGameData.home_team,      "CHC");
  strcpy(currentGameData.away_team,      "CIN");
  currentGameData.num_games  = 1;
  currentGameData.status     = 2;   // In Progress
  currentGameData.home_score = 3;
  currentGameData.away_score = 1;
  currentGameData.inning     = 3;
  strcpy(currentGameData.inning_half,    "Top");
  currentGameData.first      = 1;
  currentGameData.second     = 0;
  currentGameData.third      = 0;
  currentGameData.balls      = 0;
  currentGameData.strikes    = 0;
  currentGameData.outs       = 1;
  strcpy(currentGameData.home_pitcher,   "Rea");
  strcpy(currentGameData.away_pitcher,   "Singer");
  strcpy(currentGameData.game_time,      "7:05");
  strcpy(currentGameData.home_broadcast, "Marquee");
  strcpy(currentGameData.away_broadcast, "Bally Ohio");
  showing_loading_screen = 0;
}
#endif

void init(void) {
  // Populate the initial settings for loading
  initialize_settings();
  currentGameData.status = 99;
	window = window_create();
  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload
  });
	window_stack_push(window, true);
  time_t now = time(NULL);
  update_time(localtime(&now));

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(accel_tap_handler);

#if DEMO_MODE
  // Demo mode: populate fixture data and render directly — no JS connection.
  load_demo_data();
  change_colors();
  startGame();
#else
  bluetooth_connection_service_subscribe(bluetooth_callback);
  if (!bluetooth_connection_service_peek()) {
    bluetooth_callback(false);
  }

  // Register AppMessage handlers
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  app_message_register_outbox_failed(out_failed_handler);

  #ifdef PBL_COLOR
    app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  #else
    app_message_open(300, 50);
  #endif
  // Request settings once at startup. The phone JS sends them proactively on
  // its "ready" event, but this covers the race where that message is dropped.
  request_color_update();
  request_update();
#endif
}

void deinit(void) {
	app_message_deregister_callbacks();
  accel_tap_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
	window_destroy(window);
}

int main( void ) {
	init();
	app_event_loop();
	deinit();
	}