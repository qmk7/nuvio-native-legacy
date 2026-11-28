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

THE REST OF THE HOUSE (optional): with HA_TOKEN set and "alexa: smart_home:"
in Home Assistant, every light, switch, AC, fan, cover and scene Home Assistant
exposes joins this skill at discovery ("Alexa, turn off the kitchen light").

AWS Lambda, Python 3.12. Environment variables:
  HA_WEBHOOK_URL   https://<your-tailscale-funnel-name>/api/webhook/<webhook id>
  DEVICE_NAME      the name Alexa shows and you say ("TV" by default)
  HA_TOKEN         Home Assistant long-lived token (house devices; optional)
  HA_URL           Home Assistant address, by default the webhook's
Account linking uses Login with Amazon (see README.md): the token Alexa
sends is not used here; the webhook id is the shared secret.
"""
import json
import re
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


# ---- the rest of the house: Home Assistant as the brain ----
# Home Assistant's own Alexa Smart Home endpoint (alexa: smart_home: in its
# configuration.yaml) turns every exposed light, switch, AC, fan, cover and
# scene into Alexa endpoints. This skill adds them to its own TV and speakers at
# discovery and forwards their directives as they are, so one skill runs the
# whole house. HA_TOKEN is a Home Assistant long-lived token; it only travels in
# the Authorization header and is never printed.
def _casa_url():
    base = os.environ.get("HA_URL", "")
    if not base:
        w = os.environ.get("HA_WEBHOOK_URL", "")
        i = w.find("/api/webhook/")
        base = w[:i] if i > 0 else ""
    base = base.rstrip("/")
    return base + "/api/alexa/smart_home" if base.startswith("https://") else ""


def _casa(event):
    """Home Assistant's answer to a Smart Home directive, None when not set up or down."""
    url, tok = _casa_url(), os.environ.get("HA_TOKEN", "")
    if not url or not tok:
        return None
    req = urllib.request.Request(url, data=json.dumps(event).encode(), method="POST",
                                 headers={"Content-Type": "application/json", "Authorization": "Bearer " + tok})
    try:
        with urllib.request.urlopen(req, timeout=7) as r:
            return json.loads(r.read().decode())
    except Exception as e:
        print("home assistant devices: %s" % type(e).__name__)
        return None


def _nosso(ep):
    return ep == ENDPOINT_ID or ep.startswith(MUSICA)


def _juntar_casa(resp, event):
    """Discovery: this skill's endpoints plus Home Assistant's, never the same name twice."""
    casa = _casa(event)
    if not casa:
        return resp
    eps = resp["event"]["payload"]["endpoints"]
    nomes = {e.get("friendlyName", "").strip().lower() for e in eps}
    ids = {e.get("endpointId") for e in eps}
    novos = 0
    for e in (casa.get("event", {}).get("payload", {}).get("endpoints") or []):
        n = e.get("friendlyName", "").strip().lower()
        if not e.get("endpointId") or e["endpointId"] in ids or _nosso(e["endpointId"]) or n in nomes:
            continue
        if len(eps) >= 300:   # Alexa's limit per skill
            break
        eps.append(e)
        nomes.add(n)
        ids.add(e["endpointId"])
        novos += 1
    print("discovery: %d from Home Assistant" % novos)
    return resp


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
FALA_ONDE = {"soundbar": "the soundbar", "kitchen": "the kitchen", "bedroom": "the bedroom", "google": "the Google speakers", "casa": "all speakers"}


# ---- understanding free speech with Claude (optional) ----
# With ANTHROPIC_API_KEY set on the Lambda (and the "anthropic" package in the
# upload zip), whatever was said after "play" is read by Claude: what to
# search on YouTube Music, which speakers, radio or not, a volume. Without the
# key (or if Claude is slow or fails) the words go to Music Assistant as said.
MODELO = os.environ.get("CLAUDE_MODEL", "claude-haiku-5-5")  # fast: Alexa waits 8 s
ESQUEMA = {
    "type": "object",
    "properties": {
        "midia": {"type": "string", "enum": ["music", "video"], "description": "video only for a film or a TV series/show to watch; music otherwise or when unsure."},
        "busca": {"type": "string", "description": "What to search on YouTube Music: artist, song, album, playlist or a mood turned into a concrete search."},
        "tipo": {"type": "string", "enum": ["track", "artist", "album", "playlist", "any"]},
        "onde": {"type": "string", "description": "Where: the room or speaker name the person said (e.g. bedroom, kitchen, soundbar), 'all' for every speaker, '' when none was said."},
        "radio": {"type": "boolean", "description": "Keep playing similar music after it."},
        "volume": {"type": "integer", "description": "0-100 when a loudness was asked for, else -1."},
        "resposta": {"type": "string", "description": "A short spoken confirmation in the request's language (Gulf Arabic when the request is Arabic)."},
    },
    "required": ["midia", "busca", "tipo", "onde", "radio", "volume", "resposta"],
    "additionalProperties": False,
}
SISTEMA = (
    "You turn a music request spoken to Alexa into a YouTube Music search for Music Assistant. "
    "For 'onde' copy the room or speaker the person named, in English, lower case (\"in the bedroom\" -> bedroom, "
    "\"المطبخ\" -> kitchen, \"on the soundbar\" -> soundbar); 'all' for everywhere / whole house / all speakers / party; "
    "'' when no place was said. Home Assistant matches it to the speakers' current rooms and names. "
    "Turn moods and descriptions into a concrete search (\"something calm to sleep\" -> a calm sleep playlist; "
    "\"the song from Titanic\" -> My Heart Will Go On Celine Dion). Arabic artists keep their usual Latin spelling. "
    "midia: 'video' when the words name a film or a TV series/show to watch (MobLand, Friends, Inception, "
    "Bab Al-Hara) and no speaker or room was said, or when the TV was named; then busca is just the title. "
    "Songs, artists, albums, moods and anything that could be either (Titanic) are 'music'. "
    "radio is true unless one exact song was asked for. volume: 'quiet/low' about 20, 'loud' about 70, else -1."
)


def _entender(frase, onde_padrao):
    """Claude's reading of `frase`, or None (no key, no package, slow, refused)."""
    if not os.environ.get("ANTHROPIC_API_KEY"):
        return None
    try:
        import anthropic
    except ImportError:
        print("claude: anthropic package not in the zip")
        return None
    try:
        cli = anthropic.Anthropic(timeout=5.0, max_retries=0)  # Alexa waits 8 s for the whole answer
        pedido = dict(
            model=MODELO,
            max_tokens=1000,
            output_config={"effort": "low", "format": {"type": "json_schema", "schema": ESQUEMA}},
            system=SISTEMA,
            messages=[{"role": "user", "content": "Default speakers: %s.\nRequest: %s" % (onde_padrao, frase)}],
        )
        if "haiku" in MODELO:  # no server-side fallback for Haiku
            r = cli.messages.create(**pedido)
        else:
            r = cli.beta.messages.create(betas=["server-side-fallback-2026-07-01"], fallbacks="default", **pedido)
        if r.stop_reason == "refusal":
            print("claude: refusal")
            return None
        texto = next((b.text for b in r.content if b.type == "text"), "")
        d = json.loads(texto)
        if not d.get("busca"):
            return None
        return d
    except anthropic.APITimeoutError:
        print("claude: timeout")
    except anthropic.RateLimitError:
        print("claude: rate limited")
    except anthropic.APIStatusError as e:
        print("claude: status %s" % e.status_code)
    except anthropic.APIConnectionError:
        print("claude: connection")
    except (ValueError, StopIteration):
        print("claude: unreadable answer")
    return None


TITULO_ESQUEMA = {
    "type": "object",
    "properties": {
        "titulo": {"type": "string", "description": "The title as listed on IMDb (its usual English / Latin name)."},
        "arabe": {"type": "string", "description": "The Arabic title when the work is Arabic, else ''."},
        "tipo": {"type": "string", "enum": ["filme", "serie", ""]},
    },
    "required": ["titulo", "arabe", "tipo"],
    "additionalProperties": False,
}


def _titulo_ia(frase):
    """The film / series Alexa heard, as IMDb lists it (Arabic names spelled any way in English). None without Claude."""
    if not os.environ.get("ANTHROPIC_API_KEY"):
        return None
    try:
        import anthropic
    except ImportError:
        return None
    try:
        cli = anthropic.Anthropic(timeout=4.0, max_retries=0)
        r = cli.messages.create(
            model=MODELO, max_tokens=500,
            output_config={"effort": "low", "format": {"type": "json_schema", "schema": TITULO_ESQUEMA}},
            system="Someone asked a TV to play a film or series by voice; speech recognition wrote what they said. "
                   "Give the title as IMDb lists it, its Arabic title if it is an Arabic work, and whether it is a film or a series "
                   "(empty when unsure). If you do not recognise it, return the words as they are.",
            messages=[{"role": "user", "content": frase}],
        )
        if r.stop_reason == "refusal":
            return None
        return json.loads(next((b.text for b in r.content if b.type == "text"), "") or "null")
    except Exception as e:  # any failure: play with the words as heard
        print("claude title: %s" % type(e).__name__)
        return None


def _fala(texto, fim=True):
    return {"version": "1.0", "response": {
        "outputSpeech": {"type": "PlainText", "text": texto}, "shouldEndSession": fim}}


FALAS = {
    "o_que": ("What should I play?", "شو أشغّل لك؟"),
    "sem_ha": ("Home Assistant did not answer.", "ما ردّ هوم أسيستنت."),
    "ok": ("OK.", "تمام."),
    "tocando": ("Playing %s.", "أشغّل %s."),
    "ajuda": ("Say, play a song or an artist, in a room, or everywhere.", "قل: شغّل أغنية أو فنان، في غرفة، أو في كل البيت."),
    "nao": ("Sorry, I can't do that yet.", "آسف، ما أقدر أسوي هذا للحين."),
}


# "Alexa, play MobLand on TV from QR Music": Alexa+ keeps titles it knows from
# its own catalog and only turns the TV on, so the custom skill also takes
# "... on TV" and sends the title to QR TIME itself.
TV_FIM = re.compile(r"\s+(?:on|in)\s+(?:the\s+)?(?:tv|television|qr\s*time)\s*$|\s+(?:على|في|ع)\s*(?:ال)?(?:تلفزيون|تلفاز|شاشة|تي\s*في)\s*$", re.I)


def _tv(q):
    """The title when `q` asks for the TV, else ''."""
    m = TV_FIM.search(q or "")
    return q[:m.start()].strip() if m else ""


def _tocar_tv(q):
    msg = {"acao": "tocar", "q": q, "tipo": ""}
    ia = _titulo_ia(q)
    if ia and ia.get("titulo"):
        msg["q"], msg["q2"] = ia["titulo"], ia.get("arabe") or (q if ia["titulo"].lower() != q.lower() else "")
        msg["tipo"] = ia.get("tipo") or ""
    return msg


def _skill(event):
    req = event.get("request", {})
    tipo = req.get("type")
    ar = (req.get("locale") or "").startswith("ar")
    t = lambda k: FALAS[k][1 if ar else 0]
    print("skill %s %s" % (tipo, (req.get("intent") or {}).get("name", "")))
    if tipo == "LaunchRequest":
        return _fala(t("o_que"), fim=False)
    if tipo != "IntentRequest":
        return _fala("", fim=True)
    intent = req.get("intent") or {}
    nome = intent.get("name", "")
    if nome in ONDE_POR_INTENT:
        q = ((intent.get("slots") or {}).get("q") or {}).get("value") or ""
        if not q:
            return _fala(t("o_que"), fim=False)
        titulo = _tv(q)
        if titulo:
            msg = _tocar_tv(titulo)
            if not _ha(msg):
                return _fala(t("sem_ha"))
            return _fala(t("tocando") % msg["q"])
        onde = ONDE_POR_INTENT[nome]
        d = _entender(q, onde)
        lugar = ((d or {}).get("onde") or "").strip().lower()
        if d and d.get("midia") == "video" and lugar in ("", "tv", "television", "qr time"):
            msg = _tocar_tv((d.get("busca") or q).strip())
            if not _ha(msg):
                return _fala(t("sem_ha"))
            return _fala(t("tocando") % msg["q"])
        if d:
            lugar = (d.get("onde") or "").strip().lower()
            d["onde"] = "casa" if lugar in ("all", "everywhere") else (lugar or onde)
            msg = {"acao": "musica", "onde": d["onde"], "q": d["busca"], "radio": d["radio"]}
            if d["tipo"] != "any":
                msg["tipo"] = d["tipo"]
            if 0 <= d["volume"] <= 100:
                msg["volume"] = d["volume"]
            fala = d.get("resposta") or t("tocando") % d["busca"]
        else:
            msg = {"acao": "musica", "onde": onde, "q": q}
            fala = t("tocando") % q
        if not _ha(msg):
            return _fala(t("sem_ha"))
        return _fala(fala)
    if nome in TECLA_POR_INTENT:
        _ha({"acao": "musica_tecla", "onde": "casa", "k": TECLA_POR_INTENT[nome]})
        return _fala(t("ok"))
    if nome == "AMAZON.HelpIntent":
        return _fala(t("ajuda"), fim=False)
    return _fala(t("nao"))


def lambda_handler(event, context):
    if "request" in event and "directive" not in event:
        return _skill(event)
    d = event.get("directive", {})
    ns, name = d.get("header", {}).get("namespace"), d.get("header", {}).get("name")
    print("directive %s.%s" % (ns, name))  # never the payload: it may carry tokens
    if ns == "Alexa.Discovery":
        return _juntar_casa(_discovery(d), event)
    if ns == "Alexa.Authorization" and name == "AcceptGrant":
        return {"event": {"header": _hdr("Alexa.Authorization", "AcceptGrant.Response"), "payload": {}}}
    ep = d.get("endpoint", {}).get("endpointId", "")
    if ep and not _nosso(ep):   # a Home Assistant device: it answers for itself
        return _casa(event) or _erro(d)
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
        msg = {"acao": "tocar", "q": q, "tipo": "serie" if tipo in ("Episode", "Season") else ""}
        ia = _titulo_ia(q)
        if ia and ia.get("titulo"):
            msg["q"], msg["q2"] = ia["titulo"], ia.get("arabe") or (q if ia["titulo"].lower() != q.lower() else "")
            if not msg["tipo"] and ia.get("tipo"):
                msg["tipo"] = ia["tipo"]
        if not _ha(msg):
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
