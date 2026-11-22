// QR TIME "what's this song?" (src/songid.c): ~8 s of the playing file's
// audio, from MKV files made with ffmpeg (AC-3 + AAC tracks, AAC only,
// E-AC-3), checked by ffprobe; an MP4 is refused; AudD answers parsed.
// The network is replaced by a local file read (Range semantics kept).
//   bash tests/qrtime_songid.sh <dir-with-a.mkv-b.mkv-c.mkv-d.mp4>
#include "songid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- stubs: Range reads from a local file, no data folder
char *rede_baixar_trecho_st(const char *url, int s, long long ini, long long fim, long *tam, int *st, int *er, char *f, unsigned tf) {
  FILE *a = fopen(url, "rb");
  char *b;
  long n;
  (void)s; (void)f; (void)tf;
  *tam = 0; if (st) *st = 0; if (er) *er = 0;
  if (!a) return NULL;
  fseek(a, 0, SEEK_END);
  if (ini >= ftell(a)) { fclose(a); if (st) *st = 416; return NULL; }
  if (fim >= ftell(a)) fim = ftell(a) - 1;
  b = malloc((size_t)(fim - ini + 1));
  fseek(a, (long)ini, SEEK_SET);
  n = (long)fread(b, 1, (size_t)(fim - ini + 1), a);
  fclose(a);
  *tam = n; if (st) *st = 206;
  return b;
}
char *rede_postar_st(const char *u, int s, const char *const *c, const char *b, int *st) { (void)u; (void)s; (void)c; (void)b; *st = 0; return NULL; }
char *dados_ler(const char *n) { (void)n; return NULL; }
int dados_gravar(const char *n, const char *c) { (void)n; (void)c; return 1; }
int dados_apagar(const char *n) { (void)n; return 1; }

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static double sonda(const char *arq, const char *fmt, char *codecVisto, size_t nc) {
  char cmd[600], r[200] = "";
  FILE *p;
  snprintf(cmd, sizeof cmd, "ffprobe -v error -f %s -show_entries stream=codec_name:format=duration -of default=nw=1:nk=1 %s 2>/dev/null | tr '\\n' ' '", fmt, arq);
  p = popen(cmd, "r");
  if (p) { if (!fgets(r, sizeof r, p)) r[0] = 0; pclose(p); }
  sscanf(r, "%31s", codecVisto); (void)nc;
  { char *sp = strchr(r, ' '); return sp ? atof(sp + 1) : 0; }
}
static void caso(const char *dir, const char *arq, const char *esperado, const char *fmt) {
  char url[400], cod[16], visto[32] = "", out[400], o[200];
  size_t n; int er;
  unsigned char *a;
  snprintf(url, sizeof url, "%s/%s", dir, arq);
  a = songid_amostra(url, 12.0, 8.0, &n, cod, sizeof cod, &er);
  snprintf(o, sizeof o, "%s: %s sample at 12 s", arq, esperado);
  CONFERE(a && er == SID_OK && !strcmp(cod, esperado), o);
  if (!a) return;
  snprintf(out, sizeof out, "%s/out-%s.%s", dir, arq, cod);
  { FILE *f = fopen(out, "wb"); fwrite(a, 1, n, f); fclose(f); }
  {
    double d = sonda(out, fmt, visto, sizeof visto);
    snprintf(o, sizeof o, "%s: ffprobe decodes it as %s, %.1f s (expect ~8)", arq, visto, d);
    CONFERE(!strcmp(visto, esperado) && d > 6.5 && d < 10.5, o);
  }
  free(a);
}
int main(int argc, char **argv) {
  SidMusica m;
  const char *dir = argc > 1 ? argv[1] : ".";
  caso(dir, "a.mkv", "aac", "aac");   // AC-3 and AAC tracks: AAC preferred (smallest)
  caso(dir, "b.mkv", "aac", "aac");
  caso(dir, "c.mkv", "eac3", "eac3");
  {
    char url[400]; size_t n; int er; char cod[16];
    snprintf(url, sizeof url, "%s/d.mp4", dir);
    CONFERE(!songid_amostra(url, 12.0, 8.0, &n, cod, sizeof cod, &er) && er == SID_NAO_MKV, "MP4: refused (not available for this source)");
  }
  CONFERE(songid_parse("{\"status\":\"success\",\"result\":{\"artist\":\"Fairuz\",\"title\":\"Li Beirut\",\"album\":\"X\",\"release_date\":\"1987\"}}", &m) == SID_OK
          && !strcmp(m.titulo, "Li Beirut") && !strcmp(m.artista, "Fairuz"), "AudD match parsed");
  CONFERE(songid_parse("{\"status\":\"success\",\"result\":null}", &m) == SID_NAO_ACHOU, "AudD: no match");
  CONFERE(songid_parse("{\"status\":\"error\",\"error\":{\"error_code\":900}}", &m) == SID_ERRO, "AudD: error (bad token)");
  return falhas ? 1 : 0;
}
