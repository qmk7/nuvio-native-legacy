# QR TIME — backlog batch report

Everything here was built and tested on the Linux desktop build and checked
with the webOS ARM compiler. **Nothing in this batch has been tried on the
LG OLED B5 yet.** "Verified" below means: unit test and/or end-to-end run of
the desktop app. "Unverified" means it depends on the TV, a third-party
account or a host this build environment cannot reach.

## Done

| Feature | Where | Verified | Needs the TV / an account |
|---|---|---|---|
| Max-quality defaults (4K, DV, Atmos, quality-first source, 256 MB buffer) | data schema v2, once | first-start log + settings file | DV/Atmos output on the B5 |
| Sleep timer (15–120 min) | Settings › Screensaver; Control Center "Sleep" | real 15-min run, accelerated run | `turnOffScreen` on the B5 |
| Cinema mode (one switch, restores your values) | Control Center "Cinema" | on/off with screenshots | — |
| Listen only (screen off, sound on) | player button | unit test | `turnOffScreen` on the B5 |
| Skip intro/recap for good, per series | automatic after 2 manual skips | unit tests | — |
| Playback speed remembered per series | player | unit test | — |
| "Previously on" (synopses of the 2 previous episodes) | start of an episode after 4+ days | builds | needs episode synopses from the add-on |
| Guest mode (nothing recorded) | Control Center "Guest" | builds; gates in player save paths | — |
| Phone pages server (per-page tokens, Host/Origin checks) | Accounts › Phone | 17 security checks | inbound LAN connections on webOS |
| Phone remote (arrows, OK, back, play, type/search) | Accounts › Phone | end-to-end (curl → TV moved, search opened) | same |
| Music setup from the phone (Navidrome, WebDAV, Last.fm, Spotify, AudD) | Accounts › Phone | builds | your servers |
| Party mode (guest requests, approve on TV, vote to skip) | Accounts › Phone | 9 checks with stubbed search | your music source |
| Encrypted backup / restore of non-synced data | Accounts › Phone | PBKDF2/HMAC against RFC vectors, round trip, tamper | — |
| Profile photo from the phone | Accounts › Phone | builds | — |
| Unified health check: account server, TMDB (key type), ESPN, DeepL, AudD rows | Accounts › Providers | builds | live answers |
| Sports: automatic match reminders, goal notices (spoiler-safe), highlights search, Arabic team names, Arabic-guide matching | Settings › Live TV › Sport Center | 11 checks | YouTube search target on the B5 |
| Prayer times (22 cities, Umm al-Qura / official methods), notice or pause | Settings › This TV | within 1 min of NOAA formulas | — |
| Hijri date in the Agenda | Agenda | tabular calendar check | can differ by 1 day from Umm al-Qura |
| "What should I watch tonight?" | Control Center "Suggestion" | weights test + empty-home run | — |
| Morning greeting (weather, today's matches, new episodes) | Settings › Appearance | parser on a fixture | Open-Meteo quota was exhausted from this environment |
| Weekly summary (Friday evening) | automatic | 3 checks | — |
| LG launcher shortcuts (Sport Center, Music, Live TV) | Settings › This TV | builds | `addLaunchPoint` on the B5 |
| Arabic voice commands (play X, sport, music, live, suggestion, settings) | microphone in search | 11 checks | TV speech recognition |
| "What's this song?" (AudD) from the file's own audio | player button (with an AudD token) | AAC/E-AC-3 samples from real MKVs, ffprobe-checked | AudD account; MKV sources only |
| Automatic Arabic subtitles (DeepL) | player button (with a DeepL key) | request/answer/SRT checks | DeepL account |
| EZVIZ doorbell / camera alerts | Accounts › Phone › Home | parser checks on documented answers | EZVIZ developer app + doorbell |
| Better version (4K / DV) of films on your list | Settings › Playback | builds | your add-ons |

## Already in the app before this batch
- Real quality badge (decoder-reported resolution, DV/HDR10, Atmos, codec).
- Audio and subtitle choice remembered per series and per profile.
- Weekly match schedule with linked channels (Sport Center › Upcoming).
- YouTube Music browsing (official API) with playback in the YouTube app.

## Not done, and why
- **Spotify import without a key:** it means scraping Spotify's pages. The
  official Web API path (your own client id/secret) is kept and reachable
  from the phone page.
- **In-app YouTube playback:** needs impersonating another device to get
  around YouTube's protection.
- **Audio-chain test per Atmos direction:** needs Dolby-encoded test media
  the app cannot generate; use a Dolby Atmos demo in the YouTube app.
- **Night mode (dialogue compression after 23:00):** webOS gives apps no
  audio processing on the playback path; the Q990F has its own Night mode.
- **Continue on the phone:** would mean handing the phone a stream link
  that may carry account tokens, in formats (MKV, DV) phones often cannot play.
- **Rewind the last minutes of a live channel:** needs recording the live
  stream to disk while watching; not started.
- **"Add to YouTube Music" after song recognition:** needs YouTube write
  permission; the result card searches the YouTube app instead.
