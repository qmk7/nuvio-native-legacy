// Ver lanweb.h.
#include "lanweb.h"
#include <string.h>

static int hexv(int c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
int lanweb_campo(const char *corpo, const char *nome, char *dst, size_t n) {
  size_t m = nome ? strlen(nome) : 0, k = 0;
  const char *p = corpo;
  if (!dst || !n) return 0;
  dst[0] = 0;
  while (p && *p && m) {
    if (!strncmp(p, nome, m) && p[m] == '=') {
      p += m + 1;
      while (*p && *p != '&') {
        int c;
        if (*p == '+') { c = ' '; p++; }
        else if (*p == '%' && hexv(p[1]) >= 0 && hexv(p[2]) >= 0) { c = hexv(p[1]) * 16 + hexv(p[2]); p += 3; }
        else c = (unsigned char)*p++;
        if (k + 1 < n) dst[k++] = (char)c;
      }
      dst[k] = 0;
      return 1;
    }
    p = strchr(p, '&');
    if (p) p++;
  }
  return 0;
}
void lanweb_escapar(char *dst, size_t n, const char *s) {
  size_t k = 0;
  if (!dst || !n) return;
  for (; s && *s && k + 7 < n; s++) {
    const char *r = *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '&' ? "&amp;" : *s == '"' ? "&quot;" : *s == '\'' ? "&#39;" : NULL;
    if (r) { size_t l = strlen(r); memcpy(dst + k, r, l); k += l; } else dst[k++] = *s;
  }
  dst[k] = 0;
}

#ifdef __EMSCRIPTEN__
int  lanweb_disponivel(void) { return 0; }
int  lanweb_registrar(const char *a, const char *h, size_t m) { (void)a; (void)h; (void)m; return 0; }
void lanweb_remover(const char *a) { (void)a; }
const char *lanweb_url(const char *a) { (void)a; return ""; }
void lanweb_renovar(const char *a) { (void)a; }
void lanweb_publicar(const char *a, const char *j) { (void)a; (void)j; }
void lanweb_arquivo(const char *a, const void *d, size_t n, const char *nm) { (void)a; (void)d; (void)n; (void)nm; }
int  lanweb_proximo(LwPedido *p) { (void)p; return 0; }
void lanweb_liberar(LwPedido *p) { (void)p; }
void lanweb_parar(void) {}
int  lanweb_porta(void) { return 0; }
const char *lanweb_teste_token(const char *a) { (void)a; return ""; }
int  lanweb_registrar_token(const char *a, const char *h, size_t m, const char *t) { (void)a; (void)h; (void)m; (void)t; return 0; }
int  lanweb_gerar_token(char *d, size_t n) { (void)d; (void)n; return 0; }
void lanweb_porta_preferida(int p) { (void)p; }
#else

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define LW_TOKEN_N  12
#define LW_CAB_MAX  8192
#define LW_PRAZO_MS 8000

typedef struct {
  char nome[16], token[LW_TOKEN_N + 1];
  char *html, *estado, *arq, nomeArq[64];
  size_t arqN, maxCorpo;
} App;

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static pthread_t fio;
static int fioVivo, ouvir = -1, ouvir2 = -1, acorda[2] = { -1, -1 }, porta, erros;
static volatile int noAr;
static char hostLan[64], hostLocal[64], origem[96], urlTmp[LW_APPS][128];
static char hostAntigo[64], localAntigo[64], origemAntiga[96];   // the stand-in port, still served
static App apps[LW_APPS];
static LwPedido fila[LW_FILA];
static int filaIni, filaN;

int lanweb_disponivel(void) { return 1; }
int lanweb_porta(void) { return porta; }
static int portaPreferida;
void lanweb_porta_preferida(int p) { portaPreferida = p > 1024 && p < 65536 ? p : 0; }
// The fixed port and three spares (p, p+10000, p+20000, p+30000): something
// on the TV can keep the first one; Home Assistant tries the four in order.
#define LW_FIXAS 4
static int portaFixa(int i) { int p = portaPreferida + i * 10000; return portaPreferida && p < 65536 ? p : 0; }
static int ehFixa(int p) { for (int i = 0; i < LW_FIXAS; i++) if (p && p == portaFixa(i)) return 1; return 0; }
static int ligarFixa(int s, int *qual) {
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY);
  for (int i = 0; i < LW_FIXAS; i++) {
    if (!portaFixa(i)) continue;
    a.sin_port = htons((unsigned short)portaFixa(i));
    if (!bind(s, (struct sockaddr *)&a, sizeof a)) { *qual = portaFixa(i); return 1; }
    printf("[lanweb] porta %d ocupada (errno %d)\n", portaFixa(i), errno);
  }
  return 0;
}

// ---------------------------------------------------------------- network bits
static int privado(in_addr_t a) {
  unsigned long h = ntohl(a);
  return (h >> 24) == 10 || (h >> 20) == 0xAC1 || (h >> 16) == 0xC0A8;
}
static int ipLan(char *dst, size_t n) {
  struct sockaddr_in alvo, eu;
  socklen_t tam = sizeof eu;
  struct ifaddrs *lista = NULL, *i;
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  in_addr_t achou = 0;
  if (s >= 0) {
    memset(&alvo, 0, sizeof alvo);
    alvo.sin_family = AF_INET; alvo.sin_port = htons(9);
    inet_pton(AF_INET, "192.0.2.1", &alvo.sin_addr);   // TEST-NET: chooses the route, sends nothing
    if (!connect(s, (struct sockaddr *)&alvo, sizeof alvo) &&
        !getsockname(s, (struct sockaddr *)&eu, &tam) && privado(eu.sin_addr.s_addr))
      achou = eu.sin_addr.s_addr;
    close(s);
  }
  if (!achou && !getifaddrs(&lista)) {
    for (i = lista; i; i = i->ifa_next) {
      if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
      if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
      if (privado(((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr)) {
        achou = ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr; break;
      }
    }
    freeifaddrs(lista);
  }
  if (!achou) {
#ifdef NV_LINUX_DESKTOP
    snprintf(dst, n, "127.0.0.1");   // desktop tests without a LAN
    return 1;
#else
    return 0;
#endif
  }
  { struct in_addr a; a.s_addr = achou; return inet_ntop(AF_INET, &a, dst, (socklen_t)n) != NULL; }
}
static int gerarToken(char *dst) {
  static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";
  unsigned char b[LW_TOKEN_N * 2];
  int f = open("/dev/urandom", O_RDONLY), k = 0;
  size_t lidos = 0;
  if (f < 0) return 0;
  while (lidos < sizeof b) {
    ssize_t r = read(f, b + lidos, sizeof b - lidos);
    if (r <= 0) { close(f); return 0; }
    lidos += (size_t)r;
  }
  close(f);
  for (int i = 0; i < (int)sizeof b && k < LW_TOKEN_N; i++) if (b[i] < 248) dst[k++] = A[b[i] % 31];
  dst[k] = 0;
  return k == LW_TOKEN_N;
}

// ---------------------------------------------------------------- HTTP
static long agoraMs(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec * 1000L + tv.tv_usec / 1000; }
static int esperar(int fd, long prazo) {
  for (;;) {
    struct pollfd p[2];
    long falta = prazo - agoraMs();
    int r;
    if (falta <= 0) return 0;
    p[0].fd = fd; p[0].events = POLLIN; p[0].revents = 0;
    p[1].fd = acorda[0]; p[1].events = POLLIN; p[1].revents = 0;
    r = poll(p, 2, (int)(falta > 500 ? 500 : falta));
    if (r < 0 && errno == EINTR) continue;
    if (r < 0 || (p[1].revents & POLLIN)) return 0;
    if (p[0].revents & (POLLIN | POLLHUP | POLLERR)) return 1;
  }
}
static void enviarTudo(int c, const char *b, size_t n) {
  while (n) { ssize_t w = send(c, b, n, MSG_NOSIGNAL); if (w <= 0) return; b += w; n -= (size_t)w; }
}
static void responder(int c, int cod, const char *tipo, const char *corpo, size_t n, const char *extra) {
  char cab[900];
  const char *txt = cod == 200 ? "OK" : cod == 403 ? "Forbidden" : cod == 404 ? "Not Found"
                  : cod == 405 ? "Method Not Allowed" : cod == 413 ? "Payload Too Large"
                  : cod == 503 ? "Service Unavailable" : "Bad Request";
  int k = snprintf(cab, sizeof cab,
    "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
    "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: same-origin\r\n"
    "X-Frame-Options: DENY\r\n"
    "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
    "connect-src 'self'; img-src 'self' data: blob:; form-action 'self'; frame-ancestors 'none'\r\n"
    "%sConnection: close\r\n\r\n", cod, txt, tipo ? tipo : "text/plain; charset=utf-8", n, extra ? extra : "");
  enviarTudo(c, cab, (size_t)k);
  if (n && corpo) enviarTudo(c, corpo, n);
}
static void responderTxt(int c, int cod, const char *t) { responder(c, cod, "text/plain; charset=utf-8", t, t ? strlen(t) : 0, NULL); }
static int cabecalho(const char *cab, const char *nome, char *dst, size_t n) {
  size_t m = strlen(nome);
  const char *p = cab;
  while ((p = strstr(p, "\r\n")) != NULL) {
    p += 2;
    if (!strncasecmp(p, nome, m) && p[m] == ':') {
      const char *v = p + m + 1, *f;
      while (*v == ' ' || *v == '\t') v++;
      f = strstr(v, "\r\n");
      if (!f) f = v + strlen(v);
      snprintf(dst, n, "%.*s", (int)(f - v), v);
      return 1;
    }
  }
  return 0;
}
static App *appDoToken(const char *t, size_t n) {
  for (int i = 0; i < LW_APPS; i++)
    if (apps[i].nome[0] && strlen(apps[i].token) == n && !memcmp(apps[i].token, t, n)) return &apps[i];
  return NULL;
}
static void enfileirar(const App *a, const char *rota, const char *cli, char *corpo, size_t n) {
  LwPedido *p;
  if (filaN == LW_FILA) { free(corpo); return; }   // full: dropped (the page retries)
  p = &fila[(filaIni + filaN++) % LW_FILA];
  memset(p, 0, sizeof *p);
  snprintf(p->app, sizeof p->app, "%s", a->nome);
  snprintf(p->rota, sizeof p->rota, "%s", rota);
  snprintf(p->cliente, sizeof p->cliente, "%s", cli);
  p->corpo = corpo; p->n = n;
}

static void atender(int c, const char *cli) {
  static char cab[LW_CAB_MAX + 1];
  char metodo[8], caminho[200], host[96], orig[128], tam[24];
  long prazo = agoraMs() + LW_PRAZO_MS, cl;
  size_t n = 0, fimCab, temCorpo;
  char *sep, *rota, *corpo;
  App *a;
  memset(cab, 0, sizeof cab);
  for (;;) {
    ssize_t k;
    if (n >= LW_CAB_MAX) { responderTxt(c, 400, NULL); return; }
    if (!esperar(c, prazo)) return;
    k = recv(c, cab + n, LW_CAB_MAX - n, 0);
    if (k <= 0) return;
    n += (size_t)k; cab[n] = 0;
    if ((sep = strstr(cab, "\r\n\r\n")) != NULL) break;
  }
  fimCab = (size_t)(sep - cab) + 4;
  temCorpo = n - fimCab;
  *sep = 0;
  if (sscanf(cab, "%7s %199s", metodo, caminho) != 2) { responderTxt(c, 400, NULL); return; }
  if (!cabecalho(cab, "Host", host, sizeof host) || (strcmp(host, hostLan) && strcmp(host, hostLocal) &&
      (!hostAntigo[0] || (strcmp(host, hostAntigo) && strcmp(host, localAntigo))))) {
    puts("[lanweb] recusado: Host de fora"); responderTxt(c, 403, NULL); return;
  }
  if (cabecalho(cab, "Origin", orig, sizeof orig)) {
    char o2[96];
    snprintf(o2, sizeof o2, "http://%s", hostLocal);
    if (strcmp(orig, origem) && strcmp(orig, o2) && (!origemAntiga[0] || strcmp(orig, origemAntiga))) { puts("[lanweb] recusado: Origin de fora"); responderTxt(c, 403, NULL); return; }
  }
  { char *q = strchr(caminho, '?'); if (q) *q = 0; }
  if (caminho[0] != '/') { responderTxt(c, 400, NULL); return; }
  rota = strchr(caminho + 1, '/');
  pthread_mutex_lock(&trava);
  a = appDoToken(caminho + 1, rota ? (size_t)(rota - caminho - 1) : strlen(caminho + 1));
  if (!a) {
    pthread_mutex_unlock(&trava);
    if (strcmp(caminho, "/favicon.ico")) erros++;
    responderTxt(c, 404, NULL);
    return;
  }
  if (!rota) {   // "/<token>" -> "/<token>/" so relative fetches work
    char loc[160];
    snprintf(loc, sizeof loc, "Location: /%s/\r\n", a->token);
    pthread_mutex_unlock(&trava);
    responder(c, 302, NULL, NULL, 0, loc);
    return;
  }
  rota++;
  if (!strcmp(metodo, "GET") || !strcmp(metodo, "HEAD")) {
    int g = metodo[0] == 'G';
    if (!rota[0]) {
      char *h = a->html ? strdup(a->html) : NULL;
      printf("[lanweb] pagina %s aberta\n", a->nome);
      pthread_mutex_unlock(&trava);
      responder(c, h ? 200 : 503, "text/html; charset=utf-8", g ? h : NULL, h ? strlen(h) : 0, NULL);
      free(h);
    } else if (!strcmp(rota, "estado")) {
      char *e = strdup(a->estado ? a->estado : "{}");
      pthread_mutex_unlock(&trava);
      responder(c, 200, "application/json; charset=utf-8", g ? e : NULL, e ? strlen(e) : 0, NULL);
      free(e);
    } else if (!strcmp(rota, "arquivo") && a->arq) {
      size_t an = a->arqN;
      char *d = malloc(an ? an : 1), disp[160];
      if (d) memcpy(d, a->arq, an);
      snprintf(disp, sizeof disp, "Content-Disposition: attachment; filename=\"%s\"\r\n", a->nomeArq);
      pthread_mutex_unlock(&trava);
      if (d) responder(c, 200, "application/octet-stream", g ? d : NULL, an, disp);
      else responderTxt(c, 503, NULL);
      free(d);
    } else {
      pthread_mutex_unlock(&trava);
      responderTxt(c, 404, NULL);
    }
    return;
  }
  if (strcmp(metodo, "POST")) { pthread_mutex_unlock(&trava); responder(c, 405, NULL, NULL, 0, "Allow: GET, POST\r\n"); return; }
  {
    size_t max = a->maxCorpo;
    App copia = *a;
    pthread_mutex_unlock(&trava);
    if (!cabecalho(cab, "Content-Length", tam, sizeof tam)) { responderTxt(c, 400, NULL); return; }
    cl = strtol(tam, NULL, 10);
    if (cl < 0 || (size_t)cl > max) { responderTxt(c, 413, NULL); return; }
    corpo = malloc((size_t)cl + 1);
    if (!corpo) { responderTxt(c, 503, NULL); return; }
    if (temCorpo > (size_t)cl) temCorpo = (size_t)cl;
    memcpy(corpo, cab + fimCab, temCorpo);
    while ((long)temCorpo < cl) {
      ssize_t k;
      if (!esperar(c, prazo + 30000)) { free(corpo); return; }   // uploads (photos) get longer
      k = recv(c, corpo + temCorpo, (size_t)cl - temCorpo, 0);
      if (k <= 0) { free(corpo); return; }
      temCorpo += (size_t)k;
    }
    corpo[temCorpo] = 0;
    printf("[lanweb] %zu bytes para %s/%s\n", temCorpo, copia.nome, rota);
    pthread_mutex_lock(&trava);
    if (appDoToken(copia.token, strlen(copia.token))) enfileirar(&copia, rota, cli, corpo, temCorpo);
    else free(corpo);
    pthread_mutex_unlock(&trava);
    responder(c, 200, "application/json; charset=utf-8", "{\"ok\":true}", 11, NULL);
  }
}

// The fixed port was taken at the start (often the previous run of the app
// still closing): keep trying it, and once it is ours serve it too and show
// it in the links, so Home Assistant and bookmarks find the TV again.
static int abrirPreferida(void) {
  int um = 1, s, nova = 0;
  if (!portaPreferida || ehFixa(porta) || ouvir2 >= 0) return 0;
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return 0;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &um, sizeof um);
  if (!ligarFixa(s, &nova) || listen(s, 8)) { close(s); return 0; }
  ouvir2 = s;
  pthread_mutex_lock(&trava);
  {
    char ip[64];
    snprintf(ip, sizeof ip, "%s", hostLan);
    char *dp = strrchr(ip, ':');
    if (dp) *dp = 0;
    snprintf(hostAntigo, sizeof hostAntigo, "%s", hostLan);
    snprintf(localAntigo, sizeof localAntigo, "%s", hostLocal);
    snprintf(origemAntiga, sizeof origemAntiga, "%s", origem);
    porta = nova;
    snprintf(hostLan, sizeof hostLan, "%.47s:%d", ip, porta);
    snprintf(hostLocal, sizeof hostLocal, "127.0.0.1:%d", porta);
    snprintf(origem, sizeof origem, "http://%s", hostLan);
  }
  pthread_mutex_unlock(&trava);
  printf("[lanweb] porta fixa %d livre: links voltam para ela\n", nova);
  fflush(stdout);
  return 1;
}

static void *servir(void *arg) {
  time_t ultimaTentativa = 0;
  (void)arg;
  while (noAr) {
    struct pollfd p[3];
    int r, np = 2, qual = -1;
    if (portaPreferida && !ehFixa(porta) && time(NULL) - ultimaTentativa >= 3) {
      ultimaTentativa = time(NULL);
      abrirPreferida();
    }
    p[0].fd = ouvir; p[0].events = POLLIN; p[0].revents = 0;
    p[1].fd = acorda[0]; p[1].events = POLLIN; p[1].revents = 0;
    if (ouvir2 >= 0) { p[2].fd = ouvir2; p[2].events = POLLIN; p[2].revents = 0; np = 3; }
    r = poll(p, (nfds_t)np, 1000);
    if (r < 0 && errno == EINTR) continue;
    if (r < 0 || (p[1].revents & POLLIN)) break;
    if (p[0].revents & POLLIN) qual = ouvir;
    else if (np == 3 && (p[2].revents & POLLIN)) qual = ouvir2;
    if (qual >= 0) {
      struct sockaddr_in de;
      socklen_t dt = sizeof de;
      char cli[48];
      int c = accept(qual, (struct sockaddr *)&de, &dt);
      if (c < 0) continue;
#ifdef SO_NOSIGPIPE
      { int um = 1; setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &um, sizeof um); }
#endif
      if (de.sin_family != AF_INET || (!privado(de.sin_addr.s_addr) && (ntohl(de.sin_addr.s_addr) >> 24) != 127)) {
        close(c); continue;   // only the home network
      }
      inet_ntop(AF_INET, &de.sin_addr, cli, sizeof cli);
      atender(c, cli);
      close(c);
      if (erros >= LW_ERROS_MAX) {
        puts("[lanweb] caminhos errados demais: servidor fechado");
        noAr = 0;
        close(ouvir); ouvir = -1;   // new connections are refused, not left waiting
        if (ouvir2 >= 0) { close(ouvir2); ouvir2 = -1; }
        break;
      }
    }
  }
  return NULL;
}

static int subir(void) {
  struct sockaddr_in a;
  socklen_t tam = sizeof a;
  char ip[48];
  int um = 1;
  if (noAr) return 1;
  if (!ipLan(ip, sizeof ip)) { puts("[lanweb] sem IP de rede local"); return 0; }
  if (pipe(acorda)) { acorda[0] = acorda[1] = -1; return 0; }
  ouvir = socket(AF_INET, SOCK_STREAM, 0);
  if (ouvir < 0) { lanweb_parar(); return 0; }
  setsockopt(ouvir, SOL_SOCKET, SO_REUSEADDR, &um, sizeof um);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = 0;
  // A fixed port keeps bookmarked links (the settings page) working across
  // restarts; all four taken, any free port for now (servir keeps trying).
  { int q = 0; if (portaPreferida && ligarFixa(ouvir, &q)) goto ligada; }
  if (bind(ouvir, (struct sockaddr *)&a, sizeof a)) goto falhou;
ligada:
  if (listen(ouvir, 8) || getsockname(ouvir, (struct sockaddr *)&a, &tam)) {
  falhou:
    printf("[lanweb] servidor nao subiu (errno %d)\n", errno);
    lanweb_parar();
    return 0;
  }
  porta = ntohs(a.sin_port);
  snprintf(hostLan, sizeof hostLan, "%s:%d", ip, porta);
  snprintf(hostLocal, sizeof hostLocal, "127.0.0.1:%d", porta);
  snprintf(origem, sizeof origem, "http://%s", hostLan);
  erros = 0;
  noAr = 1;
  if (pthread_create(&fio, NULL, servir, NULL)) { noAr = 0; lanweb_parar(); return 0; }
  fioVivo = 1;
  printf("[lanweb] servidor no ar (porta %d)\n", porta);
  fflush(stdout);
  return 1;
}

void lanweb_parar(void) {
  noAr = 0;
  if (fioVivo) {
    if (acorda[1] >= 0) { char b = 1; ssize_t w = write(acorda[1], &b, 1); (void)w; }
    pthread_join(fio, NULL);
    fioVivo = 0;
  }
  if (ouvir >= 0) { close(ouvir); ouvir = -1; }
  if (ouvir2 >= 0) { close(ouvir2); ouvir2 = -1; }
  hostAntigo[0] = localAntigo[0] = origemAntiga[0] = 0;
  if (acorda[0] >= 0) { close(acorda[0]); close(acorda[1]); acorda[0] = acorda[1] = -1; }
  porta = 0;
}

static App *acharApp(const char *nome) {
  for (int i = 0; i < LW_APPS; i++) if (apps[i].nome[0] && !strcmp(apps[i].nome, nome)) return &apps[i];
  return NULL;
}
int lanweb_registrar(const char *app, const char *html, size_t maxCorpo) {
  App *a;
  if (!app || !app[0] || strlen(app) >= sizeof apps[0].nome || !html) return 0;
  if (!noAr) { lanweb_parar(); if (!subir()) return 0; }
  pthread_mutex_lock(&trava);
  a = acharApp(app);
  if (!a) {
    for (int i = 0; i < LW_APPS && !a; i++) if (!apps[i].nome[0]) a = &apps[i];
    if (!a || !gerarToken(a ? a->token : NULL)) { pthread_mutex_unlock(&trava); return 0; }
    snprintf(a->nome, sizeof a->nome, "%s", app);
  }
  free(a->html);
  a->html = strdup(html);
  a->maxCorpo = maxCorpo ? maxCorpo : LW_CORPO_PADRAO;
  pthread_mutex_unlock(&trava);
  return a->html != NULL;
}
void lanweb_remover(const char *app) {
  App *a;
  pthread_mutex_lock(&trava);
  if ((a = acharApp(app)) != NULL) { free(a->html); free(a->estado); free(a->arq); memset(a, 0, sizeof *a); }
  pthread_mutex_unlock(&trava);
}
int lanweb_registrar_token(const char *app, const char *html, size_t maxCorpo, const char *token) {
  App *a;
  size_t n = token ? strlen(token) : 0;
  if (n != LW_TOKEN_N) return 0;
  for (size_t i = 0; i < n; i++) if (!((token[i] >= 'a' && token[i] <= 'z') || (token[i] >= '2' && token[i] <= '9'))) return 0;
  if (!lanweb_registrar(app, html, maxCorpo)) return 0;
  pthread_mutex_lock(&trava);
  if ((a = acharApp(app)) != NULL) snprintf(a->token, sizeof a->token, "%s", token);
  pthread_mutex_unlock(&trava);
  return a != NULL;
}
int lanweb_gerar_token(char *dst, size_t n) { return n > LW_TOKEN_N && gerarToken(dst); }
void lanweb_renovar(const char *app) {
  App *a;
  pthread_mutex_lock(&trava);
  if ((a = acharApp(app)) != NULL) gerarToken(a->token);
  pthread_mutex_unlock(&trava);
}
const char *lanweb_url(const char *app) {
  App *a;
  const char *r = "";
  pthread_mutex_lock(&trava);
  a = noAr ? acharApp(app) : NULL;
  if (a) {
    int i = (int)(a - apps);
    snprintf(urlTmp[i], sizeof urlTmp[i], "http://%s/%s/", hostLan, a->token);
    r = urlTmp[i];
  }
  pthread_mutex_unlock(&trava);
  return r;
}
const char *lanweb_teste_token(const char *app) { App *a = acharApp(app); return a ? a->token : ""; }
void lanweb_publicar(const char *app, const char *json) {
  App *a;
  pthread_mutex_lock(&trava);
  if ((a = acharApp(app)) != NULL) { free(a->estado); a->estado = json ? strdup(json) : NULL; }
  pthread_mutex_unlock(&trava);
}
void lanweb_arquivo(const char *app, const void *dados, size_t n, const char *nome) {
  App *a;
  pthread_mutex_lock(&trava);
  if ((a = acharApp(app)) != NULL) {
    free(a->arq); a->arq = NULL; a->arqN = 0;
    if (dados && n && (a->arq = malloc(n)) != NULL) { memcpy(a->arq, dados, n); a->arqN = n; }
    snprintf(a->nomeArq, sizeof a->nomeArq, "%s", nome ? nome : "arquivo.bin");
  }
  pthread_mutex_unlock(&trava);
}
int lanweb_proximo(LwPedido *p) {
  int r = 0;
  pthread_mutex_lock(&trava);
  if (filaN) { *p = fila[filaIni]; filaIni = (filaIni + 1) % LW_FILA; filaN--; r = 1; }
  pthread_mutex_unlock(&trava);
  return r;
}
void lanweb_liberar(LwPedido *p) {
  if (!p) return;
  if (p->corpo) { memset(p->corpo, 0, p->n); free(p->corpo); }
  p->corpo = NULL; p->n = 0;
}
#endif
