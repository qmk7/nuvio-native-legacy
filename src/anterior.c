// QR TIME "PREVIOUSLY ON": a series episode starts and the person has not
// watched this series for 4 days or more (the local log, semana.h): a notice
// "What happened before?" whose card (BLUE / OK) shows the synopses of the two
// previous episodes, from the episode list the app already has (the add-on /
// Cinemeta metadata; no extra request). Once per episode per session; no
// notice when those synopses are empty.
#include "anterior.h"
#include "idioma.h"
#include "ilha.h"
#include "player.h"
#include "semana.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DIAS_LONGE 4

int anterior_achar(const CatEp *const *eps, int n, int t, int e, const CatEp *out[2]) {
  int k = 0;
  out[0] = out[1] = NULL;
  for (int i = 0; i < n; i++) {
    const CatEp *x = eps[i];
    if (!x || x->temporada <= 0 || x->episodio <= 0) continue;
    if (x->temporada > t || (x->temporada == t && x->episodio >= e)) continue;   // only before
    // Keep the two latest: out[0] = the one right before.
    if (!out[0] || x->temporada > out[0]->temporada || (x->temporada == out[0]->temporada && x->episodio > out[0]->episodio)) {
      out[1] = out[0]; out[0] = x;
    } else if (!out[1] || x->temporada > out[1]->temporada || (x->temporada == out[1]->temporada && x->episodio > out[1]->episodio)) {
      out[1] = x;
    }
  }
  if (out[0]) k++;
  if (out[1]) k++;
  return k;
}

void anterior_passo(Uint32 agora) {
  static char feito[64];
  static Uint32 desde;
  char chave[64];
  int t = 0, e = 0, idx, n;
  const CatEp *ant[2], *lista[400];
  time_t ult;
  if (!player_aberto() || (idx = player_serie_episodio(&t, &e)) < 0) { desde = 0; return; }
  snprintf(chave, sizeof chave, "%s:%d:%d", player_serie_imdb(), t, e);
  if (!strcmp(chave, feito)) return;
  if (!desde) desde = agora ? agora : 1;
  if (agora - desde < 4000u) return;              // the picture first
  if (agora - desde > 30000u) { snprintf(feito, sizeof feito, "%s", chave); return; }   // episodes never came
  ult = semana_ultimo(player_serie_imdb());
  if (!ult || time(NULL) - ult < DIAS_LONGE * 86400L) { snprintf(feito, sizeof feito, "%s", chave); return; }
  n = cat_n_episodios(idx);
  if (n <= 0) return;                             // still loading: try again next frame
  if (n > 400) n = 400;
  for (int i = 0; i < n; i++) lista[i] = cat_episodio(idx, i);
  snprintf(feito, sizeof feito, "%s", chave);
  if (!anterior_achar(lista, n, t, e, ant) || !ant[0]->sinopse[0]) return;
  {
    IlhaModal m;
    IlhaAvisoEx a;
    memset(&m, 0, sizeof m); memset(&a, 0, sizeof a);
    snprintf(m.kicker, sizeof m.kicker, "%s", i18n("Antes deste episódio"));
    snprintf(m.titulo, sizeof m.titulo, "T%dE%d  %s", ant[0]->temporada, ant[0]->episodio, ant[0]->nome);
    snprintf(m.texto, sizeof m.texto, "%s", ant[0]->sinopse);
    if (ant[1] && ant[1]->sinopse[0])
      snprintf(m.fala, sizeof m.fala, "T%dE%d  %s: %s", ant[1]->temporada, ant[1]->episodio, ant[1]->nome, ant[1]->sinopse);
    snprintf(m.icone, sizeof m.icone, "aj_rotate-ccw-clock");
    m.nBotoes = 1;
    snprintf(m.botao[0], sizeof m.botao[0], "%s", i18n("Fechar"));
    a.chave = "anterior"; a.tipo = ILHA_INFO; a.icone = "aj_rotate-ccw-clock";
    a.texto = i18n("O que aconteceu antes?"); a.ms = 12000u; a.modal = &m; a.tecla = 1;
    ilha_avisar_ex(&a);
    printf("[anterior] resumo das 2 anteriores oferecido (%s)\n", chave);
    fflush(stdout);
  }
}
