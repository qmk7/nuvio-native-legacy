// QR TIME DATA (phase 8): the persistent-data schema version, safe upgrades,
// cache invalidation and the explicit, user-controlled reset operations.
//
// Rules (docs/qrtime/DATA-SCHEMA.md):
//  - nothing here runs on its own except the schema check at startup, and that
//    check never deletes: an upgrade step only rewrites or adds;
//  - a reset runs only when the person confirms it in the Data screen, and
//    touches only its own files (per profile unless it says "this TV");
//  - cache = regenerable files only, by an allowlist; unknown names are kept.
#ifndef NV_QRDADOS_H
#define NV_QRDADOS_H
#include <stdint.h>

#define QRD_ESQUEMA 2        // current QR TIME data schema (2: max-quality defaults)
#define QRD_ARQ "qrtime-schema.txt"

// Startup, after dados_iniciar. Returns the schema found on disk before the
// check (0 = first start with QR TIME data). Never deletes anything.
int  qrdados_esquema_iniciar(void);
int  qrdados_esquema_disco(void);   // version on disk now
int  qrdados_mais_novo(void);       // 1 = data written by a NEWER app: left untouched, not downgraded
// After the settings load: applies the schema-v2 max-quality defaults once.
int  qrdados_qualidade_aplicar(void);

// Tests: replace the upgrade steps (step i upgrades from i+1 to i+2).
typedef int (*QrdPasso)(void);
void qrdados_teste_passos(const QrdPasso *passos, int n, int alvo);

// ---- cache ----
int      qrdados_e_cache(const char *nome);   // regenerable by name (allowlist)
uint64_t qrdados_cache_bytes(void);
int      qrdados_cache_limpar(void);          // files removed, -1 error

// ---- explicit resets ----
enum {
  QRR_CACHE,            // regenerable files (this TV)
  QRR_MUSICA_OFFLINE,   // offline music copies (this TV)
  QRR_MUSICA_HIST,      // music history + queue (this profile)
  QRR_MUSICA_BIBLIO,    // music favourites + playlists (this profile)
  QRR_AGENDA_SEGUIR,    // followed series (this profile)
  QRR_ESPORTE,          // Sport Center leagues, teams, channel links (this profile)
  QRR_TRAKT_REGISTRO,   // record of what this app sent to Trakt history (this profile)
  QRR_CONTAS_PERFIL,    // YouTube / Last.fm links of this profile
  QRR_CHAVES_TV,        // YouTube / Last.fm / Spotify API keys (this TV)
  QRR_PULO_SERIES,      // series that skip intro/recap on their own (this profile)
  QRR_N
};
const char *qrdados_reset_nome(int op);      // i18n key
const char *qrdados_reset_desc(int op);      // i18n key
int         qrdados_reset_por_tv(int op);    // 1 = affects every profile on this TV
int         qrdados_resetar(int op);         // 1 ok
#endif
