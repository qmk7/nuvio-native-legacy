#!/usr/bin/env python3
"""Local Navidrome-shaped Subsonic server + WebDAV folder for the Music Center
tests. It follows the Subsonic REST API 1.16.1 (+ OpenSubsonic fields) and
RFC 4918 PROPFIND; it proves our client against those shapes, not the real
servers. Audio is REAL files from the fixture dir (made by ffmpeg).

    qrtime_music_mock.py <port> <work dir> <audio dir>

Subsonic: user "qrtuser" / password "qrt pass+1" (token auth md5(pass+salt));
user "ldapuser" refuses tokens (error 41) and accepts the legacy p=enc:.
WebDAV: /dav/Music/... with Basic auth davuser / davpass.
Every request goes to <work>/requests.log with credentials REDACTED; the
tests grep that log and the app's for any secret.
"""
import hashlib, json, os, sys, threading, time, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]); WORK = sys.argv[2]; AUDIO = sys.argv[3]
USERS = {"qrtuser": "qrt pass+1", "ldapuser": "ldappw", "listener": "listenpw"}   # listener: no download role
DAVUSER, DAVPASS = "davuser", "davpass"
LOCK = threading.Lock()
STATE = {"starred": {"s3"}, "scrobbles": [], "playlists": {}, "next_pl": 1, "plays": {}}


def song(i, title, album, albumId, artist, artistId, track, f, dur, suffix, ctype, bitrate, rate, depth, ch, genre="Jazz", year=2024):
    return {"id": i, "parent": albumId, "isDir": False, "title": title, "album": album, "artist": artist,
            "track": track, "discNumber": 1, "year": year, "genre": genre, "coverArt": "al-" + albumId,
            "size": os.path.getsize(os.path.join(AUDIO, f)) if os.path.exists(os.path.join(AUDIO, f)) else 0,
            "contentType": ctype, "suffix": suffix, "duration": dur, "bitRate": bitrate, "path": "x/" + f,
            "albumId": albumId, "artistId": artistId, "type": "music", "isVideo": False,
            "samplingRate": rate, "bitDepth": depth, "channelCount": ch,
            "artists": [{"id": artistId, "name": artist}], "replayGain": {"trackGain": -6.5},
            "_file": f}


SONGS = [
    song("s1", "Blue Hour", "Night Sessions", "al1", "QRT Quartet", "ar1", 1, "a16.flac", 5, "flac", "audio/flac", 900, 44100, 16, 2),
    song("s2", "Midnight Line", "Night Sessions", "al1", "QRT Quartet", "ar1", 2, "a24.flac", 5, "flac", "audio/flac", 2900, 96000, 24, 2),
    song("s3", "Morning Tone", "Day Album", "al2", "Sun Trio", "ar2", 1, "a320.mp3", 5, "mp3", "audio/mpeg", 320, 44100, 0, 2, genre="Pop", year=2020),
    song("s4", "Surround Test", "Day Album", "al2", "Sun Trio", "ar2", 2, "a51.flac", 3, "flac", "audio/flac", 1500, 48000, 16, 6, genre="Pop", year=2020),
]
for k in range(5, 66):   # a long album for paging
    SONGS.append(song("s%d" % k, "Track %02d" % k, "Big Box", "al3", "Many Songs", "ar3", k - 4, "a16.flac", 5, "flac",
                      "audio/flac", 900, 44100, 16, 2, genre="Classical", year=1999))
BYID = {s["id"]: s for s in SONGS}


def pub(s):
    d = {k: v for k, v in s.items() if not k.startswith("_")}
    if s["id"] in STATE["starred"]:
        d["starred"] = "2026-11-01T10:00:00Z"
    return d


def albums():
    out = {}
    for s in SONGS:
        a = out.setdefault(s["albumId"], {"id": s["albumId"], "name": s["album"], "artist": s["artist"],
                                          "artistId": s["artistId"], "coverArt": "al-" + s["albumId"], "songCount": 0,
                                          "duration": 0, "year": s["year"], "genre": s["genre"], "created": "2026-01-01T00:00:00Z"})
        a["songCount"] += 1; a["duration"] += s["duration"]
    return out


def ok(**kw):
    r = {"status": "ok", "version": "1.16.1", "type": "navidrome", "serverVersion": "0.53.3 (qrt)", "openSubsonic": True}
    r.update(kw)
    return {"subsonic-response": r}


def fail(code, msg):
    return {"subsonic-response": {"status": "failed", "version": "1.16.1", "type": "navidrome", "serverVersion": "0.53.3 (qrt)",
                                  "openSubsonic": True, "error": {"code": code, "message": msg}}}


def redact(path):
    u = urllib.parse.urlsplit(path)
    q = [(k, "***" if k in ("u", "t", "s", "p") else v) for k, v in urllib.parse.parse_qsl(u.query, keep_blank_values=True)]
    return u.path + ("?" + urllib.parse.urlencode(q) if q else "")


def auth(q):
    u = q.get("u", [""])[0]
    if u not in USERS:
        return fail(40, "Wrong username or password")
    pw = USERS[u]
    if "t" in q and "s" in q:
        if u == "ldapuser":
            return fail(41, "Token authentication not supported for LDAP users")
        if hashlib.md5((pw + q["s"][0]).encode()).hexdigest() != q["t"][0]:
            return fail(40, "Wrong username or password")
        if len(q["s"][0]) < 6:
            return fail(40, "Salt too short")
        return None
    if "p" in q:
        p = q["p"][0]
        if p.startswith("enc:"):
            p = bytes.fromhex(p[4:]).decode()
        return None if p == pw else fail(40, "Wrong username or password")
    return fail(10, "Required parameter is missing")

# ---- Last.fm API 2.0 (signature checked like the real service) ----
LFM_KEY, LFM_SECRET = "qrtlfmkey", "qrtsecret"
LFM = {"polls": 0, "scrobbles": []}


def lfm_sig(params):
    s = "".join(k + params[k] for k in sorted(params) if k not in ("format", "callback", "api_sig"))
    return hashlib.md5((s + LFM_SECRET).encode()).hexdigest()


def lastfm(params):
    if params.get("api_key") != LFM_KEY:
        return {"error": 10, "message": "Invalid API key"}
    m = params.get("method", "")
    signed = m.startswith("auth.") or m.startswith("track.")
    if signed and params.get("api_sig") != lfm_sig(params):
        return {"error": 13, "message": "Invalid method signature supplied"}
    if m == "auth.getToken":
        LFM["polls"] = 0
        return {"token": "TOK1"}
    if m == "auth.getSession":
        LFM["polls"] += 1
        if LFM["polls"] < 2:
            return {"error": 14, "message": "This token has not been authorized"}
        return {"session": {"name": "qrtlistener", "key": "SK1", "subscriber": 0}}
    if m in ("track.updateNowPlaying", "track.scrobble"):
        if params.get("sk") != "SK1":
            return {"error": 9, "message": "Invalid session key"}
        if m == "track.scrobble" and os.path.exists(os.path.join(WORK, "lfm_down")):
            return {"error": 16, "message": "Temporarily unavailable"}
        with LOCK, open(os.path.join(WORK, "lastfm.log"), "a") as f:
            f.write("%s|%s|%s|%s\n" % (m, params.get("artist"), params.get("track"), params.get("timestamp", "")))
        return {"scrobbles": {"@attr": {"accepted": 1, "ignored": 0}}} if m == "track.scrobble" else {"nowplaying": {}}
    if m == "artist.getInfo":
        return {"artist": {"name": params.get("artist"), "bio": {"summary": "A test band from the fixture. <a href=\"https://www.last.fm/music/x\">Read more on Last.fm</a>"}}}
    if m == "artist.getSimilar":
        return {"similarartists": {"artist": [{"name": "Sun Trio", "match": "1"}, {"name": "Many Songs", "match": "0.5"}]}}
    return {"error": 3, "message": "Invalid Method"}


def lrclib(q):
    if q.get("artist_name") == "Sun Trio" and q.get("track_name") == "Surround Test":
        return 200, {"id": 1, "trackName": "Surround Test", "artistName": "Sun Trio", "instrumental": False,
                     "plainLyrics": "Round we go\nAnd back", "syncedLyrics": "[00:00.50] Round we go\n[00:01.75] And back"}
    if q.get("track_name") == "Blue Hour":
        return 200, {"id": 2, "instrumental": True, "plainLyrics": None, "syncedLyrics": None}
    return 404, {"code": 404, "name": "TrackNotFound", "message": "Failed to find specified track"}

# ---- Spotify Web API (metadata only): client credentials + playlists ----
SP_ID, SP_SECRET = "qrtspid", "qrtspsecret"


def sp_items():
    it = [{"track": {"name": "Blue Hour", "duration_ms": 5000, "album": {"name": "Night Sessions"}, "artists": [{"name": "QRT Quartet"}, {"name": "Guest"}]}},
          {"track": {"name": "Morning Tone (Remastered 2011)", "duration_ms": 5000, "album": {"name": "Day Album"}, "artists": [{"name": "Sun Trio"}]}},
          {"track": None},
          {"track": {"name": "Midnight Line - Live", "duration_ms": 5000, "album": {"name": "Live"}, "artists": [{"name": "QRT Quartet"}]}}]
    for k in range(100):
        it.append({"track": {"name": "Unknown Song %d" % k, "duration_ms": 200000, "album": {"name": "Elsewhere"}, "artists": [{"name": "Nobody %d" % k}]}})
    it.append({"track": None})
    it.append({"track": {"name": "Blue", "duration_ms": 1000, "album": {"name": "X"}, "artists": [{"name": "Somebody"}]}})
    return it


def spotify_token(headers, form):
    import base64
    h = headers.get("Authorization", "")
    ok = h == "Basic " + base64.b64encode(("%s:%s" % (SP_ID, SP_SECRET)).encode()).decode()
    if not ok or form.get("grant_type") != "client_credentials":
        return 400, {"error": "invalid_client", "error_description": "Invalid client"}
    return 200, {"access_token": "SPTOK", "token_type": "Bearer", "expires_in": 3600}


def spotify_api(headers, path, q):
    if headers.get("Authorization") != "Bearer SPTOK":
        return 401, {"error": {"status": 401, "message": "Invalid access token"}}
    parts = path.split("/")   # /spapi/v1/playlists/<id>[/tracks]
    pid = parts[4] if len(parts) > 4 else ""
    if pid != "5QrtPublicMix00000001":
        return 404, {"error": {"status": 404, "message": "Resource not found"}}
    if len(parts) == 5:
        return 200, {"name": "QRT Road Mix"}
    items = sp_items()
    off = int(q.get("offset", "0")); lim = int(q.get("limit", "100"))
    page = items[off:off + lim]
    nxt = "https://api.spotify.com/v1/playlists/%s/tracks?offset=%d&limit=%d" % (pid, off + lim, lim) if off + lim < len(items) else None
    return 200, {"items": page, "next": nxt, "total": len(items)}


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def log(self, line):
        with LOCK, open(os.path.join(WORK, "requests.log"), "a") as f:
            f.write(line + "\n")

    def send(self, body, ctype="application/json", code=200, extra=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def file(self, path, ctype):
        data = open(path, "rb").read()
        rng = self.headers.get("Range")
        if rng and rng.startswith("bytes="):
            a, _, b = rng[6:].partition("-")
            a = int(a or 0); b = int(b) if b else len(data) - 1
            b = min(b, len(data) - 1)
            if a >= len(data):
                return self.send(b"", ctype, 416, {"Content-Range": "bytes */%d" % len(data)})
            return self.send(data[a:b + 1], ctype, 206, {"Content-Range": "bytes %d-%d/%d" % (a, b, len(data)),
                                                       "Accept-Ranges": "bytes"})
        return self.send(data, ctype, 200, {"Accept-Ranges": "bytes"})

    # ------------------------------------------------------------ Subsonic
    def rest(self, method, q):
        bad = auth(q)
        if bad:
            return self.send(bad)
        one = lambda k: q.get(k, [""])[0]
        al = albums()
        if method == "ping":
            return self.send(ok())
        if method == "getArtists":
            arts = {}
            for s in SONGS:
                arts.setdefault(s["artistId"], {"id": s["artistId"], "name": s["artist"], "albumCount": 0, "coverArt": "ar-" + s["artistId"], "_al": set()})
                arts[s["artistId"]]["_al"].add(s["albumId"])
            idx = {}
            for a in arts.values():
                a["albumCount"] = len(a.pop("_al"))
                idx.setdefault(a["name"][0].upper(), []).append(a)
            return self.send(ok(artists={"ignoredArticles": "The", "index": [{"name": k, "artist": v} for k, v in sorted(idx.items())]}))
        if method == "getArtist":
            mine = [a for a in al.values() if a["artistId"] == one("id")]
            if not mine:
                return self.send(fail(70, "Artist not found"))
            return self.send(ok(artist={"id": one("id"), "name": mine[0]["artist"], "album": mine}))
        if method == "getAlbumList2":
            t = one("type"); size = int(one("size") or 10); off = int(one("offset") or 0)
            lst = list(al.values())
            if t == "alphabeticalByName":
                lst.sort(key=lambda a: a["name"])
            elif t == "byGenre":
                lst = [a for a in lst if a["genre"] == one("genre")]
            elif t == "starred":
                lst = []
            elif t == "recent":
                lst = [al[BYID[i]["albumId"]] for i in sorted(STATE["plays"], key=lambda i: -STATE["plays"][i])][:size]
            return self.send(ok(albumList2={"album": lst[off:off + size]}))
        if method == "getAlbum":
            a = al.get(one("id"))
            if not a:
                return self.send(fail(70, "Album not found"))
            a = dict(a); a["song"] = [pub(s) for s in SONGS if s["albumId"] == a["id"]]
            return self.send(ok(album=a))
        if method == "getGenres":
            g = {}
            for s in SONGS:
                g.setdefault(s["genre"], {"value": s["genre"], "songCount": 0, "albumCount": 0})["songCount"] += 1
            for a in al.values():
                g[a["genre"]]["albumCount"] += 1
            return self.send(ok(genres={"genre": list(g.values())}))
        if method == "getSongsByGenre":
            c = int(one("count") or 10); off = int(one("offset") or 0)
            return self.send(ok(songsByGenre={"song": [pub(s) for s in SONGS if s["genre"] == one("genre")][off:off + c]}))
        if method == "search3":
            qq = one("query").lower().strip('"')
            sc = int(one("songCount") or 20); so = int(one("songOffset") or 0)
            ss = [pub(s) for s in SONGS if qq in s["title"].lower() or qq in s["artist"].lower() or qq in s["album"].lower()]
            aa = [a for a in al.values() if qq in a["name"].lower() or qq in a["artist"].lower()][:int(one("albumCount") or 20)]
            ar = {}
            for s in SONGS:
                if qq in s["artist"].lower():
                    ar[s["artistId"]] = {"id": s["artistId"], "name": s["artist"], "albumCount": 1}
            return self.send(ok(searchResult3={"artist": list(ar.values()), "album": aa, "song": ss[so:so + sc]}))
        if method == "getRandomSongs":
            pool = [pub(x) for x in SONGS if not one("genre") or x["genre"] == one("genre")]
            return self.send(ok(randomSongs={"song": pool[:int(one("size") or 10)]}))
        if method in ("star", "unstar"):
            for i in q.get("id", []):
                (STATE["starred"].add if method == "star" else STATE["starred"].discard)(i)
            return self.send(ok())
        if method == "getStarred2":
            return self.send(ok(starred2={"artist": [], "album": [], "song": [pub(BYID[i]) for i in sorted(STATE["starred"]) if i in BYID]}))
        if method == "scrobble":
            STATE["scrobbles"].append((one("id"), one("submission"), one("time")))
            if one("submission") == "true":
                STATE["plays"][one("id")] = time.time()
            with LOCK, open(os.path.join(WORK, "scrobbles.log"), "a") as f:
                f.write("%s %s\n" % (one("id"), one("submission")))
            return self.send(ok())
        if method == "getPlaylists":
            return self.send(ok(playlists={"playlist": [{k: v for k, v in p.items() if k != "entry"} for p in STATE["playlists"].values()]}))
        if method == "getPlaylist":
            p = STATE["playlists"].get(one("id"))
            if not p:
                return self.send(fail(70, "Playlist not found"))
            d = {k: v for k, v in p.items() if k != "entry"}; d["entry"] = [pub(BYID[i]) for i in p["entry"]]
            return self.send(ok(playlist=d))
        if method == "createPlaylist":
            pid = "pl%d" % STATE["next_pl"]; STATE["next_pl"] += 1
            ids = q.get("songId", [])
            STATE["playlists"][pid] = {"id": pid, "name": one("name"), "owner": one("u"), "public": False,
                                       "songCount": len(ids), "duration": sum(BYID[i]["duration"] for i in ids), "entry": ids}
            d = dict(STATE["playlists"][pid]); d["entry"] = [pub(BYID[i]) for i in ids]
            return self.send(ok(playlist=d))
        if method == "updatePlaylist":
            p = STATE["playlists"].get(one("playlistId"))
            if not p:
                return self.send(fail(70, "Playlist not found"))
            rem = sorted({int(x) for x in q.get("songIndexToRemove", [])}, reverse=True)
            for r in rem:
                if 0 <= r < len(p["entry"]):
                    del p["entry"][r]
            p["entry"] += q.get("songIdToAdd", [])
            if one("name"):
                p["name"] = one("name")
            p["songCount"] = len(p["entry"])
            return self.send(ok())
        if method == "deletePlaylist":
            STATE["playlists"].pop(one("id"), None)
            return self.send(ok())
        if method == "getLyricsBySongId":
            if one("id") == "s1":
                return self.send(ok(lyricsList={"structuredLyrics": [{"lang": "eng", "synced": True, "line": [
                    {"start": 0, "value": "First line"}, {"start": 2500, "value": "Second line"}, {"start": 61230, "value": "Later"}]}]}))
            return self.send(ok(lyricsList={"structuredLyrics": []}))
        if method == "getLyrics":
            if one("title") == "Morning Tone":
                return self.send(ok(lyrics={"artist": "Sun Trio", "title": "Morning Tone", "value": "Plain line one\nPlain line two"}))
            return self.send(ok(lyrics={}))
        if method == "stream":
            s = BYID.get(one("id"))
            if not s:
                return self.send(fail(70, "Song not found"))
            if one("format") != "raw":
                self.log("TRANSCODE-REQUESTED " + one("format"))
            return self.file(os.path.join(AUDIO, s["_file"]), s["contentType"])
        if method == "download":
            if one("u") == "listener":
                return self.send(fail(50, "User is not authorized for the given operation."))
            s = BYID.get(one("id"))
            if not s:
                return self.send(fail(70, "Song not found"))
            return self.file(os.path.join(AUDIO, s["_file"]), "application/octet-stream")
        if method == "getCoverArt":
            return self.send(b"\xff\xd8\xff\xe0" + b"\x00" * 64, "image/jpeg")
        return self.send(fail(0, "Unknown method " + method), code=404)

    # ------------------------------------------------------------ WebDAV
    def dav_auth(self):
        import base64
        h = self.headers.get("Authorization", "")
        if not h.startswith("Basic "):
            return False
        try:
            u, _, p = base64.b64decode(h[6:]).decode().partition(":")
        except Exception:
            return False
        return u == DAVUSER and p == DAVPASS

    DAV = {   # path -> list of (name, file or None for folder)
        "/dav/Music/": [("Jazz Folder", None), ("Rock & Roll", None), ("readme.txt", "TEXT"), ("cover.jpg", "IMG"), (".hidden.flac", "a16.flac")],
        "/dav/Music/Jazz Folder/": [("01 - Blue.flac", "a16.flac"), ("02 - Hi Res.flac", "a24art.flac"), ("03 - Lossy.mp3", "a320.mp3")],
        "/dav/Music/Rock & Roll/": [("Live.wav", "a24.wav"), ("Apple.m4a", "alac.m4a")],
    }

    def dav_path(self):
        return urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)

    def do_PROPFIND(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(n) if n else b""
        self.log("PROPFIND %s depth=%s auth=%s ct=%s" % (self.path, self.headers.get("Depth"), "yes" if self.headers.get("Authorization") else "no",
                                                          self.headers.get("Content-Type", "")))
        if not self.dav_auth():
            return self.send(b"Unauthorized", "text/plain", 401, {"WWW-Authenticate": 'Basic realm="dav"'})
        p = self.dav_path()
        if not p.endswith("/"):
            p += "/"
        if p not in self.DAV:
            return self.send(b"", "text/plain", 404)
        def resp(href, folder, size=0, ct=""):
            rt = "<D:resourcetype><D:collection/></D:resourcetype>" if folder else "<D:resourcetype/>"
            extra = "" if folder else "<D:getcontentlength>%d</D:getcontentlength><D:getcontenttype>%s</D:getcontenttype>" % (size, ct)
            return ("<D:response><D:href>%s</D:href><D:propstat><D:prop>%s%s</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat>"
                    "<D:propstat><D:prop><D:quota-used-bytes/></D:prop><D:status>HTTP/1.1 404 Not Found</D:status></D:propstat></D:response>"
                    % (href, rt, extra))
        out = [resp(urllib.parse.quote(p), True)]
        for name, f in self.DAV[p]:
            href = urllib.parse.quote(p + name) + ("/" if f is None else "")
            size = os.path.getsize(os.path.join(AUDIO, f)) if f and f not in ("TEXT", "IMG") else 10
            out.append(resp(href, f is None, size, "audio/flac"))
        xml = '<?xml version="1.0" encoding="utf-8"?><D:multistatus xmlns:D="DAV:">' + "".join(out) + "</D:multistatus>"
        return self.send(xml.encode(), "application/xml; charset=utf-8", 207)

    def dav_get(self):
        self.log("DAVGET %s range=%s auth=%s" % (self.path, self.headers.get("Range", "-"), "yes" if self.headers.get("Authorization") else "no"))
        if not self.dav_auth():
            return self.send(b"Unauthorized", "text/plain", 401)
        p = self.dav_path()
        folder, _, name = p.rpartition("/")
        for n, f in self.DAV.get(folder + "/", []):
            if n == name and f and f not in ("TEXT", "IMG"):
                return self.file(os.path.join(AUDIO, f), "application/octet-stream")
        return self.send(b"", "text/plain", 404)

    def do_GET(self):
        u = urllib.parse.urlsplit(self.path)
        if u.path.startswith("/dav/"):
            return self.dav_get()
        self.log("GET " + redact(self.path) + (" RANGE " + self.headers["Range"] if self.headers.get("Range") else ""))
        if u.path.startswith("/lfm/"):
            return self.send(lastfm(dict(urllib.parse.parse_qsl(u.query))))
        if u.path.startswith("/spapi/v1/"):
            code, body = spotify_api(self.headers, u.path, dict(urllib.parse.parse_qsl(u.query)))
            return self.send(body, code=code)
        if u.path == "/lrclib/api/get":
            if self.headers.get("User-Agent", "") != "QRTIME/1.0":
                self.log("LRCLIB-UA " + self.headers.get("User-Agent", ""))
            code, body = lrclib(dict(urllib.parse.parse_qsl(u.query)))
            return self.send(body, code=code)
        if u.path.startswith("/rest/"):
            m = u.path[6:]
            if m.endswith(".view"):
                m = m[:-5]
            return self.rest(m, urllib.parse.parse_qs(u.query, keep_blank_values=True))
        return self.send(b"not found", "text/plain", 404)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(n).decode()
        params = dict(urllib.parse.parse_qsl(body))
        self.log("POST %s %s method=%s" % (self.path, self.headers.get("Content-Type", ""), params.get("method", "")))
        if self.path.startswith("/lfm/"):
            return self.send(lastfm(params))
        if self.path == "/spaccounts/api/token":
            code, body = spotify_token(self.headers, params)
            return self.send(body, code=code)
        return self.send(b"not found", "text/plain", 404)

    def do_HEAD(self):
        return self.do_GET()


ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
