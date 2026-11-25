# Where things stand (for the next session)

## Released to the TV
- 2.1.52: Music Assistant in Music (list design), phone setup page for its address + token.
- Alexa / Home Assistant: TV device (play titles, apps, volume, on/off), fixed link ports 8098/18098/28098/38098, profile picker skipped for voice requests, the account's own titles first.

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
