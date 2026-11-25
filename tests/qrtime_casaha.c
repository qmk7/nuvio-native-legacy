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
  {
    const char *fm = "{\"metas\":[{\"imdb_id\":\"tt9000001\",\"name\":\"Friends\",\"imdbRating\":\"5.1\"}]}";
    const char *fs = "{\"metas\":[{\"imdb_id\":\"tt0108778\",\"name\":\"Friends\",\"imdbRating\":\"8.9\"}]}";
    const char *im = "{\"metas\":[{\"imdb_id\":\"tt1375666\",\"name\":\"Inception\",\"imdbRating\":\"8.8\"}]}";
    const char *is = "{\"metas\":[{\"imdb_id\":\"tt7777777\",\"name\":\"Inception Stories\"}]}";
    CONFERE(casaha_entre_tipos(fm, fs, "Friends", id, sizeof id) && !strcmp(id, "tt0108778"), "film and show with the same name: the famous one (Friends, the show)");
    CONFERE(casaha_entre_tipos(im, is, "Inception", id, sizeof id) && !strcmp(id, "tt1375666"), "only the film is exact: the film");
    CONFERE(casaha_entre_tipos(NULL, fs, "Friends", id, sizeof id) && !strcmp(id, "tt0108778"), "shows only (Alexa said a season or episode)");
  }
  {
    char a[128], b[128], c2[128];
    casaha_normalizar("Bab El Hara", a, sizeof a);
    casaha_normalizar("Bab Al-Hara", b, sizeof b);
    casaha_normalizar("bab al harah", c2, sizeof c2);
    CONFERE(!strcmp(a, b) && !strcmp(b, c2), "Bab El Hara = Bab Al-Hara = bab al harah");
    casaha_normalizar("The Godfather", a, sizeof a); casaha_normalizar("Godfather", b, sizeof b);
    CONFERE(!strcmp(a, b), "\"the\" ignored");
    const char *jb = "{\"metas\":[{\"imdb_id\":\"tt0499545\",\"name\":\"Bab Al-Hara\",\"imdbRating\":\"7.9\"}]}";
    CONFERE(casaha_entre_tipos(NULL, jb, "Bab El Hara", id, sizeof id) && !strcmp(id, "tt0499545"), "English spelling finds the Arabic series");
  }
  return falhas ? 1 : 0;
}
