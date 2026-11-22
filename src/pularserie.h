// QR TIME: skip intro / recap for good, per series and per profile.
// The second time the person presses "Skip intro" (or "Skip recap") in the same
// series, that series starts skipping it on its own, as the global setting
// does for everything. Kept in skip-series-p<N>.txt ("<imdb>\t<mask>\t<n intro>\t<n recap>").
// Forgotten from Settings > Accounts, providers and data (QRR_PULO_SERIES).
#ifndef QRT_PULARSERIE_H
#define QRT_PULARSERIE_H

#define PULARSERIE_VEZES 2   // manual skips of the same kind before it becomes automatic

// A manual skip of `tipo` (INTRO_ABERTURA / INTRO_RESUMO) in this series.
// 1 = this press made it automatic for the series (tell the person).
int  pularserie_manual(const char *imdb, int tipo);
int  pularserie_sempre(const char *imdb, int tipo);   // 1 = skip on its own
void pularserie_esquecer(const char *imdb);           // this series only
void pularserie_recarregar(void);                     // profile switch / reset
#endif
