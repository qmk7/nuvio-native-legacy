// QR TIME phase 8: credentials never reach a log line. rede_texto_redigido
// (media payloads, error text) and rede_url_publica on the URL shapes IPTV,
// Subsonic and the token APIs use.
#include "rede.h"
#include <stdio.h>
#include <string.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static int limpo(const char *s) {
  static const char *const segredos[] = { "u5er", "pa55", "tok3n", "s4lt", "k3y", "AKIA", NULL };
  for (int i = 0; segredos[i]; i++) if (strstr(s, segredos[i])) return 0;
  return 1;
}

int main(void) {
  char b[512], small[24];
  const char *xt = "{\"uri\":\"http://iptv.example.net:8080/live/u5er/pa55/1234.ts\",\"type\":\"media\"}";
  rede_texto_redigido(xt, b, sizeof b);
  CONFERE(limpo(b) && strstr(b, "http://iptv.example.net:8080/...") && strstr(b, "\"type\":\"media\""),
          "Xtream live URL in a uMS payload: host kept, user/password gone, rest of the JSON intact");
  rede_texto_redigido("{\"uri\":\"https:\\/\\/u5er:pa55@cdn.example\\/movie\\/u5er\\/pa55\\/9.mkv?token=tok3n\"}", b, sizeof b);
  CONFERE(limpo(b) && strstr(b, "https://cdn.example/..."), "JSON-escaped URL with userinfo and query: redacted");
  rede_texto_redigido("a https://nd.local/rest/stream.view?u=u5er&t=tok3n&s=s4lt&id=1 and http://h2/get.php?username=u5er&password=pa55 end",
                      b, sizeof b);
  CONFERE(limpo(b) && strstr(b, "https://nd.local/...") && strstr(b, "http://h2/...") && strstr(b, " end"),
          "two links in free text (Subsonic token auth, Xtream get.php): both redacted");
  rede_texto_redigido("https://www.googleapis.com/youtube/v3/search?key=k3y&q=x", b, sizeof b);
  CONFERE(limpo(b), "API key in the query: gone");
  rede_texto_redigido("no links here, code=5", b, sizeof b);
  CONFERE(!strcmp(b, "no links here, code=5"), "text without links is unchanged");
  rede_texto_redigido("x http://iptv.example.net/live/u5er/pa55/1.ts", small, sizeof small);
  CONFERE(strlen(small) < sizeof small && limpo(small), "small buffer: truncated, terminated, still clean");
  rede_texto_redigido(NULL, b, sizeof b);
  CONFERE(!b[0], "NULL in: empty out");
  rede_url_publica("http://u5er:pa55@host:81/a/b", b, sizeof b);
  CONFERE(!strcmp(b, "http://host:81/..."), "rede_url_publica: userinfo dropped");
  return falhas ? 1 : 0;
}
