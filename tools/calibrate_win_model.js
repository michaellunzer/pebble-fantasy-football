/*
 * Where PLAYER_SD_BASE and PLAYER_SD_SLOPE in src/pkjs/index.js come from.
 *
 *   node tools/calibrate_win_model.js [leagueId]
 *
 * The win model spreads the projected margin by a per-player standard
 * deviation. Rather than guess it, this scores every projected stat line and
 * every actual stat line for a run of past weeks under one league's own
 * scoring rules, then regresses the spread of (actual - projected) on the
 * projection. Doing it in league units matters: a custom defensive rulebook
 * moves both the projection and its spread.
 *
 * Takes a couple of minutes; it pulls two large payloads per week.
 */
var https = require('https');

var LEAGUE = process.argv[2] || '1390910781842812928';
var WEEKS = [];
for (var w = 1; w <= 18; w++) WEEKS.push([2025, w]);
for (var w2 = 1; w2 <= 7; w2++) WEEKS.push([2026, w2]);

var POS = 'position[]=QB&position[]=RB&position[]=WR&position[]=TE' +
          '&position[]=K&position[]=DEF';
// Draft and bookkeeping fields, plus Sleeper's canned totals: never scored.
var NOT_SCORED = { adp_dd_ppr: 1, pos_adp_dd_ppr: 1, gp: 1,
                   pts_ppr: 1, pts_half_ppr: 1, pts_std: 1 };

function get(path) {
  return new Promise(function (res, rej) {
    https.get({ host: 'api.sleeper.app', path: path,
                headers: { 'User-Agent': 'calibrate' } }, function (r) {
      var b = '';
      r.on('data', function (d) { b += d; });
      r.on('end', function () {
        try { res(JSON.parse(b)); }
        catch (e) { rej(new Error(path + ' -> ' + r.statusCode)); }
      });
    }).on('error', rej);
  });
}

function score(stats, scoring) {
  var total = 0;
  for (var k in stats) {
    if (NOT_SCORED[k]) continue;
    var weight = scoring[k];
    if (weight) total += stats[k] * weight;
  }
  return total;
}

(async function () {
  var league = await get('/v1/league/' + LEAGUE);
  var scoring = league.scoring_settings;
  console.log('calibrating against ' + league.name + '  (rec ' +
              (scoring.rec || 0) + ')\n');

  var obs = [];
  for (var i = 0; i < WEEKS.length; i++) {
    var season = WEEKS[i][0], week = WEEKS[i][1], proj, act;
    try {
      proj = await get('/projections/nfl/' + season + '/' + week +
                       '?season_type=regular&' + POS + '&order_by=pts_ppr');
      act = await get('/stats/nfl/' + season + '/' + week +
                      '?season_type=regular&' + POS + '&order_by=pts_ppr');
    } catch (e) { console.error('  skipped ' + season + ' w' + week); continue; }

    var actual = {};
    for (var a = 0; a < act.length; a++) actual[act[a].player_id] = act[a].stats || {};
    for (var p = 0; p < proj.length; p++) {
      var e = proj[p];
      if (!e.date && !e.game_id) continue;        // no game: bye or inactive
      var got = actual[e.player_id];
      if (got === undefined) continue;
      var expected = score(e.stats || {}, scoring);
      if (expected <= 0) continue;
      obs.push([expected, score(got, scoring)]);
    }
  }

  var edges = [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 21, 24, 999];
  var pts = [];
  console.log('  projected      n    mean resid       sd');
  for (var b = 0; b < edges.length - 1; b++) {
    var lo = edges[b], hi = edges[b + 1], v = [];
    for (var o = 0; o < obs.length; o++) {
      if (obs[o][0] >= lo && obs[o][0] < hi) v.push(obs[o][1] - obs[o][0]);
    }
    if (v.length < 40) continue;
    var mean = 0, j;
    for (j = 0; j < v.length; j++) mean += v[j];
    mean /= v.length;
    var ss = 0;
    for (j = 0; j < v.length; j++) ss += (v[j] - mean) * (v[j] - mean);
    var sd = Math.sqrt(ss / (v.length - 1));
    pts.push({ mid: (lo + Math.min(hi, 36)) / 2, sd: sd, n: v.length });
    console.log('  ' + (lo + '-' + (hi === 999 ? '36+' : hi)).padEnd(10) +
                String(v.length).padStart(6) + '    ' +
                mean.toFixed(2).padStart(8) + ' ' + sd.toFixed(2).padStart(8));
  }

  // Weighted least squares through the bucket spreads.
  var sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (var q = 0; q < pts.length; q++) {
    var wt = pts[q].n;
    sw += wt; sx += wt * pts[q].mid; sy += wt * pts[q].sd;
    sxx += wt * pts[q].mid * pts[q].mid; sxy += wt * pts[q].mid * pts[q].sd;
  }
  var slope = (sw * sxy - sx * sy) / (sw * sxx - sx * sx);
  var base = (sy - slope * sx) / sw;
  console.log('\n  n = ' + obs.length);
  console.log('  PLAYER_SD_BASE  = ' + base.toFixed(2));
  console.log('  PLAYER_SD_SLOPE = ' + slope.toFixed(3));
})();
