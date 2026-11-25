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
