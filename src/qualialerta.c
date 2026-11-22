// QR TIME "A BETTER VERSION ARRIVED": films on the profile's list are checked
// against the installed add-ons in the background, ONE film every 4 minutes
// while the app is open, each at most once a day. When the best source
// becomes 4K (from below) or gains Dolby Vision, a notice says so with
// "Open". The first check of a film only records what exists (no notice).
// Kept per profile in qualidade-p<N>.txt ("<imdb>\t<height>\t<dv>\t<checked>").
// Settings > Reprodução > "Avisar quando sair versão melhor" (on by default).
#include "qualialerta.h"
#include "addons.h"
#include "ajustes.h"
#include "app.h"
#include "catalogo.h"
#include "dados.h"
#include "idioma.h"
#include "ilha.h"
#include "perfis.h"
#include "streams.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static int fio, pronto, resAltura, resDv, resOk;
static char resImdb[24], resTitulo[160];

int qualialerta_melhora(int altAntes, int dvAntes, int alt, int dv) {
  if (alt >= 2160 && altAntes < 2160) return 1;
  if (dv && !dvAntes) return 1;
  return 0;
}
static int ler(const char *imdb, int *alt, int *dv, long long *quando) {
  char n[40], *t, *p;
  size_t l = strlen(imdb);
  int r = 0;
  snprintf(n, sizeof n, "qualidade-p%d.txt", perfis_ativo());
  if (!(t = dados_ler(n))) return 0;
  for (p = t; p && *p; ) {
    if (!strncmp(p, imdb, l) && p[l] == '\t') { r = sscanf(p + l + 1, "%d\t%d\t%lld", alt, dv, quando) == 3; break; }
    p = strchr(p, '\n'); if (p) p++;
  }
  free(t);
  return r;
}
static void gravar(const char *imdb, int alt, int dv) {
  char n[40], *t, *novo, *p;
  size_t l = strlen(imdb), cap;
  snprintf(n, sizeof n, "qualidade-p%d.txt", perfis_ativo());
  t = dados_ler(n);
  cap = (t ? strlen(t) : 0) + 96;
  if (!(novo = calloc(1, cap))) { free(t); return; }
  for (p = t; p && *p; ) {
    char *nl = strchr(p, '\n');
    size_t ll = nl ? (size_t)(nl - p) : strlen(p);
    if (!(ll > l && !strncmp(p, imdb, l) && p[l] == '\t')) { strncat(novo, p, ll); strcat(novo, "\n"); }
    p = nl ? nl + 1 : NULL;
  }
  snprintf(novo + strlen(novo), cap - strlen(novo), "%s\t%d\t%d\t%lld\n", imdb, alt, dv, (long long)time(NULL));
  dados_gravar(n, novo);
  free(novo); free(t);
}

typedef struct { AddonsConsulta *c; char imdb[24], titulo[160]; } Pedido;
static void *fioChecar(void *arg) {
  Pedido *p = arg;
  Stream *lista = NULL;
  int n = addons_consulta_consultar(p->c, p->imdb, "movie", 2, NULL, NULL, &lista), alt = 0, dv = 0;
  for (int i = 0; i < n; i++) { if (lista[i].altura > alt) alt = lista[i].altura; if (lista[i].dolbyVision) dv = 1; }
  free(lista);
  addons_consulta_liberar(p->c);
  pthread_mutex_lock(&trava);
  snprintf(resImdb, sizeof resImdb, "%s", p->imdb); snprintf(resTitulo, sizeof resTitulo, "%s", p->titulo);
  resAltura = alt; resDv = dv; resOk = n >= 0; pronto = 1; fio = 0;
  pthread_mutex_unlock(&trava);
  free(p);
  return NULL;
}

void qualialerta_passo(Uint32 agora) {
  static Uint32 ult;
  static int cursor;
  int pr;
  pthread_mutex_lock(&trava);
  pr = pronto; pronto = 0;
  pthread_mutex_unlock(&trava);
  if (pr && resOk && resAltura > 0) {
    int a0 = 0, d0 = 0; long long q0 = 0;
    int tinha = ler(resImdb, &a0, &d0, &q0);
    if (tinha && qualialerta_melhora(a0, d0, resAltura, resDv)) {
      IlhaModal m;
      IlhaAvisoEx av;
      char chave[48], txt[260], f[40];
      memset(&m, 0, sizeof m); memset(&av, 0, sizeof av);
      snprintf(f, sizeof f, "%s%s%s", resAltura >= 2160 ? "4K" : "", resAltura >= 2160 && resDv ? " " : "", resDv ? "Dolby Vision" : "");
      snprintf(m.kicker, sizeof m.kicker, "%s", i18n("Versão melhor disponível"));
      snprintf(m.titulo, sizeof m.titulo, "%s", resTitulo);
      snprintf(m.linha, sizeof m.linha, i18n("Agora em %s"), f);
      snprintf(m.icone, sizeof m.icone, "aj_sparkles");
      m.nBotoes = 2;
      snprintf(m.botao[0], sizeof m.botao[0], "%s", i18n("Abrir"));
      snprintf(m.botao[1], sizeof m.botao[1], "%s", i18n("Fechar"));
      snprintf(chave, sizeof chave, "quali:%s", resImdb);
      snprintf(txt, sizeof txt, "%s: %s", resTitulo, m.linha);
      av.chave = chave; av.tipo = ILHA_ACENTO; av.icone = "aj_sparkles"; av.texto = txt; av.ms = 12000u; av.modal = &m; av.tecla = 1;
      ilha_avisar_ex(&av);
      printf("[qualidade] versao melhor: %s (%dp%s)\n", resImdb, resAltura, resDv ? " DV" : "");
    }
    gravar(resImdb, resAltura > a0 ? resAltura : a0, resDv || d0);
    fflush(stdout);
  }
  if (!ajustes_quali_alerta() || agora - ult < 240000u) return;
  ult = agora;
  pthread_mutex_lock(&trava);
  if (fio) { pthread_mutex_unlock(&trava); return; }
  pthread_mutex_unlock(&trava);
  {
    int n = cat_n();
    for (int k = 0; k < n; k++) {
      const CatItem *c = cat_item((cursor + k) % n);
      int a0, d0; long long q0;
      if (!c || !c->naLista || strcmp(c->tipo, "movie") || !c->imdb[0] || strncmp(c->imdb, "tt", 2) || cat_visto(c)) continue;
      if (ler(c->imdb, &a0, &d0, &q0) && time(NULL) - q0 < 86400) continue;
      cursor = (cursor + k + 1) % n;
      {
        Pedido *p = calloc(1, sizeof *p);
        pthread_t t;
        if (!p || !(p->c = addons_consulta_capturar())) { free(p); return; }
        snprintf(p->imdb, sizeof p->imdb, "%s", c->imdb); snprintf(p->titulo, sizeof p->titulo, "%s", c->titulo);
        fio = 1;
        if (pthread_create(&t, NULL, fioChecar, p)) { addons_consulta_liberar(p->c); free(p); fio = 0; return; }
        pthread_detach(t);
      }
      return;
    }
  }
}
void qualialerta_acao(const char *chave, int botao) {
  if (botao == 1 && !strncmp(chave, "quali:", 6)) app_abrir_titulo(chave + 6);
}
