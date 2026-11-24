// casaha.c: Cinemeta answer -> the IMDb id Home Assistant's "play" opens.
#include "casaha.h"
#include <stdint.h>
#include <stdio.h>
// Links the IPTV library for its title normaliser: its profile hooks stubbed.
uint64_t perfis_sessao_geracao(void) { return 1; }
int perfis_sessao_atual(uint64_t s) { return s == 1; }
int perfis_sessao_travar(uint64_t s) { return s == 1; }
void perfis_sessao_destravar(void) {}
int perfis_ativo(void) { return 1; }
int ajustes_livetv_formato(void) { return 0; }
const char *i18n(const char *s) { return s; }
#include <string.h>
static int falhas;
#define CONFERE(c, m) do { if (c) printf("PASS: %s\n", m); else { printf("FAIL: %s\n", m); falhas++; } } while (0)
int main(void) {
  char id[24];
  const char *j = "{\"metas\":[{\"imdb_id\":\"tt0000002\",\"name\":\"Inception: The Cobol Job\"},"
                  "{\"imdb_id\":\"tt1375666\",\"name\":\"Inception\"},{\"id\":\"kitsu:9\",\"name\":\"Inception\"}]}";
  CONFERE(casaha_escolher(j, "inception", id, sizeof id) && !strcmp(id, "tt1375666"), "the exact title wins over the first result");
  CONFERE(casaha_escolher(j, "the cobol job", id, sizeof id) && !strcmp(id, "tt0000002"), "no exact title: Cinemeta's first (most popular)");
  CONFERE(!casaha_escolher("{\"metas\":[{\"id\":\"kitsu:9\",\"name\":\"X\"}]}", "x", id, sizeof id), "never a non-IMDb id");
  CONFERE(!casaha_escolher(NULL, "x", id, sizeof id) && !casaha_escolher("{}", "x", id, sizeof id), "no answer: nothing to play");
  return falhas ? 1 : 0;
}
