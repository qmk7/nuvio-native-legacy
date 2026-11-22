// QR TIME: Arabic names for the teams and leagues ESPN gives in English.
// Used (1) on screen when the app is in Arabic, and (2) when matching a match
// against a channel's programme guide, so an Arabic guide ("الهلال × النصر")
// still finds "Al Hilal vs Al Nassr". Keys are ESPN's displayName or
// shortDisplayName exactly as the feed sends them (checked against the live
// /teams lists, 22/11/2026). A team not listed keeps its ESPN name.
#include "esportenomes.h"
#include <string.h>
#include <strings.h>

typedef struct { const char *en, *ar; } Par;
static const Par T[] = {
  // Saudi Pro League
  { "Al Hilal", "الهلال" }, { "Al Nassr", "النصر" }, { "Al Ittihad", "الاتحاد" }, { "Al Ahli", "الأهلي" },
  { "Al Shabab", "الشباب" }, { "Al Ettifaq", "الاتفاق" }, { "Al Fateh", "الفتح" }, { "Al Taawoun", "التعاون" },
  { "Al Fayha", "الفيحاء" }, { "Al Raed", "الرائد" }, { "Al Khaleej", "الخليج" }, { "Al Riyadh", "الرياض" },
  { "Al Okhdood", "الأخدود" }, { "Damac", "ضمك" }, { "Al Wehda", "الوحدة" }, { "Al Qadsiah", "القادسية" },
  { "Al Kholood", "الخلود" }, { "Al Orobah", "العروبة" }, { "Al Hazem", "الحزم" }, { "Al Najma", "النجمة" },
  { "Neom SC", "نيوم" }, { "Neom", "نيوم" }, { "Abha", "أبها" }, { "Al Diriyah", "الدرعية" }, { "Al Faisaly", "الفيصلي" },
  { "Al Tai", "الطائي" }, { "Al Akhdoud", "الأخدود" },
  // Egypt
  { "Al Ahly", "الأهلي" }, { "Zamalek", "الزمالك" }, { "Pyramids FC", "بيراميدز" }, { "Al Masry", "المصري" },
  { "Ismaily", "الإسماعيلي" },
  // AFC / Gulf
  { "Al Sadd", "السد" }, { "Al Duhail", "الدحيل" }, { "Al Rayyan", "الريان" }, { "Al Gharafa", "الغرافة" },
  { "Al Ain", "العين" }, { "Al Wahda", "الوحدة" }, { "Al Wasl", "الوصل" }, { "Shabab Al Ahli", "شباب الأهلي" },
  { "Sharjah", "الشارقة" }, { "Al Shorta", "الشرطة" }, { "Persepolis", "برسبوليس" }, { "Esteghlal", "استقلال" },
  // Premier League
  { "Arsenal", "أرسنال" }, { "Aston Villa", "أستون فيلا" }, { "AFC Bournemouth", "بورنموث" }, { "Bournemouth", "بورنموث" },
  { "Brentford", "برينتفورد" }, { "Brighton & Hove Albion", "برايتون" }, { "Brighton", "برايتون" }, { "Chelsea", "تشيلسي" },
  { "Coventry City", "كوفنتري" }, { "Coventry", "كوفنتري" }, { "Crystal Palace", "كريستال بالاس" }, { "C Palace", "كريستال بالاس" },
  { "Everton", "إيفرتون" }, { "Fulham", "فولهام" }, { "Hull City", "هال سيتي" }, { "Hull", "هال سيتي" },
  { "Ipswich Town", "إبسويتش" }, { "Ipswich", "إبسويتش" }, { "Leeds United", "ليدز يونايتد" }, { "Leeds", "ليدز" },
  { "Liverpool", "ليفربول" }, { "Manchester City", "مانشستر سيتي" }, { "Man City", "مانشستر سيتي" },
  { "Manchester United", "مانشستر يونايتد" }, { "Man Utd", "مانشستر يونايتد" }, { "Newcastle United", "نيوكاسل" },
  { "Newcastle", "نيوكاسل" }, { "Nottingham Forest", "نوتنغهام فورست" }, { "Nottm Forest", "نوتنغهام فورست" },
  { "Sunderland", "سندرلاند" }, { "Tottenham Hotspur", "توتنهام" }, { "Spurs", "توتنهام" }, { "West Ham United", "وست هام" },
  { "West Ham", "وست هام" }, { "Wolverhampton Wanderers", "وولفرهامبتون" }, { "Wolves", "وولفرهامبتون" },
  { "Burnley", "بيرنلي" }, { "Leicester City", "ليستر سيتي" }, { "Southampton", "ساوثهامبتون" },
  // LaLiga
  { "Real Madrid", "ريال مدريد" }, { "Barcelona", "برشلونة" }, { "Atlético Madrid", "أتلتيكو مدريد" }, { "Atlético", "أتلتيكو مدريد" },
  { "Athletic Club", "أتلتيك بلباو" }, { "Athletic", "أتلتيك بلباو" }, { "Sevilla", "إشبيلية" }, { "Real Betis", "ريال بيتيس" },
  { "Betis", "ريال بيتيس" }, { "Real Sociedad", "ريال سوسيداد" }, { "Valencia", "فالنسيا" }, { "Villarreal", "فياريال" },
  { "Celta Vigo", "سيلتا فيغو" }, { "Getafe", "خيتافي" }, { "Osasuna", "أوساسونا" }, { "Rayo Vallecano", "رايو فاييكانو" },
  { "Rayo", "رايو فاييكانو" }, { "Espanyol", "إسبانيول" }, { "Alavés", "ألافيس" }, { "Elche", "إلتشي" }, { "Levante", "ليفانتي" },
  { "Málaga", "ملقة" }, { "Deportivo", "ديبورتيفو" }, { "Racing Santander", "راسينغ سانتاندير" }, { "Racing", "راسينغ سانتاندير" },
  { "Girona", "جيرونا" }, { "Mallorca", "مايوركا" }, { "Las Palmas", "لاس بالماس" },
  // Serie A
  { "Juventus", "يوفنتوس" }, { "Internazionale", "إنتر ميلان" }, { "Inter Milan", "إنتر ميلان" }, { "AC Milan", "ميلان" },
  { "Milan", "ميلان" }, { "Napoli", "نابولي" }, { "AS Roma", "روما" }, { "Lazio", "لاتسيو" }, { "Atalanta", "أتالانتا" },
  { "Fiorentina", "فيورنتينا" }, { "Bologna", "بولونيا" }, { "Torino", "تورينو" }, { "Como", "كومو" }, { "Genoa", "جنوى" },
  { "Udinese", "أودينيزي" }, { "Cagliari", "كالياري" }, { "Lecce", "ليتشي" }, { "Parma", "بارما" }, { "Sassuolo", "ساسولو" },
  { "Monza", "مونزا" }, { "Venezia", "فينيزيا" }, { "Frosinone", "فروزينوني" },
  // Bundesliga
  { "Bayern Munich", "بايرن ميونخ" }, { "Bayern", "بايرن ميونخ" }, { "Borussia Dortmund", "بوروسيا دورتموند" },
  { "Dortmund", "دورتموند" }, { "Bayer Leverkusen", "باير ليفركوزن" }, { "Leverkusen", "ليفركوزن" }, { "RB Leipzig", "لايبزيغ" },
  { "Eintracht Frankfurt", "آينتراخت فرانكفورت" }, { "Frankfurt", "فرانكفورت" }, { "VfB Stuttgart", "شتوتغارت" },
  { "Stuttgart", "شتوتغارت" }, { "Borussia Mönchengladbach", "بوروسيا مونشنغلادباخ" }, { "Gladbach", "مونشنغلادباخ" },
  { "SC Freiburg", "فرايبورغ" }, { "Freiburg", "فرايبورغ" }, { "Werder Bremen", "فيردر بريمن" }, { "Bremen", "بريمن" },
  { "Schalke 04", "شالكه" }, { "Schalke", "شالكه" }, { "Hamburg SV", "هامبورغ" }, { "Hamburg", "هامبورغ" },
  { "TSG Hoffenheim", "هوفنهايم" }, { "Hoffenheim", "هوفنهايم" }, { "1. FC Union Berlin", "يونيون برلين" },
  { "Union Berlin", "يونيون برلين" }, { "FC Augsburg", "أوغسبورغ" }, { "Augsburg", "أوغسبورغ" }, { "Mainz", "ماينتس" },
  { "FC Cologne", "كولن" }, { "Cologne", "كولن" },
  // Ligue 1
  { "Paris Saint-Germain", "باريس سان جيرمان" }, { "PSG", "باريس سان جيرمان" }, { "Marseille", "مرسيليا" }, { "Lyon", "ليون" },
  { "AS Monaco", "موناكو" }, { "Monaco", "موناكو" }, { "Lille", "ليل" }, { "Lens", "لانس" }, { "Nice", "نيس" },
  { "Stade Rennais", "رين" }, { "Rennes", "رين" }, { "Strasbourg", "ستراسبورغ" }, { "Toulouse", "تولوز" }, { "Brest", "بريست" },
  { "Paris FC", "باريس إف سي" }, { "Nantes", "نانت" },
  // Champions League regulars
  { "FC Porto", "بورتو" }, { "Benfica", "بنفيكا" }, { "Sporting CP", "سبورتنغ لشبونة" }, { "Sporting", "سبورتنغ لشبونة" },
  { "Galatasaray", "غلطة سراي" }, { "Fenerbahce", "فنربخشة" }, { "PSV Eindhoven", "آيندهوفن" }, { "PSV", "آيندهوفن" },
  { "Ajax Amsterdam", "أياكس" }, { "Ajax", "أياكس" }, { "Feyenoord Rotterdam", "فينورد" }, { "Feyenoord", "فينورد" },
  { "Club Brugge", "كلوب بروج" }, { "Celtic", "سلتيك" }, { "Shakhtar Donetsk", "شاختار دونيتسك" }, { "Shakhtar", "شاختار" },
  // National teams
  { "Saudi Arabia", "السعودية" }, { "Egypt", "مصر" }, { "Morocco", "المغرب" }, { "Tunisia", "تونس" }, { "Algeria", "الجزائر" },
  { "Qatar", "قطر" }, { "United Arab Emirates", "الإمارات" }, { "Iraq", "العراق" }, { "Jordan", "الأردن" }, { "Kuwait", "الكويت" },
  { "Oman", "عُمان" }, { "Bahrain", "البحرين" }, { "Syria", "سوريا" }, { "Lebanon", "لبنان" }, { "Palestine", "فلسطين" },
  { "Libya", "ليبيا" }, { "Sudan", "السودان" }, { "Yemen", "اليمن" }, { "Argentina", "الأرجنتين" }, { "Brazil", "البرازيل" },
  { "France", "فرنسا" }, { "Germany", "ألمانيا" }, { "Spain", "إسبانيا" }, { "England", "إنجلترا" }, { "Portugal", "البرتغال" },
  { "Italy", "إيطاليا" }, { "Netherlands", "هولندا" }, { "Belgium", "بلجيكا" }, { "Croatia", "كرواتيا" }, { "Japan", "اليابان" },
  { "South Korea", "كوريا الجنوبية" }, { "Iran", "إيران" }, { "Australia", "أستراليا" }, { "USA", "الولايات المتحدة" },
  { "United States", "الولايات المتحدة" }, { "Mexico", "المكسيك" }, { "Senegal", "السنغال" }, { "Nigeria", "نيجيريا" },
  // NBA (short names)
  { "Los Angeles Lakers", "لوس أنجلوس ليكرز" }, { "Lakers", "ليكرز" }, { "Golden State Warriors", "غولدن ستايت ووريرز" },
  { "Warriors", "ووريرز" }, { "Boston Celtics", "بوسطن سلتيكس" }, { "Celtics", "سلتيكس" }, { "Chicago Bulls", "شيكاغو بولز" },
  { "Bulls", "بولز" }, { "Miami Heat", "ميامي هيت" }, { "Heat", "هيت" }, { "New York Knicks", "نيويورك نيكس" }, { "Knicks", "نيكس" },
  { "Dallas Mavericks", "دالاس مافريكس" }, { "Mavericks", "مافريكس" }, { "Denver Nuggets", "دنفر ناغتس" }, { "Nuggets", "ناغتس" },
  { "Milwaukee Bucks", "ميلووكي باكس" }, { "Bucks", "باكس" }, { "Oklahoma City Thunder", "أوكلاهوما سيتي ثاندر" },
  { "Thunder", "ثاندر" }, { "Phoenix Suns", "فينيكس صنز" }, { "Suns", "صنز" }, { "LA Clippers", "لوس أنجلوس كليبرز" },
  { "Clippers", "كليبرز" }, { "San Antonio Spurs", "سان أنطونيو سبيرز" },
};
static const Par L[] = {
  { "Saudi Pro League", "دوري روشن السعودي" }, { "Premier League", "الدوري الإنجليزي الممتاز" }, { "LaLiga", "الدوري الإسباني" },
  { "Serie A", "الدوري الإيطالي" }, { "Bundesliga", "الدوري الألماني" }, { "Ligue 1", "الدوري الفرنسي" },
  { "UEFA Champions League", "دوري أبطال أوروبا" }, { "UEFA Europa League", "الدوري الأوروبي" },
  { "AFC Champions League", "دوري أبطال آسيا" }, { "FIFA World Cup", "كأس العالم" },
  { "Egyptian Premier League", "الدوري المصري الممتاز" }, { "NBA", "NBA" }, { "WNBA", "WNBA" },
};

static const char *achar(const Par *t, size_t n, const char *en) {
  if (!en || !en[0]) return NULL;
  for (size_t i = 0; i < n; i++) if (!strcasecmp(t[i].en, en)) return t[i].ar;
  return NULL;
}
const char *esporte_nome_ar(const char *en) { return achar(T, sizeof T / sizeof *T, en); }
const char *esporte_liga_ar(const char *en) { return achar(L, sizeof L / sizeof *L, en); }

int ajustes_idioma(void);
#include "idiomacod.h"
static int emArabe(void) { const char *c = idioma_iso(ajustes_idioma()); return c && !strcmp(c, "ar"); }
const char *esporte_nome_tela(const char *en) {
  const char *ar = emArabe() ? esporte_nome_ar(en) : NULL;
  return ar ? ar : en;
}
const char *esporte_liga_tela(const char *en) {
  const char *ar = emArabe() ? esporte_liga_ar(en) : NULL;
  return ar ? ar : en;
}
