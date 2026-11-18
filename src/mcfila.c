#include "mcfila.h"
#include "dados.h"
#include "perfis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------------------------------------------------------------- lines
static void campo(char *d, size_t n, size_t *k, const char *s) {
  for (; s && *s && *k + 2 < n; s++) d[(*k)++] = (*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s;
  if (*k + 1 < n) d[(*k)++] = '\t';
  d[*k] = 0;
}
int mcfila_linha(const McFaixa *f, char *d, size_t n) {
  size_t k = 0;
  char num[160];
  d[0] = 0;
  campo(d, n, &k, f->id); campo(d, n, &k, f->titulo); campo(d, n, &k, f->artista); campo(d, n, &k, f->album);
  campo(d, n, &k, f->albumId); campo(d, n, &k, f->artistaId); campo(d, n, &k, f->capa); campo(d, n, &k, f->genero);
  campo(d, n, &k, f->formato); campo(d, n, &k, f->mime);
  snprintf(num, sizeof num, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%d", f->faixa, f->disco, f->ano, f->dur, f->kbps, f->taxa, f->bits, f->canais, f->tam, f->fav);
  if (k + strlen(num) + 1 >= n) return 0;
  memcpy(d + k, num, strlen(num) + 1);
  return 1;
}
int mcfila_ler_linha(const char *s, McFaixa *f) {
  char buf[2400], *c[20];
  int k = 0;
  snprintf(buf, sizeof buf, "%s", s);
  for (char *p = buf; k < 20; ) { c[k++] = p; p = strchr(p, '\t'); if (!p) break; *p++ = 0; }
  if (k < 20) return 0;
  memset(f, 0, sizeof *f);
  snprintf(f->id, sizeof f->id, "%s", c[0]); snprintf(f->titulo, sizeof f->titulo, "%s", c[1]);
  snprintf(f->artista, sizeof f->artista, "%s", c[2]); snprintf(f->album, sizeof f->album, "%s", c[3]);
  snprintf(f->albumId, sizeof f->albumId, "%s", c[4]); snprintf(f->artistaId, sizeof f->artistaId, "%s", c[5]);
  snprintf(f->capa, sizeof f->capa, "%s", c[6]); snprintf(f->genero, sizeof f->genero, "%s", c[7]);
  snprintf(f->formato, sizeof f->formato, "%s", c[8]); snprintf(f->mime, sizeof f->mime, "%s", c[9]);
  f->faixa = atoi(c[10]); f->disco = atoi(c[11]); f->ano = atoi(c[12]); f->dur = atoi(c[13]); f->kbps = atoi(c[14]);
  f->taxa = atoi(c[15]); f->bits = atoi(c[16]); f->canais = atoi(c[17]); f->tam = atoll(c[18]); f->fav = atoi(c[19]);
  return f->id[0] != 0;
}
static void nomeArq(char *d, size_t n, const char *base) { int p = perfis_ativo(); snprintf(d, n, "musica-%s-p%d.txt", base, p > 0 ? p : 1); }

// A growable list of tracks, written as one file.
typedef struct { McFaixa *v; int n, cap; } Lista;
static int listaPor(Lista *l, const McFaixa *f, int pos) {
  if (l->n == l->cap) {
    int nc = l->cap ? l->cap * 2 : 32;
    McFaixa *t = realloc(l->v, sizeof *t * (size_t)nc);
    if (!t) return 0;
    l->v = t; l->cap = nc;
  }
  if (pos < 0 || pos > l->n) pos = l->n;
  memmove(&l->v[pos + 1], &l->v[pos], sizeof *l->v * (size_t)(l->n - pos));
  l->v[pos] = *f;
  l->n++;
  return 1;
}
static void listaTirar(Lista *l, int i) {
  if (i < 0 || i >= l->n) return;
  memmove(&l->v[i], &l->v[i + 1], sizeof *l->v * (size_t)(l->n - i - 1));
  l->n--;
}
static char *texto(const Lista *l, const char *cabeca) {
  size_t cap = 256 + (size_t)l->n * 1400, k;
  char *t = malloc(cap);
  if (!t) return NULL;
  k = (size_t)snprintf(t, cap, "%s", cabeca ? cabeca : "");
  for (int i = 0; i < l->n; i++) {
    if (!mcfila_linha(&l->v[i], t + k, cap - k - 1)) continue;
    k += strlen(t + k);
    t[k++] = '\n';
    t[k] = 0;
  }
  return t;
}

// ---------------------------------------------------------------- queue
static Lista fila;
static int *ordem, capOrdem;   // play order -> index in `fila`
static int pos = -1, aleat, repete;
static double posSeg;

static int garantirOrdem(void) {
  if (capOrdem >= fila.cap) return 1;
  int *t = realloc(ordem, sizeof *t * (size_t)(fila.cap ? fila.cap : 32));
  if (!t) return 0;
  ordem = t; capOrdem = fila.cap ? fila.cap : 32;
  return 1;
}
static void ordemNatural(void) { garantirOrdem(); for (int i = 0; i < fila.n; i++) ordem[i] = i; }
static unsigned semente;
static unsigned aleatorio(void) { semente = semente * 1103515245u + 12345u; return semente >> 8; }
// Shuffle every slot after `primeiro` (which stays as it is).
static void embaralhar(int primeiro) {
  if (!semente) semente = (unsigned)time(NULL) | 1u;
  for (int i = fila.n - 1; i > primeiro + 1; i--) {
    int j = primeiro + 1 + (int)(aleatorio() % (unsigned)(i - primeiro));
    int t = ordem[i]; ordem[i] = ordem[j]; ordem[j] = t;
  }
}
void mcq_limpar(void) { fila.n = 0; pos = -1; posSeg = 0; }
void mcq_definir(const McFaixa *f, int n, int inicio) {
  mcq_limpar();
  for (int i = 0; i < n && i < MCQ_MAX; i++) listaPor(&fila, &f[i], -1);
  ordemNatural();
  pos = fila.n ? (inicio >= 0 && inicio < fila.n ? inicio : 0) : -1;
  if (aleat && pos >= 0) {   // the chosen track plays first, the rest shuffled
    int t = ordem[0]; ordem[0] = ordem[pos]; ordem[pos] = t;
    pos = 0;
    embaralhar(0);
  }
}
int mcq_n(void) { return fila.n; }
int mcq_atual(void) { return pos; }
const McFaixa *mcq_item(int i) { return i >= 0 && i < fila.n ? &fila.v[ordem[i]] : NULL; }
const McFaixa *mcq_faixa_atual(void) { return mcq_item(pos); }
static int inserir(const McFaixa *f, int lugar) {   // lugar in play order
  int idx;
  if (fila.n >= MCQ_MAX) return 0;
  idx = fila.n;
  if (!listaPor(&fila, f, -1) || !garantirOrdem()) return 0;
  if (lugar < 0 || lugar > fila.n - 1) lugar = fila.n - 1;
  memmove(&ordem[lugar + 1], &ordem[lugar], sizeof *ordem * (size_t)(fila.n - 1 - lugar));
  ordem[lugar] = idx;
  if (pos < 0) pos = 0;
  return 1;
}
int mcq_adicionar(const McFaixa *f) { return inserir(f, fila.n); }
int mcq_tocar_a_seguir(const McFaixa *f) { return inserir(f, pos < 0 ? 0 : pos + 1); }
int mcq_remover(int i) {
  int idx;
  if (i < 0 || i >= fila.n || i == pos) return 0;
  idx = ordem[i];
  listaTirar(&fila, idx);
  memmove(&ordem[i], &ordem[i + 1], sizeof *ordem * (size_t)(fila.n - i));
  for (int k = 0; k < fila.n; k++) if (ordem[k] > idx) ordem[k]--;
  if (i < pos) pos--;
  return 1;
}
int mcq_mover(int de, int para) {
  int v;
  if (de < 0 || de >= fila.n || para < 0 || para >= fila.n || de == para) return 0;
  v = ordem[de];
  if (de < para) memmove(&ordem[de], &ordem[de + 1], sizeof *ordem * (size_t)(para - de));
  else memmove(&ordem[para + 1], &ordem[para], sizeof *ordem * (size_t)(de - para));
  ordem[para] = v;
  if (pos == de) pos = para;
  else if (de < pos && para >= pos) pos--;
  else if (de > pos && para <= pos) pos++;
  return 1;
}
int mcq_ir(int i) { if (i < 0 || i >= fila.n) return -1; pos = i; posSeg = 0; return pos; }
int mcq_proxima(int automatico) {
  if (pos < 0 || !fila.n) return -1;
  if (automatico && repete == MCR_UMA) { posSeg = 0; return pos; }
  if (pos + 1 < fila.n) { pos++; posSeg = 0; return pos; }
  if (repete == MCR_NAO && automatico) return -1;   // the end of the queue
  if (repete == MCR_NAO) return -1;
  if (aleat) embaralhar(-1);   // a new round, a new order
  pos = 0; posSeg = 0;
  return pos;
}
int mcq_anterior(void) {
  if (pos < 0) return -1;
  if (pos > 0) pos--;
  else if (repete == MCR_TODAS) pos = fila.n - 1;
  posSeg = 0;
  return pos;
}
void mcq_aleatorio(int liga) {
  int atual;
  if (liga && !aleat && fila.n > 1) {
    // Keep the order the person built (moves included) as the order to come
    // back to: store the tracks in play order before shuffling.
    McFaixa *t = malloc(sizeof *t * (size_t)fila.n);
    if (t) {
      for (int i = 0; i < fila.n; i++) t[i] = fila.v[ordem[i]];
      memcpy(fila.v, t, sizeof *t * (size_t)fila.n);
      free(t);
      ordemNatural();
    }
  }
  atual = pos >= 0 ? ordem[pos] : -1;
  aleat = liga != 0;
  ordemNatural();
  if (aleat && atual >= 0) {
    ordem[atual] = 0; ordem[0] = atual;   // natural order, then swap: current first
    pos = 0;
    embaralhar(0);
  } else if (atual >= 0) pos = atual;
}
int mcq_eh_aleatorio(void) { return aleat; }
void mcq_repetir(int modo) { repete = modo < 0 || modo > 2 ? 0 : modo; }
int mcq_repeticao(void) { return repete; }
void mcq_posicao(double seg) { posSeg = seg < 0 ? 0 : seg; }
double mcq_posicao_salva(void) { return posSeg; }
int mcq_salvar(void) {
  char nome[48], cab[160];
  char *t;
  int ok;
  Lista emOrdem = { NULL, 0, 0 };
  nomeArq(nome, sizeof nome, "fila");
  if (!fila.n) { dados_apagar(nome); return 1; }
  // Written in play order (the shuffle survives a restart as it was).
  for (int i = 0; i < fila.n; i++) listaPor(&emOrdem, &fila.v[ordem[i]], -1);
  snprintf(cab, sizeof cab, "Q\t%d\t%d\t%d\t%.1f\n", pos, aleat, repete, posSeg);
  t = texto(&emOrdem, cab);
  free(emOrdem.v);
  ok = t && dados_gravar(nome, t);
  free(t);
  return ok;
}
int mcq_carregar(void) {
  char nome[48], *t, *lin, *sp;
  int p = -1, a = 0, r = 0;
  double s = 0;
  nomeArq(nome, sizeof nome, "fila");
  mcq_limpar();
  aleat = 0; repete = 0;
  t = dados_ler(nome);
  if (!t) return 0;
  for (lin = strtok_r(t, "\n", &sp); lin; lin = strtok_r(NULL, "\n", &sp)) {
    McFaixa f;
    if (lin[0] == 'Q' && lin[1] == '\t') { sscanf(lin + 2, "%d\t%d\t%d\t%lf", &p, &a, &r, &s); continue; }
    if (mcfila_ler_linha(lin, &f) && fila.n < MCQ_MAX) listaPor(&fila, &f, -1);
  }
  free(t);
  ordemNatural();
  pos = fila.n ? (p >= 0 && p < fila.n ? p : 0) : -1;
  aleat = a; repete = r >= 0 && r <= 2 ? r : 0; posSeg = s > 0 ? s : 0;
  return fila.n > 0;
}

// ---------------------------------------------------------------- history / favourites
static Lista hist, favs;
static int histLido = -1, favLido = -1;
static void carregarLista(Lista *l, const char *base) {
  char nome[48], *t, *lin, *sp;
  nomeArq(nome, sizeof nome, base);
  l->n = 0;
  t = dados_ler(nome);
  for (lin = t ? strtok_r(t, "\n", &sp) : NULL; lin; lin = strtok_r(NULL, "\n", &sp)) {
    McFaixa f;
    if (mcfila_ler_linha(lin, &f)) listaPor(l, &f, -1);
  }
  free(t);
}
static void gravarLista(const Lista *l, const char *base) {
  char nome[48];
  char *t = texto(l, NULL);
  nomeArq(nome, sizeof nome, base);
  if (t) { if (l->n) dados_gravar(nome, t); else dados_apagar(nome); }
  free(t);
}
static void garantirHist(void) { if (histLido != perfis_ativo()) { carregarLista(&hist, "historico"); histLido = perfis_ativo(); } }
static void garantirFav(void) { if (favLido != perfis_ativo()) { carregarLista(&favs, "favoritos"); favLido = perfis_ativo(); } }
void mch_tocou(const McFaixa *f) {
  garantirHist();
  for (int i = 0; i < hist.n; i++) if (!strcmp(hist.v[i].id, f->id)) { listaTirar(&hist, i); break; }
  listaPor(&hist, f, 0);
  while (hist.n > MCH_MAX) hist.n--;
  gravarLista(&hist, "historico");
}
int mch_n(void) { garantirHist(); return hist.n; }
const McFaixa *mch_item(int i) { garantirHist(); return i >= 0 && i < hist.n ? &hist.v[i] : NULL; }
int mcv_fav(const char *id) { garantirFav(); for (int i = 0; i < favs.n; i++) if (!strcmp(favs.v[i].id, id)) return 1; return 0; }
int mcv_alternar(const McFaixa *f) {
  garantirFav();
  for (int i = 0; i < favs.n; i++) if (!strcmp(favs.v[i].id, f->id)) { listaTirar(&favs, i); gravarLista(&favs, "favoritos"); return 0; }
  listaPor(&favs, f, 0);
  favs.v[0].fav = 1;
  gravarLista(&favs, "favoritos");
  return 1;
}
int mcv_n(void) { garantirFav(); return favs.n; }
const McFaixa *mcv_item(int i) { garantirFav(); return i >= 0 && i < favs.n ? &favs.v[i] : NULL; }

// ---------------------------------------------------------------- local playlists
typedef struct { char nome[96]; Lista l; } ListaNomeada;
static ListaNomeada listas[MCL_MAX];
static int nListas, listasLido = -1;
static void carregarListas(void) {
  char nome[48], *t, *lin, *sp;
  for (int i = 0; i < nListas; i++) listas[i].l.n = 0;
  nListas = 0;
  nomeArq(nome, sizeof nome, "listas");
  t = dados_ler(nome);
  for (lin = t ? strtok_r(t, "\n", &sp) : NULL; lin; lin = strtok_r(NULL, "\n", &sp)) {
    McFaixa f;
    if (lin[0] == 'L' && lin[1] == '\t') {
      if (nListas == MCL_MAX) break;
      snprintf(listas[nListas].nome, sizeof listas[0].nome, "%s", lin + 2);
      listas[nListas].l.n = 0;
      nListas++;
    } else if (nListas && mcfila_ler_linha(lin, &f)) listaPor(&listas[nListas - 1].l, &f, -1);
  }
  free(t);
  listasLido = perfis_ativo();
}
static void garantirListas(void) { if (listasLido != perfis_ativo()) carregarListas(); }
static void gravarListas(void) {
  char nome[48];
  size_t cap = 256, k = 0;
  char *t;
  for (int i = 0; i < nListas; i++) cap += 120 + (size_t)listas[i].l.n * 1400;
  t = malloc(cap);
  if (!t) return;
  t[0] = 0;
  for (int i = 0; i < nListas; i++) {
    char *c = texto(&listas[i].l, NULL);
    k += (size_t)snprintf(t + k, cap - k, "L\t%s\n%s", listas[i].nome, c ? c : "");
    free(c);
  }
  nomeArq(nome, sizeof nome, "listas");
  if (nListas) dados_gravar(nome, t); else dados_apagar(nome);
  free(t);
}
static int nomeOk(const char *s) {
  if (!s || !s[0] || strlen(s) >= sizeof listas[0].nome) return 0;
  for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') return 0;
  return 1;
}
int mcl_n(void) { garantirListas(); return nListas; }
const char *mcl_nome(int i) { garantirListas(); return i >= 0 && i < nListas ? listas[i].nome : ""; }
int mcl_faixas(int i, const McFaixa **out) {
  garantirListas();
  if (i < 0 || i >= nListas) { if (out) *out = NULL; return 0; }
  if (out) *out = listas[i].l.v;
  return listas[i].l.n;
}
int mcl_criar(const char *nome) {
  garantirListas();
  if (!nomeOk(nome) || nListas == MCL_MAX) return -1;
  snprintf(listas[nListas].nome, sizeof listas[0].nome, "%s", nome);
  listas[nListas].l.n = 0;
  nListas++;
  gravarListas();
  return nListas - 1;
}
int mcl_renomear(int i, const char *nome) {
  garantirListas();
  if (i < 0 || i >= nListas || !nomeOk(nome)) return 0;
  snprintf(listas[i].nome, sizeof listas[0].nome, "%s", nome);
  gravarListas();
  return 1;
}
int mcl_apagar(int i) {
  Lista l;
  garantirListas();
  if (i < 0 || i >= nListas) return 0;
  l = listas[i].l;
  memmove(&listas[i], &listas[i + 1], sizeof listas[0] * (size_t)(nListas - i - 1));
  nListas--;
  listas[nListas].l = l;   // keep the buffer for reuse
  listas[nListas].l.n = 0;
  gravarListas();
  return 1;
}
int mcl_adicionar(int i, const McFaixa *f) {
  garantirListas();
  if (i < 0 || i >= nListas || listas[i].l.n >= MCQ_MAX) return 0;
  listaPor(&listas[i].l, f, -1);
  gravarListas();
  return 1;
}
int mcl_remover(int i, int faixa) {
  garantirListas();
  if (i < 0 || i >= nListas || faixa < 0 || faixa >= listas[i].l.n) return 0;
  listaTirar(&listas[i].l, faixa);
  gravarListas();
  return 1;
}
int mcl_mover(int i, int de, int para) {
  Lista *l;
  McFaixa t;
  garantirListas();
  if (i < 0 || i >= nListas) return 0;
  l = &listas[i].l;
  if (de < 0 || de >= l->n || para < 0 || para >= l->n || de == para) return 0;
  t = l->v[de];
  listaTirar(l, de);
  listaPor(l, &t, para);
  gravarListas();
  return 1;
}
void mcfila_perfil_trocou(void) {
  histLido = favLido = listasLido = -1;
  mcq_carregar();
}
