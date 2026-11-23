#include "iptvvod.h"
#include "xtvod.h"
#include "stvod.h"
#include "m3uvod.h"
#include "stalker.h"
#include "xtream.h"
#include "iptvfontes.h"
#include "dados.h"
#include "idioma.h"
#include "perfis.h"
#include "perfisessao.h"
#include "streams.h"
#include "js.h"
#include "rede.h"
#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// The same four answers as JF_FONTES_* (servidores.c checks it).
enum { VF_NADA = 0, VF_PENDENTE, VF_PRONTO, VF_FALHOU };
// A provider list older than this is fetched again on the next start.
#define IPTVVOD_VALIDADE_S (6 * 3600)
#define IPTVVOD_CONTAS_MAX 16

// ------------------------------------------------------------- identity
static const char *const PREFIXOS_IDIOMA_SEP = "-:|]";
static int ehTagQualidade(const char *w) {
  static const char *const tags[] = { "4k", "uhd", "fhd", "hd", "sd", "hevc", "h265", "x265", "h264",
                                      "x264", "2160p", "1080p", "720p", "480p", "multi", "multisub",
                                      "sub", "subs", "dub", "dubbed", "hdr", "hdr10", "dv", "web",
                                      "webrip", "webdl", "bluray", "remux", "vod", "mkv", "mp4", "avi",
                                      "ts", "m4v", "hdrip", "brrip", "dvdrip", "hdtv", NULL };
  for (int i = 0; tags[i]; i++) if (!strcmp(w, tags[i])) return 1;
  return 0;
}
// Skips "EN - ", "EN: ", "|EN| ", "[EN] ", "EN| " (2-3 letter language code).
static const char *pularIdioma(const char *s) {
  const char *p = s, *q;
  while (*p == ' ') p++;
  if (*p == '|' || *p == '[') {
    q = p + 1;
    while (isalpha((unsigned char)*q)) q++;
    if (q - p - 1 >= 2 && q - p - 1 <= 3 && (*q == '|' || *q == ']')) { q++; while (*q == ' ' || *q == '-') q++; return q; }
    return s;
  }
  q = p;
  while (isupper((unsigned char)*q)) q++;
  if (q - p >= 2 && q - p <= 3) {
    const char *r = q;
    while (*r == ' ') r++;
    if (*r && strchr(PREFIXOS_IDIOMA_SEP, *r)) { r++; while (*r == ' ') r++; if (*r) return r; }
  }
  return s;
}
void iptvvod_normalizar(const char *nome, char *dst, size_t tam) {
  char w[64];
  size_t n = 0, k = 0;
  const char *p = pularIdioma(nome ? nome : "");
  if (!tam) return;
  dst[0] = 0;
  for (;; p++) {
    unsigned char c = (unsigned char)*p;
    if (c && (isalnum(c) || c >= 0x80)) {
      if (k + 1 < sizeof w) w[k++] = (char)(c < 0x80 ? tolower(c) : c);
      continue;
    }
    if (k) {
      int ano;
      w[k] = 0;
      ano = k == 4 && isdigit((unsigned char)w[0]) && isdigit((unsigned char)w[3]) ? atoi(w) : 0;
      if (!ehTagQualidade(w) && !(ano >= 1900 && ano <= 2100) && n + k + 2 < tam) {
        if (n) dst[n++] = ' ';
        memcpy(dst + n, w, k);
        n += k;
        dst[n] = 0;
      }
      k = 0;
    }
    if (!c) break;
  }
}
void iptvvod_titulo_limpo(const char *nome, char *dst, size_t tam) {
  const char *p = pularIdioma(nome ? nome : "");
  size_t n;
  int mudou = 1;
  if (!tam) return;
  snprintf(dst, tam, "%s", p);
  // Trailing "(2019)", "[2019]", quality tags and separators, repeatedly.
  while (mudou) {
    char *fim, *ini;
    mudou = 0;
    n = strlen(dst);
    while (n && (dst[n - 1] == ' ' || dst[n - 1] == '-' || dst[n - 1] == '|')) dst[--n] = 0;
    if (n >= 6 && (dst[n - 1] == ')' || dst[n - 1] == ']') && (dst[n - 6] == '(' || dst[n - 6] == '[') &&
        isdigit((unsigned char)dst[n - 5]) && isdigit((unsigned char)dst[n - 2])) { dst[n - 6] = 0; mudou = 1; continue; }
    fim = dst + n;
    ini = fim;
    while (ini > dst && ini[-1] != ' ') ini--;
    if (ini > dst && fim > ini) {
      char w[24];
      size_t k = (size_t)(fim - ini), i;
      if (k < sizeof w) {
        for (i = 0; i < k; i++) w[i] = (char)tolower((unsigned char)ini[i]);
        w[k] = 0;
        if (ehTagQualidade(w)) { *ini = 0; mudou = 1; }
      }
    }
  }
  if (!dst[0]) snprintf(dst, tam, "%s", nome ? nome : "");
}
static uint64_t fnv(const char *s, uint64_t h) {
  for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ULL;
  return h;
}
int iptvvod_e_id(const char *id) { return id && !strncmp(id, IPTVVOD_PREFIXO, 3); }
void iptvvod_id_montar(char *dst, size_t tam, uint64_t grupo, char kind, int temporada, int episodio) {
  if (kind == 'e') snprintf(dst, tam, "qv:%016llx.e%dx%d", (unsigned long long)grupo, temporada, episodio);
  else snprintf(dst, tam, "qv:%016llx.%c", (unsigned long long)grupo, kind);
}
int iptvvod_id_ler(const char *id, uint64_t *grupo, char *kind, int *temporada, int *episodio) {
  unsigned long long g = 0;
  int t = 0, e = 0;
  const char *p;
  if (!iptvvod_e_id(id) || strlen(id) < 3 + 16 + 2) return 0;
  for (p = id + 3; p < id + 19; p++) if (!isxdigit((unsigned char)*p) || isupper((unsigned char)*p)) return 0;
  if (id[19] != '.') return 0;
  g = strtoull(id + 3, NULL, 16);
  if ((id[20] == 'm' || id[20] == 's') && !id[21]) { t = e = 0; }
  else if (id[20] == 'e') {
    char sobra;
    if (sscanf(id + 21, "%dx%d%c", &t, &e, &sobra) != 2 || t < 0 || e <= 0 || t > 9999 || e > 99999) return 0;
  } else return 0;
  if (grupo) *grupo = g;
  if (kind) *kind = id[20];
  if (temporada) *temporada = t;
  if (episodio) *episodio = e;
  return 1;
}

// ------------------------------------------------------------- library
typedef struct {
  unsigned conta;
  int tipoProv;           // IPTV_XTREAM / IPTV_STALKER / IPTV_M3U
  char *ref;              // Stalker "cmd\x1feps", M3U movie URL; memory only (may hold credentials)
  char kind;              // 'm' movie, 's' series
  char sid[XTVOD_SID], ext[8];
  long tmdb;              // panel's hint
  int ano, nota10;
  long long adicionado;
  char *nome, *poster, *categoria;
  char norm[128];
  int grupo, proximo;     // group index; next copy in the same group (-1)
} Copia;
typedef struct {
  uint64_t chave;
  char kind;
  int primeira, n;        // first copy (best), copies
  long long adicionado;
} Grupo;
// M3U episodes (the playlist has no series call): series copy -> files.
typedef struct { unsigned conta; char sid[XTVOD_SID]; int t, e; char ext[8]; char *url, *nome; } M3uEp;
typedef struct {
  Copia *c; int n, cap;
  Grupo *g; int ng;
  M3uEp *ep; int nep, capEp;
} Biblio;

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static Biblio bib;                  // published snapshot (under trava)
static uint64_t bibSessao;
static int bibPerfil = -1;
static unsigned versao;             // bumps on every publish
static atomic_int carregando;
static atomic_uint geracao;         // a profile change invalidates the worker
static int fioVivo;

static void biblioLiberar(Biblio *b) {
  for (int i = 0; i < b->n; i++) { free(b->c[i].nome); free(b->c[i].poster); free(b->c[i].categoria); free(b->c[i].ref); }
  for (int i = 0; i < b->nep; i++) { free(b->ep[i].url); free(b->ep[i].nome); }
  free(b->c); free(b->g); free(b->ep);
  memset(b, 0, sizeof *b);
}
static char *dupTxt(const char *s) { return strdup(s ? s : ""); }
static int biblioJuntar(Biblio *b, const Copia *c) {
  if (b->n == b->cap) {
    int nova = b->cap ? b->cap * 2 : 256;
    Copia *t = realloc(b->c, (size_t)nova * sizeof *t);
    if (!t) return 0;
    b->c = t; b->cap = nova;
  }
  b->c[b->n++] = *c;
  return 1;
}

// Grouping. Keys: "t<kind><tmdb>" for a TMDB hint, "n<kind><norm>|<year>".
static uint64_t chaveTmdb(char kind, long tmdb) {
  char k[40];
  snprintf(k, sizeof k, "t%c%ld", kind, tmdb);
  return fnv(k, 1469598103934665603ULL);
}
static uint64_t chaveNome(char kind, const char *norm, int ano) {
  char k[200];
  snprintf(k, sizeof k, "n%c%s|%d", kind, norm, ano);
  return fnv(k, 1469598103934665603ULL);
}
typedef struct { uint64_t chave; int grupo; } Par;
static int parCmp(const void *a, const void *b) {
  uint64_t x = ((const Par *)a)->chave, y = ((const Par *)b)->chave;
  return (x > y) - (x < y);
}
static int achaPar(const Par *v, int n, uint64_t chave) {
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (v[m].chave == chave) return v[m].grupo;
    if (v[m].chave < chave) lo = m + 1; else hi = m - 1;
  }
  return -1;
}
static int grupoNovo(Biblio *b, int *cap, uint64_t chave, char kind) {
  if (b->ng == *cap) {
    int nova = *cap ? *cap * 2 : 256;
    Grupo *t = realloc(b->g, (size_t)nova * sizeof *t);
    if (!t) return -1;
    b->g = t; *cap = nova;
  }
  b->g[b->ng] = (Grupo){ chave, kind, -1, 0, 0 };
  return b->ng++;
}
static void grupoAnexar(Biblio *b, int g, int i) {
  Copia *c = &b->c[i];
  c->grupo = g;
  c->proximo = -1;
  if (b->g[g].primeira < 0) b->g[g].primeira = i;
  else {
    // A copy with a TMDB hint or a poster leads the group (its title and art win).
    int j = b->g[g].primeira;
    Copia *p = &b->c[j];
    if ((c->tmdb && !p->tmdb) || (!p->poster[0] && c->poster[0] && (c->tmdb || !p->tmdb))) {
      c->proximo = j; b->g[g].primeira = i;
    } else {
      while (b->c[j].proximo >= 0) j = b->c[j].proximo;
      b->c[j].proximo = i;
    }
  }
  b->g[g].n++;
  if (c->adicionado > b->g[g].adicionado) b->g[g].adicionado = c->adicionado;
}
typedef struct { uint64_t chave; int i; } Ix;
static int ixCmp(const void *a, const void *b) {
  const Ix *x = a, *y = b;
  if (x->chave != y->chave) return x->chave < y->chave ? -1 : 1;
  return (x->i > y->i) - (x->i < y->i);
}
// O(n log n): sort copies by key, consecutive runs are one group.
static int agrupar(Biblio *b) {
  int cap = 0, nv = 0, nn = 0, nsa = 0, i, r;
  Ix *v = malloc(sizeof(Ix) * (size_t)(b->n ? b->n : 1));
  Par *porNome = malloc(sizeof(Par) * (size_t)(b->n ? b->n : 1));
  Par *porNomeSemAno = malloc(sizeof(Par) * (size_t)(b->n ? b->n : 1));
  if (!v || !porNome || !porNomeSemAno) { free(v); free(porNome); free(porNomeSemAno); return 0; }
  free(b->g); b->g = NULL; b->ng = 0;
  for (i = 0; i < b->n; i++) b->c[i].grupo = -1;
  // 1. Copies with a TMDB hint, by (kind, tmdb).
  for (i = 0; i < b->n; i++) if (b->c[i].tmdb) v[nv++] = (Ix){ chaveTmdb(b->c[i].kind, b->c[i].tmdb), i };
  qsort(v, (size_t)nv, sizeof *v, ixCmp);
  for (r = 0; r < nv;) {
    int g = grupoNovo(b, &cap, v[r].chave, b->c[v[r].i].kind), k;
    if (g < 0) { free(v); free(porNome); free(porNomeSemAno); return 0; }
    for (k = r; k < nv && v[k].chave == v[r].chave; k++) grupoAnexar(b, g, v[k].i);
    r = k;
  }
  // Their titles with a year, so a copy without a hint can join them.
  for (i = 0; i < b->n; i++) {
    Copia *c = &b->c[i];
    if (c->grupo >= 0 && c->ano && c->norm[0]) porNome[nn++] = (Par){ chaveNome(c->kind, c->norm, c->ano), c->grupo };
  }
  qsort(porNome, (size_t)nn, sizeof *porNome, parCmp);
  // Year-less copies (most M3U series) may join a hinted group by title alone,
  // but only when exactly ONE such group has that title.
  {
    int w = 0;
    for (i = 0; i < b->n; i++) {
      Copia *c = &b->c[i];
      if (c->grupo >= 0 && c->norm[0]) porNomeSemAno[w++] = (Par){ chaveNome(c->kind, c->norm, 0), c->grupo };
    }
    qsort(porNomeSemAno, (size_t)w, sizeof *porNomeSemAno, parCmp);
    for (i = 1; i < w; i++)
      if (porNomeSemAno[i].chave == porNomeSemAno[i - 1].chave && porNomeSemAno[i].grupo != porNomeSemAno[i - 1].grupo &&
          porNomeSemAno[i - 1].grupo != -2) {
        int k2 = i - 1;
        while (k2 >= 0 && porNomeSemAno[k2].chave == porNomeSemAno[i].chave) porNomeSemAno[k2--].grupo = -2;
        porNomeSemAno[i].grupo = -2;
      } else if (i && porNomeSemAno[i].chave == porNomeSemAno[i - 1].chave && porNomeSemAno[i - 1].grupo == -2)
        porNomeSemAno[i].grupo = -2;
    nsa = w;
  }
  // 2 and 3. Copies without a hint: same normalized title AND year join a
  // hinted group; otherwise copies with the same key form their own group.
  nv = 0;
  for (i = 0; i < b->n; i++) if (b->c[i].grupo < 0) {
    Copia *c = &b->c[i];
    v[nv++] = (Ix){ chaveNome(c->kind, c->norm[0] ? c->norm : c->nome, c->ano), i };
  }
  qsort(v, (size_t)nv, sizeof *v, ixCmp);
  for (r = 0; r < nv;) {
    int g = b->c[v[r].i].ano ? achaPar(porNome, nn, v[r].chave) : achaPar(porNomeSemAno, nsa, v[r].chave), k;
    if (g == -2) g = -1;   // the title is ambiguous: stays its own item
    if (g < 0 && (g = grupoNovo(b, &cap, v[r].chave, b->c[v[r].i].kind)) < 0) { free(v); free(porNome); free(porNomeSemAno); return 0; }
    for (k = r; k < nv && v[k].chave == v[r].chave; k++) grupoAnexar(b, g, v[k].i);
    r = k;
  }
  free(v); free(porNome); free(porNomeSemAno);
  return 1;
}
static int grupoPorId(const Biblio *b, const char *id, char *kindOut) {
  uint64_t g;
  char k;
  int t, e;
  if (!iptvvod_id_ler(id, &g, &k, &t, &e)) return -1;
  for (int i = 0; i < b->ng; i++)
    if (b->g[i].chave == g && (k == 'e' ? b->g[i].kind == 's' : b->g[i].kind == k)) {
      if (kindOut) *kindOut = k;
      return i;
    }
  return -1;
}

// ------------------------------------------------------------- disk cache
// One file per profile: "iptvvod-p<N>.tsv". Line 1: "v1\t<epoch>". Then one
// copy per line. Provider data only (names, art, ids); no credential.
static void arquivo(char *dst, size_t tam, int perfil) { snprintf(dst, tam, "iptvvod-p%d.tsv", perfil > 0 ? perfil : 1); }
static void limparCampo(char *s) { for (; s && *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' '; }
static int gravarCache(const Biblio *b, int perfil, long long quando) {
  size_t cap = 64 + (size_t)b->n * 900, k = 0;
  char *t = malloc(cap), nome[64];
  int ok;
  if (!t) return 0;
  k += (size_t)snprintf(t + k, cap - k, "v1\t%lld\n", quando);
  for (int i = 0; i < b->n && k < cap; i++) {
    const Copia *c = &b->c[i];
    char n1[160], p1[512], c1[96];
    // Stalker cmds and M3U URLs can carry the provider's credentials: those
    // providers are listed again on each start instead of being cached.
    if (c->tipoProv != IPTV_XTREAM) continue;
    snprintf(n1, sizeof n1, "%s", c->nome); limparCampo(n1);
    snprintf(p1, sizeof p1, "%s", c->poster); limparCampo(p1);
    snprintf(c1, sizeof c1, "%s", c->categoria); limparCampo(c1);
    k += (size_t)snprintf(t + k, cap - k, "%u\t%c\t%s\t%s\t%ld\t%d\t%d\t%lld\t%s\t%s\t%s\n", c->conta, c->kind,
                          c->sid, c->ext, c->tmdb, c->ano, c->nota10, c->adicionado, n1, p1, c1);
  }
  arquivo(nome, sizeof nome, perfil);
  ok = k < cap && dados_gravar(nome, t);
  free(t);
  return ok;
}
static long long lerCache(Biblio *b, int perfil) {
  char nome[64], *t, *p;
  long long quando = 0;
  arquivo(nome, sizeof nome, perfil);
  t = dados_ler(nome);
  if (!t) return 0;
  if (sscanf(t, "v1\t%lld", &quando) != 1) { free(t); return 0; }
  p = strchr(t, '\n');
  while (p && *++p) {
    char *fim = strchr(p, '\n'), *campos[11];
    int nc = 0;
    Copia c;
    if (fim) *fim = 0;
    campos[nc++] = p;
    for (char *q = p; *q && nc < 11; q++) if (*q == '\t') { *q = 0; campos[nc++] = q + 1; }
    if (nc == 11 && (campos[1][0] == 'm' || campos[1][0] == 's') && strlen(campos[2]) < sizeof c.sid) {
      memset(&c, 0, sizeof c);
      c.conta = (unsigned)strtoul(campos[0], NULL, 10);
      c.tipoProv = IPTV_XTREAM;
      c.kind = campos[1][0];
      snprintf(c.sid, sizeof c.sid, "%s", campos[2]);
      snprintf(c.ext, sizeof c.ext, "%s", campos[3]);
      c.tmdb = atol(campos[4]); c.ano = atoi(campos[5]); c.nota10 = atoi(campos[6]);
      c.adicionado = atoll(campos[7]);
      c.nome = dupTxt(campos[8]); c.poster = dupTxt(campos[9]); c.categoria = dupTxt(campos[10]);
      iptvvod_normalizar(c.nome, c.norm, sizeof c.norm);
      if (!c.nome || !c.poster || !c.categoria || !biblioJuntar(b, &c)) {
        free(c.nome); free(c.poster); free(c.categoria); break;
      }
    }
    p = fim;
  }
  free(t);
  return quando;
}

// ------------------------------------------------------------- loading
typedef struct { Biblio *b; unsigned conta; char kind; } Coleta;
static int coletar(const XtVodItem *it, void *u) {
  Coleta *k = u;
  Copia c;
  memset(&c, 0, sizeof c);
  c.conta = k->conta; c.kind = k->kind; c.tipoProv = IPTV_XTREAM;
  snprintf(c.sid, sizeof c.sid, "%s", it->sid);
  snprintf(c.ext, sizeof c.ext, "%s", it->ext);
  c.tmdb = it->tmdbProv; c.ano = it->ano; c.nota10 = it->nota10; c.adicionado = it->adicionado;
  c.nome = dupTxt(it->nome); c.poster = dupTxt(it->poster); c.categoria = dupTxt(it->categoria);
  iptvvod_normalizar(it->nome, c.norm, sizeof c.norm);
  if (!c.nome || !c.poster || !c.categoria || !biblioJuntar(k->b, &c)) {
    free(c.nome); free(c.poster); free(c.categoria);
    return 0;
  }
  return 1;
}
static int juntarCopia(Biblio *b, unsigned conta, int tipo, char kind, const XtVodItem *it, const char *ref) {
  Copia c;
  memset(&c, 0, sizeof c);
  c.conta = conta; c.tipoProv = tipo; c.kind = kind;
  snprintf(c.sid, sizeof c.sid, "%s", it->sid);
  snprintf(c.ext, sizeof c.ext, "%s", it->ext);
  c.tmdb = it->tmdbProv; c.ano = it->ano; c.nota10 = it->nota10; c.adicionado = it->adicionado;
  c.nome = dupTxt(it->nome); c.poster = dupTxt(it->poster); c.categoria = dupTxt(it->categoria);
  c.ref = ref ? dupTxt(ref) : NULL;
  iptvvod_normalizar(it->nome, c.norm, sizeof c.norm);
  if (!c.nome || !c.poster || !c.categoria || (ref && !c.ref) || !biblioJuntar(b, &c)) {
    free(c.nome); free(c.poster); free(c.categoria); free(c.ref);
    return 0;
  }
  return 1;
}
static int coletarStalker(const XtVodItem *it, int serie, const char *cmd, const char *eps, void *u) {
  Coleta *k = u;
  char ref[STVOD_CMD + STVOD_EPS + 2];
  snprintf(ref, sizeof ref, "%s\x1f%s", cmd, serie ? eps : "");
  return juntarCopia(k->b, k->conta, IPTV_STALKER, serie ? 's' : 'm', it, ref);
}
static int coletarM3u(const XtVodItem *it, char kind, const char *serie, int t, int e, const char *url, void *u) {
  Coleta *k = u;
  Biblio *b = k->b;
  if (kind == 'm') return juntarCopia(b, k->conta, IPTV_M3U, 'm', it, url);
  {
    // An episode: its series copy is one per (account, series name).
    char norm[128], sid[XTVOD_SID];
    int i;
    M3uEp ep;
    iptvvod_normalizar(serie, norm, sizeof norm);
    if (!norm[0]) return 1;
    snprintf(sid, sizeof sid, "s%015llx", (unsigned long long)(fnv(norm, 1469598103934665603ULL) & 0xfffffffffffffffULL));
    for (i = b->n - 1; i >= 0; i--)
      if (b->c[i].conta == k->conta && b->c[i].tipoProv == IPTV_M3U && b->c[i].kind == 's' && !strcmp(b->c[i].sid, sid)) break;
    if (i < 0) {
      XtVodItem s;
      memset(&s, 0, sizeof s);
      snprintf(s.sid, sizeof s.sid, "%s", sid);
      snprintf(s.nome, sizeof s.nome, "%s", serie);
      snprintf(s.poster, sizeof s.poster, "%s", it->poster);
      snprintf(s.categoria, sizeof s.categoria, "%s", it->categoria);
      s.ano = xtvod_ano_do_nome(serie);
      if (!juntarCopia(b, k->conta, IPTV_M3U, 's', &s, NULL)) return 0;
    }
    if (b->nep == b->capEp) {
      int nova = b->capEp ? b->capEp * 2 : 256;
      M3uEp *t2 = realloc(b->ep, (size_t)nova * sizeof *t2);
      if (!t2) return 0;
      b->ep = t2; b->capEp = nova;
    }
    memset(&ep, 0, sizeof ep);
    ep.conta = k->conta; ep.t = t; ep.e = e;
    snprintf(ep.sid, sizeof ep.sid, "%s", sid);
    snprintf(ep.ext, sizeof ep.ext, "%s", it->ext);
    ep.url = dupTxt(url); ep.nome = dupTxt(it->nome);
    if (!ep.url || !ep.nome) { free(ep.url); free(ep.nome); return 0; }
    b->ep[b->nep++] = ep;
    return 1;
  }
}
static void publicar(Biblio *nova, uint64_t sessao, int perfil, unsigned ger) {
  pthread_mutex_lock(&trava);
  if (ger == atomic_load(&geracao) && nv_perfil_sessao_atual(sessao)) {
    biblioLiberar(&bib);
    bib = *nova;
    memset(nova, 0, sizeof *nova);
    bibSessao = sessao;
    bibPerfil = perfil;
    versao++;
  }
  pthread_mutex_unlock(&trava);
  biblioLiberar(nova);
}
static void copiarContaDe(Biblio *dst, const Biblio *src, unsigned conta) {
  for (int i = 0; i < src->n; i++) if (src->c[i].conta == conta) {
    Copia c = src->c[i];
    c.nome = dupTxt(c.nome); c.poster = dupTxt(c.poster); c.categoria = dupTxt(c.categoria);
    c.ref = src->c[i].ref ? dupTxt(src->c[i].ref) : NULL;
    if (!biblioJuntar(dst, &c)) { free(c.nome); free(c.poster); free(c.categoria); free(c.ref); }
  }
}
static void *fioCarga(void *u) {
  unsigned ger = (unsigned)(uintptr_t)u;
  uint64_t sessao = nv_perfil_sessao_ler();
  int perfil = perfis_ativo();
  IptvConta contas[IPTVVOD_CONTAS_MAX];
  int nContas = iptv_contas(contas, IPTVVOD_CONTAS_MAX), i, k, falhas = 0;
  Biblio velha = {0}, nova = {0};
  long long quando = lerCache(&velha, perfil), agora = (long long)time(NULL);
  int fresco = quando > 0 && agora - quando < IPTVVOD_VALIDADE_S;
  // A provider added since the cache was written has nothing in it yet.
  for (i = 0; fresco && i < nContas; i++) if (contas[i].ativa && contas[i].tipo == IPTV_XTREAM) {
    int tem = 0;
    for (k = 0; k < velha.n && !tem; k++) tem = velha.c[k].conta == contas[i].id;
    if (!tem) fresco = 0;
  }
  // 1. The cached library first: Home has rows before any network.
  if (velha.n) {
    Biblio pronta = {0};
    for (i = 0; i < nContas; i++) if (contas[i].ativa) copiarContaDe(&pronta, &velha, contas[i].id);
    if (agrupar(&pronta)) publicar(&pronta, sessao, perfil, ger);
    else biblioLiberar(&pronta);
  }
  // 2. Every provider: Xtream from the cache while it is fresh, otherwise
  // (and Stalker and M3U always) from the network. A provider that fails
  // keeps its cached copies: one failing provider never empties the others.
  {
    int precisa = !fresco;
    for (i = 0; i < nContas; i++) if (contas[i].ativa && contas[i].tipo != IPTV_XTREAM) precisa = 1;
    if (precisa) {
      for (i = 0; i < nContas && ger == atomic_load(&geracao); i++) {
        if (!contas[i].ativa) continue;
        if (contas[i].tipo == IPTV_XTREAM) {
          if (fresco) { copiarContaDe(&nova, &velha, contas[i].id); continue; }
          for (k = 0; k < 2; k++) {
            Coleta c = { &nova, contas[i].id, k ? 's' : 'm' };
            int antes = nova.n, r = xtvod_listar(contas[i].id, k, coletar, &c, sessao);
            if (r < 0) {
              for (int j = antes; j < nova.n; j++) { free(nova.c[j].nome); free(nova.c[j].poster); free(nova.c[j].categoria); free(nova.c[j].ref); }
              nova.n = antes;
              for (int j = 0; j < velha.n; j++)
                if (velha.c[j].conta == contas[i].id && velha.c[j].kind == c.kind) {
                  Copia cp = velha.c[j];
                  cp.nome = dupTxt(cp.nome); cp.poster = dupTxt(cp.poster); cp.categoria = dupTxt(cp.categoria); cp.ref = NULL;
                  if (!biblioJuntar(&nova, &cp)) { free(cp.nome); free(cp.poster); free(cp.categoria); }
                }
              falhas++;
            }
          }
        } else if (contas[i].tipo == IPTV_STALKER) {
          Coleta c = { &nova, contas[i].id, 'm' };
          if (stvod_listar(contas[i].id, coletarStalker, &c, sessao) < 0) falhas++;
        } else if (contas[i].tipo == IPTV_M3U) {
          Coleta c = { &nova, contas[i].id, 'm' };
          if (m3uvod_listar(contas[i].id, coletarM3u, &c, sessao) < 0) falhas++;
        }
      }
      if (ger == atomic_load(&geracao) && nv_perfil_sessao_atual(sessao)) {
        if (agrupar(&nova)) {
          if (nv_perfil_sessao_travar(sessao)) { gravarCache(&nova, perfil, (falhas || fresco) ? (quando ? quando : agora) : agora); nv_perfil_sessao_destravar(); }
          printf("[iptvvod] %d copia(s) de %d provedor(es), %d falha(s)\n", nova.n, nContas, falhas);
          publicar(&nova, sessao, perfil, ger);
        }
      }
    }
  }
  biblioLiberar(&velha);
  biblioLiberar(&nova);
  { int g, nm = 0, ns = 0;
    pthread_mutex_lock(&trava);
    for (g = 0; g < bib.ng; g++) { if (bib.g[g].kind == 'm') nm++; else ns++; }
    pthread_mutex_unlock(&trava);
    if (ger == atomic_load(&geracao)) printf("[iptvvod] biblioteca: %d filme(s), %d serie(s)\n", nm, ns); }
  pthread_mutex_lock(&trava);
  fioVivo = 0;
  pthread_mutex_unlock(&trava);
  atomic_store(&carregando, 0);
  fflush(stdout);
  return NULL;
}
void iptvvod_carregar(void) {
  pthread_t t;
  pthread_mutex_lock(&trava);
  if (fioVivo) { pthread_mutex_unlock(&trava); return; }
  fioVivo = 1;
  pthread_mutex_unlock(&trava);
  atomic_store(&carregando, 1);
  if (pthread_create(&t, NULL, fioCarga, (void *)(uintptr_t)atomic_load(&geracao)) != 0) {
    pthread_mutex_lock(&trava); fioVivo = 0; pthread_mutex_unlock(&trava);
    atomic_store(&carregando, 0);
    return;
  }
  pthread_detach(t);
}
static void progEsquecer(void);
void iptvvod_perfil_trocou(void) {
  atomic_fetch_add(&geracao, 1);
  progEsquecer();
  pthread_mutex_lock(&trava);
  biblioLiberar(&bib);
  bibPerfil = -1;
  bibSessao = 0;
  versao++;
  pthread_mutex_unlock(&trava);
}
// Accounts signature: ids, types and on/off. A change reloads the library.
static uint64_t assinaturaContas(void) {
  IptvConta c[IPTVVOD_CONTAS_MAX];
  int n = iptv_contas(c, IPTVVOD_CONTAS_MAX);
  uint64_t h = 1469598103934665603ULL ^ (uint64_t)perfis_ativo();
  for (int i = 0; i < n; i++) h = (h ^ ((uint64_t)c[i].id << 8 | (uint64_t)c[i].tipo << 1 | (uint64_t)!!c[i].ativa)) * 1099511628211ULL;
  return n ? h : 0;
}
int iptvvod_passo(void) {
  static uint64_t ultima;
  static unsigned visto;
  uint64_t a = assinaturaContas();
  unsigned v;
  if (a != ultima) {
    int tinha = ultima != 0;
    ultima = a;
    if (tinha) iptvvod_perfil_trocou();
    if (a) iptvvod_carregar();
  }
  pthread_mutex_lock(&trava);
  v = versao;
  pthread_mutex_unlock(&trava);
  if (v != visto) { visto = v; return 1; }
  return 0;
}
static int bibValidaLocked(void) {
  return bib.ng > 0 && bibPerfil == perfis_ativo() && nv_perfil_sessao_atual(bibSessao);
}
int iptvvod_ativo(void) {
  int r;
  pthread_mutex_lock(&trava);
  r = bibValidaLocked();
  pthread_mutex_unlock(&trava);
  return r;
}
int iptvvod_carregando(void) { return atomic_load(&carregando); }
unsigned iptvvod_fileiras_versao(void) {
  unsigned v;
  pthread_mutex_lock(&trava);
  v = versao;
  pthread_mutex_unlock(&trava);
  return v;
}
int iptvvod_chave_fileira(const char *chave) { return chave && !strncmp(chave, "iptvvod_", 8); }
int iptvvod_n_itens(char kind) {
  int n = 0;
  pthread_mutex_lock(&trava);
  if (bibValidaLocked()) for (int g = 0; g < bib.ng; g++) n += bib.g[g].kind == kind;
  pthread_mutex_unlock(&trava);
  return n;
}
int iptvvod_n_copias(const char *id) {
  int g, n = 0;
  pthread_mutex_lock(&trava);
  if (bibValidaLocked() && (g = grupoPorId(&bib, id, NULL)) >= 0) n = bib.g[g].n;
  pthread_mutex_unlock(&trava);
  return n;
}

// ------------------------------------------------------------- progress
// "iptvvod-prog-p<N>.tsv": "<item id>\t<season>\t<episode>\t<pos>\t<dur>\t<epoch>".
// One line per movie or episode watched; local ids only, never synced.
#define PROG_MAX 4000
typedef struct { char id[32]; int t, e; double pos, dur; long long quando; } Prog;
static Prog progs[PROG_MAX];
static int nProgs, progPerfil = -1;
static uint64_t progSessao;
static pthread_mutex_t travaProg = PTHREAD_MUTEX_INITIALIZER;
static void progArquivo(char *dst, size_t tam, int perfil) { snprintf(dst, tam, "iptvvod-prog-p%d.tsv", perfil > 0 ? perfil : 1); }
static void progGarantirLocked(void) {
  int perfil = perfis_ativo();
  uint64_t sessao = nv_perfil_sessao_ler();
  char nome[48], *t, *p;
  if (perfil == progPerfil && sessao == progSessao) return;
  progPerfil = perfil; progSessao = sessao; nProgs = 0;
  progArquivo(nome, sizeof nome, perfil);
  t = dados_ler(nome);
  for (p = t; p && *p && nProgs < PROG_MAX;) {
    char *fim = strchr(p, '\n');
    Prog r;
    if (fim) *fim = 0;
    memset(&r, 0, sizeof r);
    if (sscanf(p, "%31[^\t]\t%d\t%d\t%lf\t%lf\t%lld", r.id, &r.t, &r.e, &r.pos, &r.dur, &r.quando) == 6 &&
        iptvvod_id_ler(r.id, NULL, NULL, NULL, NULL) && r.dur > 0)
      progs[nProgs++] = r;
    p = fim ? fim + 1 : NULL;
  }
  free(t);
}
static void progGravarLocked(void) {
  size_t cap = (size_t)nProgs * 96 + 1, k = 0;
  char *t = malloc(cap), nome[48];
  if (!t) return;
  t[0] = 0;
  for (int i = 0; i < nProgs && k < cap; i++)
    k += (size_t)snprintf(t + k, cap - k, "%s\t%d\t%d\t%.1f\t%.1f\t%lld\n", progs[i].id, progs[i].t, progs[i].e,
                          progs[i].pos, progs[i].dur, progs[i].quando);
  progArquivo(nome, sizeof nome, progPerfil);
  if (nv_perfil_sessao_travar(progSessao)) {
    if (nProgs) dados_gravar(nome, t); else dados_apagar(nome);
    nv_perfil_sessao_destravar();
  }
  free(t);
}
int iptvvod_progresso_gravar(const char *id, int temporada, int episodio, double posSeg, double durSeg) {
  char k;
  int i;
  if (!iptvvod_id_ler(id, NULL, &k, NULL, NULL) || k == 'e' || strlen(id) >= sizeof progs[0].id || durSeg <= 1.0) return 0;
  if (k == 'm') temporada = episodio = 0;
  pthread_mutex_lock(&travaProg);
  progGarantirLocked();
  if (posSeg <= 0.0) {
    // Unwatched: every record of the item goes.
    int w = 0;
    for (i = 0; i < nProgs; i++) if (strcmp(progs[i].id, id)) progs[w++] = progs[i];
    nProgs = w;
  } else {
    for (i = 0; i < nProgs && !(!strcmp(progs[i].id, id) && progs[i].t == temporada && progs[i].e == episodio); i++) {}
    if (i == nProgs) {
      if (nProgs == PROG_MAX) {   // the oldest record makes room
        int velho = 0;
        for (int j = 1; j < nProgs; j++) if (progs[j].quando < progs[velho].quando) velho = j;
        progs[velho] = progs[--nProgs];
      }
      i = nProgs++;
      snprintf(progs[i].id, sizeof progs[i].id, "%s", id);
      progs[i].t = temporada; progs[i].e = episodio;
    }
    progs[i].pos = posSeg; progs[i].dur = durSeg; progs[i].quando = (long long)time(NULL);
  }
  progGravarLocked();
  pthread_mutex_unlock(&travaProg);
  return 1;
}
static int progPct(const Prog *r) {
  int p = r->dur > 0 ? (int)(r->pos * 100.0 / r->dur) : 0;
  return p >= IPTVVOD_VISTO_PCT ? 100 : p < 0 ? 0 : p;
}
int iptvvod_progresso_de(const char *id, int *pct, int *temporada, int *episodio) {
  int melhor = -1;
  pthread_mutex_lock(&travaProg);
  progGarantirLocked();
  for (int i = 0; i < nProgs; i++)
    if (!strcmp(progs[i].id, id) && (melhor < 0 || progs[i].quando > progs[melhor].quando)) melhor = i;
  if (melhor >= 0) {
    if (pct) *pct = progPct(&progs[melhor]);
    if (temporada) *temporada = progs[melhor].t;
    if (episodio) *episodio = progs[melhor].e;
  }
  pthread_mutex_unlock(&travaProg);
  return melhor >= 0;
}
static void progEsquecer(void) {
  pthread_mutex_lock(&travaProg);
  progPerfil = -1; nProgs = 0;
  pthread_mutex_unlock(&travaProg);
}
// The card shows (and the player resumes from) the latest position.
static void progAplicar(CatItem *it) {
  int pct = 0, t = 0, e = 0;
  if (!iptvvod_progresso_de(it->imdb, &pct, &t, &e)) return;
  it->progresso = pct;
  if (!strcmp(it->tipo, "series") && e > 0) { it->temporada = t; it->episodio = e; }
}

// ------------------------------------------------------------- Home rows
static void itemDoGrupo(const Biblio *b, int g, CatItem *it) {
  const Copia *c = &b->c[b->g[g].primeira];
  memset(it, 0, sizeof *it);
  iptvvod_id_montar(it->imdb, sizeof it->imdb, b->g[g].chave, b->g[g].kind, 0, 0);
  snprintf(it->tipo, sizeof it->tipo, "%s", b->g[g].kind == 's' ? "series" : "movie");
  iptvvod_titulo_limpo(c->nome, it->titulo, sizeof it->titulo);
  for (int j = b->g[g].primeira; j >= 0 && !it->poster[0]; j = b->c[j].proximo)
    snprintf(it->poster, sizeof it->poster, "%s", b->c[j].poster);
  if (c->ano) snprintf(it->meta, sizeof it->meta, "%d", c->ano);
  if (c->categoria[0]) snprintf(it->genero, sizeof it->genero, "%s", c->categoria);
  // The panel's rating is not IMDb's: the page labels `nota` as IMDb, so none here.
  snprintf(it->origem, sizeof it->origem, "IPTV");
}
static int recentes(const void *a, const void *b, void *u) {
  const Biblio *bb = u;
  long long x = bb->g[*(const int *)a].adicionado, y = bb->g[*(const int *)b].adicionado;
  return (y > x) - (y < x);
}
// qsort_r is not portable across the targets: a small insertion into a top-N.
static int topRecentes(const Biblio *b, char kind, int *out, int max) {
  int n = 0;
  for (int g = 0; g < b->ng; g++) {
    int k;
    if (b->g[g].kind != kind) continue;
    if (n < max) out[n++] = g;
    else if (recentes(&g, &out[n - 1], (void *)b) < 0) out[n - 1] = g;
    else continue;
    for (k = n - 1; k > 0 && recentes(&out[k], &out[k - 1], (void *)b) < 0; k--) {
      int t = out[k]; out[k] = out[k - 1]; out[k - 1] = t;
    }
  }
  return n;
}
int iptvvod_fileiras_copiar(CatItem *itens, int maxItens, CatFileira *fils, int maxFils, int *nItens) {
  int nf = 0, ni = 0, nCont = 0;
  char cont[IPTVVOD_POR_FILEIRA][32];
  if (nItens) *nItens = 0;
  // Continue watching: items whose latest record is started and not finished,
  // most recent first (a snapshot of the progress, then the library lock).
  pthread_mutex_lock(&travaProg);
  progGarantirLocked();
  {
    Prog *ord = malloc(sizeof(Prog) * (size_t)(nProgs ? nProgs : 1));
    int no = 0;
    if (ord) {
      for (int i = 0; i < nProgs; i++) {
        int j, dup = 0;
        for (j = 0; j < no; j++) if (!strcmp(ord[j].id, progs[i].id)) { dup = 1; if (progs[i].quando > ord[j].quando) ord[j] = progs[i]; break; }
        if (!dup) ord[no++] = progs[i];
      }
      for (int i = 1; i < no; i++) for (int j = i; j > 0 && ord[j].quando > ord[j - 1].quando; j--) { Prog t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
      for (int i = 0; i < no && nCont < IPTVVOD_POR_FILEIRA; i++) {
        int pct = progPct(&ord[i]);
        if (pct >= 1 && pct < 100) snprintf(cont[nCont++], 32, "%s", ord[i].id);
      }
      free(ord);
    }
  }
  pthread_mutex_unlock(&travaProg);
  pthread_mutex_lock(&trava);
  if (bibValidaLocked() && nCont && nf < maxFils) {
    int n = 0;
    for (int i = 0; i < nCont && ni + n < maxItens; i++) {
      int g = grupoPorId(&bib, cont[i], NULL);
      if (g >= 0) itemDoGrupo(&bib, g, &itens[ni + n++]);
    }
    if (n) {
      memset(&fils[nf], 0, sizeof fils[nf]);
      snprintf(fils[nf].chave, sizeof fils[nf].chave, "iptvvod_continuar");
      snprintf(fils[nf].titulo, sizeof fils[nf].titulo, "%s · IPTV", i18n("Continuar assistindo"));
      snprintf(fils[nf].tipo, sizeof fils[nf].tipo, "movie");
      fils[nf].ini = ni; fils[nf].n = n;
      ni += n; nf++;
    }
  }
  if (bibValidaLocked()) {
    for (int k = 0; k < 2 && nf < maxFils; k++) {
      int top[IPTVVOD_POR_FILEIRA], n = topRecentes(&bib, k ? 's' : 'm', top, IPTVVOD_POR_FILEIRA);
      if (!n || ni + n > maxItens) continue;
      for (int i = 0; i < n; i++) itemDoGrupo(&bib, top[i], &itens[ni + i]);
      memset(&fils[nf], 0, sizeof fils[nf]);
      snprintf(fils[nf].chave, sizeof fils[nf].chave, "iptvvod_%s_recentes", k ? "series" : "movie");
      snprintf(fils[nf].titulo, sizeof fils[nf].titulo, "%s · IPTV",
               i18n(k ? "Séries adicionadas recentemente" : "Filmes adicionados recentemente"));
      snprintf(fils[nf].tipo, sizeof fils[nf].tipo, "%s", k ? "series" : "movie");
      fils[nf].ini = ni; fils[nf].n = n;
      ni += n; nf++;
    }
  }
  pthread_mutex_unlock(&trava);
  for (int i = 0; i < ni; i++) progAplicar(&itens[i]);
  if (nItens) *nItens = ni;
  return nf;
}

// ------------------------------------------------------------- browsing
// A group is in a category when any of its copies is (providers name the
// same film's category differently).
static int grupoNaCategoria(const Biblio *b, int g, const char *cat) {
  if (!cat || !cat[0]) return 1;
  for (int j = b->g[g].primeira; j >= 0; j = b->c[j].proximo) if (!strcmp(b->c[j].categoria, cat)) return 1;
  return 0;
}
int iptvvod_categorias(char kind, char (*out)[96], int max) {
  int n = 0;
  pthread_mutex_lock(&trava);
  for (int i = 0; bibValidaLocked() && i < bib.n && n < max; i++) {
    const Copia *c = &bib.c[i];
    int k;
    if (c->kind != kind || !c->categoria[0]) continue;
    for (k = 0; k < n && strcmp(out[k], c->categoria); k++) {}
    if (k == n) snprintf(out[n++], 96, "%s", c->categoria);
  }
  pthread_mutex_unlock(&trava);
  return n;
}
static const Biblio *ordBib;   // read only under `trava`, by the comparator below
static int ordRecentes(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  long long dx = ordBib->g[x].adicionado, dy = ordBib->g[y].adicionado;
  if (dx != dy) return dy > dx ? 1 : -1;
  return (x > y) - (x < y);
}
int iptvvod_pagina(char kind, const char *categoria, int pagina, CatItem *out, int max, int *total) {
  int *ord, n = 0, ini, w = 0;
  if (total) *total = 0;
  if (pagina < 1 || max <= 0) return 0;
  pthread_mutex_lock(&trava);
  if (!bibValidaLocked() || !(ord = malloc(sizeof(int) * (size_t)bib.ng))) { pthread_mutex_unlock(&trava); return 0; }
  for (int g = 0; g < bib.ng; g++) if (bib.g[g].kind == kind && grupoNaCategoria(&bib, g, categoria)) ord[n++] = g;
  // Newest first; equal dates keep library order (O(n log n) for big lists).
  ordBib = &bib;
  qsort(ord, (size_t)n, sizeof *ord, ordRecentes);
  ini = (pagina - 1) * IPTVVOD_PAGINA;
  for (int i = ini; i < n && i < ini + IPTVVOD_PAGINA && w < max; i++) itemDoGrupo(&bib, ord[i], &out[w++]);
  if (total) *total = n;
  free(ord);
  pthread_mutex_unlock(&trava);
  for (int i = 0; i < w; i++) progAplicar(&out[i]);
  return w;
}

// ------------------------------------------------------------- search
int iptvvod_buscar(const char *termo, char kind, CatItem *saida, int max) {
  char q[128];
  int n = 0, nivel;
  iptvvod_normalizar(termo ? termo : "", q, sizeof q);
  if (!q[0] || max <= 0) return 0;
  pthread_mutex_lock(&trava);
  // Three passes keep the best matches first without sorting the library.
  for (nivel = 0; nivel < 3 && n < max && bibValidaLocked(); nivel++)
    for (int g = 0; g < bib.ng && n < max; g++) {
      const char *t = bib.c[bib.g[g].primeira].norm;
      int casa;
      if (bib.g[g].kind != kind || !t[0]) continue;
      casa = nivel == 0 ? !strcmp(t, q) : nivel == 1 ? (!strncmp(t, q, strlen(q)) && strcmp(t, q))
           : (strstr(t, q) && strncmp(t, q, strlen(q)));
      if (casa) itemDoGrupo(&bib, g, &saida[n++]);
    }
  pthread_mutex_unlock(&trava);
  for (int i = 0; i < n; i++) progAplicar(&saida[i]);
  return n;
}

// ------------------------------------------------------------- title page
// The copies of one group, copied out under the lock (the worker then talks
// to the providers without holding it).
typedef struct { unsigned conta; int tipoProv; char sid[XTVOD_SID], ext[8]; char nome[160]; char ref[1024]; } Ref;
static void refDe(Ref *r, const Copia *c) {
  r->conta = c->conta;
  r->tipoProv = c->tipoProv;
  snprintf(r->sid, sizeof r->sid, "%s", c->sid);
  snprintf(r->ext, sizeof r->ext, "%s", c->ext);
  snprintf(r->nome, sizeof r->nome, "%s", c->nome);
  r->ref[0] = 0;
  if (c->ref && strlen(c->ref) < sizeof r->ref) snprintf(r->ref, sizeof r->ref, "%s", c->ref);
}
static int copiasDo(const char *id, char kindQuer, Ref *out, int max, uint64_t *grupoOut) {
  int g, n = 0;
  char k;
  pthread_mutex_lock(&trava);
  if (bibValidaLocked() && (g = grupoPorId(&bib, id, &k)) >= 0 && (kindQuer == 0 || bib.g[g].kind == kindQuer)) {
    if (grupoOut) *grupoOut = bib.g[g].chave;
    for (int j = bib.g[g].primeira; j >= 0 && n < max; j = bib.c[j].proximo) refDe(&out[n++], &bib.c[j]);
  }
  pthread_mutex_unlock(&trava);
  return n;
}
static void fichaParaItem(const XtVodFicha *f, CatItem *it) {
  char t[160];
  if (f->sinopse[0]) snprintf(it->sinopse, sizeof it->sinopse, "%s", f->sinopse);
  if (f->genero[0]) snprintf(it->genero, sizeof it->genero, "%s", f->genero);
  if (f->direcao[0]) snprintf(it->direcao, sizeof it->direcao, "%s", f->direcao);
  if (f->fundo[0]) { snprintf(it->backdrop, sizeof it->backdrop, "%s", f->fundo); snprintf(it->backdropCatalogo, sizeof it->backdropCatalogo, "%s", f->fundo); }
  if (f->poster[0] && !it->poster[0]) snprintf(it->poster, sizeof it->poster, "%s", f->poster);

  if (f->ano && f->duracaoMin) snprintf(it->meta, sizeof it->meta, "%d · %d min", f->ano, f->duracaoMin);
  else if (f->ano) snprintf(it->meta, sizeof it->meta, "%d", f->ano);
  if (f->nome[0] && !it->titulo[0]) { iptvvod_titulo_limpo(f->nome, t, sizeof t); snprintf(it->titulo, sizeof it->titulo, "%s", t); }
  if (f->elenco[0] && !it->nElenco) {
    const char *p = f->elenco;
    while (*p && it->nElenco < CAT_ELENCO_MAX) {
      const char *v = strchr(p, ',');
      size_t n = v ? (size_t)(v - p) : strlen(p);
      while (n && *p == ' ') { p++; n--; }
      while (n && p[n - 1] == ' ') n--;
      if (n && n < sizeof it->elenco[0].nome) {
        memcpy(it->elenco[it->nElenco].nome, p, n);
        it->elenco[it->nElenco].nome[n] = 0;
        it->nElenco++;
      }
      if (!v) break;
      p = v + 1;
    }
  }
}

// Episode sources of the series opened last: (season, episode) -> copies.
#define EP_CACHE_MAX 4096
typedef struct { uint64_t grupo; int t, e; unsigned conta; int tipoProv; char sid[XTVOD_SID], ext[8]; char ref[1024]; } EpFonte;
static EpFonte epCache[EP_CACHE_MAX];
static int nEpCache;
static uint64_t epCacheGrupo;
static pthread_mutex_t travaEp = PTHREAD_MUTEX_INITIALIZER;

static int epCmp(const void *a, const void *b) {
  const CatEp *x = a, *y = b;
  if (x->temporada != y->temporada) return x->temporada < y->temporada ? -1 : 1;
  return (x->episodio > y->episodio) - (x->episodio < y->episodio);
}
// Fetches every copy's episode list and merges by (season, episode).
// Episodes of one provider copy, as XtVodEp + the reference to play each.
static int episodiosDe(const Ref *r, XtVodFicha *f, XtVodEp *v, char (*refs)[1024], int max, uint64_t sessao) {
  int n = 0;
  memset(f, 0, sizeof *f);
  if (r->tipoProv == IPTV_XTREAM) {
    n = xtvod_ficha_serie(r->conta, r->sid, f, v, max, sessao);
    for (int k = 0; k < n; k++) refs[k][0] = 0;
    return n;
  }
  if (r->tipoProv == IPTV_STALKER) {
    // "cmd\x1f1,2,3": one season, these episode numbers, the same cmd.
    const char *sep = strchr(r->ref, '\x1f');
    int nums[512], k, ne;
    char cmd[1024];
    if (!sep) return -1;
    snprintf(cmd, sizeof cmd, "%.*s", (int)(sep - r->ref), r->ref);
    ne = stvod_episodios(sep + 1, nums, 512);
    for (k = 0; k < ne && n < max; k++, n++) {
      memset(&v[n], 0, sizeof v[n]);
      v[n].temporada = 1; v[n].episodio = nums[k];
      snprintf(v[n].sid, sizeof v[n].sid, "%s", r->sid);
      snprintf(refs[n], 1024, "%s", cmd);
    }
    snprintf(f->nome, sizeof f->nome, "%s", r->nome);
    return n;
  }
  if (r->tipoProv == IPTV_M3U) {
    pthread_mutex_lock(&trava);
    for (int k = 0; bibValidaLocked() && k < bib.nep && n < max; k++)
      if (bib.ep[k].conta == r->conta && !strcmp(bib.ep[k].sid, r->sid) && strlen(bib.ep[k].url) < 1024) {
        memset(&v[n], 0, sizeof v[n]);
        v[n].temporada = bib.ep[k].t; v[n].episodio = bib.ep[k].e;
        snprintf(v[n].sid, sizeof v[n].sid, "%s", r->sid);
        snprintf(v[n].ext, sizeof v[n].ext, "%s", bib.ep[k].ext);
        snprintf(refs[n], 1024, "%s", bib.ep[k].url);
        n++;
      }
    pthread_mutex_unlock(&trava);
    snprintf(f->nome, sizeof f->nome, "%s", r->nome);
    return n;
  }
  return -1;
}
// Fetches every copy's episode list and merges by (season, episode).
static int serieMesclar(const Ref *r, int nr, uint64_t grupo, CatItem *it, CatEp *eps, int maxEps) {
  XtVodEp *v = malloc(sizeof(XtVodEp) * 2000);
  char (*refs)[1024] = malloc(1024 * 2000);
  EpFonte *novas = malloc(sizeof(EpFonte) * EP_CACHE_MAX);
  int nEps = 0, nNovas = 0, algum = 0;
  uint64_t sessao = nv_perfil_sessao_ler();
  if (!v || !novas || !refs) { free(v); free(novas); free(refs); return -1; }
  // Xtream copies first: their records carry the richest metadata.
  for (int pass = 0; pass < 2; pass++)
  for (int i = 0; i < nr; i++) {
    XtVodFicha f;
    int n;
    if ((pass == 0) != (r[i].tipoProv == IPTV_XTREAM)) continue;
    n = episodiosDe(&r[i], &f, v, refs, 2000, sessao);
    if (n < 0) continue;
    algum = 1;
    if (it && r[i].tipoProv == IPTV_XTREAM) fichaParaItem(&f, it);
    for (int k = 0; k < n; k++) {
      int j;
      if (nNovas < EP_CACHE_MAX) {
        EpFonte *ef = &novas[nNovas++];
        memset(ef, 0, sizeof *ef);
        ef->grupo = grupo; ef->t = v[k].temporada; ef->e = v[k].episodio;
        ef->conta = r[i].conta; ef->tipoProv = r[i].tipoProv;
        snprintf(ef->sid, sizeof ef->sid, "%s", v[k].sid);
        snprintf(ef->ext, sizeof ef->ext, "%s", v[k].ext);
        snprintf(ef->ref, sizeof ef->ref, "%s", refs[k]);
      }
      if (!eps) continue;
      for (j = 0; j < nEps && !(eps[j].temporada == v[k].temporada && eps[j].episodio == v[k].episodio); j++) {}
      if (j < nEps) {
        // Another provider may know the name or plot this one lacked.
        if (!eps[j].nome[0] && v[k].nome[0]) snprintf(eps[j].nome, sizeof eps[j].nome, "%s", v[k].nome);
        continue;
      }
      if (nEps >= maxEps) continue;
      memset(&eps[nEps], 0, sizeof eps[nEps]);
      eps[nEps].temporada = v[k].temporada;
      eps[nEps].episodio = v[k].episodio;
      snprintf(eps[nEps].nome, sizeof eps[nEps].nome, "%s", v[k].nome);
      snprintf(eps[nEps].sinopse, sizeof eps[nEps].sinopse, "%s", v[k].sinopse);
      snprintf(eps[nEps].thumb, sizeof eps[nEps].thumb, "%s", v[k].thumb);
      if (v[k].duracaoMin) snprintf(eps[nEps].duracao, sizeof eps[nEps].duracao, "%d min", v[k].duracaoMin);
      iptvvod_id_montar(eps[nEps].vid, sizeof eps[nEps].vid, grupo, 'e', v[k].temporada, v[k].episodio);
      nEps++;
    }
  }
  free(v); free(refs);
  if (algum) {
    pthread_mutex_lock(&travaEp);
    memcpy(epCache, novas, sizeof(EpFonte) * (size_t)nNovas);
    nEpCache = nNovas;
    epCacheGrupo = grupo;
    pthread_mutex_unlock(&travaEp);
  }
  free(novas);
  if (eps && nEps > 1) qsort(eps, (size_t)nEps, sizeof *eps, epCmp);
  return algum ? nEps : -1;
}
int iptvvod_ficha(CatItem *item, CatEp *eps, int maxEps) {
  Ref r[IPTVVOD_CONTAS_MAX];
  uint64_t grupo = 0;
  char k;
  int nr;
  if (!item || !iptvvod_id_ler(item->imdb, NULL, &k, NULL, NULL) || k == 'e') return -1;
  nr = copiasDo(item->imdb, k, r, IPTVVOD_CONTAS_MAX, &grupo);
  if (!nr) return -1;
  if (k == 'm') {
    uint64_t sessao = nv_perfil_sessao_ler();
    for (int i = 0; i < nr; i++) {
      XtVodFicha f;
      if (r[i].tipoProv == IPTV_XTREAM && xtvod_ficha_filme(r[i].conta, r[i].sid, &f, sessao)) { fichaParaItem(&f, item); return 0; }
    }
    return 0;   // the row's own data stays; sources still work
  }
  {
    int n = serieMesclar(r, nr, grupo, item, eps, maxEps);
    if (n > 0) {
      int t = -1;
      item->nTemporadas = 0;
      for (int i = 0; i < n && item->nTemporadas < CAT_TEMP_MAX; i++)
        if (eps[i].temporada != t) item->temporadas[item->nTemporadas++] = t = eps[i].temporada;
      snprintf(item->meta, sizeof item->meta, item->nTemporadas == 1 ? i18n("%d temporada") : i18n("%d temporadas"),
               item->nTemporadas);
    }
    return n;
  }
}

// ------------------------------------------------------------- sources
static pthread_mutex_t travaFontes = PTHREAD_MUTEX_INITIALIZER;
static char fontesAlvo[64];
static int fontesEstado;
static Stream *fontesLista;
static int fontesN;
static unsigned fontesPedido;

static int alturaDoNome(const char *nome) {
  char n[160];
  snprintf(n, sizeof n, " %s ", nome);
  for (char *c = n; *c; c++) *c = (char)toupper((unsigned char)*c);
  if (strstr(n, "4K") || strstr(n, "2160") || strstr(n, "UHD")) return 2160;
  if (strstr(n, "1080") || strstr(n, "FHD")) return 1080;
  if (strstr(n, " HD ") || strstr(n, "720")) return 720;
  return 0;
}
static void nomeConta(unsigned conta, char *dst, size_t tam) {
  IptvConta c[IPTVVOD_CONTAS_MAX];
  int n = iptv_contas(c, IPTVVOD_CONTAS_MAX);
  snprintf(dst, tam, "IPTV");
  for (int i = 0; i < n; i++) if (c[i].id == conta) { snprintf(dst, tam, "%s", c[i].nome[0] ? c[i].nome : "IPTV"); return; }
}
// One playable source of a copy. Stalker asks the portal for a fresh link
// (create_link; BLOCKS, worker threads only); M3U plays the playlist URL.
static int montarFonte(Stream *s, int tipoProv, unsigned conta, int episodio, int epNum, const char *sid,
                       const char *ext, const char *ref, const char *nomePainel) {
  char conta_[96], up[12];
  size_t i;
  memset(s, 0, sizeof *s);
  if (tipoProv == IPTV_XTREAM) {
    if (!xtream_fonte_vod_url(conta, episodio, sid, ext, s->url, sizeof s->url)) return 0;
  } else if (tipoProv == IPTV_STALKER) {
    char cmd[1024];
    const char *sep = ref ? strchr(ref, '\x1f') : NULL;
    snprintf(cmd, sizeof cmd, "%.*s", sep ? (int)(sep - ref) : (int)strlen(ref ? ref : ""), ref ? ref : "");
    if (!cmd[0] || !stalker_fonte_vod_link(conta, cmd, episodio ? epNum : 0, s->url, sizeof s->url, nv_perfil_sessao_ler()))
      return 0;
  } else if (tipoProv == IPTV_M3U) {
    if (!ref || (strncmp(ref, "http://", 7) && strncmp(ref, "https://", 8))) return 0;
    snprintf(s->url, sizeof s->url, "%s", ref);
  } else return 0;
  nomeConta(conta, conta_, sizeof conta_);
  s->altura = alturaDoNome(nomePainel ? nomePainel : "");
  s->fileIdx = -1;
  s->mp4 = ext && (!strcmp(ext, "mp4") || !strcmp(ext, "m4v"));
  for (i = 0; ext && ext[i] && i + 1 < sizeof up; i++) up[i] = (char)toupper((unsigned char)ext[i]);
  up[i] = 0;
  if (s->altura) snprintf(s->rotulo, sizeof s->rotulo, "%dp · %s", s->altura, conta_);
  else snprintf(s->rotulo, sizeof s->rotulo, "%s", conta_);
  snprintf(s->provedor, sizeof s->provedor, "IPTV · %s", conta_);
  snprintf(s->descricao, sizeof s->descricao, "%s\n%s", nomePainel && *nomePainel ? nomePainel : conta_, up);
  snprintf(s->bingeGroup, sizeof s->bingeGroup, "qv|%u", conta);
  return 1;
}
// The cached copies of one episode, copied out (Stalker links are then asked
// without holding the lock).
static int episodioCopias(uint64_t grupo, int t, int e, EpFonte *out, int max) {
  int n = 0;
  pthread_mutex_lock(&travaEp);
  for (int i = 0; epCacheGrupo == grupo && i < nEpCache && n < max; i++)
    if (epCache[i].t == t && epCache[i].e == e) out[n++] = epCache[i];
  pthread_mutex_unlock(&travaEp);
  return n;
}
static int montarDaRef(Stream *s, const Ref *r) {
  return montarFonte(s, r->tipoProv, r->conta, 0, 0, r->sid, r->ext, r->ref, r->nome);
}
static int montarDoEp(Stream *s, const EpFonte *f, const char *nome) {
  if (!montarFonte(s, f->tipoProv, f->conta, 1, f->e, f->sid, f->ext, f->ref, nome)) return 0;
  {
    char ep[24];
    snprintf(ep, sizeof ep, " · S%02dE%02d", f->t, f->e);
    strncat(s->rotulo, ep, sizeof s->rotulo - strlen(s->rotulo) - 1);
  }
  return 1;
}
typedef struct { char alvo[64]; unsigned pedido; } PedidoFontes;
// The provider copies of one library target (movie or episode). 1 = the
// target exists (the list may still be empty).
static int fontesDoAlvo(const char *alvo, Stream **saida, int *nSaida) {
  Stream *l = NULL;
  int n = 0, ok = 0, t = 0, e = 0;
  uint64_t grupo = 0;
  char k = 0;
  *saida = NULL; *nSaida = 0;
  if (iptvvod_id_ler(alvo, NULL, &k, &t, &e)) {
    Ref r[IPTVVOD_CONTAS_MAX];
    int nr = copiasDo(alvo, k == 'e' ? 's' : 'm', r, IPTVVOD_CONTAS_MAX, &grupo);
    if (nr && k == 'm') {
      l = calloc((size_t)nr, sizeof *l);
      for (int i = 0; l && i < nr; i++) if (montarDaRef(&l[n], &r[i])) n++;
      ok = l != NULL;
    } else if (nr && k == 'e') {
      int tem;
      pthread_mutex_lock(&travaEp);
      tem = epCacheGrupo == grupo && nEpCache > 0;
      pthread_mutex_unlock(&travaEp);
      if (!tem) serieMesclar(r, nr, grupo, NULL, NULL, 0);
      {
        EpFonte *fs = malloc(sizeof(EpFonte) * IPTVVOD_CONTAS_MAX);
        int nf = fs ? episodioCopias(grupo, t, e, fs, IPTVVOD_CONTAS_MAX) : 0;
        l = calloc(IPTVVOD_CONTAS_MAX, sizeof *l);
        for (int i = 0; l && i < nf; i++) {
          char nome[160] = "";
          for (int j = 0; j < nr; j++) if (r[j].conta == fs[i].conta) snprintf(nome, sizeof nome, "%s", r[j].nome);
          if (montarDoEp(&l[n], &fs[i], nome)) n++;
        }
        free(fs);
      }
      ok = l != NULL;
    }
  }
  if (!n) { free(l); l = NULL; }
  *saida = l; *nSaida = n;
  return ok;
}
static void *fioFontes(void *u) {
  PedidoFontes p = *(PedidoFontes *)u;
  Stream *l = NULL;
  int n = 0, ok;
  free(u);
  ok = fontesDoAlvo(p.alvo, &l, &n);
  pthread_mutex_lock(&travaFontes);
  if (p.pedido == fontesPedido) {
    free(fontesLista);
    fontesLista = l; fontesN = n; l = NULL;
    fontesEstado = ok ? VF_PRONTO : VF_FALHOU;
  }
  pthread_mutex_unlock(&travaFontes);
  free(l);
  printf("[iptvvod] %d fonte(s) para o titulo\n", n);
  fflush(stdout);
  return NULL;
}
int iptvvod_fontes_pedir(const char *alvo) {
  PedidoFontes *p;
  pthread_t t;
  if (!iptvvod_id_ler(alvo, NULL, NULL, NULL, NULL) || strlen(alvo) >= sizeof fontesAlvo) return 0;
  p = malloc(sizeof *p);
  if (!p) return 0;
  pthread_mutex_lock(&travaFontes);
  snprintf(fontesAlvo, sizeof fontesAlvo, "%s", alvo);
  free(fontesLista); fontesLista = NULL; fontesN = 0;
  fontesEstado = VF_PENDENTE;
  p->pedido = ++fontesPedido;
  pthread_mutex_unlock(&travaFontes);
  snprintf(p->alvo, sizeof p->alvo, "%s", alvo);
  if (pthread_create(&t, NULL, fioFontes, p) != 0) {
    free(p);
    pthread_mutex_lock(&travaFontes); fontesEstado = VF_FALHOU; pthread_mutex_unlock(&travaFontes);
    return 1;
  }
  pthread_detach(t);
  return 1;
}
int iptvvod_fontes_colher(const char *alvo, Stream **lista, int *n) {
  int e;
  if (lista) *lista = NULL;
  if (n) *n = 0;
  pthread_mutex_lock(&travaFontes);
  if (!alvo || strcmp(alvo, fontesAlvo)) { pthread_mutex_unlock(&travaFontes); return VF_FALHOU; }
  e = fontesEstado;
  if (e == VF_PRONTO) {
    if (lista) { *lista = fontesLista; fontesLista = NULL; }
    if (n) *n = fontesN;
    fontesN = 0;
    fontesEstado = VF_NADA;
    fontesAlvo[0] = 0;
  } else if (e == VF_FALHOU) fontesAlvo[0] = 0;
  pthread_mutex_unlock(&travaFontes);
  return e;
}

// ------------------------------------------------------------- addon titles
static const char *(*chaveTmdbFn)(void);
static char tmdbBase[160] = "https://api.themoviedb.org/3";
void iptvvod_definir_chave_tmdb(const char *(*chave)(void)) { chaveTmdbFn = chave; }
// QR TIME: the title and year the app already shows for an addon id (its
// catalogue), so a provider copy is found without TMDB, the way Nuvio One
// matches On Demand copies (title + year within one). Set by iptvvodaddons.c.
static int (*tituloFn)(const char *id, const char *tipo, char *titulo, size_t n, int *ano);
void iptvvod_definir_titulo(int (*fn)(const char *id, const char *tipo, char *titulo, size_t n, int *ano)) { tituloFn = fn; }
void iptvvod_teste_tmdb_base(const char *base) { snprintf(tmdbBase, sizeof tmdbBase, "%s", base ? base : ""); }

typedef struct { long tmdb; char kind; char titulo[128], original[128]; int ano; } Resolvido;
// "tt123[:S:E]" or "tmdb:123[:S:E]". 1 = parsed.
static int idCatalogo(const char *id, char *imdb, size_t tam, long *tmdb, int *t, int *e) {
  const char *p;
  *tmdb = 0; *t = *e = 0; imdb[0] = 0;
  if (!id) return 0;
  if (id[0] == 't' && id[1] == 't' && isdigit((unsigned char)id[2])) {
    size_t n = 2;
    while (isdigit((unsigned char)id[n])) n++;
    if (n >= tam || n > 12) return 0;
    memcpy(imdb, id, n); imdb[n] = 0;
    p = id + n;
  } else if (!strncmp(id, "tmdb:", 5) && isdigit((unsigned char)id[5])) {
    char *f;
    *tmdb = strtol(id + 5, &f, 10);
    if (*tmdb <= 0) return 0;
    p = f;
  } else return 0;
  if (*p == ':') { if (sscanf(p, ":%d:%d", t, e) != 2 || *t < 0 || *e <= 0) return 0; }
  else if (*p) return 0;
  return 1;
}
static int anoDeData(const char *s) { int a = s ? atoi(s) : 0; return a >= 1888 && a <= 2100 ? a : 0; }
// TMDB lookup of a REAL id (from an addon): never a provider id.
// 1 resolved, 0 TMDB answered without a match, -1 could not ask (no key, no
// answer): only definite answers are cached.
static int resolver(const char *imdb, long tmdb, char kind, Resolvido *r) {
  const char *chave = chaveTmdbFn ? chaveTmdbFn() : NULL;
  char url[400], data[16];
  char *corpo;
  const char *v = NULL, *f;
  memset(r, 0, sizeof *r);
  if (!chave || !chave[0] || !tmdbBase[0]) return -1;
  if (imdb[0])
    snprintf(url, sizeof url, "%s/find/%s?api_key=%s&external_source=imdb_id", tmdbBase, imdb, chave);
  else
    snprintf(url, sizeof url, "%s/%s/%ld?api_key=%s", tmdbBase, kind == 's' ? "tv" : "movie", tmdb, chave);
  corpo = rede_baixar_com(url, 15, NULL);
  if (!corpo) return -1;
  if (imdb[0]) {
    v = js_array(corpo, NULL, kind == 's' ? "tv_results" : "movie_results");
    if (!v || *v != '{') { free(corpo); return 0; }
  } else v = corpo;
  f = js_fim(v);
  r->kind = kind;
  r->tmdb = imdb[0] ? (long)js_num(v, f, "id", 0.0) : tmdb;
  data[0] = 0;
  if (kind == 's') {
    js_texto(v, f, "name", r->titulo, sizeof r->titulo);
    js_texto(v, f, "original_name", r->original, sizeof r->original);
    js_texto(v, f, "first_air_date", data, sizeof data);
  } else {
    js_texto(v, f, "title", r->titulo, sizeof r->titulo);
    js_texto(v, f, "original_title", r->original, sizeof r->original);
    js_texto(v, f, "release_date", data, sizeof data);
  }
  r->ano = anoDeData(data);
  free(corpo);
  return r->tmdb > 0 && (r->titulo[0] || r->original[0]);
}
// Does this copy really carry the resolved title? Same normalized title, or
// one containing the other with the year within one.
static int copiaConfere(const Copia *c, const char *n1, const char *n2, int ano) {
  if (!c->norm[0]) return 0;
  if ((n1[0] && !strcmp(c->norm, n1)) || (n2[0] && !strcmp(c->norm, n2))) return !ano || !c->ano || abs(c->ano - ano) <= 1;
  if (ano && c->ano && abs(c->ano - ano) <= 1 &&
      ((n1[0] && (strstr(c->norm, n1) || strstr(n1, c->norm))) || (n2[0] && (strstr(c->norm, n2) || strstr(n2, c->norm)))))
    return 1;
  return 0;
}
// Groups matching the resolved title. Copied out under the lock.
static int gruposDe(const Resolvido *r, Ref *out, int max, uint64_t *grupos, int maxG, int *nG) {
  char n1[128], n2[128];
  int n = 0;
  *nG = 0;
  iptvvod_normalizar(r->titulo, n1, sizeof n1);
  iptvvod_normalizar(r->original, n2, sizeof n2);
  pthread_mutex_lock(&trava);
  if (bibValidaLocked()) for (int g = 0; g < bib.ng && *nG < maxG; g++) {
    int dica = 0, confere = 0;
    if (bib.g[g].kind != r->kind) continue;
    for (int j = bib.g[g].primeira; j >= 0; j = bib.c[j].proximo) {
      if (bib.c[j].tmdb == r->tmdb) dica = 1;
      if (copiaConfere(&bib.c[j], n1, n2, r->ano)) confere = 1;
    }
    // A hint must agree with the title; without a hint, title AND year must.
    if (dica && !confere) { printf("[iptvvod] dica TMDB do provedor recusada (titulo diferente)\n"); continue; }
    if (!confere) continue;
    if (!dica && !r->ano) continue;
    grupos[(*nG)++] = bib.g[g].chave;
    for (int j = bib.g[g].primeira; j >= 0 && n < max; j = bib.c[j].proximo) {
      if (!copiaConfere(&bib.c[j], n1, n2, r->ano) && bib.c[j].tmdb != r->tmdb) continue;
      refDe(&out[n++], &bib.c[j]);
    }
  }
  pthread_mutex_unlock(&trava);
  return n;
}
// Resolutions are reused for the session (one TMDB call per title opened).
#define RESOLV_CACHE 64
static struct { char id[24]; char kind; Resolvido r; int ok; } resolvCache[RESOLV_CACHE];
static int nResolv;
static pthread_mutex_t travaResolv = PTHREAD_MUTEX_INITIALIZER;
int iptvvod_origem(const char *id, const char *tipo, int (*cancelado)(void *), void *ctx,
                   void *aviso, void *avisoU, void *saida) {
  Stream **lista = saida;
  char imdb[16], chaveId[24];
  long tmdb;
  int t, e, ok = 0, i, nr, nG, n = 0;
  char kind;
  Resolvido r;
  Ref refs[IPTVVOD_CONTAS_MAX * 2];
  uint64_t grupos[4];
  Stream *l;
  (void)aviso; (void)avisoU;
  if (lista) *lista = NULL;
  // A library title searched with the add-ons (iptvvod_id_addons): its own
  // copies, exactly, without matching anything.
  if (iptvvod_e_id(id)) {
    int nl = 0;
    if (!iptvvod_ativo() || !lista) return 0;
    if (cancelado && cancelado(ctx)) return -1;
    fontesDoAlvo(id, lista, &nl);
    printf("[iptvvod] %d fonte(s) IPTV do proprio titulo da biblioteca\n", nl);
    fflush(stdout);
    return nl;
  }
  if (!iptvvod_ativo() || !idCatalogo(id, imdb, sizeof imdb, &tmdb, &t, &e)) return 0;
  kind = (tipo && (!strcmp(tipo, "series") || !strcmp(tipo, "tv"))) || e > 0 ? 's' : 'm';
  if (imdb[0]) snprintf(chaveId, sizeof chaveId, "%s", imdb); else snprintf(chaveId, sizeof chaveId, "tmdb:%ld", tmdb);
  pthread_mutex_lock(&travaResolv);
  for (i = 0; i < nResolv; i++) if (resolvCache[i].kind == kind && !strcmp(resolvCache[i].id, chaveId)) {
    r = resolvCache[i].r; ok = resolvCache[i].ok; break;
  }
  pthread_mutex_unlock(&travaResolv);
  if (i == nResolv) {
    if (cancelado && cancelado(ctx)) return -1;
    ok = resolver(imdb, tmdb, kind, &r);
    // -1 = no key / no answer: not cached (a key or the network may come
    // back); the catalogue-title fallback below still runs.
    if (ok < 0) ok = 0;
    else {
    pthread_mutex_lock(&travaResolv);
    i = nResolv < RESOLV_CACHE ? nResolv++ : (int)((unsigned long)fnv(chaveId, 1469598103934665603ULL) % RESOLV_CACHE);
    snprintf(resolvCache[i].id, sizeof resolvCache[i].id, "%s", chaveId);
    resolvCache[i].kind = kind; resolvCache[i].r = r; resolvCache[i].ok = ok;
    pthread_mutex_unlock(&travaResolv);
    }
  }
  if (ok <= 0) {
    // No TMDB answer (no key, offline, or not found): the catalogue's own
    // title and year. The same gate as always decides: the copy's title must
    // agree, and with a year, within one year (gruposDe / copiaConfere).
    char base[24];
    memset(&r, 0, sizeof r);
    r.kind = kind;
    r.tmdb = imdb[0] ? 0 : tmdb;
    snprintf(base, sizeof base, "%s", imdb[0] ? imdb : chaveId);
    if (!tituloFn || !tituloFn(base, kind == 's' ? "series" : "movie", r.titulo, sizeof r.titulo, &r.ano) || !r.titulo[0]) return 0;
    printf("[iptvvod] sem TMDB: casando pelo titulo do catalogo (ano %d)\n", r.ano);
  }
  nr = gruposDe(&r, refs, IPTVVOD_CONTAS_MAX * 2, grupos, 4, &nG);
  if (!nr) return 0;
  if (cancelado && cancelado(ctx)) return -1;
  l = calloc((size_t)nr, sizeof *l);
  if (!l) return -1;
  if (kind == 'm') {
    for (i = 0; i < nr; i++) if (montarDaRef(&l[n], &refs[i])) n++;
  } else {
    // Episodes: each matching group's providers, by season/episode.
    for (int g = 0; g < nG && n < nr; g++) {
      Ref rg[IPTVVOD_CONTAS_MAX];
      char gid[64];
      int ng;
      iptvvod_id_montar(gid, sizeof gid, grupos[g], 's', 0, 0);
      ng = copiasDo(gid, 's', rg, IPTVVOD_CONTAS_MAX, NULL);
      if (!ng || serieMesclar(rg, ng, grupos[g], NULL, NULL, 0) < 0) continue;
      {
        EpFonte fs[IPTVVOD_CONTAS_MAX];
        int nf = episodioCopias(grupos[g], t, e, fs, IPTVVOD_CONTAS_MAX);
        for (int k = 0; k < nf && n < nr; k++) {
          char nome[160] = "";
          for (int j = 0; j < ng; j++) if (rg[j].conta == fs[k].conta) snprintf(nome, sizeof nome, "%s", rg[j].nome);
          if (montarDoEp(&l[n], &fs[k], nome)) n++;
        }
      }
    }
  }
  if (!n) { free(l); return 0; }
  printf("[iptvvod] %d fonte(s) IPTV para um titulo de addon\n", n);
  if (lista) *lista = l; else free(l);
  return n;
}
// Active with a TMDB key (ids resolved exactly) or with the catalogue title
// fallback (QR TIME: no key needed, as in Nuvio One).
int iptvvod_origem_ativa(void) { return iptvvod_ativo() && ((chaveTmdbFn && chaveTmdbFn() && chaveTmdbFn()[0]) || tituloFn); }

// ------------------------------------------------------------- verified ids
// A library item gets real IMDb/TMDB ids only after TMDB confirms them:
//   - with a panel TMDB hint: /movie|/tv/<hint> must answer with a title that
//     agrees with the copies' (copiaConfere: same title, or containment with
//     the year within one);
//   - without one: /search by the clean title and the year must return
//     EXACTLY ONE result whose normalized title and year agree.
// Only a title and a year (or a TMDB id the panel published) go to TMDB,
// never a provider id. Used for posters (posterprov.c) through a hook that
// answers from memory; items are verified in the background, on demand.
// Device file "iptvvod-tmdb.tsv": "<16 hex>\t<kind>\t<1 ok|2 none>\t<tmdb>\t<imdb>".
typedef struct { uint64_t grupo; char kind, estado; long tmdb; char imdb[16]; } Verif;
enum { VER_PEND = 0, VER_OK = 1, VER_NADA = 2, VER_ERRO = 3 };
static Verif *verifs;
static int nVerifs, capVerifs, verifLido, verifFio;
static pthread_mutex_t travaVerif = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sinalVerif = PTHREAD_COND_INITIALIZER;

static int verifAchar(uint64_t g, char kind) {
  int lo = 0, hi = nVerifs - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (verifs[m].grupo == g && verifs[m].kind == kind) return m;
    if (verifs[m].grupo < g || (verifs[m].grupo == g && verifs[m].kind < kind)) lo = m + 1; else hi = m - 1;
  }
  return -(lo + 1);
}
static int verifPor(uint64_t g, char kind) {
  int i = verifAchar(g, kind), pos;
  if (i >= 0) return i;
  pos = -i - 1;
  if (nVerifs == capVerifs) {
    int nova = capVerifs ? capVerifs * 2 : 256;
    Verif *t = realloc(verifs, (size_t)nova * sizeof *t);
    if (!t) return -1;
    verifs = t; capVerifs = nova;
  }
  memmove(verifs + pos + 1, verifs + pos, sizeof *verifs * (size_t)(nVerifs - pos));
  memset(&verifs[pos], 0, sizeof verifs[pos]);
  verifs[pos].grupo = g; verifs[pos].kind = kind; verifs[pos].estado = VER_PEND;
  nVerifs++;
  return pos;
}
static void verifLerLocked(void) {
  char *t, *p;
  if (verifLido) return;
  verifLido = 1;
  t = dados_ler("iptvvod-tmdb.tsv");
  for (p = t; p && *p;) {
    char *fim = strchr(p, '\n');
    unsigned long long g;
    char k, imdb[16] = "";
    int e;
    long tm;
    if (fim) *fim = 0;
    if (sscanf(p, "%16llx\t%c\t%d\t%ld\t%15s", &g, &k, &e, &tm, imdb) >= 4 && (e == VER_OK || e == VER_NADA)) {
      int i = verifPor((uint64_t)g, k);
      if (i >= 0) { verifs[i].estado = (char)e; verifs[i].tmdb = tm; snprintf(verifs[i].imdb, sizeof verifs[i].imdb, "%s", strcmp(imdb, "-") ? imdb : ""); }
    }
    p = fim ? fim + 1 : NULL;
  }
  free(t);
}
static void verifGravarLocked(void) {
  size_t cap = (size_t)nVerifs * 64 + 1, k = 0;
  char *t = malloc(cap);
  if (!t) return;
  t[0] = 0;
  for (int i = 0; i < nVerifs; i++)
    if (verifs[i].estado == VER_OK || verifs[i].estado == VER_NADA)
      k += (size_t)snprintf(t + k, cap - k, "%016llx\t%c\t%d\t%ld\t%s\n", (unsigned long long)verifs[i].grupo,
                            verifs[i].kind, verifs[i].estado, verifs[i].tmdb, verifs[i].imdb[0] ? verifs[i].imdb : "-");
  dados_gravar("iptvvod-tmdb.tsv", t);
  free(t);
}

// The copies of one group, for verification (under `trava`).
typedef struct { char norm[8][128]; int ano[8]; long dica[8]; int n; char titulo[160]; int anoTitulo; } Amostra;
static int amostraDe(uint64_t g, char kind, Amostra *a) {
  int ok = 0;
  memset(a, 0, sizeof *a);
  pthread_mutex_lock(&trava);
  for (int i = 0; bibValidaLocked() && i < bib.ng; i++) if (bib.g[i].chave == g && bib.g[i].kind == kind) {
    for (int j = bib.g[i].primeira; j >= 0 && a->n < 8; j = bib.c[j].proximo) {
      snprintf(a->norm[a->n], sizeof a->norm[0], "%s", bib.c[j].norm);
      a->ano[a->n] = bib.c[j].ano; a->dica[a->n] = bib.c[j].tmdb;
      if (!a->titulo[0] || (bib.c[j].ano && !a->anoTitulo)) {
        iptvvod_titulo_limpo(bib.c[j].nome, a->titulo, sizeof a->titulo);
        a->anoTitulo = bib.c[j].ano;
      }
      a->n++;
    }
    ok = a->n > 0;
    break;
  }
  pthread_mutex_unlock(&trava);
  return ok;
}
static int amostraConfere(const Amostra *a, const char *titulo, const char *original, int ano) {
  char n1[128], n2[128];
  iptvvod_normalizar(titulo, n1, sizeof n1);
  iptvvod_normalizar(original, n2, sizeof n2);
  for (int i = 0; i < a->n; i++) {
    Copia c;
    memset(&c, 0, sizeof c);
    snprintf(c.norm, sizeof c.norm, "%s", a->norm[i]);
    c.ano = a->ano[i];
    if (copiaConfere(&c, n1, n2, ano)) return 1;
  }
  return 0;
}
static void urlenc(const char *s, char *d, size_t n) {
  size_t k = 0;
  for (; *s && k + 4 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') d[k++] = (char)c;
    else k += (size_t)snprintf(d + k, n - k, "%%%02X", c);
  }
  d[k] = 0;
}
// Details of a real TMDB id: titles, year, IMDb id. 1 ok, 0 not found, -1 no answer.
static int tmdbDetalhes(char kind, long id, const char *chave, char *tit, char *orig, int *ano, char *imdb) {
  char url[400], data[16] = "";
  char *c;
  snprintf(url, sizeof url, "%s/%s/%ld?api_key=%s&append_to_response=external_ids", tmdbBase, kind == 's' ? "tv" : "movie", id, chave);
  c = rede_baixar_com(url, 15, NULL);
  if (!c) return -1;
  tit[0] = orig[0] = imdb[0] = 0;
  js_texto_raiz(c, kind == 's' ? "name" : "title", tit, 128);
  js_texto_raiz(c, kind == 's' ? "original_name" : "original_title", orig, 128);
  js_texto_raiz(c, kind == 's' ? "first_air_date" : "release_date", data, sizeof data);
  js_texto(c, NULL, "imdb_id", imdb, 16);
  if (strncmp(imdb, "tt", 2)) imdb[0] = 0;
  *ano = anoDeData(data);
  free(c);
  return tit[0] || orig[0];
}
static int verificar(uint64_t g, char kind, Verif *res) {
  const char *chave = chaveTmdbFn ? chaveTmdbFn() : NULL;
  Amostra a;
  char tit[128], orig[128], imdb[16];
  int ano = 0, r;
  memset(res, 0, sizeof *res);
  if (!chave || !chave[0] || !amostraDe(g, kind, &a)) return VER_ERRO;
  // 1. A panel hint, confirmed by TMDB's own title.
  for (int i = 0; i < a.n; i++) if (a.dica[i] > 0) {
    r = tmdbDetalhes(kind, a.dica[i], chave, tit, orig, &ano, imdb);
    if (r < 0) return VER_ERRO;
    if (r > 0 && amostraConfere(&a, tit, orig, ano)) {
      res->tmdb = a.dica[i];
      snprintf(res->imdb, sizeof res->imdb, "%s", imdb);
      return VER_OK;
    }
    break;   // a wrong hint is not retried with the other copies' copies of it
  }
  // 2. Search by title and year: exactly one agreeing result.
  if (!a.anoTitulo || !a.titulo[0]) return VER_NADA;
  {
    char url[600], q[400], *c;
    const char *p;
    long achado = 0;
    int casam = 0;
    urlenc(a.titulo, q, sizeof q);
    snprintf(url, sizeof url, "%s/search/%s?api_key=%s&query=%s&%s=%d", tmdbBase, kind == 's' ? "tv" : "movie",
             chave, q, kind == 's' ? "first_air_date_year" : "year", a.anoTitulo);
    c = rede_baixar_com(url, 15, NULL);
    if (!c) return VER_ERRO;
    p = js_array(c, NULL, "results");
    while (p && *p == '{') {
      const char *f = js_fim(p);
      char t1[128] = "", t2[128] = "", d[16] = "";
      if (!f) break;
      js_texto_raiz_em(p, f, kind == 's' ? "name" : "title", t1, sizeof t1);
      js_texto_raiz_em(p, f, kind == 's' ? "original_name" : "original_title", t2, sizeof t2);
      js_texto_raiz_em(p, f, kind == 's' ? "first_air_date" : "release_date", d, sizeof d);
      if (amostraConfere(&a, t1, t2, anoDeData(d))) {
        char n1[128];
        iptvvod_normalizar(t1, n1, sizeof n1);
        // Containment is fine for a hint, not for a search: exact title only.
        int exato = 0;
        for (int i = 0; i < a.n; i++) if (!strcmp(a.norm[i], n1)) exato = 1;
        if (!exato) { iptvvod_normalizar(t2, n1, sizeof n1); for (int i = 0; i < a.n; i++) if (!strcmp(a.norm[i], n1)) exato = 1; }
        if (exato) { casam++; achado = (long)js_num(p, f, "id", 0.0); }
      }
      p = js_prox(f);
    }
    free(c);
    if (casam != 1 || achado <= 0) return VER_NADA;
    r = tmdbDetalhes(kind, achado, chave, tit, orig, &ano, imdb);
    if (r < 0) return VER_ERRO;
    res->tmdb = achado;
    snprintf(res->imdb, sizeof res->imdb, "%s", imdb);
    return VER_OK;
  }
}
static void *fioVerif(void *u) {
  (void)u;
  for (;;) {
    uint64_t g = 0;
    char kind = 0;
    Verif v;
    int e, i;
    pthread_mutex_lock(&travaVerif);
    for (;;) {
      for (i = 0; i < nVerifs && verifs[i].estado != VER_PEND; i++) {}
      if (i < nVerifs) break;
      pthread_cond_wait(&sinalVerif, &travaVerif);
    }
    g = verifs[i].grupo; kind = verifs[i].kind;
    pthread_mutex_unlock(&travaVerif);
    e = verificar(g, kind, &v);
    pthread_mutex_lock(&travaVerif);
    i = verifAchar(g, kind);
    if (i >= 0) {
      verifs[i].estado = (char)e;
      verifs[i].tmdb = v.tmdb;
      snprintf(verifs[i].imdb, sizeof verifs[i].imdb, "%s", v.imdb);
      if (e == VER_OK || e == VER_NADA) verifGravarLocked();
    }
    pthread_mutex_unlock(&travaVerif);
    if (e != VER_ERRO) { printf("[iptvvod] tmdb: titulo %s\n", e == VER_OK ? "confirmado" : "sem correspondencia unica"); fflush(stdout); }
    usleep(e == VER_ERRO ? 1000000 : 250000);   // gentle with TMDB
  }
  return NULL;
}
int iptvvod_poster_ids(const char *id, const char *tipo, char *imdb, size_t n, long *tmdb) {
  uint64_t g;
  char k;
  int i, r = 0;
  (void)tipo;
  if (!iptvvod_id_ler(id, &g, &k, NULL, NULL) || k == 'e') return 0;
  if (!chaveTmdbFn || !chaveTmdbFn() || !chaveTmdbFn()[0]) return 0;
  pthread_mutex_lock(&travaVerif);
  verifLerLocked();
  i = verifAchar(g, k);
  if (i >= 0 && verifs[i].estado == VER_OK && verifs[i].imdb[0]) {
    snprintf(imdb, n, "%s", verifs[i].imdb);
    if (tmdb) *tmdb = verifs[i].tmdb;
    r = 1;
  } else if (i < 0 && verifPor(g, k) >= 0) {
    if (!verifFio) {
      pthread_t t;
      if (pthread_create(&t, NULL, fioVerif, NULL) == 0) { pthread_detach(t); verifFio = 1; }
    }
    pthread_cond_signal(&sinalVerif);
  }
  pthread_mutex_unlock(&travaVerif);
  return r;
}

// ------------------------------------------------- add-on id of a library title
// QR TIME: the same film opened from the IPTV library also gets the add-ons'
// sources (Nuvio One shows both together). The add-ons need the title's real
// IMDb id; the provider's own ids (qv:, stream ids) never leave the app. In
// order: the TMDB-confirmed id (iptvvod_poster_ids' verification), a home
// catalogue item with the same title and year, then Cinemeta's search by the
// TITLE (a public name, not a provider id) with the same gate as the copies:
// equal normalised title and, when both are known, the year within one.
// Runs on a search worker (may use the network). 1 = out holds "tt..[:S:E]".
static int (*catImdbFn)(const char *norm, int ano, char kind, char *imdb, size_t n);
void iptvvod_definir_imdb_catalogo(int (*fn)(const char *norm, int ano, char kind, char *imdb, size_t n)) { catImdbFn = fn; }
static char cineBase[160] = "https://v3-cinemeta.strem.io";
void iptvvod_teste_cinemeta_base(const char *base) { snprintf(cineBase, sizeof cineBase, "%s", base ? base : ""); }
#define IDADD_MAX 64
static struct { uint64_t g; char kind; char imdb[16]; } idAdd[IDADD_MAX];
static int nIdAdd;
static pthread_mutex_t travaIdAdd = PTHREAD_MUTEX_INITIALIZER;

static int ttValido(const char *s) {
  size_t i;
  if (!s || s[0] != 't' || s[1] != 't') return 0;
  for (i = 2; isdigit((unsigned char)s[i]); i++) {}
  return i >= 4 && i <= 12 && !s[i];
}
// Cinemeta's search: {"metas":[{"imdb_id":"tt..","name":"..","releaseInfo":"1955"},...]}.
int iptvvod_cinemeta_escolher(const char *json, const char *norm, int ano, char *imdb, size_t n) {
  const char *p;
  int achados = 0;
  char unico[16] = "";
  if (!json || !norm || !norm[0]) return 0;
  for (p = js_array(json, NULL, "metas"); p && *p == '{'; ) {
    const char *f = js_fim(p);
    char nome[200] = "", nn[128], id[24] = "", rel[24] = "";
    int a;
    if (!f) break;
    js_texto_raiz_em(p, f, "name", nome, sizeof nome);
    if (!js_texto_raiz_em(p, f, "imdb_id", id, sizeof id) || !id[0]) js_texto_raiz_em(p, f, "id", id, sizeof id);
    js_texto_raiz_em(p, f, "releaseInfo", rel, sizeof rel);
    a = atoi(rel);
    iptvvod_normalizar(nome, nn, sizeof nn);
    if (ttValido(id) && !strcmp(nn, norm)) {
      if (ano > 0 && a > 0) {
        if (a - ano <= 1 && ano - a <= 1) { snprintf(imdb, n, "%s", id); return 1; }
      } else if (!achados++) snprintf(unico, sizeof unico, "%s", id);
    }
    p = js_prox(f);
  }
  // No year to compare: only an unambiguous name.
  if (achados == 1) { snprintf(imdb, n, "%s", unico); return 1; }
  return 0;
}
static int cinemetaBuscar(const char *nome, const char *norm, int ano, char kind, char *imdb, size_t n) {
  char url[512], q[300];
  size_t k = 0;
  char *resp;
  int r;
  if (!cineBase[0]) return 0;
  for (const char *s = nome; *s && k + 4 < sizeof q; s++) {
    unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.') q[k++] = (char)c;
    else k += (size_t)snprintf(q + k, sizeof q - k, "%%%02X", c);
  }
  q[k] = 0;
  snprintf(url, sizeof url, "%s/catalog/%s/top/search=%s.json", cineBase, kind == 's' ? "series" : "movie", q);
  resp = rede_baixar(url, 8);
  r = iptvvod_cinemeta_escolher(resp, norm, ano, imdb, n);
  free(resp);
  return r;
}
int iptvvod_id_addons(const char *id, char *out, size_t tam) {
  uint64_t g;
  char k, kind, imdb[16] = "", nome[160] = "", norm[128] = "", gid[64];
  int t = 0, e = 0, ano = 0, i, achou = 0, conhecido = 0;
  if (!iptvvod_id_ler(id, &g, &k, &t, &e)) return 0;
  kind = k == 'e' ? 's' : k;
  pthread_mutex_lock(&travaIdAdd);
  for (i = 0; i < nIdAdd; i++) if (idAdd[i].g == g && idAdd[i].kind == kind) { snprintf(imdb, sizeof imdb, "%s", idAdd[i].imdb); conhecido = 1; break; }
  pthread_mutex_unlock(&travaIdAdd);
  if (!conhecido) {
    pthread_mutex_lock(&travaVerif);
    verifLerLocked();
    i = verifAchar(g, kind);
    if (i >= 0 && verifs[i].estado == VER_OK && ttValido(verifs[i].imdb)) { snprintf(imdb, sizeof imdb, "%s", verifs[i].imdb); achou = 1; }
    pthread_mutex_unlock(&travaVerif);
    if (!achou) {
      int gi;
      char kk;
      iptvvod_id_montar(gid, sizeof gid, g, kind, 0, 0);
      pthread_mutex_lock(&trava);
      if (bibValidaLocked() && (gi = grupoPorId(&bib, gid, &kk)) >= 0 && bib.g[gi].primeira >= 0) {
        const Copia *c = &bib.c[bib.g[gi].primeira];
        iptvvod_titulo_limpo(c->nome ? c->nome : "", nome, sizeof nome);
        snprintf(norm, sizeof norm, "%s", c->norm);
        ano = c->ano;
        for (int j = bib.g[gi].primeira; !ano && j >= 0; j = bib.c[j].proximo) ano = bib.c[j].ano;
      }
      pthread_mutex_unlock(&trava);
      if (norm[0]) achou = (catImdbFn && catImdbFn(norm, ano, kind, imdb, sizeof imdb) && ttValido(imdb))
                        || cinemetaBuscar(nome, norm, ano, kind, imdb, sizeof imdb);
      if (!achou) imdb[0] = 0;
    }
    if (imdb[0]) {   // a miss is asked again next time (network may be back)
    pthread_mutex_lock(&travaIdAdd);
    i = nIdAdd < IDADD_MAX ? nIdAdd++ : (int)(g % IDADD_MAX);
    idAdd[i].g = g; idAdd[i].kind = kind;
    snprintf(idAdd[i].imdb, sizeof idAdd[i].imdb, "%s", imdb);
    pthread_mutex_unlock(&travaIdAdd);
    }
    printf("[iptvvod] titulo da biblioteca %s id IMDb para os addons\n", imdb[0] ? "com" : "sem");
    fflush(stdout);
  }
  if (!imdb[0]) return 0;
  if (k == 'e') snprintf(out, tam, "%s:%d:%d", imdb, t, e);
  else snprintf(out, tam, "%s", imdb);
  return 1;
}

int iptvvod_teste_esperar(int ms) {
  for (int i = 0; i < ms / 20; i++) {
    if (!atomic_load(&carregando)) return 1;
    usleep(20000);
  }
  return !atomic_load(&carregando);
}
