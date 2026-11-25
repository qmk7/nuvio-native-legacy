"""QR TIME for Alexa: a private Smart Home skill (development stage, the
owner's Amazon account only) that shows the TV to Alexa as one device and
forwards what is asked to Home Assistant at home:

  "Alexa, play Inception on TV"        -> Alexa.RemoteVideoPlayer SearchAndPlay
  "Alexa, turn on / turn off the TV"   -> Alexa.PowerController
  "Alexa, pause / resume the TV"       -> Alexa.PlaybackController
  "Alexa, open YouTube on TV"          -> Alexa.Launcher (any app on the TV)
  "Alexa, volume up / mute the TV"     -> Alexa.StepSpeaker

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


def _discovery(directive):
    nome = os.environ.get("DEVICE_NAME", "TV")
    cap = lambda iface, **kw: dict({"type": "AlexaInterface", "interface": iface, "version": "3"}, **kw)
    return {
        "event": {
            "header": _hdr("Alexa.Discovery", "Discover.Response"),
            "payload": {"endpoints": [{
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


def lambda_handler(event, context):
    d = event.get("directive", {})
    ns, name = d.get("header", {}).get("namespace"), d.get("header", {}).get("name")
    print("directive %s.%s" % (ns, name))  # never the payload: it may carry tokens
    if ns == "Alexa.Discovery":
        return _discovery(d)
    if ns == "Alexa.Authorization" and name == "AcceptGrant":
        return {"event": {"header": _hdr("Alexa.Authorization", "AcceptGrant.Response"), "payload": {}}}
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
