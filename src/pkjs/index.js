/* Sleeper fantasy football -> Pebble Time 2
 *
 * Runs on the phone. Pulls the user's selected Sleeper leagues, works out a win
 * probability for each matchup from live score + projections for the starters
 * who have not played yet, and ships one compact row per league to the watch.
 */

var API = 'https://api.sleeper.app';
// Served from GitHub Pages on this project's own subdomain. It is compiled
// into the .pbw and sideloaded onto the watch, so it must stay stable:
// changing it means rebuilding and reinstalling on every watch.
var CONFIG_URL = 'https://pebble-fantasy-football.michaellunzer.com/config.html';
var SEP = '\x1F';

// Per-player standard deviation used to spread the projected margin. Tuned so
// the model reproduces the direction and rough magnitude of Sleeper's own
// number on real matchups.
var PLAYER_SD = 9.0;

// Set to an array of rows to bypass the network and render fixtures instead.
// Used only to screenshot the 3-league and scrolling layouts. null = off.
var DEMO_LEAGUES = null;

// ------------------------------------------------------------------ utilities

function get(url, cb) {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.onload = function () {
    if (xhr.status >= 200 && xhr.status < 300) {
      try {
        cb(null, JSON.parse(xhr.responseText));
      } catch (e) {
        cb('parse error for ' + url);
      }
    } else {
      cb('HTTP ' + xhr.status + ' for ' + url);
    }
  };
  xhr.onerror = function () { cb('network error for ' + url); };
  xhr.timeout = 20000;
  xhr.ontimeout = function () { cb('timeout for ' + url); };
  xhr.send();
}

function settings() {
  try {
    var raw = localStorage.getItem('settings');
    return raw ? JSON.parse(raw) : null;
  } catch (e) {
    return null;
  }
}

// Abramowitz & Stegun 7.1.26 — JS has no Math.erf.
function erf(x) {
  var sign = x < 0 ? -1 : 1;
  x = Math.abs(x);
  var t = 1 / (1 + 0.3275911 * x);
  var y = 1 - ((((1.061405429 * t - 1.453152027) * t + 1.421413741) * t -
                0.284496736) * t + 0.254829592) * t * Math.exp(-x * x);
  return sign * y;
}

function normalCdf(z) {
  return 0.5 * (1 + erf(z / Math.SQRT2));
}

// Pebble's system fonts are Latin-1; smart quotes and emoji render as tofu, so
// fold the common typographic characters down and drop anything still exotic.
function asciify(name) {
  return String(name)
    .replace(/[\u2018\u2019\u201B\u02BC]/g, "'")
    .replace(/[\u201C\u201D]/g, '"')
    .replace(/[\u2013\u2014]/g, '-')
    .replace(/[\u2026]/g, '...')
    .replace(/[^\x20-\x7E]/g, '')
    .replace(/\s+/g, ' ')
    .trim() || '?';
}

function teamName(user) {
  if (!user) return '?';
  var md = user.metadata || {};
  return asciify(md.team_name || user.display_name || user.username || '?');
}

function today() {
  var d = new Date();
  var m = d.getMonth() + 1;
  var day = d.getDate();
  return d.getFullYear() + '-' + (m < 10 ? '0' : '') + m + '-' +
         (day < 10 ? '0' : '') + day;
}

// ---------------------------------------------------------------- projections

function projectionKey(season, week) {
  return 'proj:' + season + ':' + week;
}

function loadProjections(season, week, cb) {
  var key = projectionKey(season, week);
  try {
    var cached = localStorage.getItem(key);
    if (cached) {
      var obj = JSON.parse(cached);
      if (Date.now() - obj.ts < 3600 * 1000) {
        console.log('projections: cache hit');
        return cb(null, obj.map);
      }
    }
  } catch (e) { /* fall through to refetch */ }

  var url = API + '/projections/nfl/' + season + '/' + week +
            '?season_type=regular&position[]=QB&position[]=RB&position[]=WR' +
            '&position[]=TE&position[]=K&position[]=DEF&order_by=pts_ppr';
  get(url, function (err, list) {
    if (err) return cb(err);
    var map = {};
    for (var i = 0; i < list.length; i++) {
      var e = list[i];
      var st = e.stats || {};
      map[e.player_id] = {
        ppr: st.pts_ppr || 0,
        half: st.pts_half_ppr || 0,
        std: st.pts_std || 0,
        date: e.date || null
      };
    }
    try {
      localStorage.setItem(key, JSON.stringify({ ts: Date.now(), map: map }));
    } catch (e2) {
      console.log('projections: cache write failed, continuing');
    }
    cb(null, map);
  });
}

// ------------------------------------------------------------------ win model

// Three states, not two. A starter on a bye, inactive, or simply missing from
// the projections has no game date at all -- counting them as "yet to play"
// would inflate the players-left figure with players who will never take the
// field, and that figure is what makes a deficit readable.
//   'done'    already scored, or their game has finished
//   'pending' has a game still to come
//   'none'    no game: bye, inactive, or unknown
function playState(pid, pts, proj, now) {
  if (pts > 0) return 'done';
  var p = proj[pid];
  if (!p || !p.date) return 'none';
  if (p.date < now) return 'done';
  return 'pending';
}

function projectTeam(entry, proj, scoringField, now) {
  var current = entry.points || 0;
  var remaining = 0;
  var yetToPlay = 0;
  var starters = entry.starters || [];
  var pointsArr = entry.starters_points || [];
  for (var i = 0; i < starters.length; i++) {
    var pid = starters[i];
    if (!pid || pid === '0') continue;          // empty roster slot
    var pts = pointsArr[i] || 0;
    if (playState(pid, pts, proj, now) !== 'pending') continue;
    var p = proj[pid];
    remaining += (p && p[scoringField]) ? p[scoringField] : 0;
    yetToPlay += 1;
  }
  return { current: current, projected: current + remaining, left: yetToPlay };
}

function winProbability(mine, theirs) {
  var left = mine.left + theirs.left;
  if (left === 0) {
    if (mine.current > theirs.current) return 100;
    if (mine.current < theirs.current) return 0;
    return 50;
  }
  var sigma = Math.max(3, PLAYER_SD * Math.sqrt(left));
  var p = normalCdf((mine.projected - theirs.projected) / sigma);
  var pct = Math.round(p * 100);
  if (pct < 1) pct = 1;
  if (pct > 99) pct = 99;
  return pct;
}

function scoringField(league) {
  var rec = (league && league.scoring_settings && league.scoring_settings.rec) || 0;
  if (rec >= 1) return 'ppr';
  if (rec >= 0.5) return 'half';
  return 'std';
}

function fmt(points) {
  return (Math.round((points || 0) * 100) / 100).toFixed(2);
}

function record(roster) {
  var s = (roster && roster.settings) || {};
  var r = (s.wins || 0) + '-' + (s.losses || 0);
  if (s.ties) r += '-' + s.ties;
  return r;
}

// ------------------------------------------------------------------- pipeline

function buildRow(cfg, league, data, proj, now) {
  var rosters = data.rosters, users = data.users, matchups = data.matchups;

  var userById = {};
  for (var i = 0; i < users.length; i++) userById[users[i].user_id] = users[i];

  var rosterById = {};
  var myRosterId = league.roster_id || null;
  for (var j = 0; j < rosters.length; j++) {
    rosterById[rosters[j].roster_id] = rosters[j];
    if (!myRosterId && rosters[j].owner_id === cfg.user_id) {
      myRosterId = rosters[j].roster_id;
    }
  }
  if (!myRosterId) return null;

  var byRoster = {};
  for (var k = 0; k < matchups.length; k++) byRoster[matchups[k].roster_id] = matchups[k];

  var me = byRoster[myRosterId];
  if (!me) return null;

  var opp = null;
  for (var m = 0; m < matchups.length; m++) {
    if (matchups[m].matchup_id === me.matchup_id &&
        matchups[m].roster_id !== myRosterId) {
      opp = matchups[m];
      break;
    }
  }
  if (!opp) return null;

  var field = scoringField(league);
  var mine = projectTeam(me, proj, field, now);
  var theirs = projectTeam(opp, proj, field, now);
  var win = winProbability(mine, theirs);
  // mine/theirs also carry projected totals and how many starters are still to
  // play; both go to the watch, since a deficit only means something next to
  // the number of players left to erase it.

  function nameFor(rid) {
    var r = rosterById[rid];
    return teamName(r ? userById[r.owner_id] : null);
  }

  return [
    nameFor(myRosterId),
    nameFor(opp.roster_id),
    fmt(me.points),
    fmt(opp.points),
    String(win),
    record(rosterById[myRosterId]),
    record(rosterById[opp.roster_id]),
    String(mine.left),
    String(theirs.left),
    fmt(mine.projected),
    fmt(theirs.projected)
  ].join(SEP);
}

function fetchLeague(leagueId, week, cb) {
  var out = {};
  var pending = 4;
  var failed = null;

  function done(key) {
    return function (err, data) {
      if (err) failed = failed || err;
      else out[key] = data;
      if (--pending === 0) cb(failed, out);
    };
  }

  get(API + '/v1/league/' + leagueId, done('league'));
  get(API + '/v1/league/' + leagueId + '/rosters', done('rosters'));
  get(API + '/v1/league/' + leagueId + '/users', done('users'));
  get(API + '/v1/league/' + leagueId + '/matchups/' + week, done('matchups'));
}

function refresh() {
  if (DEMO_LEAGUES) {
    console.log('using DEMO_LEAGUES fixture');
    return sendRows(DEMO_LEAGUES, 0);
  }

  var cfg = settings();
  if (!cfg || !cfg.leagues || cfg.leagues.length === 0) {
    return sendStatus('Open settings to add your Sleeper leagues', 0);
  }

  get(API + '/v1/state/nfl', function (err, state) {
    if (err) return sendStatus('Cannot reach Sleeper', 0);
    var season = state.season;
    var week = state.display_week || state.week;

    loadProjections(season, week, function (perr, proj) {
      if (perr) {
        console.log('projections failed: ' + perr + ' (continuing without)');
        proj = {};
      }
      var now = today();
      var rows = new Array(cfg.leagues.length);
      var pending = cfg.leagues.length;

      cfg.leagues.forEach(function (league, idx) {
        fetchLeague(league.league_id, week, function (lerr, data) {
          if (!lerr && data.rosters && data.users && data.matchups) {
            var merged = data.league || {};
            merged.roster_id = league.roster_id;
            rows[idx] = buildRow(cfg, merged, data, proj, now);
          } else {
            console.log('league ' + league.league_id + ' failed: ' + lerr);
          }
          if (--pending === 0) {
            var clean = rows.filter(function (r) { return !!r; });
            if (clean.length === 0) {
              sendStatus('No matchups found for week ' + week, 0);
            } else {
              sendRows(clean, week);
            }
          }
        });
      });
    });
  });
}

// ---------------------------------------------------------------- app glance

// Put the closest matchup on the launcher, so the score is visible without
// opening the app. "Closest" means nearest a coin-flip, which is the one worth
// looking at; a 95% blowout is not news.
function updateGlance(rows, week) {
  if (!rows.length) return;

  var best = null, bestGap = 999;
  rows.forEach(function (row) {
    var f = row.split(SEP);
    var gap = Math.abs(parseInt(f[4], 10) - 50);
    if (gap < bestGap) { bestGap = gap; best = f; }
  });
  if (!best) return;

  var pregame = parseFloat(best[2]) === 0 && parseFloat(best[3]) === 0;
  var subtitle = pregame
    ? best[0] + ' vs ' + best[1] + '  ' + best[4] + '%'
    : best[0] + ' ' + best[2] + '-' + best[3] + '  ' + best[4] + '%';

  // Expire a week out so a stale score cannot sit on the launcher forever.
  var expiry = new Date(Date.now() + 7 * 24 * 3600 * 1000).toISOString();

  try {
    Pebble.appGlanceReload([{
      layout: {
        icon: 'system://images/SCHEDULED_EVENT',
        subtitleTemplateString: subtitle.substring(0, 40)
      },
      expirationTime: expiry
    }], function () {
      console.log('glance updated: ' + subtitle);
    }, function () {
      console.log('glance reload failed');
    });
  } catch (e) {
    console.log('appGlanceReload unavailable: ' + e);
  }
}

// ------------------------------------------------------------ scheduled wake

// Exactly one wakeup is ever pending: each run cancels and re-arms. That keeps
// us clear of the 8-slot-per-app limit no matter how long the season runs.
// Only armed when the user opts in, because a wakeup visibly launches the app.
function nextWakeupAt(rows) {
  var anyLive = rows.some(function (row) {
    var f = row.split(SEP);
    var left = parseInt(f[7], 10) + parseInt(f[8], 10);
    var scored = parseFloat(f[2]) > 0 || parseFloat(f[3]) > 0;
    return left > 0 && scored;
  });
  if (!anyLive) return 0;              // nothing in flight; do not wake
  return Math.floor(Date.now() / 1000) + 30 * 60;
}

// ------------------------------------------------------------- watch transport

function sendStatus(text, week) {
  Pebble.sendAppMessage({ STATUS: text, LEAGUE_COUNT: 0, WEEK: week || 0 });
}

// One league per message, each sent only after the previous is acknowledged.
function sendRows(rows, week) {
  console.log('sending ' + rows.length + ' leagues for week ' + week);
  updateGlance(rows, week);
  var i = 0;
  function step() {
    if (i >= rows.length) return;
    var payload = {
      LEAGUE_IDX: i,
      LEAGUE_COUNT: rows.length,
      LEAGUE_DATA: rows[i],
      WEEK: week || 0
    };
    // Ride the wakeup time along with the last league of the batch.
    if (i === rows.length - 1) {
      var cfg = settings();
      payload.WAKEUP_AT = (cfg && cfg.autoRefresh) ? nextWakeupAt(rows) : 0;
    }
    Pebble.sendAppMessage(payload, function () {
      i += 1;
      step();
    }, function (e) {
      console.log('send failed at index ' + i + ', retrying once');
      Pebble.sendAppMessage(payload, function () { i += 1; step(); },
                            function () { console.log('giving up at ' + i); });
    });
  }
  step();
}

// ------------------------------------------------------------------- lifecycle

Pebble.addEventListener('ready', function () {
  console.log('pkjs ready');
  refresh();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload && e.payload.REQUEST_REFRESH) {
    console.log('refresh requested from watch');
    refresh();
  }
});

Pebble.addEventListener('showConfiguration', function () {
  var cfg = settings();
  var url = CONFIG_URL + '?current=' +
            encodeURIComponent(cfg ? JSON.stringify(cfg) : '');
  console.log('opening config: ' + url);
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response) return;
  var decoded;
  try {
    decoded = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    console.log('bad config response');
    return;
  }
  try {
    localStorage.setItem('settings', JSON.stringify(decoded));
  } catch (err2) {
    console.log('could not persist settings');
  }
  console.log('settings saved: ' + (decoded.leagues || []).length + ' leagues');
  refresh();
});
