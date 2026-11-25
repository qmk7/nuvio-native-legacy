# The owner's Home Assistant setup, as it runs today

Everything Alexa and QR TIME need on the Home Assistant Green, in one place.
Copy-paste sources:

| What | Where in Home Assistant | File |
|---|---|---|
| `rest_command.qrtime` + `wake_on_lan` | `configuration.yaml` (end of file), then Restart | [configuration.yaml](configuration.yaml) |
| Automation **QR TIME: Alexa** (webhook) | Settings › Automations › ⋮ › Edit in YAML | [automation-qrtime-alexa.yaml](automation-qrtime-alexa.yaml) |
| Script **qrtime_music** | Settings › Automations › Scripts › ⋮ › Edit in YAML | [script-qrtime-music.yaml](script-qrtime-music.yaml) |

What each piece does:

- **Alexa smart-home skill "QR TIME"** (AWS Lambda `qrtime-alexa`, us-east-1) shows a
  device **TV**: play a title in QR TIME, open any TV app, volume, mute,
  pause, on/off. With `MUSIC_DEVICES` it also shows music devices.
- **Alexa custom skill "QR Music"** (same Lambda, model `skill_music_model.json`):
  "Alexa, play Fairuz from QR Music", "Alexa, ask QR Music to play something calm
  in the bedroom". With `ANTHROPIC_API_KEY` on the Lambda, Claude (Haiku 5.5)
  reads the free sentence: search, room / speaker, radio, volume.
- The Lambda posts JSON to the webhook (Tailscale Funnel address); the
  automation wakes the TV, opens QR TIME and posts to the TV's settings link
  (ports 8098, 18098, 28098, 38098), or hands music to the script.
- The script plays from YouTube Music through **Music Assistant** on the
  speakers of the room (Home Assistant Areas) or the speaker named; "all"
  uses the Google group **All speakers**.

Lambda environment: `HA_WEBHOOK_URL`, `DEVICE_NAME=TV`,
`MUSIC_DEVICES=Soundbar=soundbar,Google speakers=google,All speakers=casa`,
`ANTHROPIC_API_KEY` (optional), `CLAUDE_MODEL` (optional). Timeout 10 s,
memory 512 MB. The upload zip = `lambda_function.py` + the `anthropic`
package for the Lambda's Python.

Secrets are not in these files: the webhook id and the TV link code are the
owner's own values (placeholders below), the Claude key lives only in AWS.
