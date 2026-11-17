// THESPORTSDB TV LISTINGS (QR TIME): which TV channels show a match, per
// country. ESPN names mostly US broadcasters; TheSportsDB lists, for example,
// beIN SPORTS in Saudi Arabia, so the Sport Center can find the user's own
// Middle East channels.
//
// Used: v1 eventsday.php?d=YYYY-MM-DD&s=<sport> (idEvent, strHomeTeam,
// strAwayTeam, strTimestamp) and lookuptv.php?id=<idEvent> (tvevent[]:
// strChannel, strCountry), with the free key "123" unless the user has one.
// A community-maintained database: listings can be missing or late, and the
// free key is rate-limited (about 30 requests a minute), so lookups are on
// demand (a match page), cached, and spaced. Its failure leaves ESPN's data.
#ifndef NV_TSDB_H
#define NV_TSDB_H
#include <stddef.h>
#include <time.h>
#include "espn.h"

#define TSDB_TV_MAX 16
typedef struct { char canal[64], pais[48]; } TsdbTv;

// Pure parsers (tests).
// The event of eventsday.php that is this match (same teams, kick-off within
// 3 h). Writes its idEvent; 1 when found.
int tsdb_achar_evento(const char *json, const EspEvento *e, char *id, size_t n);
// lookuptv.php: channels, Middle East countries first. Returns how many.
int tsdb_parse_tv(const char *json, TsdbTv *out, int max);

void tsdb_definir_base(const char *base);   // tests: a local fixture
void tsdb_definir_chave(const char *chave); // the user's key ("" = free "123")
// Channels of match `e` (BLOCKS; worker thread). Cached per event for the
// session. Returns how many, 0 none, -1 no answer.
int tsdb_canais(const EspEvento *e, TsdbTv *out, int max);
#endif
