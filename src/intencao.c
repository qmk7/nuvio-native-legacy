// QR TIME VOICE INTENTS: what the person said into the remote's microphone
// (the TV's own speech recognition, sistexto.h) is read for a few commands
// before it becomes a search. Arabic first, English and Portuguese too:
//   "شغّل / شغل / ابحث عن / play / search / tocar <x>"  -> search for <x>
//   "الرياضة / مركز الرياضة / sports / esportes"        -> Sport Center
//   "الموسيقى / الأغاني / music / música"              -> Music
//   "البث المباشر / القنوات / live tv / canais"          -> Live TV
//   "ماذا أشاهد / اقترح / what should I watch / sugere" -> tonight's suggestion
//   "الإعدادات / settings / ajustes"                    -> Settings
// Anything else stays a plain search. Arabic is folded first (diacritics
// out, alef/yaa/taa marbuta forms unified) so spelling variants match.
#include "intencao.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// Fold: ASCII lower-case; Arabic diacritics (U+064B..U+0652, U+0640 tatweel)
// removed; أ إ آ -> ا, ى -> ي, ة -> ه; runs of spaces -> one.
static void dobrar(const char *s, char *d, size_t n) {
  size_t k = 0;
  const unsigned char *p = (const unsigned char *)s;
  while (*p && k + 3 < n) {
    if (p[0] == 0xD9 && ((p[1] >= 0x8B && p[1] <= 0x92) || p[1] == 0x80)) { p += 2; continue; }
    if (p[0] == 0xD8 && (p[1] == 0xA3 || p[1] == 0xA5 || p[1] == 0xA2)) { d[k++] = (char)0xD8; d[k++] = (char)0xA7; p += 2; continue; }
    if (p[0] == 0xD9 && p[1] == 0x89) { d[k++] = (char)0xD9; d[k++] = (char)0x8A; p += 2; continue; }
    if (p[0] == 0xD8 && p[1] == 0xA9) { d[k++] = (char)0xD9; d[k++] = (char)0x87; p += 2; continue; }
    if (*p < 0x80) {
      char c = (char)tolower(*p);
      if (isspace(*p) || ispunct(*p)) c = ' ';
      if (c == ' ' && (!k || d[k - 1] == ' ')) { p++; continue; }
      d[k++] = c; p++; continue;
    }
    d[k++] = (char)*p++;
  }
  while (k && d[k - 1] == ' ') k--;
  d[k] = 0;
}
static int comeca(const char *t, const char *pre) { size_t l = strlen(pre); return !strncmp(t, pre, l) && (t[l] == ' ' || !t[l]); }
static int tem(const char *t, const char *frase) { return strstr(t, frase) != NULL; }

int intencao_analisar(const char *texto, char *busca, size_t n) {
  char t[400], p[400];
  static const char *const BUSCA[] = { "شغل", "ابحث عن", "ابحث", "دور على", "دور", "افتح فيلم", "افتح مسلسل",
                                       "play", "search for", "search", "find", "tocar", "toque", "procurar", "buscar", NULL };
  if (busca && n) busca[0] = 0;
  if (!texto || !texto[0]) return INT_NADA;
  dobrar(texto, t, sizeof t);
  // Commands first (whole phrases anywhere in what was said).
  dobrar("ماذا اشاهد", p, sizeof p); if (tem(t, p)) return INT_SUGESTAO;
  dobrar("وش اشوف", p, sizeof p); if (tem(t, p)) return INT_SUGESTAO;
  dobrar("اقترح", p, sizeof p); if (tem(t, p)) return INT_SUGESTAO;
  if (tem(t, "what should i watch") || tem(t, "suggest") || tem(t, "sugere") || tem(t, "o que assistir")) return INT_SUGESTAO;
  dobrar("مركز الرياضة", p, sizeof p); if (tem(t, p)) return INT_ESPORTES;
  dobrar("الرياضة", p, sizeof p); if (!strcmp(t, p)) return INT_ESPORTES;
  dobrar("افتح الرياضة", p, sizeof p); if (!strcmp(t, p)) return INT_ESPORTES;
  if (!strcmp(t, "sports") || !strcmp(t, "sport center") || !strcmp(t, "esportes") || !strcmp(t, "centro de esportes")) return INT_ESPORTES;
  dobrar("الموسيقى", p, sizeof p); if (!strcmp(t, p)) return INT_MUSICA;
  dobrar("الاغاني", p, sizeof p); if (!strcmp(t, p)) return INT_MUSICA;
  dobrar("افتح الموسيقى", p, sizeof p); if (!strcmp(t, p)) return INT_MUSICA;
  if (!strcmp(t, "music") || !strcmp(t, "musica") || !strcmp(t, "música")) return INT_MUSICA;
  dobrar("البث المباشر", p, sizeof p); if (tem(t, p)) return INT_AOVIVO;
  dobrar("القنوات", p, sizeof p); if (!strcmp(t, p)) return INT_AOVIVO;
  if (!strcmp(t, "live tv") || !strcmp(t, "tv ao vivo") || !strcmp(t, "canais")) return INT_AOVIVO;
  dobrar("الاعدادات", p, sizeof p); if (!strcmp(t, p)) return INT_AJUSTES;
  if (!strcmp(t, "settings") || !strcmp(t, "ajustes")) return INT_AJUSTES;
  // "play X" / "شغل X": the rest is the search.
  for (int i = 0; BUSCA[i]; i++) {
    dobrar(BUSCA[i], p, sizeof p);
    if (comeca(t, p) && t[strlen(p)] == ' ') {
      const char *r = texto;
      // Same number of words off the ORIGINAL text (keeps its spelling).
      {
        int palavras = 1;
        for (int w = 0; p[w]; w++) if (p[w] == ' ') palavras++;
        while (palavras-- > 0) { while (*r && *r != ' ') r++; while (*r == ' ') r++; }
      }
      if (busca && n) snprintf(busca, n, "%s", r);
      return busca && busca[0] ? INT_BUSCA : INT_NADA;
    }
  }
  return INT_NADA;
}
