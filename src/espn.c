#include "espn.h"
#include "dados.h"
#include "js.h"
#include "rede.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const EspLiga LIGAS[] = {
  { "soccer/ksa.1",          "soccer",     "Saudi Pro League",      1 },
  { "soccer/eng.1",          "soccer",     "Premier League",        1 },
  { "soccer/esp.1",          "soccer",     "LaLiga",                1 },
  { "soccer/ita.1",          "soccer",     "Serie A",               0 },
  { "soccer/ger.1",          "soccer",     "Bundesliga",            0 },
  { "soccer/fra.1",          "soccer",     "Ligue 1",               0 },
  { "soccer/uefa.champions", "soccer",     "UEFA Champions League", 1 },
  { "soccer/uefa.europa",    "soccer",     "UEFA Europa League",    0 },
  { "soccer/afc.champions",  "soccer",     "AFC Champions League",  1 },
  { "soccer/fifa.world",     "soccer",     "FIFA World Cup",        0 },
  { "soccer/egy.1",          "soccer",     "Egyptian Premier League", 0 },
  { "basketball/nba",        "basketball", "NBA",                   1 },
  { "basketball/wnba",       "basketball", "WNBA",                  0 },
  { "tennis/atp",            "tennis",     "ATP",                   0 },
  { "tennis/wta",            "tennis",     "WTA",                   0 },
};
int espn_ligas(const EspLiga **out) {
  if (out) *out = LIGAS;
  return (int)(sizeof LIGAS / sizeof *LIGAS);
}
const EspLiga *espn_liga(const char *chave) {
  for (size_t i = 0; chave && i < sizeof LIGAS / sizeof *LIGAS; i++) if (!strcmp(LIGAS[i].chave, chave)) return &LIGAS[i];
  return NULL;
}

// Days from the civil date (UTC), no timegm dependency.
static long long diasDe(int a, int m, int d) {
  a -= m <= 2;
  long long era = (a >= 0 ? a : a - 399) / 400;
  unsigned yoe = (unsigned)(a - era * 400);
  unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long long)doe - 719468;
}
time_t espn_instante(const char *iso) {
  int a, m, d, h = 0, mi = 0, s = 0, n;
  char z = 0;
  if (!iso) return 0;
  n = sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d", &a, &m, &d, &h, &mi, &s);
  if (n < 5) return 0;
  if (n == 5) s = 0;
  // Must be UTC ("Z"); an explicit offset is applied.
  {
    const char *t = strchr(iso, 'T');
    const char *p = t ? t + 1 : iso;
    while (*p && (isdigit((unsigned char)*p) || *p == ':' || *p == '.')) p++;
    z = *p;
    if (z == '+' || z == '-') {
      int oh = 0, om = 0;
      if (sscanf(p + 1, "%2d:%2d", &oh, &om) >= 1 || sscanf(p + 1, "%2d%2d", &oh, &om) >= 1) {
        int off = oh * 3600 + om * 60;
        return (time_t)((diasDe(a, m, d) * 86400LL) + h * 3600 + mi * 60 + s - (z == '+' ? off : -off));
      }
    } else if (z && z != 'Z') return 0;
  }
  if (a < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
  return (time_t)(diasDe(a, m, d) * 86400LL + h * 3600 + mi * 60 + s);
}
void espn_janela(time_t agora, char *dst, size_t tam) {
  time_t de = agora - 7 * 86400, ate = agora + 7 * 86400;
  struct tm a, b;
  gmtime_r(&de, &a);
  gmtime_r(&ate, &b);
  snprintf(dst, tam, "%04d%02d%02d-%04d%02d%02d", a.tm_year + 1900, a.tm_mon + 1, a.tm_mday,
           b.tm_year + 1900, b.tm_mon + 1, b.tm_mday);
}

static void copiar(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s ? s : ""); }
static void https(char *u) {   // ESPN logos come as http sometimes
  if (!strncmp(u, "http://", 7)) { memmove(u + 8, u + 7, strlen(u + 7) + 1); memcpy(u, "https://", 8); }
  if (strncmp(u, "https://", 8)) u[0] = 0;
}
// Value of a depth-1 key of the object at `obj`.
static const char *chaveRaiz(const char *obj, const char *fim, const char *chave) {
  size_t n = strlen(chave);
  int prof = 0;
  for (const char *p = obj; p < fim && *p; p++) {
    if (*p == '"') {
      const char *a = p + 1, *q = a;
      while (q < fim && *q && *q != '"') { if (*q == '\\') q++; q++; }
      if (q >= fim) return NULL;
      if (prof == 1 && (size_t)(q - a) == n && !strncmp(a, chave, n)) {
        const char *v = q + 1;
        while (*v == ' ' || *v == ':' || *v == '\n' || *v == '\t' || *v == '\r') v++;
        return v;
      }
      p = q;
    } else if (*p == '{' || *p == '[') prof++;
    else if (*p == '}' || *p == ']') { if (--prof <= 0) return NULL; }
  }
  return NULL;
}
static int objeto(const char *obj, const char *fim, const char *chave, const char **ini, const char **f) {
  const char *v = chaveRaiz(obj, fim, chave);
  if (!v || (*v != '{' && *v != '[')) return 0;
  *ini = v;
  *f = js_fim(v);
  return *f != NULL;
}
static void lerTime(const char *c, const char *cf, EspTime *t) {
  const char *o, *of;
  memset(t, 0, sizeof *t);
  js_texto_raiz_em(c, cf, "score", t->placar, sizeof t->placar);
  if (!t->placar[0]) {
    // Some feeds send the score as a number or an object {"displayValue"}.
    const char *v = chaveRaiz(c, cf, "score");
    if (v && isdigit((unsigned char)*v)) snprintf(t->placar, sizeof t->placar, "%d", atoi(v));
    else if (v && *v == '{') { const char *vf = js_fim(v); if (vf) js_texto(v, vf, "displayValue", t->placar, sizeof t->placar); }
  }
  {
    const char *w = chaveRaiz(c, cf, "winner");
    t->vencedor = w && !strncmp(w, "true", 4);
  }
  if (objeto(c, cf, "team", &o, &of) || objeto(c, cf, "athlete", &o, &of)) {
    js_texto_raiz_em(o, of, "id", t->id, sizeof t->id);
    js_texto_raiz_em(o, of, "displayName", t->nome, sizeof t->nome);
    js_texto_raiz_em(o, of, "shortDisplayName", t->curto, sizeof t->curto);
    js_texto_raiz_em(o, of, "abbreviation", t->abrev, sizeof t->abrev);
    js_texto_raiz_em(o, of, "logo", t->logo, sizeof t->logo);
    if (!t->logo[0]) {   // tennis players: a country flag
      const char *fo, *ff;
      if (objeto(o, of, "flag", &fo, &ff)) js_texto(fo, ff, "href", t->logo, sizeof t->logo);
    }
    https(t->logo);
  }
  if (!t->curto[0]) copiar(t->curto, sizeof t->curto, t->nome);
}
// One competition (a match) into `e`; 1 when it has two sides.
static int lerCompeticao(const char *c, const char *cf, EspEvento *e) {
  const char *p = js_array(c, cf, "competitors");
  int lado = 0;
  EspTime a, b;
  int casaA = -1;
  memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
  while (p && *p == '{' && lado < 2) {
    const char *f = js_fim(p);
    char ha[8] = "";
    if (!f) break;
    js_texto_raiz_em(p, f, "homeAway", ha, sizeof ha);
    if (lado == 0) { lerTime(p, f, &a); casaA = !strcmp(ha, "home"); }
    else lerTime(p, f, &b);
    lado++;
    p = js_prox(f);
  }
  if (lado < 2 || !a.nome[0] || !b.nome[0]) return 0;
  // Home first (ESPN lists home first for soccer, away first for US sports).
  if (casaA == 0) { e->casa = b; e->fora = a; } else { e->casa = a; e->fora = b; }
  {
    const char *v, *vf;
    if (objeto(c, cf, "venue", &v, &vf)) js_texto_raiz_em(v, vf, "fullName", e->local, sizeof e->local);
  }
  {
    // broadcasts: [{"market":"national","names":["ESPN+"]}]
    const char *bp = js_array(c, cf, "broadcasts");
    size_t n = 0;
    while (bp && *bp == '{' && n + 4 < sizeof e->tv) {
      const char *bf = js_fim(bp), *nm;
      if (!bf) break;
      nm = js_array(bp, bf, "names");
      if (nm && *nm == '"') {   // the first name of each broadcast entry
        char s[48] = "";
        js_cadeia(nm, s, sizeof s);
        if (s[0] && !strstr(e->tv, s)) n += (size_t)snprintf(e->tv + n, sizeof e->tv - n, "%s%s", n ? ", " : "", s);
      }
      bp = js_prox(bf);
    }
  }
  return 1;
}
static void lerStatus(const char *ev, const char *evf, EspEvento *e) {
  const char *s, *sf, *t, *tf;
  char estado[8] = "", nome[40] = "";
  if (!objeto(ev, evf, "status", &s, &sf)) return;
  js_texto_raiz_em(s, sf, "displayClock", e->relogio, sizeof e->relogio);
  if (objeto(s, sf, "type", &t, &tf)) {
    js_texto_raiz_em(t, tf, "state", estado, sizeof estado);
    js_texto_raiz_em(t, tf, "shortDetail", e->detalhe, sizeof e->detalhe);
    js_texto_raiz_em(t, tf, "name", nome, sizeof nome);
  }
  e->estado = !strcmp(estado, "in") ? ESP_AO_VIVO : !strcmp(estado, "post") ? ESP_FIM : ESP_PRE;
  if (strstr(nome, "POSTPONED") || strstr(nome, "CANCELED") || strstr(nome, "CANCELLED") || strstr(nome, "SUSPENDED"))
    e->estado = ESP_ADIADO;
}

int espn_parse(const char *json, const char *liga, int (*cb)(const EspEvento *, void *), void *u) {
  const char *p, *raiz;
  int n = 0;
  if (!json) return -1;
  raiz = json;
  while (*raiz && (unsigned char)*raiz <= ' ') raiz++;
  if (*raiz != '{' || !strstr(raiz, "\"events\"")) return -1;
  p = js_array(raiz, NULL, "events");
  while (p && *p == '{') {
    const char *f = js_fim(p);
    EspEvento base;
    char data[32] = "";
    if (!f) break;
    memset(&base, 0, sizeof base);
    js_texto_raiz_em(p, f, "id", base.id, sizeof base.id);
    copiar(base.liga, sizeof base.liga, liga);
    js_texto_raiz_em(p, f, "date", data, sizeof data);
    base.ini = espn_instante(data);
    lerStatus(p, f, &base);
    {
      // Team sports: competitions[0]. Tennis: groupings[].competitions[] are
      // the matches of a tournament event.
      const char *g = js_array(p, f, "groupings");
      if (g) {
        while (g && *g == '{') {
          const char *gf = js_fim(g), *c;
          if (!gf) break;
          c = js_array(g, gf, "competitions");
          while (c && *c == '{') {
            const char *cf = js_fim(c);
            EspEvento e = base;
            char cid[16] = "", cd[32] = "";
            if (!cf) break;
            js_texto_raiz_em(c, cf, "id", cid, sizeof cid);
            js_texto_raiz_em(c, cf, "date", cd, sizeof cd);
            if (cid[0]) snprintf(e.id, sizeof e.id, "%s", cid);
            if (cd[0]) e.ini = espn_instante(cd);
            lerStatus(c, cf, &e);
            if (e.id[0] && e.ini && lerCompeticao(c, cf, &e)) { n++; if (cb && !cb(&e, u)) return n; }
            c = js_prox(cf);
          }
          g = js_prox(gf);
        }
      } else {
        const char *c = js_array(p, f, "competitions");
        if (c && *c == '{') {
          const char *cf = js_fim(c);
          EspEvento e = base;
          if (cf && e.id[0] && e.ini && lerCompeticao(c, cf, &e)) { n++; if (cb && !cb(&e, u)) return n; }
        }
      }
    }
    p = js_prox(f);
  }
  return n;
}

// ---- fetching with a disk cache ----
static char base[200] = "https://site.api.espn.com/apis/site/v2/sports";
void espn_definir_base(const char *b) { snprintf(base, sizeof base, "%s", b ? b : ""); }

static void arquivo(const char *liga, char *dst, size_t tam) {
  snprintf(dst, tam, "esporte-%s.json", liga);
  for (char *c = dst; *c; c++) if (*c == '/') *c = '-';
}
int espn_buscar(const char *liga, time_t agora, int (*cb)(const EspEvento *, void *), void *u,
                time_t *quando, int *falhou) {
  char url[400], janela[32], nome[96];
  char *corpo;
  int n = -1;
  if (quando) *quando = 0;
  if (falhou) *falhou = 0;
  if (!espn_liga(liga)) return -1;
  espn_janela(agora, janela, sizeof janela);
  snprintf(url, sizeof url, "%s/%s/scoreboard?dates=%s&limit=1000", base, liga, janela);
  corpo = rede_baixar(url, 15);
  arquivo(liga, nome, sizeof nome);
  if (corpo && (n = espn_parse(corpo, liga, NULL, NULL)) >= 0) {
    // A good answer replaces the cache: "<epoch>\n<body>".
    size_t len = strlen(corpo);
    char *t = malloc(len + 32);
    if (t) {
      snprintf(t, len + 32, "%lld\n%s", (long long)agora, corpo);
      dados_gravar_leve(nome, t);
      free(t);
    }
    if (quando) *quando = agora;
    n = espn_parse(corpo, liga, cb, u);
    free(corpo);
    return n;
  }
  free(corpo);
  if (falhou) *falhou = 1;
  printf("[esporte] %s: o provedor nao respondeu um placar valido; usando a copia salva\n", liga);
  fflush(stdout);
  {
    char *c = dados_ler(nome), *body;
    if (!c) return -1;
    body = strchr(c, '\n');
    if (body) {
      if (quando) *quando = (time_t)atoll(c);
      n = espn_parse(body + 1, liga, cb, u);
    }
    free(c);
    return n;
  }
}
