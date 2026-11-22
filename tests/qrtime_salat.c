// QR TIME prayer times (src/salat.c): Riyadh on 22/11/2026 against values
// cross-checked with NOAA's solar formulas (sunrise, noon, sunset, Fajr within
// 1 min), Umm al-Qura Isha (+90 min, +120 in Ramadan), the Hijri date.
//   bash tests/qrtime_salat.sh
#include "salat.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
int ajustes_salat_cidade(void) { return 1; }   // Riyadh
int ajustes_salat_modo(void) { return 0; }
void ilha_avisar(const char *c, int t, const char *i, const char *x, unsigned m, int k) { (void)c; (void)t; (void)i; (void)x; (void)m; (void)k; }
const char *i18n(const char *s) { return s; }
void player_sono_pausar(void) {}
int mcp_estado(void) { return 0; }
void mcp_alternar_pausa(void) {}
static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static int hm(time_t t) { return (int)((t + 3 * 3600) % 86400 / 60); }   // Riyadh local minutes (UTC+3)
#define H(h, m) ((h) * 60 + (m))
int main(void) {
  time_t dia = 1795305600, o[SALAT_N], r[SALAT_N];   // 2026-11-22 00:00 UTC
  int a, m, d;
  CONFERE(salat_horarios_em(24.7136, 46.6753, SM_UMM_QURA, 0, dia, o), "computed");
  CONFERE(hm(o[SALAT_FAJR]) == H(4, 52) && hm(o[SALAT_SHURUQ]) == H(6, 14) && hm(o[SALAT_DHUHR]) == H(11, 40)
          && hm(o[SALAT_ASR]) == H(14, 43) && hm(o[SALAT_MAGHRIB]) == H(17, 4) && hm(o[SALAT_ISHA]) == H(18, 34),
          "Riyadh 22/11/2026: 04:52 06:14 11:40 14:43 17:04 18:34");
  CONFERE(o[SALAT_ISHA] - o[SALAT_MAGHRIB] == 90 * 60, "Umm al-Qura: Isha 90 min after Maghrib");
  salat_horarios_em(24.7136, 46.6753, SM_UMM_QURA, 1, dia, r);
  CONFERE(r[SALAT_ISHA] - r[SALAT_MAGHRIB] == 120 * 60, "...120 min in Ramadan");
  salat_horarios_em(30.0444, 31.2357, SM_EGITO, 0, dia, r);
  CONFERE(r[SALAT_ISHA] - r[SALAT_MAGHRIB] > 60 * 60 && r[SALAT_ISHA] - r[SALAT_MAGHRIB] < 100 * 60, "Egypt: Isha by angle (17.5 deg)");
  CONFERE(salat_hijri(1771372800, &a, &m, &d) && a == 1447 && m == 9 && (d == 1 || d == 2), "18/02/2026 = 1 Ramadan 1447 (tabular, +-1 day)");
  {
    const SalatCidade *c;
    CONFERE(salat_cidades(&c) == 22 && !strcmp(c[0].nome, "Riyadh"), "city list (matches the setting's values)");
  }
  {
    time_t q;
    int p = salat_proxima(dia + 9 * 3600, &q);   // 09:00 UTC = 12:00 Riyadh time
    CONFERE(p == SALAT_ASR && hm(q) == H(14, 43), "next prayer after noon: Asr");
  }
  return falhas ? 1 : 0;
}
