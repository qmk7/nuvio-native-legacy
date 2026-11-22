// QR TIME Arabic subtitles by DeepL (src/legtrad.c): request body, DeepL's
// documented answer, SRT rebuilt with the same timing, free vs pro host.
//   bash tests/qrtime_legtrad.sh
#include "legtrad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
char *rede_postar_st(const char *u, int s, const char *const *c, const char *b, int *st) { (void)u; (void)s; (void)c; (void)b; *st = 0; return NULL; }
char *dados_ler(const char *n) { (void)n; return NULL; }
int dados_gravar(const char *n, const char *c) { (void)n; (void)c; return 1; }
int dados_apagar(const char *n) { (void)n; return 1; }
int legenda_copiar_cues(LegendaCue **s) { *s = NULL; return 0; }
static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
int main(void) {
  LegendaCue c[2];
  char tr[4][768], *b, *srt;
  memset(c, 0, sizeof c);
  c[0].inicio = 61.5; c[0].fim = 63.25; snprintf(c[0].texto, sizeof c[0].texto, "Where are you going?");
  c[1].inicio = 3725.0; c[1].fim = 3727.0; snprintf(c[1].texto, sizeof c[1].texto, "Home & <i>fast</i>");
  b = legtrad_corpo(c, 2);
  CONFERE(b && strstr(b, "target_lang=AR") && strstr(b, "&text=Where%20are%20you%20going%3F") && strstr(b, "&text=Home%20%26%20%3Ci%3Efast%3C%2Fi%3E"),
          "request: Arabic target, each line url-encoded (tags kept for DeepL's xml handling)");
  free(b);
  CONFERE(legtrad_parse("{\"translations\":[{\"detected_source_language\":\"EN\",\"text\":\"إلى أين أنت ذاهب؟\"},{\"detected_source_language\":\"EN\",\"text\":\"المنزل و <i>بسرعة</i>\"}]}", tr, 4) == 2
          && !strcmp(tr[0], "إلى أين أنت ذاهب؟"), "DeepL answer parsed, in order");
  CONFERE(legtrad_parse("{\"message\":\"Wrong endpoint\"}", tr, 4) == 0, "an error answer: nothing");
  snprintf(c[0].texto, sizeof c[0].texto, "%s", tr[0]); snprintf(c[1].texto, sizeof c[1].texto, "%s", tr[1]);
  srt = legtrad_srt(c, 2);
  CONFERE(srt && strstr(srt, "1\n00:01:01,500 --> 00:01:03,250\nإلى أين أنت ذاهب؟\n") && strstr(srt, "2\n01:02:05,000 --> 01:02:07,000\n"),
          "SRT rebuilt with the original timing");
  free(srt);
  CONFERE(strstr(legtrad_host("abc-123:fx"), "api-free.deepl.com") && strstr(legtrad_host("abc-123"), "//api.deepl.com"), "free keys (:fx) use the free host");
  CONFERE(!legtrad_definir_chave("bad key!") && legtrad_definir_chave("abc-123:fx"), "key characters checked");
  return falhas ? 1 : 0;
}
