// Clay configuration
var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

// App Info
var version = "3.6";

// Global Settings Variables
var favoriteTeam = 19;
var shakeEnabled = 1;
var shakeTime = 5;
var refreshTime = [3600, 60];
var basesDisplay = 1;
var primaryColor = "FFFFFF";
var secondaryColor = "FFFFFF";
var backgroundColor = "AA0000";
/*
* Future Settings:
*/
// var vibrateOnChange
// var vibrateOnLeadChange
// var vibrateOnGameEnd
// var vibrateOnGameStart

// Global Variables
var offset = 0;
// Cache of the last successful non-zero game data, keyed by local date string.
// Used to keep showing the final score until midnight even if a subsequent
// refresh returns 0 games (transient API error or game removed from schedule).
var lastGameResult = null;  // { date: 'YYYY-MM-DD', data: { number_of_games, game_data } }
// Timer handle for proactive game-start refresh.
var s_start_timer = null;

function getCurrentDate() {
  var d = new Date();
  return d.getFullYear() + '-' +
         ('0' + (d.getMonth() + 1)).slice(-2) + '-' +
         ('0' + d.getDate()).slice(-2);
}

var teams =["", "LAA", "HOU", "OAK", "TOR", "ATL", "MIL", "STL", "CHC", "ARI", "LAD", "SF", "CLE", "SEA", "MIA", "NYM", "WSH", "BAL", "SD", "PHI", "PIT", "TEX", "TB", "BOS", "CIN", "COL", "KC", "DET", "MIN", "CWS", "NYY", "NL", "AL"];
// Official MLB Stats API team IDs, parallel to teams[]
var teamIds = [0, 108, 117, 133, 141, 144, 158, 138, 112, 109, 119, 137, 114, 136, 146, 121, 120, 110, 135, 143, 134, 140, 139, 111, 113, 115, 118, 116, 142, 145, 147, 0, 0];
var retry = 0;
var all_star_game = 0;
var reload_timeout = 0;

// MLB Stats API uses different abbreviations for some teams; normalize to match teams[]
var abbrevNorm = { 'SFG': 'SF', 'WSN': 'WSH', 'SDP': 'SD', 'TBR': 'TB', 'KCR': 'KC', 'ANA': 'LAA' };
function normalizeAbbrev(abbrev) {
  return abbrevNorm[abbrev] || abbrev;
}

// Reverse map: MLB Stats API team ID → abbreviation, built from teams[] and teamIds[]
var teamIdToAbbrev = {};
for (var i = 1; i < teamIds.length; i++) {
  if (teamIds[i]) { teamIdToAbbrev[teamIds[i]] = teams[i]; }
}

// Extract last name from a full name string
function lastName(fullName) {
  if (!fullName) { return ''; }
  var parts = fullName.split(' ');
  return parts[parts.length - 1];
}

// Function to parse a score
function parseScore(raw_score){
  var score = parseInt(raw_score);
  if(score === ""){
    score = 0;
  }
  return score;
}

// Retry state for sendDataToWatch — limit retries to avoid infinite loops.
var s_send_retries = 0;
var MAX_SEND_RETRIES = 3;

// Function to send a message to the Pebble using AppMessage API
function sendDataToWatch(data){
  s_send_retries = 0;
  function trySend() {
    Pebble.sendAppMessage(data,
      function() { s_send_retries = 0; }, // success — nothing to do
      function(e) {
        console.log('sendAppMessage failed (attempt ' + (s_send_retries + 1) + '): ' + JSON.stringify(e));
        if (s_send_retries < MAX_SEND_RETRIES) {
          s_send_retries++;
          setTimeout(trySend, 1000);
        } else {
          console.log('sendAppMessage gave up after ' + MAX_SEND_RETRIES + ' retries.');
          s_send_retries = 0;
        }
      }
    );
  }
  trySend();
}

// Convert an ISO 8601 UTC game time to local 12-hour "H:MM" format (no leading zero)
function formatGameTime(isoString) {
  var d = new Date(isoString);
  var hours = d.getHours();
  var minutes = ('0' + d.getMinutes()).slice(-2);
  if (hours === 0) { hours = 12; }
  else if (hours > 12) { hours -= 12; }
  return hours + ':' + minutes;
}

// Map MLB Stats API detailedState values to the status strings compileDataForWatch() expects
function mapGameStatus(detailedState, abstractGameState) {
  if (!detailedState) { return 'Preview'; }
  if (detailedState === 'Scheduled') { return 'Preview'; }
  // Treat as Final only when both fields agree — abstractGameState alone can
  // flash 'Final' transiently during reviews or long breaks mid-game.
  if (abstractGameState === 'Final' && (detailedState === 'Final' || detailedState === 'Game Over' || detailedState === 'Completed Early')) { return 'Final'; }
  if (detailedState === 'Game Over' || detailedState === 'Completed Early') { return 'Final'; }
  // Delayed/suspended games were already in progress; keep them as such
  if (detailedState === 'Delay' || detailedState.indexOf('Delayed') === 0 || detailedState === 'Suspended') { return 'In Progress'; }
  return detailedState;
}

// Extract the first matching TV and radio broadcast names for a given side ('home' or 'away')
function getBroadcasts(broadcasts, side) {
  var tv = '', radio = '';
  if (!broadcasts || !broadcasts.length) { return { tv: tv, radio: radio }; }
  for (var i = 0; i < broadcasts.length; i++) {
    var b = broadcasts[i];
    // Accept a side-specific broadcast, or a national broadcast as a TV fallback
    var relevant = (b.homeAway === side) || (b.homeAway === 'national' && tv === '');
    if (!relevant) { continue; }
    if (b.type === 'TV' && tv === '') {
      tv = b.name || '';
    } else if ((b.type === 'AM' || b.type === 'FM' || b.type === 'Radio') && radio === '') {
      radio = b.name || '';
    }
  }
  return { tv: tv, radio: radio };
}

// Transform a single MLB Stats API game object into the shape compileDataForWatch() expects
function transformMLBGame(game) {
  var linescore       = game.linescore || {};
  var offense         = linescore.offense || {};
  var linescoreTeams  = linescore.teams  || {};
  var homeLS          = linescoreTeams.home || {};
  var awayLS          = linescoreTeams.away || {};
  var homeBroadcasts  = getBroadcasts(game.broadcasts, 'home');
  var awayBroadcasts  = getBroadcasts(game.broadcasts, 'away');

  return {
    game_status:          mapGameStatus(game.status.detailedState, game.status.abstractGameState),
    home_team:            normalizeAbbrev(game.teams.home.team.abbreviation || teamIdToAbbrev[game.teams.home.team.id] || ''),
    away_team:            normalizeAbbrev(game.teams.away.team.abbreviation || teamIdToAbbrev[game.teams.away.team.id] || ''),
    home_pitcher:         lastName((game.teams.home.probablePitcher && game.teams.home.probablePitcher.fullName) || ''),
    away_pitcher:         lastName((game.teams.away.probablePitcher && game.teams.away.probablePitcher.fullName) || ''),
    game_time:            formatGameTime(game.gameDate),
    game_start_ms:        new Date(game.gameDate).getTime(),
    home_tv_broadcast:    homeBroadcasts.tv,
    home_radio_broadcast: homeBroadcasts.radio,
    away_tv_broadcast:    awayBroadcasts.tv,
    away_radio_broadcast: awayBroadcasts.radio,
    runners_on_base:      (offense.first  ? 1 : 0) + ':' +
                          (offense.second ? 1 : 0) + ':' +
                          (offense.third  ? 1 : 0),
    home_score: homeLS.runs !== undefined ? homeLS.runs : (game.teams.home.score || 0),
    away_score: awayLS.runs !== undefined ? awayLS.runs : (game.teams.away.score || 0),
    inning:     linescore.currentInning || 0,
    inning_half: linescore.inningHalf  || '',
    balls:      linescore.balls   !== undefined ? linescore.balls   : 0,
    strikes:    linescore.strikes !== undefined ? linescore.strikes : 0,
    outs:       linescore.outs    !== undefined ? linescore.outs    : 0
  };
}

// Function to build the dictionary of relevant data for the AppMessage
function compileDataForWatch(raw_data, game){
  // Determine how much the data has changed
  // Prepare the dictionary depending on game status
  // Compile dictionary based on what has changed
  retry = 0;
  var data = raw_data.game_data[game];
  var game_status = data.game_status;
  var dictionary = {'TYPE':1};

  if (game_status == 'Preview' || game_status == 'Warmup' || game_status == 'Pre-Game' || game_status == 'Postponed' || game_status == 'Cancelled'){

    dictionary.NUM_GAMES = 1;
    if (game_status == 'Preview'){
      dictionary.STATUS = 0;
    } else {
      dictionary.STATUS = 1;
    }
    dictionary.HOME_TEAM = data.home_team;
    dictionary.AWAY_TEAM = data.away_team;
    dictionary.HOME_PITCHER = data.home_pitcher;
    dictionary.AWAY_PITCHER = data.away_pitcher;
    if (game_status == 'Postponed'){
      dictionary.GAME_TIME = "PPD";
    } else if (game_status == 'Cancelled'){
      dictionary.GAME_TIME = "CAN";
    } else {
      dictionary.GAME_TIME = data.game_time;
    }
    if(dictionary.HOME_TEAM == teams[favoriteTeam]){
      dictionary.HOME_BROADCAST = data.home_radio_broadcast;
      dictionary.AWAY_BROADCAST = data.home_tv_broadcast;
    } else {
      dictionary.HOME_BROADCAST = data.away_radio_broadcast;
      dictionary.AWAY_BROADCAST = data.away_tv_broadcast;
    }
    sendDataToWatch(dictionary);

    // Schedule a proactive refresh near the actual game start time so the
    // watch doesn't have to wait up to refresh_time_off minutes to notice
    // the game has started.
    if (s_start_timer) { clearTimeout(s_start_timer); s_start_timer = null; }
    var msUntilStart = data.game_start_ms - Date.now();
    if (msUntilStart > 0 && msUntilStart <= 90 * 60 * 1000) {
      // Game starts within 90 minutes — fire 60 s after scheduled start.
      s_start_timer = setTimeout(function() {
        s_start_timer = null;
        newGameDataRequest();
      }, msUntilStart + 60000);
    } else if (msUntilStart <= 0 && msUntilStart > -30 * 60 * 1000) {
      // Scheduled start just passed but game still shows Pre-Game — retry in 2 min.
      s_start_timer = setTimeout(function() {
        s_start_timer = null;
        newGameDataRequest();
      }, 2 * 60 * 1000);
    }

  } else if (game_status == 'In Progress'){

    dictionary.NUM_GAMES = 1;
    dictionary.STATUS = 2;
    dictionary.HOME_TEAM = data.home_team;
    dictionary.AWAY_TEAM = data.away_team;
    dictionary.FIRST = parseInt(data.runners_on_base.split(":")[0]);
    dictionary.SECOND = parseInt(data.runners_on_base.split(":")[1]);
    dictionary.THIRD = parseInt(data.runners_on_base.split(":")[2]);
    dictionary.HOME_SCORE = parseScore(data.home_score);
    dictionary.AWAY_SCORE = parseScore(data.away_score);
    dictionary.INNING = parseInt(data.inning);
    dictionary.INNING_HALF = data.inning_half;
    dictionary.BALLS = parseInt(data.balls);
    dictionary.STRIKES = parseInt(data.strikes);
    dictionary.OUTS = parseInt(data.outs);
    if (s_start_timer) { clearTimeout(s_start_timer); s_start_timer = null; }
    sendDataToWatch(dictionary);

  } else {

    dictionary.NUM_GAMES = 1;
    dictionary.STATUS = 3;
    dictionary.HOME_TEAM = data.home_team;
    dictionary.AWAY_TEAM = data.away_team;
    dictionary.HOME_SCORE = parseScore(data.home_score);
    dictionary.AWAY_SCORE = parseScore(data.away_score);
    dictionary.INNING = parseInt(data.inning);
    if (s_start_timer) { clearTimeout(s_start_timer); s_start_timer = null; }
    sendDataToWatch(dictionary);

  }
}

// Assign a display priority to a game status.
// Higher value = more relevant to show right now.
//   3: actively in progress
//   2: imminent (Warmup / Pre-Game — API transitions here ~30 min before first pitch)
//   1: scheduled but not yet soon (Preview)
//   0: completed or not happening (Final / Postponed / Cancelled)
function gameStatusPriority(status) {
  if (status === 'In Progress') { return 3; }
  if (status === 'Warmup' || status === 'Pre-Game') { return 2; }
  if (status === 'Preview') { return 1; }
  return 0;  // Final, Postponed, Cancelled, unknown
}

function chooseGame(data){
  var s1 = data.game_data[0].game_status;
  var s2 = data.game_data[1].game_status;
  var p1 = gameStatusPriority(s1);
  var p2 = gameStatusPriority(s2);

  // Game with the higher priority wins.
  // Ties: game 2 wins when both are Final (most recent result);
  //       game 1 wins when both are Preview (sooner start) or both are equal active states.
  if (p2 > p1) { return 1; }
  if (p1 > p2) { return 0; }
  // Equal priority — both Final or both the same pre-game state
  if (p1 === 0) { return 1; }  // both done: show game 2 (most recent)
  return 0;                    // both Preview or both active: show game 1 (sooner)
}

// Function to process the incoming data.
// requestDate: the YYYY-MM-DD date string used in the API request that
// produced gameData.  Used as the cache key so a response that arrives after
// local midnight is not stored under tomorrow's date with yesterday's games.
function processGameData(gameData, requestDate){
  var number_of_games = gameData.number_of_games;
  var today = getCurrentDate();

  if (number_of_games === 0) {
    // No games returned — check if we have a cached result from today's schedule.
    // Only reuse the cache when the cached date matches today; a response
    // processed after midnight must not re-surface yesterday's Final game.
    if (lastGameResult && lastGameResult.date === today) {
      console.log('0 games from API; reusing cached result for ' + today);
      processGameData(lastGameResult.data, lastGameResult.date);
    } else {
      console.log('No games found for ' + today + ' (team index ' + favoriteTeam + ', id ' + teamIds[favoriteTeam] + '); sending NO_GAME_TODAY');
      var dictionary = { 'TYPE':1, 'NUM_GAMES': 0 };
      sendDataToWatch(dictionary);
    }
  } else {
    // Cache keyed by the request date, not the wall-clock time of the response.
    lastGameResult = { date: requestDate || today, data: gameData };
    if (number_of_games == 1){
      compileDataForWatch(gameData, 0);
    } else {
      compileDataForWatch(gameData, chooseGame(gameData));
    }
  }
}

// Function to get MLB data from the official MLB Stats API (https://statsapi.mlb.com)
// No API key required. Replaces the private chs.network server.
function getGameData(offset){
  // Build the date string (YYYY-MM-DD) first so it can be passed to every
  // processGameData call as the authoritative cache key.  Using the request
  // date (not the wall-clock time of the response) prevents a post-midnight
  // response from being cached under tomorrow's date with yesterday's game.
  var d = new Date();
  d.setDate(d.getDate() + offset);
  var date = d.getFullYear() + '-' +
             ('0' + (d.getMonth() + 1)).slice(-2) + '-' +
             ('0' + d.getDate()).slice(-2);

  var teamId = teamIds[favoriteTeam];
  if (!teamId) {
    // All-Star teams or invalid index have no Stats API team ID
    processGameData({ number_of_games: 0, game_data: [] }, date);
    return;
  }

  var url = 'https://statsapi.mlb.com/api/v1/schedule' +
            '?sportId=1' +
            '&date=' + date +
            '&teamId=' + teamId +
            '&hydrate=linescore,broadcasts(all),probablePitcher';

  var request = new XMLHttpRequest();
  request.timeout = 10000;

  request.onload = function() {
    if (this.status !== 200) {
      if (reload_timeout < 5) {
        reload_timeout++;
        getGameData(offset);
      } else {
        processGameData({ number_of_games: 0, game_data: [] }, date);
      }
      return;
    }
    var raw;
    try {
      raw = JSON.parse(this.responseText);
      console.log("Raw data from MLB Stats API: " + JSON.stringify(raw));
    } catch(e) {
      if (reload_timeout < 5) {
        reload_timeout++;
        getGameData(offset);
      } else {
        processGameData({ number_of_games: 0, game_data: [] }, date);
      }
      return;
    }
    reload_timeout = 0;
    var games = (raw.dates && raw.dates.length > 0) ? raw.dates[0].games : [];
    var number_of_games = games.length;
    console.log('API date=' + date + ' teamId=' + teamId + ' games=' + number_of_games + ' retry=' + retry);

    // Check for All-Star game (NL or AL team on either side)
    if (number_of_games === 1) {
      var homeAbbrev = normalizeAbbrev(games[0].teams.home.team.abbreviation);
      var awayAbbrev = normalizeAbbrev(games[0].teams.away.team.abbreviation);
      if (homeAbbrev === 'NL' || homeAbbrev === 'AL' || awayAbbrev === 'NL' || awayAbbrev === 'AL') {
        all_star_game = 1;
        sendSettings();
      } else {
        all_star_game = 0;
      }
    } else {
      all_star_game = 0;
    }

    var game_data;
    try {
      game_data = games.map(transformMLBGame);
    } catch(e) {
      console.log('transformMLBGame error: ' + e.message);
      processGameData({ number_of_games: 0, game_data: [] }, date);
      return;
    }
    var transformed = {
      number_of_games: number_of_games,
      game_data: game_data
    };

    if (offset === -1 && number_of_games === 0) {
      offset = 0;
      getGameData(offset);
    } else if (number_of_games === 0 && retry === 0) {
      getGameData(offset);
      retry = 1;
    } else if (offset === -1 && number_of_games === 1 && (transformed.game_data[0].game_status === 'Postponed' || transformed.game_data[0].game_status === 'Cancelled')) {
      offset = 0;
      getGameData(offset);
    } else if (offset === -1 && number_of_games === 2 && (transformed.game_data[1].game_status === 'Postponed' || transformed.game_data[1].game_status === 'Cancelled')) {
      offset = 0;
      getGameData(offset);
    } else {
      processGameData(transformed, date);
    }
  };

  // On network error, retry up to 5 times
  request.onerror = function() {
    if (reload_timeout < 5) {
      reload_timeout++;
      getGameData(offset);
    } else {
      processGameData({ number_of_games: 0, game_data: [] }, date);
    }
  };

  // On timeout, retry up to 5 times
  request.ontimeout = function() {
    if (reload_timeout < 5) {
      reload_timeout++;
      getGameData(offset);
    } else {
      processGameData({ number_of_games: 0, game_data: [] }, date);
    }
  };

  request.open('GET', url);
  request.send();
}

// Function called to refresh game data
function newGameDataRequest(){
  // Reset retry counter so each scheduled refresh gets a fresh attempt.
  retry = 0;
  reload_timeout = 0;
  // Always use today's local date (offset 0).
  // After local midnight the date increments naturally, so the final score of
  // a completed game remains visible until the first refresh after midnight,
  // at which point the new day's schedule is fetched.
  offset = 0;
  getGameData(offset);
}

// Function to append settings to data
function sendSettings(){
  var favoriteTeamTemp = favoriteTeam;
  // Change to All Star Game logos
  if(all_star_game == 1){
    if ((favoriteTeam == 1) || (favoriteTeam == 2) || (favoriteTeam == 3) || (favoriteTeam == 4) || (favoriteTeam == 12) || (favoriteTeam == 13) || (favoriteTeam == 17) || (favoriteTeam == 21) || (favoriteTeam == 22) || (favoriteTeam == 23) || (favoriteTeam == 26) || (favoriteTeam == 27) || (favoriteTeam == 28) || (favoriteTeam == 29) || (favoriteTeam == 30)) {
      favoriteTeamTemp = 32;
      backgroundColor = "000055";
    } else {
      favoriteTeamTemp = 31;
      backgroundColor = "AA0000";
    }
    secondaryColor = "FFFFFF";
    primaryColor = "FFFFFF";
  }
  var dictionary = {'TYPE':0, 'PREF_FAVORITE_TEAM':parseInt(favoriteTeamTemp), 'PREF_SHAKE_ENABELED':parseInt(shakeEnabled), 'PREF_SHAKE_TIME':parseInt(shakeTime), 'PREF_REFRESH_TIME_OFF':parseInt(refreshTime[0]), 'PREF_REFRESH_TIME_ON': parseInt(refreshTime[1]), 'PREF_PRIMARY_COLOR': primaryColor, 'PREF_SECONDARY_COLOR': secondaryColor, 'PREF_BACKGROUND_COLOR': backgroundColor, 'PREF_BASES_DISPLAY': parseInt(basesDisplay)};
  sendDataToWatch(dictionary);
}

// Function to load the stored settings
function loadSettings(){
  var stored = parseInt(localStorage.getItem(1));
  favoriteTeam = isNaN(stored) ? 8 : stored;

  stored = parseInt(localStorage.getItem(2));
  shakeEnabled = isNaN(stored) ? 1 : stored;

  stored = parseInt(localStorage.getItem(3));
  shakeTime = isNaN(stored) ? 5 : stored;

  stored = parseInt(localStorage.getItem(4));
  refreshTime[0] = isNaN(stored) ? 3600 : stored;

  stored = parseInt(localStorage.getItem(5));
  refreshTime[1] = isNaN(stored) ? 60 : stored;

  primaryColor = localStorage.getItem(6) || "FFFFFF";
  secondaryColor = localStorage.getItem(7) || "FFFFFF";
  backgroundColor = localStorage.getItem(8) || "AA0000";

  stored = parseInt(localStorage.getItem(9));
  basesDisplay = isNaN(stored) ? 1 : stored;

  sendSettings();
}

// Function to store settings
function storeSettings(configuration){
  if (configuration.hasOwnProperty('favorite_team') === true) {
    var newTeam = parseInt(configuration.favorite_team);
    if (newTeam !== favoriteTeam) {
      lastGameResult = null;
    }
    favoriteTeam = newTeam;
    localStorage.setItem(1, favoriteTeam);
  }
  if (configuration.hasOwnProperty('shake_enabeled') === true) {
    shakeEnabled = parseInt(configuration.shake_enabeled);
    localStorage.setItem(2, shakeEnabled);
  }
  if (configuration.hasOwnProperty('shake_time') === true) {
    shakeTime = parseInt(configuration.shake_time);
    localStorage.setItem(3, shakeTime);
  }
  if (configuration.hasOwnProperty('refresh_off') === true) {
    refreshTime[0] = parseInt(configuration.refresh_off);
    localStorage.setItem(4, refreshTime[0]);
  }
  if (configuration.hasOwnProperty('refresh_game') === true) {
    refreshTime[1] = parseInt(configuration.refresh_game);
    localStorage.setItem(5, refreshTime[1]);
  }
  if (configuration.hasOwnProperty('primary_color') === true) {
    primaryColor = configuration.primary_color;
    localStorage.setItem(6, primaryColor);
  }
  if (configuration.hasOwnProperty('secondary_color') === true) {
    secondaryColor = configuration.secondary_color;
    localStorage.setItem(7, secondaryColor);
  }
  if (configuration.hasOwnProperty('background_color') === true) {
    backgroundColor = configuration.background_color;
    localStorage.setItem(8, backgroundColor);
  }
  if (configuration.hasOwnProperty('bases_display') === true) {
    basesDisplay = configuration.bases_display;
    localStorage.setItem(9, basesDisplay);
  }
  sendSettings();
  newGameDataRequest();
}

// Function to send initial settings/processes to watch and load data
function initializeData(){
  loadSettings();
  // Game data is requested by the watch (TYPE=1) after it receives settings.
  // Calling newGameDataRequest() here races with the settings send: if settings
  // is still in-flight when game data is sent, sendAppMessage rejects it and
  // after MAX_SEND_RETRIES the watch is left on the loading screen indefinitely.
}

// Called when JS is ready
Pebble.addEventListener("ready", function(e) {
  initializeData();
});

// Called when incoming message from the Pebble is received
Pebble.addEventListener("appmessage", function(e) {
  if (!e || !e.payload) {
    console.log('appmessage: empty or missing payload, ignoring.');
    return;
  }
  var type = parseInt(e.payload.TYPE);
  if(type == 1){
    newGameDataRequest();
  } else if(type == 2) {
    loadSettings();
  }
});

// Convert a 24-bit color integer (as returned by Clay's color picker) to a
// 6-character uppercase hex string that GColorFromHEX() / storeSettings() expects.
function colorIntToHex(value) {
  var hex = value.toString(16).toUpperCase();
  while (hex.length < 6) { hex = '0' + hex; }
  return hex;
}

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) {
    console.log('No configuration received to save.');
    return;
  }
  console.log('Configuration received: ' + e.response);
  var raw = clay.getSettings(e.response, false);
  // Unwrap Clay's {value: ...} objects into a flat key→value map
  var settings = {};
  Object.keys(raw).forEach(function(key) {
    settings[key] = (raw[key] !== null && typeof raw[key] === 'object' && raw[key].hasOwnProperty('value'))
      ? raw[key].value
      : raw[key];
  });
  // Clay returns color picker values as 24-bit integers; convert to 6-char hex
  // strings so the existing storeSettings() / C-side GColorFromHEX() code works.
  ['primary_color', 'secondary_color', 'background_color'].forEach(function(key) {
    if (typeof settings[key] === 'number') {
      settings[key] = colorIntToHex(settings[key]);
    }
  });
  storeSettings(settings);
});
