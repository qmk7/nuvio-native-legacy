// Sport Center data and store (src/espn.c, src/esporte.c): parsing, time,
// window, cache and provider failure over HTTP against the local fixture,
// follows, channel links and EPG suggestions.
//
//   qrtime_esporte <data dir> <base url>        (fixture running)
//   qrtime_esporte <data dir> <base url> offline (fixture down: cache)
#include "espn.h"
#include "esporte.h"
#include "tsdb.h"
#include "dados.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int perfil = 1;
uint64_t perfis_sessao_geracao(void) { return (uint64_t)perfil; }
int perfis_sessao_atual(uint64_t s) { return s == (uint64_t)perfil; }
int perfis_sessao_travar(uint64_t s) { return s == (uint64_t)perfil; }
void perfis_sessao_destravar(void) {}
int perfis_ativo(void) { return perfil; }

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

typedef struct { EspEvento v[32]; int n; } Lista;
static int juntar(const EspEvento *e, void *u) { Lista *l = u; if (l->n < 32) l->v[l->n++] = *e; return 1; }
static const EspEvento *achar(const Lista *l, const char *id) { for (int i = 0; i < l->n; i++) if (!strcmp(l->v[i].id, id)) return &l->v[i]; return NULL; }

// Guide stand-in for suggestions.
static int progs(const char *canal, time_t de, time_t ate, char (*t)[128], time_t *ini, time_t *fim, int max, void *u) {
  time_t k = *(time_t *)u;
  (void)de; (void)ate; (void)max;
  if (!strcmp(canal, "xtream:@00000001:301")) { snprintf(t[0], 128, "Roshn Saudi League: Al-Hilal vs Al-Nassr"); ini[0] = k - 600; fim[0] = k + 7200; return 1; }
  if (!strcmp(canal, "xtream:@00000001:302")) { snprintf(t[0], 128, "Al Hilal Magazine"); ini[0] = k - 300; fim[0] = k + 3600; return 1; }
  if (!strcmp(canal, "xtream:@00000001:303")) { snprintf(t[0], 128, "Live Football"); ini[0] = k - 300; fim[0] = k + 3600; return 1; }
  if (!strcmp(canal, "xtream:@00000001:304")) { snprintf(t[0], 128, "Al Hilal vs Al Nassr"); ini[0] = k + 86400; fim[0] = k + 90000; return 1; }
  if (!strcmp(canal, "xtream:@00000001:306")) { snprintf(t[0], 128, "الهلال × النصر"); ini[0] = k - 600; fim[0] = k + 7200; return 1; }
  return 0;
}

int ajustes_idioma(void) { return 0; }   // test stub: esportenomes.c asks the UI language
int main(int argc, char **argv) {
  char base[200], j[40];
  time_t quando = 0, agora = time(NULL);
  int falhou = 0, n;
  if (argc < 3) { fprintf(stderr, "usage: %s <data dir> <base url> [offline]\n", argv[0]); return 2; }
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(NULL);
  snprintf(base, sizeof base, "%s/espn", argv[2]);
  espn_definir_base(base);

  if (argc > 3) {
    Lista l = {0};
    n = espn_buscar("soccer/ksa.1", agora, juntar, &l, &quando, &falhou);
    CONFERE(n == 4 && falhou && quando > 0 && quando <= agora, "provider down: the cached scoreboard, with its time");
    n = espn_buscar("soccer/eng.1", agora, NULL, NULL, &quando, &falhou);
    CONFERE(n == 0 && falhou, "provider down, cached empty league: empty, not an error");
    return falhas ? 1 : 0;
  }

  // ---- time ----
  CONFERE(espn_instante("2026-11-17T20:00Z") == (time_t)1794945600, "ISO minute in UTC");
  CONFERE(espn_instante("2026-11-17T20:00:30Z") == (time_t)1794945630, "ISO with seconds");
  CONFERE(espn_instante("2026-11-17T23:00+03:00") == (time_t)1794945600, "explicit +03:00 offset");
  CONFERE(espn_instante("tomorrow") == 0 && espn_instante("2026-11-17T20:00X") == 0, "invalid dates refused");
  espn_janela((time_t)1794945600, j, sizeof j);
  CONFERE(!strcmp(j, "20261110-20261124"), "window: a week before to a week after");
  {
    char m[2][8];
    CONFERE(espn_meses((time_t)1794945600, m) == 1 && !strcmp(m[0], "202611"), "ESPN query by month (it refuses day ranges): one month");
    CONFERE(espn_meses((time_t)1796587200, m) == 2 && !strcmp(m[0], "202611") && !strcmp(m[1], "202612"), "window across two months: both");
  }
  {
    // ESPN 2026-11: each competitor has "type":"team" BEFORE "team":{...}. The
    // value "team" must not be taken for the key (the empty Sport Center bug).
    static const char J[] = "{\"events\":[{\"id\":\"1\",\"date\":\"2026-11-22T16:30Z\",\"status\":{\"type\":{\"state\":\"pre\"}},"
      "\"competitions\":[{\"id\":\"1\",\"competitors\":["
      "{\"id\":\"364\",\"type\":\"team\",\"homeAway\":\"home\",\"score\":\"0\",\"team\":{\"id\":\"364\",\"displayName\":\"Liverpool\"}},"
      "{\"id\":\"359\",\"type\":\"team\",\"homeAway\":\"away\",\"score\":\"0\",\"team\":{\"id\":\"359\",\"displayName\":\"Arsenal\"}}]}]}]}";
    Lista l = {0};
    CONFERE(espn_parse(J, "soccer/eng.1", juntar, &l) == 1 && l.n == 1 && !strcmp(l.v[0].casa.nome, "Liverpool") && !strcmp(l.v[0].fora.nome, "Arsenal"),
            "competitor with \"type\":\"team\" before the team object: names read");
  }
  CONFERE(espn_parse("<html>", "soccer/ksa.1", NULL, NULL) < 0 && espn_parse("{\"x\":1}", "soccer/ksa.1", NULL, NULL) < 0,
          "a body that is not a scoreboard is a failure");

  // ---- network ----
  {
    Lista l = {0};
    const EspEvento *e;
    n = espn_buscar("soccer/ksa.1", agora, juntar, &l, &quando, &falhou);
    CONFERE(n == 4 && !falhou && quando == agora, "Saudi Pro League: 4 events, fresh");
    e = achar(&l, "700001");
    CONFERE(e && e->estado == ESP_AO_VIVO && !strcmp(e->relogio, "67'") && !strcmp(e->casa.nome, "Al Hilal") &&
            !strcmp(e->casa.placar, "2") && !strcmp(e->fora.placar, "1") && !strcmp(e->tv, "SSC") &&
            !strcmp(e->local, "Kingdom Arena"), "live match: state, clock, score, broadcaster, venue");
    CONFERE(e && !strncmp(e->casa.logo, "https://", 8), "logo upgraded to https");
    e = achar(&l, "700003");
    CONFERE(e && e->estado == ESP_FIM && e->casa.vencedor && !e->fora.vencedor, "finished match and winner");
    e = achar(&l, "700002");
    CONFERE(e && e->estado == ESP_PRE && !e->casa.placar[0] && e->ini > agora, "upcoming match, no score");
    e = achar(&l, "700004");
    CONFERE(e && e->estado == ESP_ADIADO, "postponed match");
  }
  {
    Lista l = {0};
    espn_buscar("basketball/nba", agora, juntar, &l, &quando, &falhou);
    CONFERE(l.n == 1 && !strcmp(l.v[0].casa.nome, "Los Angeles Lakers") && !strcmp(l.v[0].fora.nome, "Boston Celtics"),
            "NBA: away listed first, still shown home vs away");
  }
  {
    Lista l = {0};
    espn_buscar("tennis/atp", agora, juntar, &l, &quando, &falhou);
    CONFERE(l.n == 1 && !strcmp(l.v[0].id, "900001") && !strcmp(l.v[0].casa.nome, "Carlos Alcaraz") &&
            l.v[0].estado == ESP_AO_VIVO && strstr(l.v[0].casa.logo, "esp.png"), "tennis: the match inside the tournament");
  }
  {
    Lista l = {0};
    n = espn_buscar("soccer/eng.1", agora, juntar, &l, &quando, &falhou);
    CONFERE(n == 0 && !falhou, "empty league: no events, not a failure");
    n = espn_buscar("soccer/esp.1", agora, juntar, &l, &quando, &falhou);
    CONFERE(n < 0 && falhou, "HTML gateway error: a failure, no cache yet");
    CONFERE(espn_buscar("soccer/nope.9", agora, juntar, &l, &quando, &falhou) < 0, "unknown league refused");
  }

  // ---- follows (per profile) ----
  esporte_carregar();
  CONFERE(esporte_segue_liga("soccer/ksa.1") && !esporte_segue_liga("soccer/ita.1"), "default followed leagues");
  esporte_definir_liga("soccer/ita.1", 1);
  esporte_definir_liga("soccer/ksa.1", 0);
  esporte_alternar_time("soccer/ksa.1", "11", "Al Hilal");
  esporte_definir_esconder_placar(1);
  esporte_carregar();
  CONFERE(esporte_segue_liga("soccer/ita.1") && !esporte_segue_liga("soccer/ksa.1"), "league choices survive a reload");
  CONFERE(esporte_segue_time("soccer/ksa.1", "11") && !esporte_segue_time("soccer/ksa.1", "12"), "followed team");
  CONFERE(esporte_esconder_placar(), "hide scores survives a reload");
  {
    EspEvento e;
    memset(&e, 0, sizeof e);
    snprintf(e.liga, sizeof e.liga, "soccer/ksa.1");
    snprintf(e.fora.id, sizeof e.fora.id, "11");
    CONFERE(esporte_e_meu(&e), "an away match of a followed team is mine");
  }

  // ---- channel links ----
  CONFERE(esporte_ligar("700001", "xtream:@00000001:301", "SSC 1") && esporte_ligar("700001", "xtream:@00000001:305", "SSC 2"),
          "link a channel and an alternate");
  CONFERE(esporte_ligar("700001", "xtream:@00000001:301", "SSC 1"), "linking twice is fine (no duplicate)");
  {
    EspCanal c[8];
    int k = esporte_canais("700001", c, 8);
    CONFERE(k == 2 && !strcmp(c[0].canal, "xtream:@00000001:301"), "main first, alternate second");
    esporte_tornar_principal("700001", "xtream:@00000001:305");
    k = esporte_canais("700001", c, 8);
    CONFERE(k == 2 && !strcmp(c[0].canal, "xtream:@00000001:305"), "make the alternate the main");
    esporte_desligar("700001", "xtream:@00000001:301");
    k = esporte_canais("700001", c, 8);
    CONFERE(k == 1 && esporte_recusado("700001", "xtream:@00000001:301"), "unlinked channel is remembered as wrong");
  }

  // ---- suggestions from the guide ----
  {
    Lista l = {0};
    EspSugestao s[8];
    EspCanalInfo cs[] = {
      { "xtream:@00000001:301", "SSC Sport 1 HD", "Sports" },     // programme names both teams (recusado above)
      { "xtream:@00000001:302", "Al Hilal TV", "Sports" },        // one team only
      { "xtream:@00000001:303", "SSC Extra", "Sports" },          // broadcaster SSC, generic programme
      { "xtream:@00000001:304", "Sports 4", "Sports" },           // right title, wrong day
      { "nvm3u:@00000002:x", "EVENT: Al Hilal vs Al Nassr", "Events" }, // event channel name
      { "xtream:@00000001:306", "KSA Sports", "Sports" },         // Arabic title: matched by the Arabic names
    };
    const EspEvento *e;
    time_t ko;
    int k;
    espn_buscar("soccer/ksa.1", agora, juntar, &l, &quando, &falhou);
    e = achar(&l, "700001");
    ko = e ? e->ini : agora;
    k = e ? esporte_sugerir(e, cs, 6, progs, &ko, s, 8) : 0;
    CONFERE(k == 4, "suggestions: four channels (a refused one and a wrong-day one left out)");
    CONFERE(k >= 1 && !strcmp(s[0].canal, "xtream:@00000001:306") && s[0].certeza == ESP_PASSA, "Arabic programme naming both teams: showing it");
    CONFERE(k >= 2 && !strcmp(s[1].canal, "nvm3u:@00000002:x") && s[1].certeza == ESP_PASSA, "event channel named for both teams: showing it");
    CONFERE(k >= 3 && !strcmp(s[2].canal, "xtream:@00000001:302") && s[2].certeza == ESP_TALVEZ, "one team in the programme: maybe");
    CONFERE(k >= 4 && !strcmp(s[3].canal, "xtream:@00000001:303") && s[3].certeza == ESP_TALVEZ, "listed broadcaster on air: maybe");
    esporte_ligar("700001", "xtream:@00000001:301", "SSC 1");   // linking again lifts the refusal
    k = e ? esporte_sugerir(e, cs, 6, progs, &ko, s, 8) : 0;
    CONFERE(k == 5 && !strcmp(s[0].canal, "xtream:@00000001:301") && s[0].certeza == ESP_PASSA && strstr(s[0].motivo, "Al-Hilal"),
            "programme naming both teams: showing it, with the programme as the reason");
  }
  {
    EspTime t;
    memset(&t, 0, sizeof t);
    snprintf(t.nome, sizeof t.nome, "Manchester United FC");
    snprintf(t.curto, sizeof t.curto, "Man United");
    snprintf(t.abrev, sizeof t.abrev, "MUN");
    CONFERE(esporte_time_em(&t, "Premier League: Man United v Chelsea") && esporte_time_em(&t, "MUN vs CHE") &&
            !esporte_time_em(&t, "Manchester City v Chelsea"), "team words: short name, abbreviation, no false match");
  }

  // ---- TheSportsDB: per-country channels ----
  {
    Lista l = {0};
    TsdbTv tv[8];
    const EspEvento *e;
    char b2[200], id[24];
    int k;
    snprintf(b2, sizeof b2, "%s/tsdb", argv[2]);
    tsdb_definir_base(b2);
    espn_buscar("soccer/ksa.1", agora, juntar, &l, &quando, &falhou);
    e = achar(&l, "700001");
    CONFERE(e && tsdb_achar_evento("{\"events\":[{\"idEvent\":\"9\",\"strHomeTeam\":\"Al-Hilal\",\"strAwayTeam\":\"Al-Nassr\"}]}", e, id, sizeof id) &&
            !strcmp(id, "9"), "same match by team names (hyphens, no time)");
    CONFERE(e && !tsdb_achar_evento("{\"events\":[{\"idEvent\":\"9\",\"strHomeTeam\":\"Al-Hilal\",\"strAwayTeam\":\"Al-Ahli\"}]}", e, id, sizeof id),
            "different opponent: not the same match");
    k = tsdb_parse_tv("{\"tvevent\":[{\"strChannel\":\"Sky\",\"strCountry\":\"United Kingdom\"},"
                      "{\"strChannel\":\"beIN SPORTS 1\",\"strCountry\":\"Saudi Arabia\"}]}", tv, 8);
    CONFERE(k == 2 && !strcmp(tv[0].canal, "beIN SPORTS 1"), "Middle East channels first");
    CONFERE(tsdb_parse_tv("{\"tvevent\":null}", tv, 8) == 0, "no listings: none");
    k = e ? tsdb_canais(e, tv, 8) : -1;
    CONFERE(k == 3 && !strcmp(tv[0].canal, "beIN SPORTS 1") && !strcmp(tv[0].pais, "Saudi Arabia"),
            "over HTTP: eventsday -> lookuptv, Saudi channels first");
    if (e) {
      EspEvento x = *e;
      EspSugestao s[8];
      EspCanalInfo cs[] = { { "c1", "AR: beIN Sports 1 HD", "Sports" }, { "c2", "beIN Sports 11", "Sports" },
                            { "c3", "SSC 1", "Sports" } };
      snprintf(x.tvPais, sizeof x.tvPais, "beIN SPORTS 1|Saudi Arabia;SSC 1 HD|Saudi Arabia");
      k = esporte_sugerir(&x, cs, 3, NULL, NULL, s, 8);
      CONFERE(k == 2 && s[0].certeza == ESP_PASSA && strstr(s[0].motivo, "TheSportsDB"), "a listed channel on the user's list: showing it");
      { int tem11 = 0; for (int i = 0; i < k; i++) tem11 |= !strcmp(s[i].canal, "c2");
        CONFERE(!tem11, "beIN Sports 11 is not beIN SPORTS 1"); }
    }
  }

  // ---- profile isolation ----
  perfil = 2;
  esporte_carregar();
  { EspCanal c[8];
    CONFERE(!esporte_segue_time("soccer/ksa.1", "11") && esporte_canais("700001", c, 8) == 0 && !esporte_esconder_placar(),
            "profile 2: none of profile 1's follows, links or settings"); }
  return falhas ? 1 : 0;
}
