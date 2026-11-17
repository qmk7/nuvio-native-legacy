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
TMDB_MOVIE = {
    "27205": {"id": 27205, "title": "Inception", "original_title": "Inception", "release_date": "2010-07-15",
              "imdb_id": "tt1375666"},
    "603": {"id": 603, "title": "The Matrix", "original_title": "The Matrix", "release_date": "1999-03-30",
            "imdb_id": "tt0133093"},
    "999001": {"id": 999001, "title": "Stalker Only Movie", "original_title": "Stalker Only Movie",
               "release_date": "2015-05-05", "imdb_id": "tt9990001"},
}
TMDB_TV = {"1396": {"id": 1396, "name": "Breaking Bad", "original_name": "Breaking Bad", "first_air_date": "2008-01-20",
                    "external_ids": {"imdb_id": "tt0903747"}}}
# Searches by title + year: one exact result, two identical titles (ambiguous), none.
TMDB_SEARCH = {
    ("movie", "stalker only movie", "2015"): [{"id": 999001, "title": "Stalker Only Movie",
                                               "original_title": "Stalker Only Movie", "release_date": "2015-05-05"}],
    ("movie", "local indie film", "2021"): [{"id": 1, "title": "Local Indie Film", "release_date": "2021-01-01"},
                                            {"id": 2, "title": "Local Indie Film", "release_date": "2021-06-01"}],
}

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

# ---- ESPN-shaped scoreboards (Sport Center): times relative to NOW ----
def _iso(dt):
    return time.strftime("%Y-%m-%dT%H:%MZ", time.gmtime(dt))
def _team(i, name, short, abbr, home, score=None, winner=None):
    c = {"homeAway": "home" if home else "away",
         "team": {"id": i, "displayName": name, "shortDisplayName": short, "abbreviation": abbr,
                  "logo": "http://a.espncdn.com/i/teamlogos/%s.png" % i}}
    if score is not None: c["score"] = score
    if winner is not None: c["winner"] = winner
    return c
def _ev(eid, start, state, detail, comps, clock="", name="STATUS_X", bcast=None, venue="Stadium"):
    comp = {"id": eid, "date": _iso(start), "competitors": comps, "venue": {"fullName": venue}}
    if bcast: comp["broadcasts"] = [{"market": "national", "names": [b]} for b in bcast]
    return {"id": eid, "date": _iso(start), "name": "x",
            "status": {"displayClock": clock, "type": {"state": state, "shortDetail": detail, "name": name}},
            "competitions": [comp]}
def espn(path):
    now = int(time.time())
    if path == "soccer/ksa.1":
        return {"leagues": [{"id": "1", "name": "Saudi Pro League"}], "events": [
            _ev("700001", now - 30 * 60, "in", "67'", [_team("11", "Al Hilal", "Al Hilal", "HIL", True, "2"),
                _team("12", "Al Nassr", "Al Nassr", "NAS", False, "1")], clock="67'", name="STATUS_SECOND_HALF",
                bcast=["SSC"], venue="Kingdom Arena"),
            _ev("700002", now + 2 * 3600, "pre", "Today 8:00 PM", [_team("13", "Al Ittihad", "Al Ittihad", "ITT", True),
                _team("14", "Al Ahli", "Al Ahli", "AHL", False)], name="STATUS_SCHEDULED", bcast=["SSC"]),
            _ev("700003", now - 2 * 86400, "post", "FT", [_team("11", "Al Hilal", "Al Hilal", "HIL", True, "3", True),
                _team("14", "Al Ahli", "Al Ahli", "AHL", False, "0", False)], name="STATUS_FULL_TIME"),
            _ev("700004", now + 86400, "pre", "Postponed", [_team("15", "Al Shabab", "Al Shabab", "SHB", True),
                _team("16", "Al Fateh", "Al Fateh", "FAT", False)], name="STATUS_POSTPONED"),
        ]}
    if path == "basketball/nba":
        # US sports list the AWAY team first.
        return {"events": [_ev("800001", now + 5 * 3600, "pre", "Tonight", [
            _team("2", "Boston Celtics", "Celtics", "BOS", False), _team("13", "Los Angeles Lakers", "Lakers", "LAL", True)],
            bcast=["ESPN"])]}
    if path == "tennis/atp":
        g = {"grouping": {"slug": "mens-singles"}, "competitions": [
            {"id": "900001", "date": _iso(now - 3600), "status": {"type": {"state": "in", "shortDetail": "Set 2"}},
             "competitors": [{"homeAway": "home", "athlete": {"id": "a1", "displayName": "Carlos Alcaraz",
                              "shortDisplayName": "C. Alcaraz", "flag": {"href": "https://a.espncdn.com/f/esp.png"}}},
                             {"homeAway": "away", "athlete": {"id": "a2", "displayName": "Jannik Sinner",
                              "shortDisplayName": "J. Sinner"}}]}]}
        return {"events": [{"id": "T1", "date": _iso(now - 86400), "name": "ATP Finals",
                            "status": {"type": {"state": "in"}}, "groupings": [g]}]}
    if path == "soccer/eng.1":
        return {"events": []}
    if path == "soccer/esp.1":
        return "<html>gateway error</html>"
    return {"events": []}

# ---- TheSportsDB v1 (TV listings for the live Saudi match) ----
def tsdb(path, q):
    now = int(time.time())
    if path.endswith("eventsday.php"):
        live = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(now - 30 * 60))   # no "Z": some answers omit it
        return {"events": [
            {"idEvent": "555000", "strEvent": "Al Ittihad vs Al Ahli", "strHomeTeam": "Al-Ittihad", "strAwayTeam": "Al-Ahli",
             "strTimestamp": time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(now + 2 * 3600))},
            {"idEvent": "555001", "strEvent": "Al-Hilal vs Al-Nassr", "strHomeTeam": "Al-Hilal", "strAwayTeam": "Al-Nassr",
             "strTimestamp": live}]}
    if path.endswith("lookuptv.php"):
        if q.get("id") == "555001":
            return {"tvevent": [{"idEvent": "555001", "strChannel": "Sky Sports Football", "strCountry": "United Kingdom"},
                                {"idEvent": "555001", "strChannel": "beIN SPORTS 1", "strCountry": "Saudi Arabia"},
                                {"idEvent": "555001", "strChannel": "SSC 1 HD", "strCountry": "Saudi Arabia"}]}
        return {"tvevent": None}
    return {"events": None}

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
            # The sports channel carries the live Sport Center match (espn()):
            # its programme names both teams, so the match page suggests it.
            title = "Roshn Saudi League: Al Hilal vs Al Nassr" if cid == "qrt.sports" else "%s show %d" % (name, k)
            out.append('<programme start="%s" stop="%s" channel="%s"><title>%s</title>'
                       '<desc>Test programme %d of %s.</desc></programme>' % (fmt(a), fmt(b), cid, title, k, name))
    out.append("</tv>")
    return "\n".join(out).encode()


def m3u():
    return ("#EXTM3U url-tvg=\"%s/epg.xml\"\n"
            "#EXTINF:-1 tvg-id=\"qrt.m3u1\" tvg-logo=\"\" group-title=\"M3U Music\",M3U Music Hits\n"
            "%s/m3u/1.m3u8\n"
            "#EXTINF:-1 tvg-id=\"qrt.m3u2\" tvg-logo=\"\" group-title=\"M3U Docs\",M3U Documentary\n"
            "%s/m3u/2.m3u8\n" % (BASE, BASE, BASE)).encode()


# ---- YouTube Data API v3 / Google OAuth device flow (shape of the official
# responses; fixtures only, never proof of the real service) ----
YT_KEY = "QRTYTKEY"
YT_TOKENS = {"QRTACCESS", "QRTACCESS2"}
YT_POLLS = {"n": 0}


def yt_thumb(i):
    return {"default": {"url": "%s/logo/yt%s.jpg" % (BASE, i)},
            "high": {"url": "https://i.ytimg.com/vi/%s/hqdefault.jpg" % i}}


def yt_video(i, title, chan="UCqrtChannel0000000001", ctitle="QRT Channel", dur="PT4M13S", views="1534000", live="none"):
    return {"kind": "youtube#video", "id": i,
            "snippet": {"publishedAt": "2026-11-10T12:00:00Z", "channelId": chan, "title": title,
                        "description": "Description of " + title, "thumbnails": yt_thumb(i),
                        "channelTitle": ctitle, "liveBroadcastContent": live},
            "contentDetails": {"duration": dur}, "statistics": {"viewCount": views}}


YT_VIDEOS = {v["id"]: v for v in [
    yt_video("qrtVideo001", "QRT Trending One"),
    yt_video("qrtVideo002", "QRT Trending Two", dur="PT1H2M3S", views="2500000000"),
    yt_video("qrtVideo003", "QRT Live Now", dur="P0D", live="live", views="900"),
    yt_video("qrtSearch01", "Cooking pasta at home"),
    yt_video("qrtSearch02", "Pasta sauce in 10 minutes", dur="PT10M"),
    yt_video("qrtUpload01", "Channel upload one"),
]}


def yt_search_item(v):
    return {"kind": "youtube#searchResult", "id": {"kind": "youtube#video", "videoId": v["id"]},
            "snippet": {k: v["snippet"][k] for k in ("publishedAt", "channelId", "title", "description",
                                                       "thumbnails", "channelTitle", "liveBroadcastContent")}}


def yt_pl_item(v, owner="Me"):
    sn = dict(v["snippet"])
    sn["channelTitle"] = owner
    sn["videoOwnerChannelTitle"] = v["snippet"]["channelTitle"]
    sn["resourceId"] = {"kind": "youtube#video", "videoId": v["id"]}
    return {"kind": "youtube#playlistItem", "snippet": sn}


def yt_err(code, reason, msg):
    return {"error": {"code": code, "message": msg, "errors": [{"reason": reason, "message": msg}]}}


def youtube(handler, p, q):
    res = p[len("/yt/v3/"):]
    auth = handler.headers.get("Authorization", "")
    acct = auth.startswith("Bearer ") and auth[7:] in YT_TOKENS
    if q.get("mine") == "true" or (res == "playlistItems" and q.get("playlistId", "").startswith("PLqrt")):
        if not acct:
            return 401, yt_err(401, "authError", "Invalid Credentials")
    elif not acct:
        k = q.get("key", "")
        if k == "QUOTAKEY":
            return 403, yt_err(403, "quotaExceeded", "The request cannot be completed because you have exceeded your quota.")
        if k != YT_KEY:
            return 400, yt_err(400, "badRequest", "API key not valid. Please pass a valid API key.")
    if res == "videos" and q.get("chart") == "mostPopular":
        items = [YT_VIDEOS[i] for i in ("qrtVideo001", "qrtVideo002", "qrtVideo003")]
        if q.get("pageToken") == "PAGE2":
            return 200, {"kind": "youtube#videoListResponse", "items": [YT_VIDEOS["qrtUpload01"]]}
        return 200, {"kind": "youtube#videoListResponse", "nextPageToken": "PAGE2", "items": items}
    if res == "videos":
        ids = [i for i in q.get("id", "").split(",") if i in YT_VIDEOS]
        return 200, {"kind": "youtube#videoListResponse", "items": [YT_VIDEOS[i] for i in ids]}
    if res == "search":
        qq = q.get("q", "").lower()
        hits = [yt_search_item(YT_VIDEOS[i]) for i in ("qrtSearch01", "qrtSearch02") if "pasta" in qq]
        return 200, {"kind": "youtube#searchListResponse", "items": hits}
    if res == "subscriptions":
        return 200, {"kind": "youtube#subscriptionListResponse", "items": [
            {"kind": "youtube#subscription", "snippet": {"title": "QRT Channel", "description": "",
             "resourceId": {"kind": "youtube#channel", "channelId": "UCqrtChannel0000000001"},
             "thumbnails": yt_thumb("ch1")}}]}
    if res == "playlists":
        return 200, {"kind": "youtube#playlistListResponse", "items": [
            {"kind": "youtube#playlist", "id": "PLqrtMine01", "snippet": {"title": "My QRT playlist",
             "channelTitle": "Me", "thumbnails": yt_thumb("pl1")}, "contentDetails": {"itemCount": 2}}]}
    if res == "playlistItems":
        pl = q.get("playlistId", "")
        if pl == "UUqrtChannel0000000001":
            return 200, {"kind": "youtube#playlistItemListResponse", "items": [yt_pl_item(YT_VIDEOS["qrtUpload01"], "QRT Channel")]}
        if pl == "PLqrtMine01":
            gone = {"kind": "youtube#playlistItem", "snippet": {"title": "Deleted video", "resourceId": {"videoId": "qrtGone0000"}}}
            return 200, {"kind": "youtube#playlistItemListResponse", "items": [yt_pl_item(YT_VIDEOS["qrtVideo001"]), gone,
                                                                               yt_pl_item(YT_VIDEOS["qrtSearch02"])]}
        return 404, yt_err(404, "playlistNotFound", "not found")
    return 404, yt_err(404, "notFound", "not found")


def goauth(p, form):
    if form.get("client_id") != "qrt-client.apps.googleusercontent.com":
        return 401, {"error": "invalid_client", "error_description": "The OAuth client was not found."}
    if p == "/goauth/device/code":
        YT_POLLS["n"] = 0
        return 200, {"device_code": "QRTDEVICE", "user_code": "QRT-WXYZ", "expires_in": 1800, "interval": 1,
                     "verification_url": "https://www.google.com/device"}
    if p == "/goauth/token":
        if form.get("client_secret") != "qrt-secret":
            return 401, {"error": "invalid_client"}
        g = form.get("grant_type", "")
        if g == "refresh_token":
            if form.get("refresh_token") == "QRTREFRESH":
                return 200, {"access_token": "QRTACCESS2", "expires_in": 3599, "token_type": "Bearer"}
            return 400, {"error": "invalid_grant"}
        if form.get("device_code") != "QRTDEVICE":
            return 400, {"error": "invalid_grant"}
        YT_POLLS["n"] += 1
        if YT_POLLS["n"] < 2:
            return 428, {"error": "authorization_pending"}
        return 200, {"access_token": "QRTACCESS", "refresh_token": "QRTREFRESH", "expires_in": 3599,
                     "scope": "https://www.googleapis.com/auth/youtube.readonly", "token_type": "Bearer"}
    return 404, {"error": "not_found"}


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
        if "key=" in safe and safe.startswith("/yt/"):
            safe = safe.split("key=")[0] + "key=***"
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
        if p.startswith("/yt/v3/"):
            code, body = youtube(self, p, q)
            return self.send(body, code=code)
        if p.startswith("/tsdb/123/"):
            return self.send(tsdb(p, q))
        if p.startswith("/espn/") and p.endswith("/scoreboard"):
            body = espn(p[len("/espn/"):-len("/scoreboard")])
            if isinstance(body, str):
                return self.send(body.encode(), "text/html", 502)
            return self.send(body)
        if p.startswith("/3/"):
            if q.get("api_key") != "QRTKEY":
                return self.send({"status_code": 7}, code=401)
            if p.startswith("/3/find/"):
                return self.send(TMDB_FIND.get(p[len("/3/find/"):], {"movie_results": [], "tv_results": []}))
            if p.startswith("/3/movie/"):
                m = TMDB_MOVIE.get(p[len("/3/movie/"):])
                return self.send(m) if m else self.send({"status_code": 34}, code=404)
            if p.startswith("/3/tv/"):
                m = TMDB_TV.get(p[len("/3/tv/"):])
                return self.send(m) if m else self.send({"status_code": 34}, code=404)
            if p.startswith("/3/search/"):
                kind = p[len("/3/search/"):]
                year = q.get("year") or q.get("first_air_date_year") or ""
                return self.send({"results": TMDB_SEARCH.get((kind, q.get("query", "").lower(), year), [])})
            return self.send({"status_code": 34}, code=404)
        # ---- Stremio-style add-ons for the guest add-on list ----
        if p in ("/addon/manifest.json", "/addon2/manifest.json"):
            n = "QRT Test Addon" if p.startswith("/addon/") else "QRT Second Addon"
            return self.send({"id": "org.qrt." + p.split("/")[1], "name": n, "version": "1.0.0",
                              "resources": ["catalog", "stream"], "types": ["movie"],
                              "catalogs": [{"type": "movie", "id": "top", "name": "Top"}]})
        if p == "/notaddon/manifest.json":
            return self.send(b"<html>not an add-on</html>", "text/html")
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

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        form = dict(urllib.parse.parse_qsl(self.rfile.read(n).decode()))
        with open(WORK + "/requests.log", "a") as f:
            f.write("POST " + self.path + " " + self.headers.get("Content-Type", "") + "\n")
        if self.path.startswith("/goauth/"):
            code, body = goauth(self.path, form)
            return self.send(body, code=code)
        return self.send(b"not found", "text/plain", 404)


ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
