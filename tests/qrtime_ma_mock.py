#!/usr/bin/env python3
"""A pretend Music Assistant for tests: POST /api {"command", "args"} with a
Bearer token; answers the few commands QR TIME uses and logs every command
(name + args) to <dir>/ma-commands.log. Usage: qrtime_ma_mock.py PORT DIR TOKEN"""
import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT, DIR, TOKEN = int(sys.argv[1]), sys.argv[2], sys.argv[3]
IMG = {"metadata": {"images": [{"type": "thumb", "path": "http://127.0.0.1:%d/img.png" % PORT, "remotely_accessible": True}]}}
def item(i, kind, name, sub=""):
    d = {"item_id": i, "provider": "ytmusic", "name": name, "uri": "ytmusic://%s/%s" % (kind, i), "media_type": kind}
    if sub: d["artists"] = [{"name": sub}]
    d.update(IMG)
    return d
RECENT = [item("t1", "track", "Kifak Inta", "Fairuz"), item("t2", "track", "Tamally Maak", "Amr Diab")]
PLAYERS = [{"player_id": "media_player.q990f", "display_name": "Q-Series Soundbar", "state": "idle", "available": True},
           {"player_id": "cast_group_all", "name": "All speakers", "type": "group", "available": True}]

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
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
        elif cmd == "music/playlists/library_items": res = [item("p1", "playlist", "Liked music")]
        elif cmd == "music/playlists/playlist_tracks": res = RECENT
        elif cmd.endswith("/library_items"): res = []
        out = json.dumps({"message_id": "1", "result": res}).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(out))); self.end_headers(); self.wfile.write(out)

HTTPServer(("127.0.0.1", PORT), H).serve_forever()
