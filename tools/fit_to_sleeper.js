/*
 * Fits PLAYER_SD_BASE and PLAYER_SD_SLOPE so the watch agrees with the
 * Sleeper app's win percentages.
 *
 *   node tools/fit_to_sleeper.js
 *
 * The companion to tools/calibrate_win_model.js, which fits the same two
 * constants to what players actually did. That one measures; this one matches.
 * They disagree, and the choice between them is in the comment above the
 * constants in src/pkjs/index.js.
 *
 * Sleeper does not publish win probabilities, so every observation here is
 * read off a screenshot of the app by hand. Two observations against two free
 * parameters is interpolation, not a fit -- it reproduces them exactly and
 * promises nothing elsewhere. ADD OBSERVATIONS as you collect them. Each one
 * needs, from a single moment:
 *
 *   diff   your projected total minus theirs, as the WATCH computes them.
 *          Take these from the watch, not from Sleeper: a gap in the
 *          projections themselves would otherwise be absorbed into the spread
 *          and quietly corrupt it.
 *   rem    what is still to come from each player yet to finish, both sides
 *          pooled, again as the watch sees them.
 *   target the percentage the Sleeper app showed for YOUR side.
 */

var OBSERVATIONS = [
  { note: 'Boujee Boys, 2026 w8, Smith-Njigba alone on Monday night',
    diff: 130.10 - 153.79, rem: [26.73], target: 7 },
  { note: 'Hoyt, 2026 w8, Burden III against Odunze and the Seahawks',
    diff: 140.86 - 149.28, rem: [10.06, 8.27, 4.93], target: 25 }
];

// Abramowitz & Stegun 7.1.26, as in the app.
function erf(x) {
  var s = x < 0 ? -1 : 1;
  x = Math.abs(x);
  var a1 = 0.254829592, a2 = -0.284496736, a3 = 1.421413741,
      a4 = -1.453152027, a5 = 1.061405429, p = 0.3275911;
  var t = 1 / (1 + p * x);
  return s * (1 - ((((a5 * t + a4) * t + a3) * t + a2) * t + a1) * t *
              Math.exp(-x * x));
}
function normalCdf(z) { return 0.5 * (1 + erf(z / Math.SQRT2)); }

function predict(base, slope, o) {
  var v = 0;
  for (var i = 0; i < o.rem.length; i++) {
    var sd = base + slope * o.rem[i];
    v += sd * sd;
  }
  return 100 * normalCdf(o.diff / Math.max(3, Math.sqrt(v)));
}

function cost(base, slope) {
  var e = 0;
  for (var i = 0; i < OBSERVATIONS.length; i++) {
    var d = predict(base, slope, OBSERVATIONS[i]) - OBSERVATIONS[i].target;
    e += d * d;
  }
  return e;
}

function search(b0, b1, bStep, s0, s1, sStep, best) {
  for (var b = b0; b <= b1; b += bStep) {
    for (var s = s0; s <= s1; s += sStep) {
      var c = cost(b, s);
      if (c < best.cost) best = { base: b, slope: s, cost: c };
    }
  }
  return best;
}

var best = search(0, 12, 0.005, 0, 1.2, 0.005, { cost: Infinity });
best = search(Math.max(0, best.base - 0.02), best.base + 0.02, 0.0005,
              Math.max(0, best.slope - 0.02), best.slope + 0.02, 0.0005, best);

console.log('observations: ' + OBSERVATIONS.length +
            '   free parameters: 2' +
            (OBSERVATIONS.length <= 2 ? '   (interpolating, not fitting)' : ''));
console.log('\n  PLAYER_SD_BASE  = ' + best.base.toFixed(2));
console.log('  PLAYER_SD_SLOPE = ' + best.slope.toFixed(3));
console.log('\n  residuals');
for (var i = 0; i < OBSERVATIONS.length; i++) {
  var o = OBSERVATIONS[i];
  console.log('    ' + predict(best.base, best.slope, o).toFixed(1) +
              '% vs Sleeper ' + o.target + '%   ' + o.note);
}
