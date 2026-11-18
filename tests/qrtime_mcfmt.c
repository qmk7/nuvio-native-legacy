// Music Center: format probing (src/mcfmt.c) and software decoding
// (src/mcdec.c, dr_libs) on REAL files made by ffmpeg in qrtime_mcfmt.sh.
//
//   qrtime_mcfmt <fixture dir>
#include "mcfmt.h"
#include "mcdec.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

static unsigned char *ler(const char *dir, const char *nome, size_t *n) {
  char c[600];
  FILE *f;
  unsigned char *b;
  long t;
  snprintf(c, sizeof c, "%s/%s", dir, nome);
  if (!(f = fopen(c, "rb"))) { *n = 0; return NULL; }
  fseek(f, 0, SEEK_END); t = ftell(f); fseek(f, 0, SEEK_SET);
  b = malloc((size_t)t + 1);
  *n = fread(b, 1, (size_t)t, f);
  fclose(f);
  return b;
}
// Probe as the app does: first 64 KiB, then as much as the parser asks.
static int sondar(const char *dir, const char *nome, McFmt *m, McFaixa *f) {
  size_t n, pede = 65536, prec;
  unsigned char *b = ler(dir, nome, &n);
  int t;
  memset(f, 0, sizeof *f);
  if (!b) return -1;
  t = mcfmt_ler(b, n < pede ? n : pede, (long long)n, m, f, &prec);
  if (prec && prec != (size_t)-1) { memset(f, 0, sizeof *f); t = mcfmt_ler(b, prec < n ? prec : n, (long long)n, m, f, &prec); }
  free(b);
  return t;
}

int main(int argc, char **argv) {
  const char *d = argc > 1 ? argv[1] : ".";
  McFmt m;
  McFaixa f;
  char desc[96];

  CONFERE(sondar(d, "a16.flac", &m, &f) == MCF_FLAC && m.taxa == 44100 && m.bits == 16 && m.canais == 2 && m.completo,
          "FLAC 16-bit / 44.1 kHz stereo from STREAMINFO");
  CONFERE(f.dur == 5 && !strcmp(f.titulo, "Test Tone") && !strcmp(f.artista, "QRT Artist") && !strcmp(f.album, "QRT Album")
          && f.faixa == 3 && f.ano == 2024 && !strcmp(f.genero, "Jazz"), "FLAC Vorbis comments and duration");
  mcfmt_descrever(&f, desc, sizeof desc);
  CONFERE(!strcmp(desc, "FLAC 16-bit / 44.1 kHz · 2.0"), "description: FLAC 16-bit / 44.1 kHz · 2.0");
  CONFERE(sondar(d, "a24.flac", &m, &f) == MCF_FLAC && m.taxa == 96000 && m.bits == 24 && m.canais == 2, "FLAC 24-bit / 96 kHz");
  CONFERE(sondar(d, "a24art.flac", &m, &f) == MCF_FLAC && m.bits == 24 && !strcmp(f.titulo, "With Cover"),
          "FLAC with a 200 KiB cover picture: tags still read (second read)");
  CONFERE(sondar(d, "a51.flac", &m, &f) == MCF_FLAC && m.canais == 6 && m.taxa == 48000, "FLAC 5.1 (6 channels) / 48 kHz");
  CONFERE(sondar(d, "a24.wav", &m, &f) == MCF_WAV && m.taxa == 48000 && m.bits == 24 && m.canais == 2 && f.dur == 5
          && !strcmp(m.codec, "PCM"), "WAV 24-bit / 48 kHz PCM (extensible)");
  CONFERE(sondar(d, "a16.aiff", &m, &f) == MCF_AIFF && m.taxa == 44100 && m.bits == 16 && f.dur == 5, "AIFF 16-bit / 44.1 kHz");
  CONFERE(sondar(d, "a320.mp3", &m, &f) == MCF_MP3 && m.taxa == 44100 && m.kbps == 320 && m.bits == 0
          && !strcmp(f.titulo, "MP3 Title") && !strcmp(f.artista, "MP3 Artist") && f.faixa == 7, "MP3 320 kbps CBR + ID3v2 tags; no fake bit depth");
  CONFERE(f.dur >= 4 && f.dur <= 5, "MP3 duration");
  mcfmt_descrever(&f, desc, sizeof desc);
  CONFERE(!strcmp(desc, "MP3 320 kbps · 44.1 kHz · 2.0"), "description: MP3 320 kbps · 44.1 kHz · 2.0");
  CONFERE(sondar(d, "aac.m4a", &m, &f) == MCF_M4A && !strcmp(m.codec, "AAC") && m.taxa == 44100 && m.canais == 2
          && !strcmp(f.titulo, "AAC Title") && f.dur == 5, "M4A AAC (fast start): codec, rate, tags, duration");
  CONFERE(sondar(d, "alac.m4a", &m, &f) == MCF_M4A && !strcmp(m.codec, "ALAC") && m.bits == 24 && m.taxa == 96000
          && !strcmp(f.formato, "alac"), "M4A ALAC 24-bit / 96 kHz from the ALAC cookie");
  {
    size_t prec = 0, n;
    unsigned char *b = ler(d, "aac_tail.m4a", &n);
    memset(&f, 0, sizeof f);
    mcfmt_ler(b, 65536 < n ? 65536 : n, (long long)n, &m, &f, &prec);
    CONFERE(!m.completo && prec == (size_t)-1, "M4A with moov at the end: reported as needing the tail, not guessed");
    free(b);
  }
  {
    unsigned char lixo[64] = "this is not audio at all, just text";
    CONFERE(mcfmt_ler(lixo, sizeof lixo, 64, &m, &f, NULL) == MCF_DESCONHECIDO, "garbage: unknown");
  }

  // ---- decoding (desktop / tests) ----
  {
    size_t n;
    unsigned char *b;
    McDecInfo in;
    McDec *dec;
    int32_t buf[4096 * 2];
    long tot = 0, k;
    b = ler(d, "a16.flac", &n);
    CONFERE(mcdec_flac_md5_confere(b, n) == 1, "FLAC 16-bit decode is bit-exact (STREAMINFO MD5)");
    free(b);
    b = ler(d, "a24.flac", &n);
    CONFERE(mcdec_flac_md5_confere(b, n) == 1, "FLAC 24-bit / 96 kHz decode is bit-exact (STREAMINFO MD5)");
    free(b);
    b = ler(d, "a51.flac", &n);
    CONFERE(mcdec_flac_md5_confere(b, n) == 1, "FLAC 5.1 decode is bit-exact (STREAMINFO MD5)");
    {   // a corrupted byte in the audio must fail the check
      b[n - 100] ^= 0x55;
      CONFERE(mcdec_flac_md5_confere(b, n) != 1, "a corrupted FLAC fails the MD5 check");
    }
    free(b);
    b = ler(d, "a24.wav", &n);
    dec = mcdec_abrir(b, n, &in);
    CONFERE(dec && in.bits == 24 && in.taxa == 48000 && in.frames == 240000, "WAV decoder: 24-bit, 48 kHz, 240000 frames");
    while ((k = mcdec_ler(dec, buf, 4096)) > 0) tot += k;
    CONFERE(tot == 240000, "WAV: every frame decoded");
    {   // 24-bit PCM survives: the low byte of a 24-bit sample is not zeroed
      int baixo = 0;
      mcdec_buscar(dec, 1000);
      k = mcdec_ler(dec, buf, 1000);
      for (long i = 0; i < k * 2; i++) baixo |= (buf[i] >> 8) & 0xFF;
      CONFERE(baixo != 0, "24-bit samples keep their low byte (no truncation to 16-bit)");
    }
    mcdec_fechar(dec);
    free(b);
    b = ler(d, "a320.mp3", &n);
    dec = mcdec_abrir(b, n, &in);
    tot = 0;
    while (dec && (k = mcdec_ler(dec, buf, 4096)) > 0) tot += k;
    CONFERE(dec && in.taxa == 44100 && tot > 44100 * 4 && tot < 44100 * 6, "MP3 decoder: ~5 s at 44.1 kHz");
    mcdec_fechar(dec);
    free(b);
  }
  return falhas ? 1 : 0;
}
