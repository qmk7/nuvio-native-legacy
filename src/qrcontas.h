// QR TIME ACCOUNTS AND PROVIDERS (phase 8): one list of every provider the app
// talks to, with its real state (read from the owning module, never assumed),
// and the operations each one supports: test the connection, switch on/off,
// refresh its data, open its own settings. No SDL here (tests use it).
#ifndef NV_QRCONTAS_H
#define NV_QRCONTAS_H

enum {
  QRC_IPTV = 1, QRC_ADDONS, QRC_MUSICA, QRC_TRAKT, QRC_SIMKL, QRC_YOUTUBE, QRC_LASTFM,
  QRC_SPOTIFY, QRC_TMDB, QRC_TSDB, QRC_JELLYFIN, QRC_PLEX
};
// State shown on the row.
enum { QRC_E_NAO = 0,      // not configured
       QRC_E_CONFIG,       // configured (no connection check made)
       QRC_E_LIGADO,       // connected / signed in / test passed
       QRC_E_DESLIGADO,    // configured but switched off by the person
       QRC_E_TESTANDO,
       QRC_E_ERRO };       // last test failed
// What the row can do.
enum { QRC_P_TESTAR = 1, QRC_P_ALTERNAR = 2, QRC_P_ATUALIZAR = 4, QRC_P_ABRIR = 8, QRC_P_CHAVE = 16 };

typedef struct {
  int  tipo;
  char ref[24];         // IPTV account id / music source key
  char nome[96];        // display name (provider's own text, or an i18n key when `traduzir`)
  int  traduzir;
  int  estado, pode;
  char detalhe[160];    // i18n key or free text (counts, host); never a credential
} QrcLinha;

#define QRC_MAX 48
// Builds the list from the modules' current state (UI thread; no network).
int  qrc_montar(QrcLinha *out, int max);
// Starts a connection test on a worker (IPTV, music source, YouTube key).
// 1 started. The row reads QRC_E_TESTANDO until it ends.
int  qrc_testar(const QrcLinha *l);
// Switch on/off (IPTV account, add-ons list item, music source). 1 ok.
int  qrc_alternar(const QrcLinha *l);
// Refresh this provider's data (drops its caches; next use fetches again). 1 ok.
int  qrc_atualizar(const QrcLinha *l);
// TheSportsDB personal key ("" = back to the free key). Stored as a secret.
int  qrc_tsdb_chave(const char *chave);
void qrc_tsdb_carregar(void);   // startup
// Last test result for a row (QRC_E_* and a short message), -1 none.
int  qrc_resultado(const QrcLinha *l, char *msg, unsigned n);
// Tests: wait for the running test.
int  qrc_teste_esperar(int ms);
#endif
