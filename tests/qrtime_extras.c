// QR TIME: tonight's suggestion (noite.c) and the morning greeting's weather
// parser (bomdia.c, an Open-Meteo answer as documented; no network here).
//   bash tests/qrtime_extras.sh
#include "noite.h"
#include "bomdia.h"
#include "catalogo.h"
#include "dados.h"
#include "semana.h"
#include "velmem.h"
#include "perfis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static void item(const char *imdb, const char *tipo, int naLista, int prog, int nota) {
  CatItem *c = calloc(1, sizeof *c);
  snprintf(c->imdb, sizeof c->imdb, "%s", imdb); snprintf(c->tipo, sizeof c->tipo, "%s", tipo);
  snprintf(c->titulo, sizeof c->titulo, "T %s", imdb);
  c->naLista = naLista; c->progresso = prog; c->nota = nota;
  cat_acrescentar(c);
  free(c);
}
int main(void) {
  BdClima c;
  dados_iniciar(getenv("NUVIO_DADOS"));
  CONFERE(bomdia_parse("{\"latitude\":24.75,\"current_units\":{\"temperature_2m\":\"°C\"},\"current\":{\"time\":\"2026-11-22T08:00\",\"interval\":900,"
                       "\"temperature_2m\":21.4,\"weather_code\":2},\"daily\":{\"time\":[\"2026-11-22\"],\"temperature_2m_max\":[29.8],\"temperature_2m_min\":[16.1]}}", &c)
          && c.temp > 21.3 && c.temp < 21.5 && c.codigo == 2 && c.max > 29.7 && c.min < 16.2, "Open-Meteo answer parsed");
  CONFERE(!bomdia_parse("{\"error\":true,\"reason\":\"Daily API request limit exceeded.\"}", &c), "an error answer: no weather line");
  CONFERE(!strcmp(bomdia_tempo(0), "Céu limpo") && !strcmp(bomdia_tempo(63), "Chuva") && !strcmp(bomdia_tempo(95), "Tempestade"), "weather codes");
  item("tt0000001", "movie", 0, 0, 50);
  item("tt0000002", "series", 1, 0, 0);
  item("tt0000003", "channel", 1, 0, 90);
  item("tt0000004", "movie", 0, 40, 0);
  {
    CatItem t; memset(&t, 0, sizeof t);
    snprintf(t.imdb, sizeof t.imdb, "tt9"); snprintf(t.tipo, sizeof t.tipo, "channel");
    CONFERE(noite_peso(&t, 0) == 0, "channels are never suggested");
    snprintf(t.tipo, sizeof t.tipo, "series"); t.naLista = 1;
    CONFERE(noite_peso(&t, 0) == 6 && noite_peso(&t, 1) == 6, "on the list: weighs most (not doubled for series at night)");
    snprintf(t.tipo, sizeof t.tipo, "movie"); t.naLista = 0; t.progresso = 40;
    CONFERE(noite_peso(&t, 0) == 4 && noite_peso(&t, 1) == 8, "started and not finished; films double in the evening");
  }
  {
    int vistos[8] = { 0 }, chan = 0;
    for (unsigned s = 1; s < 400; s++) {
      int i = noite_escolher(s * 7919u, 1);
      const CatItem *x = i >= 0 ? cat_item(i) : NULL;
      if (x && !strcmp(x->tipo, "channel")) chan++;
      if (x) vistos[x->imdb[8] - '0']++;
    }
    CONFERE(!chan && vistos[1] && vistos[2] && vistos[4], "picks vary over the candidates, never a channel");
    CONFERE(vistos[4] > vistos[1], "weights hold: an unfinished film beats a plain one");
  }
  {
    time_t t = 1795305600 + 12 * 3600;   // 22/11/2026
    SemResumo r;
    perfis_definir_ativo(1);
    semana_registrar("tt1", "movie", "Film\tOne", "Filme · Drama", 5400, t - 2 * 86400);
    semana_registrar("tt2", "series", "Show", "Programa de TV · Comédia · Drama", 1800, t - 86400);
    semana_registrar("tt2", "series", "Show", "Programa de TV · Comédia · Drama", 2400, t);
    semana_registrar("tt3", "movie", "Old", "Filme · Ação", 7200, t - 20 * 86400);
    semana_registrar("tt4", "channel", "News", "", 3600, t);
    semana_registrar("tt5", "movie", "Tiny", "Filme · Ação", 30, t);
    CONFERE(semana_resumo(t, &r) && (int)r.segundos == 9600 && r.filmes == 1 && r.series == 1, "last 7 days: films, series, time (channels and <1 min left out)");
    CONFERE(!strcmp(r.topo, "Film One") && !strcmp(r.genero, "Drama"), "most watched title (tabs cleaned) and genre");
    perfis_definir_ativo(2);
    CONFERE(!semana_resumo(t, &r), "another profile: its own log");
    perfis_definir_ativo(1);
  }
  velmem_gravar("tt0903747", 125); velmem_gravar("tt0944947", 150);
  CONFERE(velmem_ler("tt0903747") == 125 && velmem_ler("tt0944947") == 150 && !velmem_ler("tt1"), "speed per series");
  velmem_gravar("tt0903747", 100);
  CONFERE(!velmem_ler("tt0903747") && velmem_ler("tt0944947") == 150, "back to 1x: forgotten, the other series kept");
  perfis_definir_ativo(2);
  CONFERE(!velmem_ler("tt0944947"), "per profile");
  perfis_definir_ativo(1);
  return falhas ? 1 : 0;
}
