#!/usr/bin/env python3
"""A pretend Music Assistant for tests: POST /api {"command", "args"} with a
Bearer token; answers the few commands QR TIME uses and logs every command
(name + args) to <dir>/ma-commands.log. Usage: qrtime_ma_mock.py PORT DIR TOKEN"""
import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT, DIR, TOKEN = int(sys.argv[1]), sys.argv[2], sys.argv[3]
def item(i, kind, name, sub=""):
    d = {"item_id": i, "provider": "ytmusic", "name": name, "uri": "ytmusic://%s/%s" % (kind, i), "media_type": kind,
         "metadata": {"images": [{"type": "thumb", "path": "http://127.0.0.1:%d/img/%s.png" % (PORT, i), "remotely_accessible": True}]}}
    if sub: d["artists"] = [{"name": sub}]
    return d
def png(seed):
    """A small solid-colour cover (no extra modules): one colour per item."""
    import zlib, struct
    h = sum(seed.encode()) * 2654435761
    c = bytes(((h >> s) & 255) // 2 + 60 for s in (0, 8, 16))
    w = 64
    raw = b"".join(b"\0" + c * w for _ in range(w))
    def ch(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, w, 8, 2, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b"")
RECENT = [item("t1", "track", "Kifak Inta", "Fairuz"), item("t2", "track", "Tamally Maak", "Amr Diab"),
          item("t4", "track", "Nassam Alayna El Hawa", "Fairuz"), item("a2", "artist", "Kadim Al Sahir"),
          item("p2", "playlist", "Arabic Classics"), item("t5", "track", "Ya Bent El Sultan", "Ahmed Adaweya"),
          item("t6", "track", "Enta Omri", "Umm Kulthum")]
PLAYERS = [{"player_id": "media_player.q990f", "display_name": "Q-Series Soundbar", "state": "idle", "available": True},
           {"player_id": "cast_group_all", "name": "All speakers", "type": "group", "available": True}]

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        if self.path.startswith("/img/"):
            b = png(self.path)
            self.send_response(200); self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
        else:
            self.send_response(404); self.end_headers()
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(n) or b"{}")
        if self.headers.get("Authorization") != "Bearer " + TOKEN:
            self.send_response(401); self.end_headers(); return
        cmd, args = body.get("command"), body.get("args") or {}
        with open(DIR + "/ma-commands.log", "a") as f:
            f.write(cmd + " " + json.dumps(args, sort_keys=True) + "\n")
        res = None
        if cmd == "players/all": res = PLAYERS
        elif cmd == "music/recently_played_items": res = RECENT
        elif cmd == "music/search":
            res = {"tracks": [item("t3", "track", "Habibi Ya Nour El Ain", "Amr Diab")],
                   "artists": [item("a1", "artist", "Amr Diab")], "albums": [], "playlists": [item("p9", "playlist", "Amr Diab Hits")]}
        elif cmd == "music/playlists/library_items": res = [item("p1", "playlist", "Liked music"), item("p3", "playlist", "Road trip"), item("p4", "playlist", "Sleep")]
        elif cmd == "music/artists/library_items": res = [item("a3", "artist", "Fairuz"), item("a4", "artist", "Amr Diab"), item("a5", "artist", "Umm Kulthum")]
        elif cmd == "player_queues/play_media":
            uri = (args.get("media") or [""])[0]
            hit = next((i for i in RECENT if i["uri"] == uri), None)
            for p in PLAYERS:
                if p["player_id"] == args.get("queue_id") and hit:
                    p["state"] = "playing"; p["elapsed_time"] = 63
                    p["current_media"] = {"title": hit["name"], "artist": (hit.get("artists") or [{"name": ""}])[0]["name"],
                                          "image_url": hit["metadata"]["images"][0]["path"], "duration": 245}
        elif cmd == "music/playlists/playlist_tracks": res = RECENT
        elif cmd.endswith("/library_items"): res = []
        out = json.dumps({"message_id": "1", "result": res}).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(out))); self.end_headers(); self.wfile.write(out)

HTTPServer(("127.0.0.1", PORT), H).serve_forever()
