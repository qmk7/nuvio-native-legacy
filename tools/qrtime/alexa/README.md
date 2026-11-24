# QR TIME for Alexa

"Alexa, play Inception on TV" → the TV wakes up, QR TIME opens and plays it.

```
Echo ──> your private Alexa skill (Smart Home, development stage)
     ──> AWS Lambda  (lambda_function.py)
     ──> Home Assistant webhook, reached through Tailscale Funnel (free)
     ──> Wake-on-LAN + open QR TIME + POST <settings link>/tocar
```

Nothing here is public: the skill stays in development (only your Amazon
account sees it), the webhook id is the shared secret, and the TV link only
answers on your home network.

## 1. Home Assistant (at home)

1. **LG webOS TV** integration: Settings › Devices & services (usually found
   by itself). Note the TV's entity id (e.g. `media_player.lg_webos_tv`).
2. On the TV: **Always Ready** / "Turn on via Wi-Fi" on, so Wake-on-LAN works.
3. Copy `qrtime_alexa.yaml` to `/config/packages/` and add to
   `configuration.yaml`:
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```
4. In `secrets.yaml` add `qrtime_link_tocar`, `qrtime_link_tecla`,
   `qrtime_tv_mac`, `qrtime_webhook_id` (see the top of the yaml).
   The link is in QR TIME: Settings › Accounts › Phone › "TV settings from
   the phone". The webhook id: any long random string.
5. Restart Home Assistant.
6. **Tailscale** add-on (Settings › Add-ons › Add-on store › Tailscale):
   install, log in, and turn on **Funnel** for port 8123 in its options. Your
   address is `https://<name>.<tailnet>.ts.net`.
7. Test from a phone on mobile data:
   `curl -X POST -H 'Content-Type: application/json' -d '{"acao":"tocar","q":"Inception"}' https://<name>.<tailnet>.ts.net/api/webhook/<webhook id>`

## 2. AWS Lambda

1. AWS console, region **N. Virginia (us-east-1)** for English (US); if the
   Alexa console lists another region for your language, use that one.
2. Lambda › Create function › Author from scratch › Python 3.12 › name
   `qrtime-alexa`. Paste `lambda_function.py`, Deploy.
3. Configuration › Environment variables:
   - `HA_WEBHOOK_URL` = `https://<name>.<tailnet>.ts.net/api/webhook/<webhook id>`
   - `DEVICE_NAME` = `TV` (what you say to Alexa)
4. Copy the function's ARN (top right).

## 3. The Alexa skill (developer.amazon.com, same account as your Echo)

1. Alexa Developer Console › Create Skill › name `QR TIME` › type
   **Smart Home** › languages: English (and Arabic if offered) › Create.
2. Smart Home service endpoint › Default endpoint = the Lambda ARN. Copy the
   **Skill ID** shown there.
3. Back in Lambda: Add trigger › **Alexa** › Alexa Smart Home › paste the
   Skill ID.
4. Account linking (required by Amazon for Smart Home), using **Login with
   Amazon** so no server of yours is needed:
   - developer.amazon.com › Login with Amazon › Create a security profile →
     copy its Client ID and Client Secret; in its Web settings add the three
     "Alexa Redirect URLs" the skill's Account Linking page shows.
   - Skill › Account Linking: Authorization URI
     `https://www.amazon.com/ap/oa`, Access Token URI
     `https://api.amazon.com/auth/o2/token`, the Client ID/Secret above,
     scope `profile:user_id`, Save.
5. Alexa app on the phone › More › Skills & Games › Your Skills › **Dev** ›
   QR TIME › Enable, sign in, then **Discover devices**. A device called
   "TV" appears. If LG's own skill already shows a "TV", rename one of them.

## What you can say

- "Alexa, play Inception on TV" / "Alexa, watch Breaking Bad on TV"
- "Alexa, turn on the TV" / "Alexa, turn off the TV"
- "Alexa, pause TV" / "Alexa, resume TV"

Which words Alexa accepts in Arabic depends on Amazon's support for this
interface in your Alexa language; English always works.
