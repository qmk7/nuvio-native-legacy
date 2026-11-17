// IPTV LIBRARY (QR TIME): movies and series of the user's IPTV providers as
// one library, next to the personal servers in servidores.c.
//
// IDENTITY. Every provider copy keeps its own identity: account id, provider
// type, stream/series/episode id (xtvod.h). Copies of the same title are
// grouped into ONE content item whose id is local to this TV:
//   movie / series   "qv:<16 hex>.m" / "qv:<16 hex>.s"
//   episode          "qv:<16 hex>.e<season>x<episode>"
// The 16 hex are a hash of the grouping key, never a provider id. "qv:" is a
// personal-library namespace (jfid.h jfid_e), so the id never reaches Trakt,
// the Nuvio account, addons, Discord or any metadata lookup.
//
// GROUPING (no network, provider data only):
//   1. copies whose panel gives the same TMDB id (a hint) group together;
//   2. a copy without one joins a group whose copy has the same normalized
//      title AND the same year;
//   3. anything else is its own item (provider-local).
// A TMDB hint is never used as an identity outside this module: matching
// against addon titles (resolved IMDb/TMDB ids) is a separate step.
//
// THREADS. Loading runs on its own worker; Home copies a snapshot under the
// lock; ficha and the source lookup BLOCK and run on worker threads.
#ifndef NV_IPTVVOD_H
#define NV_IPTVVOD_H
#include <stddef.h>
#include <stdint.h>
#include "catalogo.h"

struct Stream;

#define IPTVVOD_PREFIXO "qv:"
#define IPTVVOD_FIL_MAX 2           // Home rows: recent movies, recent series
#define IPTVVOD_POR_FILEIRA 24

// ---- identity (pure; tests) ----
// Normalized matching title: lower-case ASCII, language prefixes ("EN - ",
// "|EN|", "[EN]"), quality tags (4K, FHD, HEVC...) and the year removed.
void iptvvod_normalizar(const char *nome, char *dst, size_t tam);
// Title for display: language prefix, "(year)" and quality tags removed.
void iptvvod_titulo_limpo(const char *nome, char *dst, size_t tam);
int  iptvvod_e_id(const char *id);
// Parses an id: kind 'm', 's' or 'e' (with season/episode). 1 if valid.
int  iptvvod_id_ler(const char *id, uint64_t *grupo, char *kind, int *temporada, int *episodio);
void iptvvod_id_montar(char *dst, size_t tam, uint64_t grupo, char kind, int temporada, int episodio);

// ---- library ----
void iptvvod_carregar(void);        // (re)load this profile's providers, async
void iptvvod_perfil_trocou(void);   // drop everything, reload for the new profile
int  iptvvod_passo(void);           // UI thread, once per frame: 1 = rows changed
int  iptvvod_ativo(void);           // the library has content for this profile
int  iptvvod_carregando(void);
unsigned iptvvod_fileiras_versao(void);
int  iptvvod_chave_fileira(const char *chave);   // "iptvvod_..."
int  iptvvod_fileiras_copiar(CatItem *itens, int maxItens, CatFileira *fils, int maxFils, int *nItens);
// Counts for tests and diagnostics: content items and provider copies.
int  iptvvod_n_itens(char kind);
int  iptvvod_n_copias(const char *id);

// Title page (BLOCKS): metadata and, for series, the merged episode list of
// every provider (episode vid = qv episode id). -1 unknown id / no answer.
int  iptvvod_ficha(CatItem *item, CatEp *eps, int maxEps);
// Sources of a movie or an episode id, one per provider copy. pedir returns
// at once; colher (UI thread) answers JF_FONTES_* like the servers.
int  iptvvod_fontes_pedir(const char *alvo);
int  iptvvod_fontes_colher(const char *alvo, struct Stream **lista, int *n);

// ---- matching addon titles (resolved ids only) ----
// Sources of the IPTV library for a catalogue title that came from an addon:
// "tt0133093", "tt0903747:1:2", "tmdb:603", "tmdb:1396:1:2". The id is a
// real IMDb/TMDB id; it is resolved with TMDB (title, original title, year)
// and only groups whose provider TMDB hint matches AND whose title agrees
// (or, with no hint, title and year agree) contribute sources. Provider ids
// never leave this TV. Registered as an extra source of addons.c.
// Returns sources (>= 0, *saida malloc'd) or -1.
int  iptvvod_origem(const char *id, const char *tipo, int (*cancelado)(void *), void *ctx,
                    void *aviso, void *avisoU, void *saida);
int  iptvvod_origem_ativa(void);        // library loaded and a TMDB key
void iptvvod_ligar_aos_addons(void);   // iptvvodaddons.c
// The TMDB key in use (descoberta.c). Tests point the base URL at a fixture.
void iptvvod_definir_chave_tmdb(const char *(*chave)(void));
void iptvvod_teste_tmdb_base(const char *base);

// Tests: wait for the current load (ms). 1 when idle.
int  iptvvod_teste_esperar(int ms);
#endif
