// QR TIME "WHAT SHOULD I WATCH TONIGHT?": the Control Center's "Sugestão"
// tile. One title from what the app already has on the Home (no new
// requests): titles on the profile's list weigh most, then ones started and
// not finished, then well-rated ones; watched titles and channels never come.
// From 19:00 films come first ("cinema tonight"). The card: Open, Another
// suggestion, Close. The same title is not suggested twice in a session.
#include "noite.h"
#include "app.h"
#include "catalogo.h"
#include "idioma.h"
#include "ilha.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VISTOS 32
static char sugeridos[VISTOS][64];
static int nSug;
static char atual[64];

int noite_peso(const CatItem *c, int filmePrimeiro) {
  int p;
  if (!c || !c->imdb[0] || (strcmp(c->tipo, "movie") && strcmp(c->tipo, "series"))) return 0;
  if (cat_visto(c)) return 0;
  for (int i = 0; i < nSug; i++) if (!strcmp(sugeridos[i], c->imdb)) return 0;
  p = c->naLista ? 6 : c->progresso > 0 && c->progresso < 90 ? 4 : c->nota >= 70 ? 2 : 1;
  if (filmePrimeiro && !strcmp(c->tipo, "movie")) p *= 2;
  return p;
}
int noite_escolher(unsigned semente, int filmePrimeiro) {
  long total = 0;
  int n = cat_n(), esc = -1;
  for (int i = 0; i < n; i++) {
    const CatItem *c = cat_item(i);
    int p = noite_peso(c, filmePrimeiro), dup = 0;
    for (int j = 0; j < i && p; j++) { const CatItem *o = cat_item(j); if (o && !strcmp(o->imdb, c->imdb)) dup = 1; }   // same title in two rows
    if (!p || dup) continue;
    total += p;
    // Reservoir by weight: item i replaces the choice with probability p/total.
    semente = semente * 1103515245u + 12345u;
    if ((long)((semente >> 8) % (unsigned)total) < p) esc = i;
  }
  return esc;
}
void noite_sugerir(void) {
  time_t t = time(NULL);
  struct tm lt;
  int i, filme;
  localtime_r(&t, &lt);
  filme = lt.tm_hour >= 19 || lt.tm_hour < 3;
  i = noite_escolher((unsigned)t ^ (unsigned)nSug * 2654435761u, filme);
  if (i < 0 && nSug) { nSug = 0; i = noite_escolher((unsigned)t, filme); }   // all suggested already: start over
  if (i < 0) { ilha_avisar("noite", ILHA_INFO, "aj_dices", i18n("Nada para sugerir: a Home ainda não tem filmes ou séries."), 3500u, 0); return; }
  {
    const CatItem *c = cat_item(i);
    IlhaModal m;
    IlhaAvisoEx a;
    char chave[80], txt[240];
    memset(&m, 0, sizeof m); memset(&a, 0, sizeof a);
    snprintf(atual, sizeof atual, "%s", c->imdb);
    snprintf(sugeridos[nSug++ % VISTOS], sizeof sugeridos[0], "%s", c->imdb);
    if (nSug > VISTOS) nSug = VISTOS;
    snprintf(m.kicker, sizeof m.kicker, "%s", i18n(filme ? "Para esta noite" : "Para hoje"));
    snprintf(m.titulo, sizeof m.titulo, "%s", c->titulo);
    snprintf(m.linha, sizeof m.linha, "%s", c->meta);
    snprintf(m.nota, sizeof m.nota, "%s", i18n(c->naLista ? "Está na sua lista" : c->progresso > 0 ? "Você começou e não terminou" : "Bem avaliado"));
    snprintf(m.texto, sizeof m.texto, "%s", c->sinopse);
    snprintf(m.arte, sizeof m.arte, "%s", c->backdrop[0] ? c->backdrop : c->poster);
    m.nBotoes = 3;
    snprintf(m.botao[0], sizeof m.botao[0], "%s", i18n("Abrir"));
    snprintf(m.botao[1], sizeof m.botao[1], "%s", i18n("Outra sugestão"));
    snprintf(m.botao[2], sizeof m.botao[2], "%s", i18n("Fechar"));
    snprintf(chave, sizeof chave, "noite:%s", c->imdb);
    snprintf(txt, sizeof txt, "%s: %s", i18n(filme ? "Para esta noite" : "Para hoje"), c->titulo);
    a.chave = chave; a.tipo = ILHA_ACENTO; a.icone = "aj_dices"; a.texto = txt; a.ms = 20000u;
    a.modal = &m; a.abrir = 1;
    ilha_avisar_ex(&a);
    printf("[noite] sugestao: %s (%s)\n", c->imdb, c->tipo);
    fflush(stdout);
  }
}
void noite_acao(const char *chave, int botao) {
  if (strncmp(chave, "noite:", 6)) return;
  if (botao == 1) app_abrir_titulo(chave + 6);
  else if (botao == 2) noite_sugerir();
}
void noite_teste_esquecer(void) { nSug = 0; }
