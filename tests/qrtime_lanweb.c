// QR TIME phone pages server (src/lanweb.c): tokens per page, Host/Origin
// checks, size limits, the request queue, state and file downloads.
//   bash tests/qrtime_lanweb.sh
#include "lanweb.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static char resp[70000];

static int pedir(const char *metodo, const char *caminho, const char *host, const char *origem, const char *corpo) {
  struct sockaddr_in a;
  char req[4096], h[64];
  int s = socket(AF_INET, SOCK_STREAM, 0), n = 0, k, cod = 0;
  memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((unsigned short)lanweb_porta());
  inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  { struct timeval tv = { 3, 0 }; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); }
  if (connect(s, (struct sockaddr *)&a, sizeof a)) { close(s); return -1; }
  snprintf(h, sizeof h, "127.0.0.1:%d", lanweb_porta());
  k = snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: %s\r\n%s%s%s", metodo, caminho, host ? host : h,
               origem ? "Origin: " : "", origem ? origem : "", origem ? "\r\n" : "");
  if (corpo) k += snprintf(req + k, sizeof req - k, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %zu\r\n\r\n%s", strlen(corpo), corpo);
  else k += snprintf(req + k, sizeof req - k, "\r\n");
  send(s, req, (size_t)k, 0);
  while ((k = (int)recv(s, resp + n, sizeof resp - 1 - (size_t)n, 0)) > 0) n += k;
  resp[n] = 0;
  close(s);
  sscanf(resp, "HTTP/1.1 %d", &cod);
  return cod;
}

int main(void) {
  char c1[64], c2[64], b[128];
  LwPedido p;
  CONFERE(lanweb_registrar("controle", "<html>remote</html>", 64) && lanweb_registrar("festa", "<html>party</html>", 0), "two pages up");
  CONFERE(strstr(lanweb_url("controle"), "http://") && strcmp(lanweb_teste_token("controle"), lanweb_teste_token("festa")) && strlen(lanweb_teste_token("controle")) == 12,
          "each page has its own 12-symbol token");
  snprintf(c1, sizeof c1, "/%s/", lanweb_teste_token("controle"));
  snprintf(c2, sizeof c2, "/%s/", lanweb_teste_token("festa"));
  CONFERE(pedir("GET", c1, NULL, NULL, NULL) == 200 && strstr(resp, "remote") && strstr(resp, "Content-Security-Policy"), "page served with CSP");
  CONFERE(pedir("GET", "/abcdefghjkmn/", NULL, NULL, NULL) == 404, "wrong token: 404");
  CONFERE(pedir("GET", c1, "evil.example:80", NULL, NULL) == 403, "foreign Host (DNS rebinding): 403");
  snprintf(b, sizeof b, "%stecla", c1);
  CONFERE(pedir("POST", b, NULL, "http://evil.example", "k=up") == 403, "foreign Origin: 403");
  CONFERE(pedir("POST", b, NULL, NULL, "k=up") == 200, "POST accepted");
  CONFERE(lanweb_proximo(&p) && !strcmp(p.app, "controle") && !strcmp(p.rota, "tecla") && !strcmp(p.corpo, "k=up") && !strcmp(p.cliente, "127.0.0.1"),
          "queued for the app thread with app, route, body, client");
  lanweb_liberar(&p);
  CONFERE(!lanweb_proximo(&p), "queue empty after");
  CONFERE(pedir("POST", b, NULL, NULL, "k=0123456789012345678901234567890123456789012345678901234567890123456789") == 413 && !lanweb_proximo(&p),
          "body over the page's limit: 413, nothing queued");
  lanweb_publicar("festa", "{\"fila\":[]}");
  snprintf(b, sizeof b, "%sestado", c2);
  CONFERE(pedir("GET", b, NULL, NULL, NULL) == 200 && strstr(resp, "application/json") && strstr(resp, "{\"fila\":[]}"), "state JSON");
  lanweb_arquivo("festa", "ABC\0DEF", 7, "x.bin");
  snprintf(b, sizeof b, "%sarquivo", c2);
  CONFERE(pedir("GET", b, NULL, NULL, NULL) == 200 && strstr(resp, "attachment; filename=\"x.bin\"") && strstr(resp, "Content-Length: 7"), "file download");
  lanweb_renovar("controle");
  CONFERE(pedir("GET", c1, NULL, NULL, NULL) == 404, "renewed: the old link stops");
  snprintf(c1, sizeof c1, "/%s/", lanweb_teste_token("controle"));
  CONFERE(pedir("GET", c1, NULL, NULL, NULL) == 200, "...and the new one works");
  {
    char d[64];
    CONFERE(lanweb_campo("a=1&nome=Ol%C3%A1+mundo&x=", "nome", d, sizeof d) && !strcmp(d, "Olá mundo") &&
            lanweb_campo("a=1&x=", "x", d, sizeof d) && !d[0] && !lanweb_campo("a=1", "nome", d, sizeof d), "form field decoding");
    lanweb_escapar(d, sizeof d, "<b>\"x\"&'");
    CONFERE(!strcmp(d, "&lt;b&gt;&quot;x&quot;&amp;&#39;"), "HTML escaping");
  }
  for (int i = 0; i < LW_ERROS_MAX + 2; i++) pedir("GET", "/zzzzzzzzzzzz/", NULL, NULL, NULL);
  CONFERE(pedir("GET", c1, NULL, NULL, NULL) != 200, "too many wrong paths: server closes");
  lanweb_parar();
  {
    // The fixed port is taken at the start (the previous run still closing):
    // a stand-in port first, then the fixed one once it is free again.
    struct sockaddr_in a;
    int um = 1, dono = socket(AF_INET, SOCK_STREAM, 0), pref = 0, antes;
    socklen_t t = sizeof a;
    char u[160];
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY);
    setsockopt(dono, SOL_SOCKET, SO_REUSEADDR, &um, sizeof um);
    if (!bind(dono, (struct sockaddr *)&a, sizeof a) && !listen(dono, 1) && !getsockname(dono, (struct sockaddr *)&a, &t)) pref = ntohs(a.sin_port);
    lanweb_porta_preferida(pref);
    CONFERE(pref && lanweb_registrar("ajustes", "<html>settings</html>", 64) && lanweb_porta() != pref, "fixed port taken: up on a stand-in port");
    antes = lanweb_porta();
    close(dono);
    for (int i = 0; i < 60 && lanweb_porta() != pref; i++) usleep(100000);
    snprintf(u, sizeof u, ":%d/", pref);
    CONFERE(lanweb_porta() == pref && strstr(lanweb_url("ajustes"), u), "fixed port free again: taken, and the link shows it");
    snprintf(c1, sizeof c1, "/%s/", lanweb_teste_token("ajustes"));
    CONFERE(pedir("GET", c1, NULL, NULL, NULL) == 200 && strstr(resp, "settings"), "served on the fixed port");
    {
      char h[64];
      snprintf(h, sizeof h, "127.0.0.1:%d", antes);
      // pedir() connects to lanweb_porta(); the stand-in's Host is still accepted
      CONFERE(pedir("GET", c1, h, NULL, NULL) == 200, "the stand-in port's links keep working");
    }
    lanweb_parar();
  }
  return falhas ? 1 : 0;
}
