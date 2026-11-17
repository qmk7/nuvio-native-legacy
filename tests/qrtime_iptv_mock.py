#!/usr/bin/env python3
"""Local IPTV test provider for tests/qrtime_livetv.sh (a TEST FIXTURE, not a
real provider). One HTTP server speaks three protocols on 127.0.0.1:

  Xtream Codes  /player_api.php?username=U&password=P[&action=...]
                /live/U/P/<id>.m3u8|.ts         (tiny placeholder body)
  M3U           /list.m3u   (#EXTM3U url-tvg -> /epg.xml, tvg-id, group-title)
  XMLTV         /epg.xml    programmes relative to NOW, written with +0300
                            offsets so the guide must convert time zones
  Stalker       .../load.php or .../portal.php ?type=stb&action=handshake ...

Every request is logged to <workdir>/requests.log (path + query, never a
password: the Xtream password is replaced by ***), so the test can prove what
the app actually asked for.

  python3 tests/qrtime_iptv_mock.py <port> <workdir>
"""
import json, sys, time, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1])
WORK = sys.argv[2]
USER, PASS, MAC = "qrtuser", "qrtpass", "00:1A:79:00:00:42"
BASE = "http://127.0.0.1:%d" % PORT

XT_CATS = [{"category_id": "10", "category_name": "QRT News", "parent_id": 0},
           {"category_id": "20", "category_name": "QRT Sports", "parent_id": 0}]
XT_CHANS = [
    {"num": 1, "name": "QRT News One", "stream_type": "live", "stream_id": 101,
     "stream_icon": BASE + "/logo/news1.png", "epg_channel_id": "qrt.news1",
     "category_id": "10", "tv_archive": 0},
    {"num": 2, "name": "QRT News Two", "stream_type": "live", "stream_id": 102,
     "stream_icon": "", "epg_channel_id": "qrt.news2", "category_id": "10", "tv_archive": 1,
     "tv_archive_duration": 3},
    {"num": 3, "name": "QRT Sports Arena", "stream_type": "live", "stream_id": 201,
     "stream_icon": "", "epg_channel_id": "qrt.sports", "category_id": "20", "tv_archive": 0},
]
ST_GENRES = [{"id": "1", "title": "Stalker Movies"}, {"id": "2", "title": "Stalker Kids"}]
ST_CHANS = [
    {"id": "501", "name": "Stalker Cinema", "number": "1", "cmd": "ffmpeg %s/st/501" % BASE,
     "tv_genre_id": "1", "logo": ""},
    {"id": "502", "name": "Stalker Toons", "number": "2", "cmd": "ffmpeg %s/st/502" % BASE,
     "tv_genre_id": "2", "logo": ""},
]


def xmltv():
    """Three programmes per channel around now, in +0300 local time."""
    now = int(time.time())
    start = now - now % 1800 - 1800                 # half-hour grid, one slot back
    off = 3 * 3600

    def fmt(t):
        return time.strftime("%Y%m%d%H%M%S", time.gmtime(t + off)) + " +0300"
    out = ['<?xml version="1.0" encoding="UTF-8"?>', '<tv generator-info-name="qrtime-test">']
    ids = {"qrt.news1": "QRT News One", "qrt.news2": "QRT News Two",
           "qrt.sports": "QRT Sports Arena", "qrt.m3u1": "M3U Music Hits",
           "qrt.m3u2": "M3U Documentary"}
    for cid, name in ids.items():
        out.append('<channel id="%s"><display-name>%s</display-name></channel>' % (cid, name))
    for cid, name in ids.items():
        for k in range(4):
            a, b = start + k * 1800, start + (k + 1) * 1800
            out.append('<programme start="%s" stop="%s" channel="%s"><title>%s show %d</title>'
                       '<desc>Test programme %d of %s.</desc></programme>' % (fmt(a), fmt(b), cid, name, k, k, name))
    out.append("</tv>")
    return "\n".join(out).encode()


def m3u():
    return ("#EXTM3U url-tvg=\"%s/epg.xml\"\n"
            "#EXTINF:-1 tvg-id=\"qrt.m3u1\" tvg-logo=\"\" group-title=\"M3U Music\",M3U Music Hits\n"
            "%s/m3u/1.m3u8\n"
            "#EXTINF:-1 tvg-id=\"qrt.m3u2\" tvg-logo=\"\" group-title=\"M3U Docs\",M3U Documentary\n"
            "%s/m3u/2.m3u8\n" % (BASE, BASE, BASE)).encode()


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, body, ctype="application/json", code=200):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        safe = self.path.replace(PASS, "***")
        with open(WORK + "/requests.log", "a") as f:
            f.write(safe + "\n")
        p = u.path
        if p.endswith("/player_api.php"):
            ok = q.get("username") == USER and q.get("password") == PASS
            a = q.get("action", "")
            if not ok:
                return self.send({"user_info": {"auth": 0}})
            if a == "get_live_categories":
                return self.send(XT_CATS)
            if a == "get_live_streams":
                cat = q.get("category_id")
                return self.send([c for c in XT_CHANS if not cat or c["category_id"] == cat])
            if a == "get_short_epg":
                return self.send({"epg_listings": []})
            if a:
                return self.send([])
            return self.send({"user_info": {"username": USER, "auth": 1, "status": "Active",
                                            "exp_date": str(int(time.time()) + 30 * 86400),
                                            "is_trial": "0", "active_cons": "0", "max_connections": "2",
                                            "allowed_output_formats": ["m3u8", "ts"]},
                              "server_info": {"url": "127.0.0.1", "port": str(PORT),
                                              "server_protocol": "http", "timezone": "Europe/Istanbul",
                                              "timestamp_now": int(time.time())}})
        if p == "/list.m3u":
            return self.send(m3u(), "audio/x-mpegurl")
        if p == "/epg.xml" or p == "/xmltv.php":
            return self.send(xmltv(), "application/xml")
        if p.endswith("load.php") or p.endswith("portal.php"):
            a = q.get("action", "")
            if a == "handshake":
                return self.send({"js": {"token": "QRTTOKEN", "random": "x"}})
            if a == "get_profile":
                return self.send({"js": {"id": 1, "name": "qrt", "status": 0}})
            if a == "get_genres":
                return self.send({"js": ST_GENRES})
            if a == "get_ordered_list":
                page = int(q.get("p", "1") or 1)
                data = ST_CHANS if page <= 1 else []
                return self.send({"js": {"total_items": len(ST_CHANS), "max_page_items": 14, "data": data}})
            if a == "create_link":
                cmd = q.get("cmd", "")
                return self.send({"js": {"cmd": cmd if cmd.startswith("ffmpeg") else "ffmpeg " + cmd}})
            if a == "get_all_channels":
                return self.send({"js": {"data": ST_CHANS}})
            return self.send({"js": []})
        if p.startswith(("/live/", "/m3u/", "/st/")):
            # Placeholder media: the desktop build has no video decoder; on a
            # TV this would need a real stream (validated on hardware).
            return self.send(b"#EXTM3U\n#EXT-X-ENDLIST\n", "application/vnd.apple.mpegurl")
        if p.startswith("/logo/"):
            return self.send(b"", "image/png", 404)
        return self.send(b"not found", "text/plain", 404)


ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
