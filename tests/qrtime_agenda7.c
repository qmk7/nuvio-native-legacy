// QR TIME Agenda (phase 7): the Trakt calendar (UTC instants -> this TV's
// local date and time), confirmed / estimated / unknown dates (never
// invented), following a series per profile, and the calendar's shows joining
// the Agenda. src/agenda.c is INCLUDED (statics: tmdbLer, calendarioTrakt).
//
//   bash tests/qrtime_agenda7.sh
#include "../src/agenda.c"
#include "perfis.h"
#include <unistd.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

// The account's calendar, as Trakt shapes it.
static const char CAL[] =
  "[{\"first_aired\":\"2026-11-20T02:00:00.000Z\",\"episode\":{\"season\":2,\"number\":5,\"title\":\"Five\",\"ids\":{\"trakt\":1}},"
  "\"show\":{\"title\":\"QRT Show\",\"year\":2025,\"ids\":{\"trakt\":10,\"imdb\":\"tt9000001\",\"tmdb\":555}}},"
  "{\"first_aired\":\"2026-11-27T02:00:00.000Z\",\"episode\":{\"season\":2,\"number\":6,\"title\":\"Six\"},"
  "\"show\":{\"title\":\"QRT Show\",\"ids\":{\"imdb\":\"tt9000001\"}}},"
  "{\"first_aired\":\"2026-11-21T20:30:00.000Z\",\"episode\":{\"season\":1,\"number\":1,\"title\":\"Pilot\"},"
  "\"show\":{\"title\":\"Other Show\",\"ids\":{\"imdb\":\"tt9000002\"}}},"
  "{\"first_aired\":\"2026-11-22T02:00:00.000Z\",\"episode\":{\"season\":1,\"number\":2},"
  "\"show\":{\"title\":\"No IMDb\",\"ids\":{\"tmdb\":777}}},"
  "{\"first_aired\":null,\"episode\":{\"season\":3,\"number\":1},\"show\":{\"title\":\"TBA\",\"ids\":{\"imdb\":\"tt9000003\"}}},"
  "{\"first_aired\":\"2026-11-16T02:00:00.000Z\",\"episode\":{\"season\":2,\"number\":4},"
  "\"show\":{\"title\":\"QRT Show\",\"ids\":{\"imdb\":\"tt9000001\"}}}]";

static char *falsoGet(const char *url, int s, const char *const *cab, int *st) {
  (void)s; (void)cab;
  if (strstr(url, "/calendars/my/shows/")) { *st = 200; return strdup(CAL); }
  *st = 404;
  return NULL;
}

static char *semRede(const char *url, int s, const char *const *cab, int *st) { (void)url; (void)s; (void)cab; *st = 0; return NULL; }

int main(void) {
  const char *dir = getenv("NUVIO_DADOS");
  AgEpCal v[16];
  int n;
  dados_iniciar(".");
  if (!dir || strcmp(dados_dir(), dir)) { printf("FAIL: data dir is not the test folder\n"); return 1; }
  agenda_definir_hoje("2026-11-18");

  // ---- timezones: the same instant, two local dates ----
  setenv("TZ", "Asia/Riyadh", 1); tzset();
  n = agenda_trakt_cal_parse(CAL, v, 16);
  CONFERE(n == 4, "calendar: 4 dated episodes of IMDb-identified shows (null date and TMDB-only show skipped)");
  CONFERE(!strcmp(v[1].imdb, "tt9000001") && !strcmp(v[1].data, "2026-11-20") && !strcmp(v[1].hora, "05:00") && v[1].temp == 2 && v[1].ep == 5,
          "Riyadh: 02:00 UTC on the 20th = 05:00 on the 20th");
  setenv("TZ", "America/New_York", 1); tzset();
  n = agenda_trakt_cal_parse(CAL, v, 16);
  CONFERE(!strcmp(v[1].data, "2026-11-19") && !strcmp(v[1].hora, "21:00"), "New York: the same instant is 21:00 on the 19th (no day shift bug)");
  CONFERE(v[0].ep == 4 && v[1].ep == 5 && !strcmp(v[2].imdb, "tt9000002") && v[3].ep == 6, "sorted by instant");
  setenv("TZ", "Asia/Riyadh", 1); tzset();

  // ---- confirmed / estimated / unknown ----
  {
    AgItem it;
    memset(&it, 0, sizeof it);
    CONFERE(agenda_confianca(&it) == AG_DATA_DESCONHECIDA, "no date: unknown");
    snprintf(it.dataProx, sizeof it.dataProx, "2026-12-01"); it.temporada = 2; it.episodio = 7;
    CONFERE(agenda_confianca(&it) == AG_DATA_CONFIRMADA, "full date for an identified episode: confirmed");
    it.episodio = 0;
    CONFERE(agenda_confianca(&it) == AG_DATA_ESTIMADA, "a season's date, no episode listed: estimated");
    snprintf(it.dataProx, sizeof it.dataProx, "2027"); it.episodio = 1;
    CONFERE(agenda_confianca(&it) == AG_DATA_ESTIMADA, "a partial date (year only): estimated");
    CONFERE(!strcmp(agenda_confianca_rotulo(AG_DATA_CONFIRMADA), "Data confirmada"), "label key");
  }
  // TMDB: no next episode, but the next season has its air_date -> the season date, estimated.
  {
    AgBusca b;
    static const char TV[] = "{\"name\":\"Season Wait\",\"status\":\"Returning Series\",\"next_episode_to_air\":null,"
      "\"last_episode_to_air\":{\"air_date\":\"2025-05-01\",\"season_number\":1,\"episode_number\":8},"
      "\"seasons\":[{\"season_number\":0,\"air_date\":\"2030-01-01\"},{\"season_number\":1,\"air_date\":\"2025-03-01\"},"
      "{\"season_number\":2,\"air_date\":\"2027-02-14\"}],\"number_of_seasons\":2}";
    static const char TV2[] = "{\"name\":\"Unknown\",\"status\":\"Returning Series\",\"next_episode_to_air\":null,"
      "\"seasons\":[{\"season_number\":1,\"air_date\":\"2024-01-01\"},{\"season_number\":2,\"air_date\":null}]}";
    AgItem it;
    memset(&b, 0, sizeof b);
    tmdbLer(&b, TV);
    CONFERE(!strcmp(b.dataProx, "2027-02-14") && b.temp == 2 && b.ep == 0, "TMDB: next season's own date (specials ignored)");
    memset(&it, 0, sizeof it);
    snprintf(it.dataProx, sizeof it.dataProx, "%s", b.dataProx); it.temporada = b.temp; it.episodio = b.ep;
    CONFERE(agenda_confianca(&it) == AG_DATA_ESTIMADA, "...and it is shown as estimated, not as an episode date");
    memset(&b, 0, sizeof b);
    tmdbLer(&b, TV2);
    CONFERE(!b.dataProx[0], "season announced without a date: no date is invented");
  }

  // ---- following a series (per profile, persisted) ----
  CONFERE(!agenda_segue("tt9000009"), "not followed yet");
  CONFERE(agenda_seguir("tt9000009", "Followed\tShow", "", 1) && agenda_segue("tt9000009"), "follow a series");
  CONFERE(!agenda_seguir("qv:00000000000000aa.s", "Local", "", 1), "a local id cannot be followed (the Agenda's sources need IMDb)");
  {
    char *t = dados_ler("agenda-follows-p1.txt");
    CONFERE(t && strstr(t, "tt9000009\tFollowed Show"), "saved for this profile (tab in the title sanitized)");
    free(t);
  }
  agenda_montar();
  {
    int achou = 0;
    for (int i = 0; i < agenda_n(); i++) achou |= !strcmp(agenda_lista(i)->imdb, "tt9000009");
    CONFERE(achou, "a followed series is in the Agenda");
  }
  perfis_definir_ativo(2);
  CONFERE(!agenda_segue("tt9000009"), "profile 2 does not follow it");
  perfis_definir_ativo(1);
  CONFERE(agenda_segue("tt9000009"), "profile 1 still does");
  CONFERE(!agenda_seguir("tt9000009", NULL, NULL, 0) && !agenda_segue("tt9000009"), "unfollow");

  // ---- the Trakt calendar feeds the Agenda ----
  trakt_definir("fixture-token", "fixture-client");
  agenda_rede_teste(falsoGet);
  calendarioTrakt("2026-11-18");
  {
    AgEpCal p[8];
    const AgItem *r;
    int np = agenda_proximos("tt9000001", p, 8);
    CONFERE(np == 2 && p[0].ep == 5 && p[1].ep == 6, "upcoming episodes of a show (the past one, S2E4, left out)");
    r = agenda_registro("tt9000001");
    CONFERE(r && !strcmp(r->dataProx, "2026-11-20") && r->temporada == 2 && r->episodio == 5, "the show's next episode comes from Trakt's exact instant");
    CONFERE(r && agenda_confianca(r) == AG_DATA_CONFIRMADA, "and it is a confirmed date");
    agenda_montar();
    {
      int a = 0, b2 = 0;
      for (int i = 0; i < agenda_n(); i++) { a |= !strcmp(agenda_lista(i)->imdb, "tt9000001"); b2 |= !strcmp(agenda_lista(i)->imdb, "tt9000002"); }
      CONFERE(a && b2, "the calendar's shows join the Agenda");
    }
    perfis_definir_ativo(2);
    CONFERE(agenda_proximos("tt9000001", p, 8) == 0, "profile 2 does not see profile 1's calendar");
    perfis_definir_ativo(1);
  }
  // Offline: no answer erases nothing and invents nothing.
  agenda_rede_teste(semRede);
  calendarioTrakt("2026-11-18");
  {
    const AgItem *r = agenda_registro("tt9000001");
    CONFERE(r && !strcmp(r->dataProx, "2026-11-20"), "offline: the known date stays (no erase, no guess)");
  }
  return falhas ? 1 : 0;
}
