#include "espn.h"
#include "dados.h"
#include "js.h"
#include "rede.h"
#include <ctype.h>
#include <pthread.h>
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
        // Only a KEY counts: a string followed by ':'. A value equal to the
        // name ("type":"team" before "team":{...}, ESPN 2026-11) is skipped.
        const char *v = q + 1;
        while (v < fim && (*v == ' ' || *v == '\n' || *v == '\t' || *v == '\r')) v++;
        if (v < fim && *v == ':') {
          v++;
          while (v < fim && (*v == ' ' || *v == '\n' || *v == '\t' || *v == '\r')) v++;
          return v;
        }
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
    js_texto_raiz_em(o, of, "color", t->cor, sizeof t->cor);
    js_texto_raiz_em(o, of, "alternateColor", t->cor2, sizeof t->cor2);
    for (char *q = t->cor; *q; q++) if (!isxdigit((unsigned char)*q)) { t->cor[0] = 0; break; }
    for (char *q = t->cor2; *q; q++) if (!isxdigit((unsigned char)*q)) { t->cor2[0] = 0; break; }
    if (strlen(t->cor) != 6) t->cor[0] = 0;
    if (strlen(t->cor2) != 6) t->cor2[0] = 0;
    if (!t->logo[0]) {   // tennis players: a country flag
      const char *fo, *ff;
      if (objeto(o, of, "flag", &fo, &ff)) js_texto(fo, ff, "href", t->logo, sizeof t->logo);
    }
    https(t->logo);
  }
  if (!t->curto[0]) copiar(t->curto, sizeof t->curto, t->nome);
  {
    const char *r = js_array(c, cf, "records");
    if (r && *r == '{') { const char *rf = js_fim(r); if (rf) js_texto_raiz_em(r, rf, "summary", t->recorde, sizeof t->recorde); }
  }
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

// QR TIME: the league's own logo, from the scoreboard's "leagues"[0].logos
// (the "500-dark" one, made for dark backgrounds, when ESPN has it).
static pthread_mutex_t travaLogo = PTHREAD_MUTEX_INITIALIZER;
static struct { char liga[32], url[200]; } logos[ESPN_LIGAS_MAX];
static int nLogos;
static void anotarLogo(const char *raiz, const char *liga) {
  const char *l = js_array(raiz, NULL, "leagues"), *lf, *p;
  char url[200] = "", u[200];
  if (!l || *l != '{' || !(lf = js_fim(l))) return;
  for (p = js_array(l, lf, "logos"); p && *p == '{'; ) {
    const char *f = js_fim(p);
    if (!f) break;
    u[0] = 0;
    js_texto_raiz_em(p, f, "href", u, sizeof u);
    if (!strncmp(u, "https://", 8) && (!url[0] || strstr(u, "dark"))) snprintf(url, sizeof url, "%s", u);
    p = js_prox(f);
  }
  if (!url[0]) return;
  pthread_mutex_lock(&travaLogo);
  {
    int i;
    for (i = 0; i < nLogos && strcmp(logos[i].liga, liga); i++) {}
    if (i == nLogos && nLogos < ESPN_LIGAS_MAX) nLogos++;
    if (i < nLogos) { snprintf(logos[i].liga, sizeof logos[i].liga, "%s", liga); snprintf(logos[i].url, sizeof logos[i].url, "%s", url); }
  }
  pthread_mutex_unlock(&travaLogo);
}
int espn_liga_logo(const char *liga, char *url, size_t n) {
  int r = 0;
  url[0] = 0;
  pthread_mutex_lock(&travaLogo);
  for (int i = 0; i < nLogos; i++) if (!strcmp(logos[i].liga, liga)) { snprintf(url, n, "%s", logos[i].url); r = 1; break; }
  pthread_mutex_unlock(&travaLogo);
  return r;
}
int espn_parse(const char *json, const char *liga, int (*cb)(const EspEvento *, void *), void *u) {
  const char *p, *raiz;
  int n = 0;
  if (!json) return -1;
  raiz = json;
  while (*raiz && (unsigned char)*raiz <= ' ') raiz++;
  if (*raiz != '{' || !strstr(raiz, "\"events\"")) return -1;
  if (liga) anotarLogo(raiz, liga);
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

static void arquivo(const char *liga, const char *mes, char *dst, size_t tam) {
  snprintf(dst, tam, "esporte-%s-%s.json", liga, mes);
  for (char *c = dst; *c; c++) if (*c == '/') *c = '-';
}
// ESPN's scoreboard stopped accepting a day range ("dates=YYYYMMDD-YYYYMMDD"
// answers 400 "Failed to get events endpoint", measured 2026-11-22); a single
// day or a whole month ("dates=YYYYMM") still works. The ±7-day window spans
// one or two months: those are fetched and only the window's events are kept.
int espn_meses(time_t agora, char m[2][8]) {
  time_t de = agora - 7 * 86400, ate = agora + 7 * 86400;
  struct tm a, b;
  gmtime_r(&de, &a);
  gmtime_r(&ate, &b);
  snprintf(m[0], 8, "%04d%02d", a.tm_year + 1900, a.tm_mon + 1);
  snprintf(m[1], 8, "%04d%02d", b.tm_year + 1900, b.tm_mon + 1);
  return strcmp(m[0], m[1]) ? 2 : 1;
}
typedef struct { int (*cb)(const EspEvento *, void *); void *u; time_t de, ate; int n; } Janela;
static int naJanela(const EspEvento *e, void *u) {
  Janela *j = u;
  if (e->ini && (e->ini < j->de || e->ini > j->ate)) return 1;   // outside the window: skip
  j->n++;
  return j->cb ? j->cb(e, j->u) : 1;
}
// QR TIME: DAY BY DAY, like Nuvio One (SportsFeeds.scoreboardDayRaw). A
// month of the NBA is 4.5 MB of JSON (measured 2026-11-24), parsed on the
// TV's CPU; one day is ~150 KB. Each day is cached on its own
// ("esporte-<league>-<YYYYMMDD>.json", "<epoch>\n<body>") and asked again
// only when stale: the days around today every 90 s, later days every 6 h,
// past days once a day. ESPN days are US Eastern days (UTC-5 here).
static void diaChave(time_t t, char *d, size_t n) {
  struct tm m;
  t -= 5 * 3600;
  gmtime_r(&t, &m);
  snprintf(d, n, "%04d%02d%02d", m.tm_year + 1900, m.tm_mon + 1, m.tm_mday);
}
static long validade(int d) { return d >= -1 && d <= 1 ? 90 : d > 1 ? 6 * 3600 : 24 * 3600; }
// The same match comes once even when two days' answers both carry it.
typedef struct { Janela *j; char vistos[400][24]; int nv; } Unico;
static int umaVez(const EspEvento *e, void *u) {
  Unico *x = u;
  for (int i = 0; i < x->nv; i++) if (!strcmp(x->vistos[i], e->id)) return 1;
  if (x->nv < 400) snprintf(x->vistos[x->nv++], sizeof x->vistos[0], "%s", e->id);
  return naJanela(e, x->j);
}
int espn_buscar_dias(const char *liga, time_t agora, int d0, int d1, int (*cb)(const EspEvento *, void *), void *u,
                     time_t *quando, int *falhou) {
  char url[400], dia[12], nome[112];
  int algum = 0;
  Janela jan = { cb, u, agora - 7 * 86400, agora + 8 * 86400, 0 };
  Unico *un;
  static const char *const cab[] = {   // the same browser identity Nuvio One sends
    "User-Agent: Mozilla/5.0 (Linux; Android 12; TV) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36", NULL };
  if (quando) *quando = 0;
  if (falhou) *falhou = 0;
  if (!espn_liga(liga) || !(un = calloc(1, sizeof *un))) return -1;
  un->j = &jan;
  for (int d = d0; d <= d1; d++) {
    char *c, *corpo = NULL, *body = NULL;
    time_t q = 0;
    diaChave(agora + (time_t)d * 86400, dia, sizeof dia);
    arquivo(liga, dia, nome, sizeof nome);
    if ((c = dados_ler(nome)) != NULL && (body = strchr(c, '\n')) != NULL) { q = (time_t)atoll(c); body++; }
    if (!body || agora - q > validade(d) || q > agora) {
      snprintf(url, sizeof url, "%s/%s/scoreboard?dates=%s&limit=1000", base, liga, dia);
      corpo = rede_baixar_com(url, 15, cab);
      if (corpo && espn_parse(corpo, liga, umaVez, un) >= 0) {
        size_t len = strlen(corpo);
        char *t = malloc(len + 32);
        if (t) { snprintf(t, len + 32, "%lld\n%s", (long long)agora, corpo); dados_gravar_leve(nome, t); free(t); }
        if (quando && (!*quando || agora < *quando)) *quando = agora;
        algum = 1;
        free(corpo); free(c);
        continue;
      }
      free(corpo);
      if (falhou) *falhou = 1;
      if (body) { printf("[esporte] %s %s: sem resposta valida; usando a copia salva\n", liga, dia); fflush(stdout); }
    }
    if (body && espn_parse(body, liga, umaVez, un) >= 0) {
      if (quando && (!*quando || q < *quando)) *quando = q;   // the oldest data shown
      algum = 1;
    }
    free(c);
  }
  free(un);
  return algum ? jan.n : -1;
}
int espn_buscar(const char *liga, time_t agora, int (*cb)(const EspEvento *, void *), void *u,
                time_t *quando, int *falhou) {
  return espn_buscar_dias(liga, agora, -7, 7, cb, u, quando, falhou);
}

// ---------------------------------------------------------------- standings (QR TIME)
// site.api.espn.com/apis/v2/sports/<league>/standings: "children" (groups:
// one for a league, conferences in the NBA), each with standings.entries[]
// {team{id,displayName,shortDisplayName,logos[{href}]}, stats[{name,value}]}.
static double estat(const char *ent, const char *fim, const char *nome, double padrao) {
  const char *p;
  for (p = js_array(ent, fim, "stats"); p && *p == '{'; ) {
    const char *f = js_fim(p);
    char n[40] = "";
    if (!f) break;
    js_texto_raiz_em(p, f, "name", n, sizeof n);
    if (!strcmp(n, nome)) return js_num(p, f, "value", padrao);
    p = js_prox(f);
  }
  return padrao;
}
static int cmpPos(const void *a, const void *b) {
  const EspPos *x = a, *y = b;
  int g = strcmp(x->grupo, y->grupo);
  return g ? g : x->pos - y->pos;
}
int espn_tabela_parse(const char *json, EspPos *out, int max) {
  const char *g;
  int n = 0;
  if (!json || !strstr(json, "\"standings\"")) return -1;
  for (g = js_array(json, NULL, "children"); g && *g == '{' && n < max; ) {
    const char *gf = js_fim(g), *st, *e;
    char grupo[48] = "";
    if (!gf) break;
    js_texto_raiz_em(g, gf, "name", grupo, sizeof grupo);
    if ((st = strstr(g, "\"standings\"")) && st < gf && (st = strchr(st, '{')) && st < gf) {
      for (e = js_array(st, js_fim(st), "entries"); e && *e == '{' && n < max; ) {
        const char *ef = js_fim(e), *t;
        EspPos *x = &out[n];
        if (!ef) break;
        memset(x, 0, sizeof *x);
        snprintf(x->grupo, sizeof x->grupo, "%s", grupo);
        if ((t = strstr(e, "\"team\"")) && t < ef && (t = strchr(t, '{')) && t < ef) {
          const char *tf = js_fim(t), *lg;
          js_texto_raiz_em(t, tf, "id", x->id, sizeof x->id);
          js_texto_raiz_em(t, tf, "displayName", x->nome, sizeof x->nome);
          js_texto_raiz_em(t, tf, "shortDisplayName", x->curto, sizeof x->curto);
          if ((lg = js_array(t, tf, "logos")) && *lg == '{') js_texto_raiz_em(lg, js_fim(lg), "href", x->logo, sizeof x->logo);
        }
        x->jogos = (int)estat(e, ef, "gamesPlayed", 0);
        x->v = (int)estat(e, ef, "wins", 0);
        x->e = (int)estat(e, ef, "ties", 0);
        x->d = (int)estat(e, ef, "losses", 0);
        x->saldo = (int)estat(e, ef, "pointDifferential", 0);
        x->pts = (int)estat(e, ef, "points", -1);
        x->pos = (int)estat(e, ef, "rank", 0);
        if (x->nome[0]) n++;
        e = js_prox(ef);
      }
    }
    g = js_prox(gf);
  }
  // Basketball has no "rank": by wins, then fewer losses.
  for (int i = 0; i < n; i++) if (!out[i].pos) {
    int r = 1;
    for (int j = 0; j < n; j++) if (!strcmp(out[j].grupo, out[i].grupo) && (out[j].v > out[i].v || (out[j].v == out[i].v && out[j].d < out[i].d))) r++;
    out[i].pos = r;
  }
  qsort(out, (size_t)n, sizeof *out, cmpPos);
  return n;
}
int espn_tabela(const char *liga, EspPos *out, int max, time_t *quando, int *falhou) {
  static const char *const cab[] = {
    "User-Agent: Mozilla/5.0 (Linux; Android 12; TV) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36", NULL };
  char url[400], nome[112], raiz[200], *corpo, *sp;
  int n;
  time_t agora = time(NULL);
  if (quando) *quando = 0;
  if (falhou) *falhou = 0;
  if (!espn_liga(liga)) return -1;
  // The scoreboard base is ".../apis/site/v2/sports"; standings live at ".../apis/v2/sports".
  snprintf(raiz, sizeof raiz, "%s", base);
  if ((sp = strstr(raiz, "/apis/site/v2/"))) memmove(sp + 6, sp + 11, strlen(sp + 11) + 1);
  snprintf(url, sizeof url, "%s/%s/standings", raiz, liga);
  snprintf(nome, sizeof nome, "esporte-tabela-%s.json", liga);
  for (char *c = nome; *c; c++) if (*c == '/') *c = '_';
  corpo = rede_baixar_com(url, 15, cab);
  if (corpo && (n = espn_tabela_parse(corpo, out, max)) > 0) {
    size_t len = strlen(corpo);
    char *t = malloc(len + 32);
    if (t) { snprintf(t, len + 32, "%lld\n%s", (long long)agora, corpo); dados_gravar_leve(nome, t); free(t); }
    if (quando) *quando = agora;
    free(corpo);
    return n;
  }
  free(corpo);
  if (falhou) *falhou = 1;
  {
    char *c = dados_ler(nome), *body;
    n = -1;
    if (c && (body = strchr(c, '\n')) != NULL && (n = espn_tabela_parse(body + 1, out, max)) > 0 && quando) *quando = (time_t)atoll(c);
    free(c);
    return n;
  }
}
