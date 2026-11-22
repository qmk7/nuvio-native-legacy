// QR TIME PRAYER TIMES, computed on the TV (no network): the sun's position
// for the chosen city (standard astronomical formulas, as in the PrayTimes
// algorithm), with each country's official method:
//   Saudi Arabia   Umm al-Qura: Fajr 18.5 deg, Isha 90 min after Maghrib
//                  (120 min in Ramadan)
//   Egypt          19.5 / 17.5 deg      Kuwait 18 / 17.5 deg
//   UAE            18.2 / 18.2 deg      Qatar 18 deg / 90 min
//   Jordan, others Muslim World League 18 / 17 deg
// Asr: shadow factor 1 (Shafi'i, the official method in these countries).
// Times are absolute instants; the screen shows them in the TV's time zone.
//
// Settings > Esta TV > "Horários de oração": a city, and whether to
//   - only show a quiet notice at each prayer, or
//   - also pause what is playing (film, series, music).
// HIJRI: the tabular (Kuwaiti) calendar; Umm al-Qura's official calendar
// can differ by one day.
#ifndef QRT_SALAT_H
#define QRT_SALAT_H
#include <SDL2/SDL.h>
#include <time.h>

enum { SALAT_FAJR, SALAT_SHURUQ, SALAT_DHUHR, SALAT_ASR, SALAT_MAGHRIB, SALAT_ISHA, SALAT_N };
typedef struct { const char *nome; double lat, lon; int metodo; } SalatCidade;
enum { SM_UMM_QURA, SM_EGITO, SM_MWL, SM_DUBAI, SM_KUWAIT, SM_QATAR };

int  salat_cidades(const SalatCidade **out);          // the list the setting offers
// The six times of the local day containing `quando` for city i. 1 ok.
int  salat_horarios(int cidade, time_t quando, time_t out[SALAT_N]);
int  salat_horarios_em(double lat, double lon, int metodo, int ramadan, time_t diaUtc0, time_t out[SALAT_N]);
const char *salat_nome(int s);                        // i18n key
// Hijri date (tabular). 1 ok.
int  salat_hijri(time_t quando, int *ano, int *mes, int *dia);
const char *salat_mes_hijri(int mes);                 // i18n key

void salat_passo(Uint32 agora);                       // app thread: notices / pause
// Test: the next prayer after `agora` for the configured city (-1 = off).
int  salat_proxima(time_t agora, time_t *quando);
#endif
