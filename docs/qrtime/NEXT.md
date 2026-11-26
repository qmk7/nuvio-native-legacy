# Where things stand (for the next session)

## Released to the TV
- 2.1.52: Music Assistant in Music (list design), phone setup page for its address + token.
- Alexa / Home Assistant: TV device (play titles, apps, volume, on/off), fixed link ports 8098/18098/28098/38098, profile picker skipped for voice requests, the account's own titles first.

## Released since (2.1.67 and later)
- Music Assistant home "option 3" released; Music IS Music Assistant once connected (all sections from it, Genres/Offline/Queue hidden);
  Magic Remote on its pages; rows glide; home rows load in parallel; a dropped first call is retried; volume keys -> speaker (if webOS hands them over).
- Arabic titles said in English (casaha_normalizar + q2) released.
- Open: owner's Alexa shows two "TV" devices (ours + LG's own): rename LG's to "LG" or ours to "QR TIME".

## Built, NOT released (waiting for the owner)
- Music Assistant home "option 3": now-playing card (cover, progress, controls, speaker) + rows of covers (recent, playlists, artists, albums, "See all"). Commit "design(music): Music Assistant home as option 3". Release = a commit with [ipk].
- Checked only against tests/qrtime_ma_mock.py (tests/qrtime_ma_ui.sh), not against the owner's real Music Assistant.

## Owner's to-do (outside the code)
- Music Assistant: create a long-lived token, enter it + http://192.168.100.104:8095 on the phone music setup page.
- Alexa "QR Music" skill: Claude key on the Lambda (ANTHROPIC_API_KEY), timeout 10 s, memory 512 MB, upload the latest zip (lambda_function.py + anthropic package), paste skill_music_model.json, new qrtime_music script and the 3 lines in the automation (tools/qrtime/alexa/ha/).

## Open questions
- The real Music Assistant's command names (search, library_items, play_media, players/all current_media) are from its source, not its docs: verify on first real use.
- Alexa+ sends "play X on <device>" to its own devices: music by voice only through the QR Music skill ("from QR Music" / "ask QR Music").
- YouTube playback inside QR TIME: owner has not chosen an option.

## Ideas the owner picked (Home Assistant + Alexa + QR TIME)
Phase 1 (voice control of QR TIME, all through the TV device / QR Music skill):
- 2 "continue on TV" (newest unfinished), 16 next episode / skip intro / rewind 30 s,
  17 subtitles / audio track by voice, 3 sleep timer by voice, 27 "goodnight" (stop TV + music everywhere, remember where).
Phase 2 (music):
- 7 move the music to another room, 21 "like this song" (YouTube Music favourite),
  23 lyrics of whatever plays (any speaker) on the TV, 20 the film's soundtrack after the film,
  22 party mode (all speakers + big now-playing with lyrics on the TV + guests' requests).
Phase 3 (home):
- 1 cinema mode (lights, TV, soundbar, QR TIME; lights up a little on pause; needs smart lights),
  10 doorbell on the TV (pause, door picture, Echo announces; EZVIZ), 26 small notices over the film
  (washer done, door...), 12 guest mode (guest profile + scene).
Phase 4 (smarter):
- 15 "recommend something" by voice, 18 "what should I watch" spoken summary,
  9 "what's this song" saved to YouTube Music, 19 move the film to another TV (needs a second TV).

Owner's devices for phase 3: doorbell EZVIZ (QR TIME already has ezviz.c alerts), lights Tuya (Smart Life):
add the Tuya integration to Home Assistant (Settings > Devices & services > Tuya, sign in with the Smart Life app's QR) before cinema mode.

More picked ideas: 29 "who is this actor?" (paused scene: cast on screen), 31 "what happened last episode?" (spoiler-free recap spoken),
32 "add X to my list" -> Trakt watchlist (trakt_watchlist(imdb, 1) exists in trakt.c; new TV-link route "lista" + Lambda phrase;
title resolved like casaha.c), 35 "play what I listened to yesterday" (Music Assistant recently played at that hour).
Also picked: 50 "who sings this?" (Music Assistant now-playing of any speaker spoken by Alexa; shown on the TV when it is on).
Also picked (all): 51 live channel by voice (Xtream), 52 latest episode, 53 random episode, 54 trailer,
55 search by actor / genre on screen, 56 something short (< 25 min), 57 "what is it about" (spoiler-free),
58 "is it worth watching" (IMDb / RT), 59 next season / episode date (Trakt), 60 YouTube by voice (official app, search / video).

## QR-Yummy (cooking app) + Alexa
qmk7/QR-Yummy: PWA on Cloudflare Workers. Personal recipes live only on the phone; "family books" are on the server
(Durable Object FamilyBook, SQLite, /api/family/<secret id>), and the worker already calls Claude (/api/ask, recipe-ai.js).
Plan: an Alexa custom skill whose HTTPS endpoint is the Worker itself (/api/alexa, no Lambda), reading a family book:
"ask my kitchen how to make kabsa", "next step", timers, "what can I cook with...". Recipes to use by voice go in a family book.
Owner wants everything in the cloud, not only family books: QR-Yummy already has «مكتبة البيت» (Household Durable Object:
books, recipes, shopping list 'shop', week plan 'week'). Make the personal book live there too, and let Alexa ADD:
"add milk to my shopping list" (shop), "add kabsa to Thursday" (week), "save a recipe ..." (recipe, Claude structures it);
the app pulls the change on its next sync.
Owner chose: Alexa's own shopping list synced into QR-Yummy (skill with list permissions + AlexaHouseholdListEvent
ItemsCreated -> read the item via the List Management API -> add to the Household 'shop' list in Arabic (Claude translates)
-> delete it from Alexa's list). "add cucumber to my shopping list" works without the skill name. Verify list events under Alexa+ first.
Also keep "ask my kitchen ..." for recipes / week plan.
TV side (casaha q2 + normalizar) built but not released: ship with the next [ipk].
