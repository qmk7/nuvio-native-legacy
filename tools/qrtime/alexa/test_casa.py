"""python3 -I test_casa.py: the house devices from Home Assistant join discovery and get their directives."""
import json, os, sys, threading
from http.server import BaseHTTPRequestHandler, HTTPServer
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lambda_function as lf

pedidos = []
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_POST(self):
        corpo = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        pedidos.append((self.path, self.headers.get("Authorization"), corpo))
        ns = corpo["directive"]["header"]["namespace"]
        if ns == "Alexa.Discovery":
            r = {"event": {"header": {}, "payload": {"endpoints": [
                {"endpointId": "light#kitchen", "friendlyName": "Kitchen light"},
                {"endpointId": "media_player#tv", "friendlyName": "TV"},          # same name as ours: dropped
                {"endpointId": "climate#ac", "friendlyName": "Living room AC"}]}}}
        else:
            r = {"event": {"header": {"name": "Response"}, "payload": {}, "endpoint": corpo["directive"]["endpoint"]}}
        b = json.dumps(r).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)

srv = HTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
lf._casa_url = lambda: "http://127.0.0.1:%d/api/alexa/smart_home" % srv.server_port
os.environ.update(HA_TOKEN="tok-test", DEVICE_NAME="TV", MUSIC_DEVICES="Soundbar=soundbar")
falhas = 0
def ok(c, m):
    global falhas
    print(("PASS: " if c else "FAIL: ") + m); falhas += not c

disc = {"directive": {"header": {"namespace": "Alexa.Discovery", "name": "Discover", "payloadVersion": "3", "messageId": "1"},
                      "payload": {"scope": {"type": "BearerToken", "token": "x"}}}}
eps = lf.lambda_handler(disc, None)["event"]["payload"]["endpoints"]
ids = [e["endpointId"] for e in eps]
ok("qrtime-tv" in ids and any(i.startswith(lf.MUSICA) for i in ids), "the TV and the speakers stay")
ok("light#kitchen" in ids and "climate#ac" in ids, "Home Assistant's devices join")
ok("media_player#tv" not in ids, "a second 'TV' from Home Assistant is left out")
ok(pedidos[-1][1] == "Bearer tok-test" and pedidos[-1][0] == "/api/alexa/smart_home", "asks Home Assistant with its token")

d = {"directive": {"header": {"namespace": "Alexa.PowerController", "name": "TurnOff", "payloadVersion": "3", "messageId": "2", "correlationToken": "c"},
                   "endpoint": {"endpointId": "light#kitchen", "scope": {"type": "BearerToken", "token": "x"}}, "payload": {}}}
n = len(pedidos)
r = lf.lambda_handler(d, None)
ok(len(pedidos) == n + 1 and pedidos[-1][2]["directive"]["endpoint"]["endpointId"] == "light#kitchen", "the light's directive goes to Home Assistant")
ok(r["event"]["endpoint"]["endpointId"] == "light#kitchen", "...and its answer goes back to Alexa")

os.environ.pop("HA_TOKEN")
eps = lf.lambda_handler(disc, None)["event"]["payload"]["endpoints"]
ok(not any("#" in e["endpointId"] for e in eps), "without HA_TOKEN only the TV and the speakers")
r = lf.lambda_handler(d, None)
ok(r["event"]["header"]["name"] == "ErrorResponse", "without HA_TOKEN a house device answers an error, not a crash")
srv.shutdown()
print("test_casa: ok" if not falhas else "test_casa: FAILED")
sys.exit(falhas)
