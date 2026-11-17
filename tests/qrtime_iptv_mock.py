#!/usr/bin/env python3
"""Local IPTV test provider for tests/qrtime_livetv.sh (a TEST FIXTURE, not a
real provider). One HTTP server speaks three protocols on 127.0.0.1:

  Xtream Codes  /player_api.php?username=U&password=P[&action=...]
                /live/U/P/<id>.m3u8|.ts         (tiny placeholder body)
  M3U           /list.m3u   (#EXTM3U url-tvg -> /epg.xml, tvg-id, group-title)
  XMLTV         /epg.xml    programmes relative to NOW, written with +0300
                            offsets so the guide must convert time zones
  Stalker       .../load.php or .../portal.php ?type=stb&action=handshake ...
  Xtream VOD    get_vod_categories/streams/info, get_series_categories/series/
                info; /movie/U/P/<id>.<ext> and /series/U/P/<id>.<ext>.
                A SECOND Xtream login (qrtuser2/qrtpass2) offers an overlapping
                catalog, so the unified library can be tested across providers.

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
USER2, PASS2 = "qrtuser2", "qrtpass2"
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
# ---- VOD / series. Account 1 and account 2 overlap on purpose:
#  The Matrix: tmdb 603 on both; Inception: tmdb on 1, only title+year on 2;
#  Local Indie Film and Local Show: no tmdb anywhere (stay provider-local).
VOD_CATS = {1: [{"category_id": "50", "category_name": "QRT Action"},
                {"category_id": "51", "category_name": "QRT Indie"}],
            2: [{"category_id": "7", "category_name": "P2 Movies"}]}
VOD = {1: [{"num": 1, "name": "EN - The Matrix (1999)", "stream_type": "movie", "stream_id": 1001,
            "stream_icon": BASE + "/img/matrix.jpg", "rating": "8.7", "added": "1700000100",
            "category_id": "50", "container_extension": "mkv", "tmdb": "603"},
           {"num": 2, "name": "Local Indie Film", "stream_type": "movie", "stream_id": 1002,
            "stream_icon": "", "rating": 0, "added": "1700000300", "category_id": "51",
            "container_extension": "mp4", "year": "2021"},
           {"num": 3, "name": "Inception", "stream_type": "movie", "stream_id": "1003",
            "stream_icon": BASE + "/img/inception.jpg", "rating": 8.8, "added": "1700000200",
            "category_id": "50", "container_extension": "mp4", "tmdb_id": 27205, "year": "2010"}],
       2: [{"num": 1, "name": "The Matrix", "stream_type": "movie", "stream_id": 2001,
            "stream_icon": "", "rating": "7", "added": "1700001000", "category_id": "7",
            "container_extension": "mp4", "tmdb": "603", "year": 1999},
           {"num": 2, "name": "Inception 4K (2010)", "stream_type": "movie", "stream_id": 2002,
            "stream_icon": "", "added": "1700001100", "category_id": "7", "container_extension": "mkv",
            "tmdb": ""}]}
SER_CATS = {1: [{"category_id": "60", "category_name": "QRT Drama"}],
            2: [{"category_id": "8", "category_name": "P2 Series"}]}
SERIES = {1: [{"num": 1, "name": "Breaking Bad", "series_id": 3001, "cover": BASE + "/img/bb.jpg",
               "category_id": "60", "tmdb": "1396", "releaseDate": "2008-01-20", "rating": "9.5",
               "last_modified": "1700002000"},
              {"num": 2, "name": "Local Show", "series_id": 3002, "cover": "", "category_id": "60",
               "last_modified": "1700002100"}],
          2: [{"num": 1, "name": "Breaking Bad (2008)", "series_id": 4001, "cover": "",
               "category_id": "8", "tmdb_id": 1396, "last_modified": "1700003000"}]}
def ep(i, s, e, t):
    return {"id": str(i), "episode_num": e, "title": t, "container_extension": "mp4", "season": s,
            "info": {"plot": "Plot of " + t, "duration_secs": 2820, "movie_image": ""}}
SERIES_INFO = {
    (1, "3001"): {"seasons": [], "info": {"name": "Breaking Bad", "plot": "A chemistry teacher.",
                  "cast": "Bryan Cranston, Aaron Paul", "genre": "Drama", "releaseDate": "2008-01-20",
                  "backdrop_path": [BASE + "/img/bb-bd.jpg"], "tmdb": "1396", "episode_run_time": "47"},
                  "episodes": {"1": [ep(40001, 1, 1, "Pilot"), ep(40002, 1, 2, "Cat's in the Bag")],
                               "2": [ep(40011, 2, 1, "Seven Thirty-Seven")]}},
    (1, "3002"): {"info": {"name": "Local Show", "plot": "Only here."},
                  "episodes": {"1": [ep(41001, 1, 1, "Local One"), ep(41002, 1, 2, "Local Two")]}},
    (2, "4001"): {"info": {"name": "Breaking Bad", "tmdb_id": 1396},
                  "episodes": [[ep(50001, 1, 1, "Pilot"), ep(50002, 1, 2, "Cat's in the Bag")],
                               [ep(50011, 2, 1, "Seven Thirty-Seven")]]},
}
def vod_info(acct, vid):
    for m in VOD[acct]:
        if str(m["stream_id"]) == vid:
            return {"info": {"name": m["name"], "plot": "Plot of " + m["name"], "genre": "Action",
                             "cast": "Keanu Reeves, Carrie-Anne Moss", "director": "Wachowskis",
                             "releasedate": "%s-01-01" % (m.get("year") or (m["name"].split("(")[-1][:4] if "(" in m["name"] else "")),
                             "duration_secs": 8160,
                             "backdrop_path": [BASE + "/img/bd.jpg"], "tmdb_id": m.get("tmdb", "")},
                    "movie_data": {"stream_id": m["stream_id"], "name": m["name"],
                                   "container_extension": m["container_extension"]}}
    return {"info": [], "movie_data": []}

# ---- TMDB (only the calls the IPTV library makes for addon titles) ----
TMDB_FIND = {
    "tt0133093": {"movie_results": [{"id": 603, "title": "The Matrix", "original_title": "The Matrix",
                                     "release_date": "1999-03-30"}], "tv_results": []},
    "tt1375666": {"movie_results": [{"id": 27205, "title": "Inception", "original_title": "Inception",
                                     "release_date": "2010-07-15"}], "tv_results": []},
    "tt0903747": {"movie_results": [], "tv_results": [{"id": 1396, "name": "Breaking Bad",
                                     "original_name": "Breaking Bad", "first_air_date": "2008-01-20"}]},
    # A trap: TMDB says 603 is a different film, so a panel hint of 603 is wrong.
    "tt9999999": {"movie_results": [{"id": 603, "title": "Totally Different Film",
                                     "original_title": "Totally Different Film", "release_date": "1987-01-01"}]},
    "tt0000404": {"movie_results": [], "tv_results": []},
}
TMDB_MOVIE = {"27205": {"id": 27205, "title": "Inception", "original_title": "Inception", "release_date": "2010-07-15"}}

# ---- Stalker VOD: The Matrix (title+year only), a portal-only movie, and
# Breaking Bad as a series with episodes 1 and 2 (one season).
ST_VOD_CATS = [{"id": "*", "title": "All"}, {"id": "11", "title": "ST Films"}, {"id": "12", "title": "ST Shows"}]
ST_VOD = [
    {"id": "7001", "name": "The Matrix", "cmd": "/media/7001.mpg", "year": "1999", "category_id": "11",
     "screenshot_uri": "", "is_series": "0", "added": "2023-01-01 10:00:00"},
    {"id": "7002", "name": "Stalker Only Movie", "cmd": "/media/7002.mpg", "year": "2015", "category_id": "11",
     "screenshot_uri": "", "is_series": 0, "added": "2023-01-02 10:00:00"},
    {"id": "7003", "name": "Breaking Bad", "cmd": "/media/7003.mpg", "year": "2008", "category_id": "12",
     "screenshot_uri": "", "is_series": "1", "series": [1, 2], "added": "2023-01-03 10:00:00"},
]
def vod_m3u():
    return ("#EXTM3U\n"
            "#EXTINF:-1 tvg-logo=\"\" group-title=\"VOD Movies\",The Matrix (1999)\n" + BASE + "/m3uvod/matrix.mp4\n"
            "#EXTINF:-1 group-title=\"VOD Movies\",M3U Only Movie (2018)\n" + BASE + "/m3uvod/only.mkv\n"
            "#EXTINF:-1 group-title=\"Series\",Breaking Bad S01E01\n" + BASE + "/m3uvod/bb101.mp4\n"
            "#EXTINF:-1 group-title=\"Series\",Breaking Bad S01E02\n" + BASE + "/m3uvod/bb102.mp4\n"
            "#EXTINF:-1 group-title=\"Live\",Live News HD\n" + BASE + "/m3u/live1.m3u8\n").encode()

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
        safe = self.path.replace(PASS2, "***").replace(PASS, "***")
        if "api_key=" in safe:
            safe = safe.split("api_key=")[0] + "api_key=***"
        with open(WORK + "/requests.log", "a") as f:
            f.write(safe + "\n")
        p = u.path
        if p.endswith("/player_api.php"):
            acct = 1 if (q.get("username"), q.get("password")) == (USER, PASS) else \
                   2 if (q.get("username"), q.get("password")) == (USER2, PASS2) else 0
            a = q.get("action", "")
            if not acct:
                return self.send({"user_info": {"auth": 0}})
            if a == "get_vod_categories":
                return self.send(VOD_CATS[acct])
            if a == "get_vod_streams":
                return self.send(VOD[acct])
            if a == "get_vod_info":
                return self.send(vod_info(acct, q.get("vod_id", "")))
            if a == "get_series_categories":
                return self.send(SER_CATS[acct])
            if a == "get_series":
                return self.send(SERIES[acct])
            if a == "get_series_info":
                return self.send(SERIES_INFO.get((acct, q.get("series_id", "")), {"info": [], "episodes": []}))
            if acct == 2 and a in ("get_live_categories", "get_live_streams"):
                return self.send([])
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
        if p.startswith("/3/"):
            if q.get("api_key") != "QRTKEY":
                return self.send({"status_code": 7}, code=401)
            if p.startswith("/3/find/"):
                return self.send(TMDB_FIND.get(p[len("/3/find/"):], {"movie_results": [], "tv_results": []}))
            if p.startswith("/3/movie/"):
                m = TMDB_MOVIE.get(p[len("/3/movie/"):])
                return self.send(m) if m else self.send({"status_code": 34}, code=404)
            return self.send({"status_code": 34}, code=404)
        if p == "/vod.m3u":
            return self.send(vod_m3u(), "audio/x-mpegurl")
        if p == "/list.m3u":
            return self.send(m3u(), "audio/x-mpegurl")
        if p == "/epg.xml" or p == "/xmltv.php":
            return self.send(xmltv(), "application/xml")
        if p.endswith("load.php") or p.endswith("portal.php"):
            a = q.get("action", "")
            if q.get("type") == "vod":
                if a == "get_categories":
                    return self.send({"js": ST_VOD_CATS})
                if a == "get_ordered_list":
                    page = int(q.get("p", "1") or 1)
                    return self.send({"js": {"total_items": len(ST_VOD), "max_page_items": 14,
                                             "data": ST_VOD if page <= 1 else []}})
                if a == "create_link":
                    cmd = q.get("cmd", "")
                    vid = cmd.rsplit("/", 1)[-1].split(".")[0]
                    return self.send({"js": {"cmd": "ffmpeg %s/stvod/%s.mp4?series=%s" % (BASE, vid, q.get("series", "0"))}})
                return self.send({"js": []})
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
        if p.startswith(("/movie/", "/series/", "/stvod/", "/m3uvod/")):
            # A tiny MP4 header: enough for the app's source check (video/*);
            # real decoding is only provable on the TV.
            return self.send(b"\x00\x00\x00\x18ftypmp42\x00\x00\x00\x00mp42isom" + b"\x00" * 64, "video/mp4")
        if p.startswith(("/live/", "/m3u/", "/st/")):
            # Placeholder media: the desktop build has no video decoder; on a
            # TV this would need a real stream (validated on hardware).
            return self.send(b"#EXTM3U\n#EXT-X-ENDLIST\n", "application/vnd.apple.mpegurl")
        if p.startswith("/logo/"):
            return self.send(b"", "image/png", 404)
        return self.send(b"not found", "text/plain", 404)


ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
