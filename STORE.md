# Pebble App Store Listing

## App Name
Fantasy Football

## Tagline
All your Sleeper leagues, on your wrist

## Short description
> the `--description` value, kept under ~100 characters

Every Sleeper league on one screen: live scores, who's left to play, and win odds from projections.

## Description

🏈 Fantasy football is mostly a Sunday spent glancing at a phone. This moves the
glance to your wrist.

- 👀 **Made for the ten-second check-in.** Raise your wrist, read where your week
  stands, get back to the game. No phone, no unlocking, no hunting for the
  right screen.
- 🏆 **Every league at once.** Most people are in more than one, and seeing them
  together is the point. The screen divides itself between them so each matchup
  stays readable instead of collapsing into a list.
- 👆 **Scroll it with your finger.** In more than three leagues? The list keeps
  going, and you swipe through it the way a touchscreen should work. The
  buttons still do it too, if you'd rather.
- ⚖️ **Numbers with the context that makes them mean something.** A score on its
  own won't tell you whether you're winning. How many players you still have
  left to play will.
- 🔍 **Depth when you want it.** One press opens the lineup behind a score;
  another shows your opponent's.
- ⚙️ **Nothing to sign up for.** Enter your Sleeper username, tick your leagues,
  done.

**Worth knowing**

- 🟣 **Sleeper only** for now
- 📊 **The win odds are this app's model**, not Sleeper's own number — live score
  plus projections for the starters yet to play. It tracks the same direction,
  including the weeks where the better record is the underdog, but it won't
  match their figure exactly
- ⌚ **Built for Pebble Time 2**
- 🔋 **Background refresh is off by default**, because waking the watch briefly
  opens the app
- ℹ️ Not affiliated with Sleeper. Their API is free for non-commercial use

## Category
daily

## Release notes (1.0.0)
First release. Sleeper support, live scoring, projection-based win odds, and a
lineup view with your opponent a button press away.

## Icons
- `icon_80x80.png` (small)
- `icon_144x144.png` (large)

Regenerate with `python3 tools/make_icons.py`. They are drawn rather than cropped
from a screenshot: at 80x80 a slice of the UI is an unreadable smudge. The
football says what the app is for, and the red/green bar is the app's own
win-probability bar in the same two colours the watch draws it with.

## Banner
`banner_720x320.png` — regenerate with `python3 tools/make_banner.py`.

Built from the real screenshots rather than mock-ups, so the banner cannot
drift from what the app actually looks like: change the screenshots, re-run the
script. The layout measures the title against the space the devices leave,
instead of hard-coding coordinates that silently break when a string changes.
