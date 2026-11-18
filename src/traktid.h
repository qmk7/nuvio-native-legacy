// QR TIME: the ONE gate for every identity that goes to Trakt.
//
// Trakt only understands its own ids and the public ones (IMDb, TMDB, TVDB).
// An app id can be one of those, or a provider-LOCAL id that must never leave
// this TV as if it were IMDb/TMDB: IPTV VOD ("qv:..."), Jellyfin/Emby/Plex
// ("jf:", "em:", "px:"), raw Xtream stream ids, Stalker ids, Kitsu, anything
// else. Before this gate, "qv:..." could reach a scrobble body as
// {"imdb":"qv:..."} and "tmdb:603" as {"imdb":"tmdb"}.
//
// Accepted:
//   "tt1234567"            IMDb (7+ digits), optional ":S:E" for an episode
//   "tmdb:603", "tmdb:m603", "tmdb:t1396"   TMDB, optional ":S:E"
//   "tvdb:81189"           TVDB, optional ":S:E"
//   "qv:<16 hex>.m|.s|.e<S>x<E>"  IPTV VOD: ONLY when TMDB already confirmed
//                          its canonical IMDb/TMDB id (iptvvod_poster_ids);
//                          otherwise refused and the progress stays local.
// Everything else: refused (TID_LOCAL / TID_INVALIDO).
#ifndef NV_TRAKTID_H
#define NV_TRAKTID_H
#include <stddef.h>

enum { TID_INVALIDO = 0, TID_LOCAL, TID_IMDB, TID_TMDB, TID_TVDB };

typedef struct {
  int  tipo;               // TID_*
  char imdb[16];           // TID_IMDB
  long num;                // TID_TMDB / TID_TVDB
  int  temporada, episodio; // -1 / 0 when not an episode
  int  resolvido;          // 1: came from a local id through a confirmed resolution
} TraktId;

// Classify / resolve. Returns out->tipo; only TID_IMDB/TMDB/TVDB may be sent.
int traktid_resolver(const char *id, TraktId *out);
int traktid_enviavel(const char *id);    // shorthand: resolver() >= TID_IMDB
// The "ids" object's contents: "\"imdb\":\"tt1\"" / "\"tmdb\":603" / "\"tvdb\":81189".
int traktid_ids_json(const TraktId *t, char *dst, size_t n);
// The canonical app id: "tt1", "tmdb:603", "tvdb:81189", with ":S:E" when an episode.
int traktid_canonico(const TraktId *t, char *dst, size_t n);
// Tests: the IPTV resolution (default: iptvvod_poster_ids when linked).
void traktid_teste_resolvedor(int (*f)(const char *id, const char *tipo, char *imdb, size_t n, long *tmdb));
#endif
