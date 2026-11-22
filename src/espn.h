// SPORT CENTER DATA (QR TIME): ESPN's public scoreboard feed, behind one
// adapter.
//
// STATUS OF THE SOURCE. site.api.espn.com/apis/site/v2/sports/<sport>/<league>/
// scoreboard is PUBLIC but UNDOCUMENTED: no key, no contract, no SLA. It can
// change shape or disappear. So: every field is optional when parsing, a body
// that is not a scoreboard is a failure (the previous copy stays), each
// league's last good answer is cached on disk with its time, and the screen
// says when the data is from and that the provider is not answering. Nothing
// here pretends it is a stable API.
//
// WINDOW. One request per league for the days today-7 .. today+7 (ESPN days
// are US Eastern; a match near midnight still lands in the window because the
// window is two weeks wide and times are absolute UTC instants).
//
// Parsing has no network (tests feed fixtures); espn_atualizar runs on the
// Sport Center worker thread.
#ifndef NV_ESPN_H
#define NV_ESPN_H
#include <stddef.h>
#include <time.h>

#define ESPN_LIGAS_MAX 32
typedef struct {
  const char *chave;     // "soccer/eng.1": also the ESPN path
  const char *esporte;   // "soccer" | "basketball" | "tennis"
  const char *nome;      // display name
  int padrao;            // followed by default
} EspLiga;
// The catalog of leagues the Sport Center knows (ESPN paths).
int espn_ligas(const EspLiga **out);
const EspLiga *espn_liga(const char *chave);

typedef struct {
  char id[16], nome[64], curto[40], abrev[8], logo[256];
  char placar[8];        // "" when not started / unknown
  int  vencedor;
} EspTime;
enum { ESP_PRE = 0, ESP_AO_VIVO, ESP_FIM, ESP_ADIADO };
typedef struct {
  char id[24];           // ESPN event id (stable for a match)
  char liga[32];         // league key ("soccer/eng.1")
  time_t ini;            // UTC instant
  int  estado;           // ESP_*
  char relogio[16];      // "67'" / "Q3 4:12"
  char detalhe[64];      // "Halftime", "FT", "Sat 3:00 PM"
  EspTime casa, fora;    // tennis: player 1 / player 2
  char local[96];
  char tv[128];          // broadcasters ESPN lists, ", " separated (mostly US)
  char tvPais[512];      // TheSportsDB channels "name|country;..." (tsdb.c), "" = none/unknown
} EspEvento;

// Parses one scoreboard body for league `liga`. Calls `cb` per event.
// Returns events delivered, -1 when the body is not a scoreboard.
int espn_parse(const char *json, const char *liga,
               int (*cb)(const EspEvento *e, void *u), void *u);
// "2026-11-17T20:00Z" / "2026-11-17T20:00:00Z" -> UTC instant (0 = invalid).
time_t espn_instante(const char *iso);
// "YYYYMMDD-YYYYMMDD" for agora-7 .. agora+7 days (UTC dates).
void espn_janela(time_t agora, char *dst, size_t tam);
// The month(s) the window covers ("YYYYMM"); ESPN accepts a month, not a day range. 1 or 2.
int espn_meses(time_t agora, char m[2][8]);

// ---- fetching with a disk cache (BLOCKS; worker thread) ----
// Base URL (tests point it at a fixture). Default: ESPN.
void espn_definir_base(const char *base);
// Fetches league `liga` (or reads its cache when the network fails). The
// body is delivered to `cb`. *quando = time of the data used (fetch time or
// cache time); *falhou = 1 when the network did not give a valid answer.
int espn_buscar(const char *liga, time_t agora, int (*cb)(const EspEvento *e, void *u), void *u,
                time_t *quando, int *falhou);
// ---- league standings (QR TIME) ----
typedef struct { char grupo[48], id[16], nome[64], curto[40], logo[256]; int pos, jogos, v, e, d, saldo, pts; } EspPos;
int espn_tabela_parse(const char *json, EspPos *out, int max);   // rows by group then position; -1 not standings
// BLOCKS (worker): fetch (cache esporte-tabela-<league>.json on success, used when offline).
int espn_tabela(const char *liga, EspPos *out, int max, time_t *quando, int *falhou);
#endif
