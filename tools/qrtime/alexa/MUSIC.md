# Music by voice: "Alexa, play Fairuz on Soundbar"

```
Echo ──> QR TIME skill (a "Soundbar" / "All speakers" device)
     ──> AWS Lambda ──> Home Assistant webhook (the same one as the TV)
     ──> script "qrtime_music" ──> Music Assistant ──> YouTube Music
     ──> the speakers you mapped
```

## 1. Music Assistant (at home, once)
1. Home Assistant › Settings › Apps (Add-ons) › store › **Music Assistant** ›
   Install › Start › Show in sidebar.
2. Open it › Settings › **Providers**:
   - Music: **YouTube Music** (sign in as its page explains).
   - Players: whatever it finds (Chromecast, AirPlay, DLNA, Alexa…).
3. Settings › **Players**: note each speaker. For "all speakers" make a
   **sync group** of them (Players › Add group).
4. Home Assistant › Settings › Devices & services: the **Music Assistant**
   integration appears; its players are `media_player.*` entities.

## 2. The script
Settings › Automations & scenes › Scripts › Create script › ⋮ › Edit in YAML,
paste, fill the `media_player.` names from step 1.4, Save:

```yaml
alias: qrtime_music
mode: queued
fields:
  acao: {}
  onde: {}
  q: {}
  k: {}
  passos: {}
sequence:
  - variables:
      alvo: >-
        {{ {'soundbar': 'media_player.SOUNDBAR_FROM_MUSIC_ASSISTANT',
            'casa': 'media_player.SYNC_GROUP_FROM_MUSIC_ASSISTANT'}.get(onde, '') }}
  - condition: template
    value_template: "{{ alvo != '' }}"
  - choose:
      - conditions: "{{ acao == 'musica' }}"
        sequence:
          - action: music_assistant.play_media
            target:
              entity_id: "{{ alvo }}"
            data:
              media_id: "{{ q }}"
              enqueue: replace
              radio_mode: true
      - conditions: "{{ acao == 'musica_volume' }}"
        sequence:
          - repeat:
              count: "{{ [ (passos | int(0)) | abs, 20 ] | min }}"
              sequence:
                - action: "{{ 'media_player.volume_up' if (passos | int(0)) > 0 else 'media_player.volume_down' }}"
                  target:
                    entity_id: "{{ alvo }}"
      - conditions: "{{ acao == 'musica_tecla' }}"
        sequence:
          - action: >-
              {{ {'pause': 'media_player.media_pause', 'stop': 'media_player.media_stop',
                  'next': 'media_player.media_next_track',
                  'previous': 'media_player.media_previous_track'}.get(k, 'media_player.media_play') }}
            target:
              entity_id: "{{ alvo }}"
```

## 3. The TV automation hands music over
In "QR TIME: Alexa" (Edit in YAML), right before the step that says
`stop: "done"`, add:

```yaml
  - if:
      - condition: template
        value_template: "{{ acao.startswith('musica') }}"
    then:
      - action: script.qrtime_music
        data:
          acao: "{{ acao }}"
          onde: "{{ trigger.json.onde | default('') }}"
          q: "{{ trigger.json.q | default('') }}"
          k: "{{ trigger.json.k | default('') }}"
          passos: "{{ trigger.json.passos | default(0) }}"
      - stop: "music"
```

## 4. The Lambda
Configuration › Environment variables › add
`MUSIC_DEVICES` = `Soundbar=soundbar,All speakers=casa`
then "Alexa, discover devices".

## What you can say
- "Alexa, play Fairuz on Soundbar" / "Alexa, play Despacito on All speakers"
- "Alexa, next on Soundbar", "Alexa, pause All speakers", "Alexa, volume up on Soundbar"

Whether Alexa sends a song request to a device like this, instead of
answering with its own music service, is Amazon's call: try it once set up.
