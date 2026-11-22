// QR TIME voice intents (src/intencao.c).
//   bash tests/qrtime_intencao.sh
#include "intencao.h"
#include <stdio.h>
#include <string.h>
static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
int main(void) {
  char b[200];
  CONFERE(intencao_analisar("شغّل مسلسل الهيبة", b, sizeof b) == INT_BUSCA && !strcmp(b, "مسلسل الهيبة"), "شغّل X (with shadda) -> search X");
  CONFERE(intencao_analisar("ابحث عن Breaking Bad", b, sizeof b) == INT_BUSCA && !strcmp(b, "Breaking Bad"), "ابحث عن X -> search X, original spelling");
  CONFERE(intencao_analisar("Play The Office", b, sizeof b) == INT_BUSCA && !strcmp(b, "The Office"), "play X (English)");
  CONFERE(intencao_analisar("مركز الرياضة", b, sizeof b) == INT_ESPORTES && intencao_analisar("افتح الرياضة", b, sizeof b) == INT_ESPORTES, "Sport Center");
  CONFERE(intencao_analisar("الموسيقى", b, sizeof b) == INT_MUSICA && intencao_analisar("الأغاني", b, sizeof b) == INT_MUSICA, "Music (hamza folded)");
  CONFERE(intencao_analisar("البث المباشر", b, sizeof b) == INT_AOVIVO && intencao_analisar("live TV", b, sizeof b) == INT_AOVIVO, "Live TV");
  CONFERE(intencao_analisar("ماذا أشاهد الليلة؟", b, sizeof b) == INT_SUGESTAO && intencao_analisar("وش أشوف", b, sizeof b) == INT_SUGESTAO, "what should I watch (MSA and Saudi)");
  CONFERE(intencao_analisar("الإعدادات", b, sizeof b) == INT_AJUSTES, "settings");
  CONFERE(intencao_analisar("الهيبة", b, sizeof b) == INT_NADA && intencao_analisar("Breaking Bad", b, sizeof b) == INT_NADA, "a title alone stays a plain search");
  CONFERE(intencao_analisar("فيلم عن الرياضة", b, sizeof b) == INT_NADA, "a sentence that only mentions sport is a search");
  CONFERE(intencao_analisar("شغل", b, sizeof b) == INT_NADA && intencao_analisar("", b, sizeof b) == INT_NADA, "a command word alone: nothing");
  return falhas ? 1 : 0;
}
