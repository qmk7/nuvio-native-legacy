#include "traktid.h"
#include "traktscrobble.h"
#include "metaep.h"
#include <stdio.h>
#include <string.h>

// O QUE O TRAKT ACEITA NUM PAUSE (respostas 422 dos registros 10162-10172):
// "Progress should be at least 1.0% to pause" e, a partir de 80%, "Use stop to
// scrobble". A partir de 80% vai o stop de conclusao (SCR_STOP_PCT), como no
// NuvioTV oficial.
static int pausaAceita(double pct) { return pct >= 1.0 && pct < 80.0; }

int scrobble_alvo_congelar(TraktScrobbleAlvo *out, int *pronto, const char *id,
                           int listaFinal, int parFinalExiste, int tvdbEp) {
  if (!out || !pronto) return 0;
  if (*pronto) return out->id[0] != 0;
  // A numeric pair is safe only while it exists in the final topology. An
  // exact TVDB id captured from the provisional snapshot is already stable.
  if ((!listaFinal || !parFinalExiste) && tvdbEp <= 0) return 0;
  if (!id || !*id) return 0;
  memset(out, 0, sizeof *out);
  snprintf(out->id, sizeof out->id, "%s", id);
  out->tvdbEp = tvdbEp > 0 ? tvdbEp : 0;
  *pronto = 1;
  return 1;
}

int scrobble_decidir(ScrobbleEstado *s, int evento, const char *id, double pct) {
  int mesmo;
  if (!id || !*id) return SCR_NADA;
  mesmo = !strcmp(s->id, id);
  if (evento == SCR_EV_TOCANDO) {
    if (s->ativo && mesmo) return SCR_NADA;
    snprintf(s->id, sizeof s->id, "%s", id);
    s->ativo = 1;
    return SCR_START;
  }
  if (evento == SCR_EV_PONTO) {
    // Refresh progress without pausing remote Now Watching.
    return s->ativo && mesmo && pausaAceita(pct) ? SCR_START : SCR_NADA;
  }
  if (evento == SCR_EV_PAUSOU) {
    if (!s->ativo || !mesmo) return SCR_NADA;
    s->ativo = 0;
    return pausaAceita(pct) ? SCR_PAUSE : SCR_NADA;
  }
  if (evento == SCR_EV_SAIU) {
    snprintf(s->id, sizeof s->id, "%s", id);
    s->ativo = 0;
    if (pct >= SCR_STOP_PCT) return SCR_STOP;
    return pausaAceita(pct) ? SCR_PAUSE : SCR_NADA;
  }
  return SCR_NADA;
}

int scrobble_concluiu(double posSeg, double durSeg, int fimDeLegacy) {
  if (fimDeLegacy) return 1;
  return durSeg > 1.0 && posSeg >= durSeg * SCR_VISTO_PCT / 100.0;
}

void scrobble_ids_guardar(ScrobbleEpIds *out, const char *tvdb,
                          const char *tmdb, const char *trakt) {
  if (!out) return;
  memset(out->proprio, 0, sizeof out->proprio);
  if (tvdb && tvdb[0]) snprintf(out->proprio[0], sizeof out->proprio[0], "%s", tvdb);
  if (tmdb && tmdb[0]) snprintf(out->proprio[1], sizeof out->proprio[1], "%s", tmdb);
  if (trakt && trakt[0]) snprintf(out->proprio[2], sizeof out->proprio[2], "%s", trakt);
  out->tvdb = out->proprio[0][0] ? out->proprio[0] : NULL;
  out->tmdb = out->proprio[1][0] ? out->proprio[1] : NULL;
  out->trakt = out->proprio[2][0] ? out->proprio[2] : NULL;
}

void scrobble_ids_limpar(ScrobbleEpIds *out) {
  if (!out) return;
  memset(out, 0, sizeof *out);
}

void scrobble_corpo_ep(char *dst, size_t n, const char *id, double pct,
                       const ScrobbleEpIds *ep) {
  TraktId t;
  char ids[64];
  if (n) dst[0] = 0;
  if (pct < 0.0) pct = 0.0;
  if (pct > 100.0) pct = 100.0;
  // Only a Trakt-compatible identity makes a body; a local id makes none.
  if (traktid_resolver(id, &t) < TID_IMDB || !traktid_ids_json(&t, ids, sizeof ids)) return;
  if (t.temporada >= 0 && t.episodio > 0) {
    if (ep && ((ep->tvdb && ep->tvdb[0]) || (ep->trakt && ep->trakt[0]))) {
      // These ids identify the episode itself (Trakt's episode contract: TVDB/Trakt).
      char eids[160] = "";
      size_t k = 0;
      if (ep->tvdb && ep->tvdb[0]) k += (size_t)snprintf(eids + k, sizeof eids - k, "\"tvdb\":%s", ep->tvdb);
      if (ep->trakt && ep->trakt[0] && k < sizeof eids) snprintf(eids + k, sizeof eids - k, "%s\"trakt\":%s", k ? "," : "", ep->trakt);
      snprintf(dst, n, "{\"episode\":{\"ids\":{%s}},\"progress\":%.2f}", eids, pct);
      return;
    }
    snprintf(dst, n, "{\"show\":{\"ids\":{%s}},\"episode\":{\"season\":%d,\"number\":%d},\"progress\":%.2f}",
             ids, t.temporada, t.episodio, pct);
    return;
  }
  snprintf(dst, n, "{\"movie\":{\"ids\":{%s}},\"progress\":%.2f}", ids, pct);
}

void scrobble_corpo(char *dst, size_t n, const char *id, double pct) {
  scrobble_corpo_ep(dst, n, id, pct, NULL);
}

void scrobble_corpo_alvo(char *dst, size_t n, const TraktScrobbleAlvo *alvo, double pct) {
  if (!alvo) { if (n) dst[0] = 0; return; }
  if (alvo->tvdbEp <= 0) { scrobble_corpo(dst, n, alvo->id, pct); return; }
  if (pct < 0.0) pct = 0.0;
  if (pct > 100.0) pct = 100.0;
  snprintf(dst, n, "{\"episode\":{\"ids\":{\"tvdb\":%d}},\"progress\":%.2f}",
           alvo->tvdbEp, pct);
}

const char *scrobble_nome(int acao) {
  return acao == SCR_START ? "start" : acao == SCR_PAUSE ? "pause"
       : acao == SCR_STOP ? "stop" : "";
}
