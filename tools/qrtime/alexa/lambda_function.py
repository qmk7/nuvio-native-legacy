"""QR TIME for Alexa: a private Smart Home skill (development stage, the
owner's Amazon account only) that shows the TV to Alexa as one device and
forwards what is asked to Home Assistant at home:

  "Alexa, play Inception on TV"        -> Alexa.RemoteVideoPlayer SearchAndPlay
  "Alexa, turn on / turn off the TV"   -> Alexa.PowerController
  "Alexa, pause / resume the TV"       -> Alexa.PlaybackController
  "Alexa, open YouTube on TV"          -> Alexa.Launcher (any app on the TV)
  "Alexa, volume up / mute the TV"     -> Alexa.StepSpeaker

MUSIC (optional, once Music Assistant runs in Home Assistant): each entry of
MUSIC_DEVICES ("Soundbar=soundbar,All speakers=casa") becomes one more Alexa
device; "Alexa, play Fairuz on Soundbar" posts {"acao": "musica",
"onde": "soundbar", "q": "Fairuz"} and Home Assistant plays it from YouTube
Music on the speakers it maps "soundbar" to.

Home Assistant (tools/qrtime/alexa/qrtime_alexa.yaml) wakes the TV
(Wake-on-LAN), opens QR TIME and asks it to play the title through the
settings page's fixed link (POST <link>/tocar). Nothing is stored here.

AWS Lambda, Python 3.12, no packages. Environment variables:
  HA_WEBHOOK_URL   https://<your-tailscale-funnel-name>/api/webhook/<webhook id>
  DEVICE_NAME      the name Alexa shows and you say ("TV" by default)
Account linking uses Login with Amazon (see README.md): the token Alexa
sends is not used here; the webhook id is the shared secret.
"""
import json
import os
import time
import urllib.request
import uuid

ENDPOINT_ID = "qrtime-tv"


def _ha(payload):
    url = os.environ.get("HA_WEBHOOK_URL", "")
    if not url.startswith("https://"):
        print("HA_WEBHOOK_URL missing or not https")
        return False
    req = urllib.request.Request(url, data=json.dumps(payload).encode(), method="POST",
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=6) as r:
            return 200 <= r.status < 300
    except Exception as e:  # network or HA down: Alexa says the device is not responding
        print("home assistant: %s" % type(e).__name__)
        return False


def _hdr(namespace, name, correlation=None):
    h = {"namespace": namespace, "name": name, "payloadVersion": "3", "messageId": str(uuid.uuid4())}
    if correlation:
        h["correlationToken"] = correlation
    return h


def _now():
    return time.strftime("%Y-%m-%dT%H:%M:%S.00Z", time.gmtime())


def _ok(directive, props=None):
    return {
        "event": {
            "header": _hdr("Alexa", "Response", directive["header"].get("correlationToken")),
            "endpoint": {"endpointId": directive.get("endpoint", {}).get("endpointId", ENDPOINT_ID)},
            "payload": {},
        },
        "context": {"properties": props or []},
    }


def _erro(directive, tipo="ENDPOINT_UNREACHABLE", msg="Home Assistant did not answer"):
    return {
        "event": {
            "header": _hdr("Alexa", "ErrorResponse", directive["header"].get("correlationToken")),
            "endpoint": {"endpointId": directive.get("endpoint", {}).get("endpointId", ENDPOINT_ID)},
            "payload": {"type": tipo, "message": msg},
        }
    }


MUSICA = "qrtime-musica-"


def _musicas():
    """[(alexa name, key)] from MUSIC_DEVICES="Soundbar=soundbar,All speakers=casa"."""
    r = []
    for par in os.environ.get("MUSIC_DEVICES", "").split(","):
        nome, _, chave = par.partition("=")
        nome, chave = nome.strip(), "".join(c for c in chave.strip().lower() if c.isalnum())
        if nome and chave:
            r.append((nome, chave))
    return r


def _discovery(directive):
    nome = os.environ.get("DEVICE_NAME", "TV")
    cap = lambda iface, **kw: dict({"type": "AlexaInterface", "interface": iface, "version": "3"}, **kw)
    musica = [{
        "endpointId": MUSICA + chave,
        "manufacturerName": "QR TIME",
        "friendlyName": n,
        "description": "Music from YouTube Music through Home Assistant",
        "displayCategories": ["SPEAKER"],
        "capabilities": [
            cap("Alexa"),
            cap("Alexa.PowerController", properties={"supported": [{"name": "powerState"}], "retrievable": False}),
            cap("Alexa.RemoteVideoPlayer", configuration={"operations": ["SearchAndPlay"]}),
            cap("Alexa.PlaybackController", supportedOperations=["Play", "Pause", "Stop", "Next", "Previous"]),
            cap("Alexa.StepSpeaker"),
            cap("Alexa.EndpointHealth", properties={"supported": [{"name": "connectivity"}], "retrievable": False}),
        ],
    } for n, chave in _musicas()]
    return {
        "event": {
            "header": _hdr("Alexa.Discovery", "Discover.Response"),
            "payload": {"endpoints": musica + [{
                "endpointId": ENDPOINT_ID,
                "manufacturerName": "QR TIME",
                "friendlyName": nome,
                "description": "QR TIME on the LG TV",
                "displayCategories": ["TV"],
                "capabilities": [
                    cap("Alexa"),
                    cap("Alexa.PowerController", properties={"supported": [{"name": "powerState"}], "retrievable": False}),
                    cap("Alexa.RemoteVideoPlayer", configuration={"operations": ["SearchAndPlay"]}),
                    cap("Alexa.PlaybackController", supportedOperations=["Play", "Pause", "Stop"]),
                    cap("Alexa.Launcher"),
                    cap("Alexa.StepSpeaker"),
                    cap("Alexa.EndpointHealth", properties={"supported": [{"name": "connectivity"}], "retrievable": False}),
                ],
            }]},
        }
    }


def _titulo(payload):
    """The title Alexa understood: the Video entity first, then any other."""
    ents = payload.get("entities") or []
    for tipo in ("Video", "Franchise", "Episode", "Season"):
        for e in ents:
            if e.get("type") == tipo and e.get("value"):
                return e["value"], tipo
    for e in ents:
        if e.get("value"):
            return e["value"], e.get("type", "")
    return "", ""


def _musica(d, ns, name, onde):
    """A music device: everything goes to Home Assistant with where to play."""
    pl = d.get("payload") or {}
    msg = None
    if ns == "Alexa.RemoteVideoPlayer":
        q, tipo = _titulo(pl)
        if not q:
            return _erro(d, "INVALID_VALUE", "No title")
        msg = {"acao": "musica", "onde": onde, "q": q}
    elif ns == "Alexa.PlaybackController":
        k = {"Play": "play", "Pause": "pause", "Stop": "stop", "Next": "next", "Previous": "previous"}.get(name)
        msg = k and {"acao": "musica_tecla", "onde": onde, "k": k}
    elif ns == "Alexa.StepSpeaker" and name == "AdjustVolume":
        msg = {"acao": "musica_volume", "onde": onde, "passos": int(pl.get("volumeSteps") or 0)}
    elif ns == "Alexa.StepSpeaker" and name == "SetMute":
        msg = {"acao": "musica_tecla", "onde": onde, "k": "pause" if pl.get("mute") else "play"}
    elif ns == "Alexa.PowerController":
        msg = {"acao": "musica_tecla", "onde": onde, "k": "play" if name == "TurnOn" else "stop"}
    elif ns == "Alexa" and name == "ReportState":
        r = _ok(d)
        r["event"]["header"]["name"] = "StateReport"
        return r
    if not msg:
        return _erro(d, "INVALID_DIRECTIVE", "Not supported")
    if not _ha(msg):
        return _erro(d)
    if ns == "Alexa.PowerController":
        return _ok(d, [{"namespace": "Alexa.PowerController", "name": "powerState", "value": "ON" if name == "TurnOn" else "OFF",
                        "timeOfSample": _now(), "uncertaintyInMilliseconds": 500}])
    return _ok(d)


# ---- the custom "YouTube Music" voice skill (same Lambda, second skill) ----
# "Alexa, ask YouTube Music to play Fairuz on the soundbar": a custom skill
# (interaction model: skill_music_model.json) whose intents name the speaker;
# the song goes to Home Assistant like the smart-home music devices do.
ONDE_POR_INTENT = {
    "TocarIntent": "soundbar",
    "TocarSoundbarIntent": "soundbar",
    "TocarGoogleIntent": "google",
    "TocarTodasIntent": "casa",
}
TECLA_POR_INTENT = {
    "AMAZON.PauseIntent": "pause", "AMAZON.StopIntent": "stop", "AMAZON.CancelIntent": "stop",
    "AMAZON.ResumeIntent": "play", "AMAZON.NextIntent": "next", "AMAZON.PreviousIntent": "previous",
}
FALA_ONDE = {"soundbar": "the soundbar", "google": "the Google speakers", "casa": "all speakers"}


def _fala(texto, fim=True):
    return {"version": "1.0", "response": {
        "outputSpeech": {"type": "PlainText", "text": texto}, "shouldEndSession": fim}}


def _skill(event):
    req = event.get("request", {})
    tipo = req.get("type")
    print("skill %s %s" % (tipo, (req.get("intent") or {}).get("name", "")))
    if tipo == "LaunchRequest":
        return _fala("What should I play?", fim=False)
    if tipo != "IntentRequest":
        return _fala("", fim=True)
    intent = req.get("intent") or {}
    nome = intent.get("name", "")
    if nome in ONDE_POR_INTENT:
        q = ((intent.get("slots") or {}).get("q") or {}).get("value") or ""
        if not q:
            return _fala("What should I play?", fim=False)
        onde = ONDE_POR_INTENT[nome]
        if not _ha({"acao": "musica", "onde": onde, "q": q}):
            return _fala("Home Assistant did not answer.")
        return _fala("Playing %s on %s." % (q, FALA_ONDE[onde]))
    if nome in TECLA_POR_INTENT:
        _ha({"acao": "musica_tecla", "onde": "casa", "k": TECLA_POR_INTENT[nome]})
        return _fala("OK.")
    if nome == "AMAZON.HelpIntent":
        return _fala("Say, play a song or an artist, on the soundbar, the Google speakers, or all speakers.", fim=False)
    return _fala("Sorry, I can't do that yet.")


def lambda_handler(event, context):
    if "request" in event and "directive" not in event:
        return _skill(event)
    d = event.get("directive", {})
    ns, name = d.get("header", {}).get("namespace"), d.get("header", {}).get("name")
    print("directive %s.%s" % (ns, name))  # never the payload: it may carry tokens
    if ns == "Alexa.Discovery":
        return _discovery(d)
    if ns == "Alexa.Authorization" and name == "AcceptGrant":
        return {"event": {"header": _hdr("Alexa.Authorization", "AcceptGrant.Response"), "payload": {}}}
    ep = d.get("endpoint", {}).get("endpointId", "")
    if ep.startswith(MUSICA):
        return _musica(d, ns, name, ep[len(MUSICA):])
    if ns == "Alexa.PowerController":
        on = name == "TurnOn"
        if not _ha({"acao": "ligar" if on else "desligar"}):
            return _erro(d)
        return _ok(d, [{"namespace": "Alexa.PowerController", "name": "powerState", "value": "ON" if on else "OFF",
                        "timeOfSample": _now(), "uncertaintyInMilliseconds": 500}])
    if ns == "Alexa.RemoteVideoPlayer":
        q, tipo = _titulo(d.get("payload", {}))
        if not q:
            return _erro(d, "INVALID_VALUE", "No title")
        if not _ha({"acao": "tocar", "q": q, "tipo": "serie" if tipo in ("Episode", "Season") else ""}):
            return _erro(d)
        return _ok(d)
    if ns == "Alexa.PlaybackController":
        tecla = {"Play": "play", "Pause": "pause", "Stop": "stop"}.get(name)
        if tecla and not _ha({"acao": "tecla", "k": tecla}):
            return _erro(d)
        return _ok(d)
    if ns == "Alexa.Launcher" and name == "LaunchTarget":
        app = (d.get("payload") or {}).get("name") or ""
        if not app:
            return _erro(d, "INVALID_VALUE", "No app")
        if not _ha({"acao": "abrir", "app": app}):
            return _erro(d)
        return _ok(d)
    if ns == "Alexa.StepSpeaker":
        pl = d.get("payload") or {}
        if name == "AdjustVolume":
            msg = {"acao": "volume", "passos": int(pl.get("volumeSteps") or 0)}
        elif name == "SetMute":
            msg = {"acao": "mudo", "mudo": bool(pl.get("mute"))}
        else:
            return _erro(d, "INVALID_DIRECTIVE", "Not supported")
        if not _ha(msg):
            return _erro(d)
        return _ok(d)
    if ns == "Alexa" and name == "ReportState":
        r = _ok(d)
        r["event"]["header"]["name"] = "StateReport"
        return r
    return _erro(d, "INVALID_DIRECTIVE", "Not supported")
