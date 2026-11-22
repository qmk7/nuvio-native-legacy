// Ver salat.h.
#include "salat.h"
#include "ajustes.h"
#include "idioma.h"
#include "ilha.h"
#include "mcplayer.h"
#include "player.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const SalatCidade CID[] = {
  { "Riyadh", 24.7136, 46.6753, SM_UMM_QURA }, { "Jeddah", 21.5433, 39.1728, SM_UMM_QURA },
  { "Makkah", 21.3891, 39.8579, SM_UMM_QURA }, { "Madinah", 24.5247, 39.5692, SM_UMM_QURA },
  { "Dammam", 26.4207, 50.0888, SM_UMM_QURA }, { "Khobar", 26.2172, 50.1971, SM_UMM_QURA },
  { "Al Ahsa", 25.3830, 49.5866, SM_UMM_QURA }, { "Taif", 21.2703, 40.4158, SM_UMM_QURA },
  { "Abha", 18.2164, 42.5053, SM_UMM_QURA }, { "Tabuk", 28.3835, 36.5662, SM_UMM_QURA },
  { "Buraidah", 26.3260, 43.9750, SM_UMM_QURA }, { "Hail", 27.5114, 41.7208, SM_UMM_QURA },
  { "Jazan", 16.8892, 42.5511, SM_UMM_QURA }, { "Najran", 17.5656, 44.2289, SM_UMM_QURA },
  { "Dubai", 25.2048, 55.2708, SM_DUBAI }, { "Abu Dhabi", 24.4539, 54.3773, SM_DUBAI },
  { "Doha", 25.2854, 51.5310, SM_QATAR }, { "Kuwait City", 29.3759, 47.9774, SM_KUWAIT },
  { "Manama", 26.2285, 50.5860, SM_UMM_QURA }, { "Muscat", 23.5880, 58.3829, SM_MWL },
  { "Cairo", 30.0444, 31.2357, SM_EGITO }, { "Amman", 31.9454, 35.9284, SM_MWL },
};
#define NCID ((int)(sizeof CID / sizeof *CID))
int salat_cidades(const SalatCidade **out) { if (out) *out = CID; return NCID; }

static const char *const NOMES[SALAT_N] = { "Fajr", "Nascer do sol", "Dhuhr", "Asr", "Maghrib", "Isha" };
const char *salat_nome(int s) { return s >= 0 && s < SALAT_N ? NOMES[s] : ""; }

// ---------------------------------------------------------------- astronomy
#define RAD (M_PI / 180.0)
static double fixa(double a, double b) { a = a - b * floor(a / b); return a < 0 ? a + b : a; }
// Sun declination and equation of time (hours) for Julian date jd.
static void sol(double jd, double *decl, double *eqt) {
  double D = jd - 2451545.0;
  double g = fixa(357.529 + 0.98560028 * D, 360), q = fixa(280.459 + 0.98564736 * D, 360);
  double L = fixa(q + 1.915 * sin(g * RAD) + 0.020 * sin(2 * g * RAD), 360);
  double e = 23.439 - 0.00000036 * D;
  double RA = atan2(cos(e * RAD) * sin(L * RAD), cos(L * RAD)) / RAD / 15.0;
  *eqt = q / 15.0 - fixa(RA, 24);
  if (*eqt > 12) *eqt -= 24; if (*eqt < -12) *eqt += 24;
  *decl = asin(sin(e * RAD) * sin(L * RAD)) / RAD;
}
// Hours (UTC) when the sun is `ang` degrees below the horizon, before (-1) or after (+1) noon.
static double hora(double jdDia, double lat, double lon, double ang, int lado, double t0) {
  double decl, eqt, noon, c;
  sol(jdDia + t0 / 24.0, &decl, &eqt);
  noon = 12 - lon / 15.0 - eqt;
  c = (-sin(ang * RAD) - sin(decl * RAD) * sin(lat * RAD)) / (cos(decl * RAD) * cos(lat * RAD));
  if (c > 1 || c < -1) return NAN;
  return noon + lado * acos(c) / RAD / 15.0;
}
static double asr(double jdDia, double lat, double lon, double t0) {
  double decl, eqt;
  sol(jdDia + t0 / 24.0, &decl, &eqt);
  double ang = -atan(1.0 / (1.0 + tan(fabs(lat - decl) * RAD))) / RAD;   // shadow factor 1
  return hora(jdDia, lat, lon, ang, 1, t0);
}
int salat_horarios_em(double lat, double lon, int metodo, int ramadan, time_t diaUtc0, time_t out[SALAT_N]) {
  static const double FAJR[] = { 18.5, 19.5, 18.0, 18.2, 18.0, 18.0 }, ISHA[] = { 0, 17.5, 17.0, 18.2, 17.5, 0 };
  double jd = 2440587.5 + (double)diaUtc0 / 86400.0, t[SALAT_N], decl, eqt;
  if (metodo < 0 || metodo > SM_QATAR) return 0;
  // Two passes: first with rough times, then the sun's position at each time.
  double r[SALAT_N] = { 5, 6, 12, 13, 18, 18 };
  for (int pass = 0; pass < 2; pass++) {
    sol(jd + r[SALAT_DHUHR] / 24.0, &decl, &eqt);
    t[SALAT_FAJR] = hora(jd, lat, lon, FAJR[metodo], -1, r[SALAT_FAJR]);
    t[SALAT_SHURUQ] = hora(jd, lat, lon, 0.833, -1, r[SALAT_SHURUQ]);
    t[SALAT_DHUHR] = 12 - lon / 15.0 - eqt;
    t[SALAT_ASR] = asr(jd, lat, lon, r[SALAT_ASR]);
    t[SALAT_MAGHRIB] = hora(jd, lat, lon, 0.833, 1, r[SALAT_MAGHRIB]);
    t[SALAT_ISHA] = ISHA[metodo] > 0 ? hora(jd, lat, lon, ISHA[metodo], 1, r[SALAT_ISHA])
                                     : t[SALAT_MAGHRIB] + (ramadan && metodo == SM_UMM_QURA ? 2.0 : 1.5);
    for (int i = 0; i < SALAT_N; i++) if (isnan(t[i])) return 0;
    memcpy(r, t, sizeof r);
  }
  t[SALAT_DHUHR] += 1.0 / 60.0;   // Dhuhr: a minute after the zenith (common practice)
  for (int i = 0; i < SALAT_N; i++) out[i] = diaUtc0 + (time_t)lround(t[i] * 3600.0 / 60.0) * 60;   // to the minute
  return 1;
}

// ---------------------------------------------------------------- hijri (tabular)
int salat_hijri(time_t quando, int *ano, int *mes, int *dia) {
  long jd = (long)floor((double)quando / 86400.0 + 2440587.5 + 0.5);
  long l = jd - 1948440 + 10632, n = (l - 1) / 10631;
  l = l - 10631 * n + 354;
  long j = ((10985 - l) / 5316) * ((50 * l) / 17719) + (l / 5670) * ((43 * l) / 15238);
  l = l - ((30 - j) / 15) * ((17719 * j) / 50) - (j / 16) * ((15238 * j) / 43) + 29;
  *mes = (int)((24 * l) / 709);
  *dia = (int)(l - (709 * *mes) / 24);
  *ano = (int)(30 * n + j - 30);
  return *mes >= 1 && *mes <= 12;
}
static const char *const MESES[12] = { "Muharram", "Safar", "Rabi al-Awwal", "Rabi al-Thani", "Jumada al-Ula", "Jumada al-Akhirah",
                                       "Rajab", "Shaban", "Ramadan", "Shawwal", "Dhu al-Qadah", "Dhu al-Hijjah" };
const char *salat_mes_hijri(int m) { return m >= 1 && m <= 12 ? MESES[m - 1] : ""; }

int salat_horarios(int cidade, time_t quando, time_t out[SALAT_N]) {
  struct tm lt;
  time_t dia;
  int a, m, d;
  if (cidade < 0 || cidade >= NCID) return 0;
  localtime_r(&quando, &lt);
  // The local calendar day, computed at UTC midnight of that date (the
  // formulas give UTC hours for the date).
  {
    struct tm u = { 0 };
    u.tm_year = lt.tm_year; u.tm_mon = lt.tm_mon; u.tm_mday = lt.tm_mday;
    dia = timegm(&u);
  }
  salat_hijri(quando, &a, &m, &d);
  return salat_horarios_em(CID[cidade].lat, CID[cidade].lon, CID[cidade].metodo, m == 9, dia, out);
}

// ---------------------------------------------------------------- notices
int salat_proxima(time_t agora, time_t *quando) {
  int c = ajustes_salat_cidade() - 1;
  time_t t[SALAT_N];
  if (c < 0) return -1;
  for (int dia = 0; dia < 2; dia++) {
    if (!salat_horarios(c, agora + dia * 86400, t)) return -1;
    for (int i = 0; i < SALAT_N; i++) if (i != SALAT_SHURUQ && t[i] > agora) { if (quando) *quando = t[i]; return i; }
  }
  return -1;
}
void salat_passo(Uint32 agoraMs) {
  static Uint32 ultimo;
  static time_t avisado;
  time_t agora = time(NULL), t[SALAT_N];
  int c = ajustes_salat_cidade() - 1, modo = ajustes_salat_modo();
  if (agoraMs - ultimo < 5000u) return;
  ultimo = agoraMs;
  if (c < 0 || !salat_horarios(c, agora, t)) return;
  for (int i = 0; i < SALAT_N; i++) {
    char msg[160];
    if (i == SALAT_SHURUQ || agora < t[i] || agora > t[i] + 90 || avisado == t[i]) continue;
    avisado = t[i];
    snprintf(msg, sizeof msg, i18n("Hora da oração: %s"), i18n(salat_nome(i)));
    ilha_avisar("salat", ILHA_INFO, "aj_moon-star", msg, 12000u, 0);
    if (modo == 1) {
      player_sono_pausar();
      if (mcp_estado() == MCP_TOCANDO) mcp_alternar_pausa();
    }
    printf("[salat] %s%s\n", salat_nome(i), modo == 1 ? " (pausa)" : "");
    fflush(stdout);
  }
}
