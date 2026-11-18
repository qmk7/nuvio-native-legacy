#include "mcdec.h"
#include <stdlib.h>
#include <string.h>

#if defined(NV_LINUX_DESKTOP) || defined(MC_DEC_TESTE)
#include "md5.h"
#include <stdio.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "vendor/dr_libs/dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "vendor/dr_libs/dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "vendor/dr_libs/dr_mp3.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

struct McDec {
  int tipo;            // 1 flac, 2 wav, 3 mp3
  void *buf;           // our copy of the file
  drflac *flac;
  drwav wav;
  drmp3 mp3;
  int canais;
  float *tmp; long tmpCap;
};
int mcdec_disponivel(void) { return 1; }

McDec *mcdec_abrir(const void *dados, size_t n, McDecInfo *info) {
  McDec *d = calloc(1, sizeof *d);
  if (!d || !(d->buf = malloc(n ? n : 1))) { free(d); return NULL; }
  memcpy(d->buf, dados, n);
  if (info) memset(info, 0, sizeof *info);
  if (n >= 4 && !memcmp(dados, "fLaC", 4) && (d->flac = drflac_open_memory(d->buf, n, NULL))) {
    d->tipo = 1; d->canais = d->flac->channels;
    if (info) { snprintf(info->codec, sizeof info->codec, "FLAC"); info->taxa = (int)d->flac->sampleRate; info->bits = d->flac->bitsPerSample;
                info->canais = d->flac->channels; info->frames = (long long)d->flac->totalPCMFrameCount; }
    return d;
  }
  if (n >= 12 && !memcmp(dados, "RIFF", 4) && drwav_init_memory(&d->wav, d->buf, n, NULL)) {
    d->tipo = 2; d->canais = d->wav.channels;
    if (info) { snprintf(info->codec, sizeof info->codec, "PCM"); info->taxa = (int)d->wav.sampleRate; info->bits = d->wav.bitsPerSample;
                info->canais = d->wav.channels; info->frames = (long long)d->wav.totalPCMFrameCount; }
    return d;
  }
  if (drmp3_init_memory(&d->mp3, d->buf, n, NULL)) {
    d->tipo = 3; d->canais = (int)d->mp3.channels;
    if (info) { snprintf(info->codec, sizeof info->codec, "MP3"); info->taxa = (int)d->mp3.sampleRate; info->bits = 0;
                info->canais = (int)d->mp3.channels; info->frames = (long long)drmp3_get_pcm_frame_count(&d->mp3); }
    return d;
  }
  free(d->buf);
  free(d);
  return NULL;
}
long mcdec_ler(McDec *d, int32_t *out, long frames) {
  if (!d || frames <= 0) return 0;
  if (d->tipo == 1) return (long)drflac_read_pcm_frames_s32(d->flac, (drflac_uint64)frames, out);
  if (d->tipo == 2) return (long)drwav_read_pcm_frames_s32(&d->wav, (drwav_uint64)frames, out);
  if (d->tipo == 3) {
    long need = frames * d->canais, got;
    if (d->tmpCap < need) { float *t = realloc(d->tmp, sizeof(float) * (size_t)need); if (!t) return 0; d->tmp = t; d->tmpCap = need; }
    got = (long)drmp3_read_pcm_frames_f32(&d->mp3, (drmp3_uint64)frames, d->tmp);
    for (long i = 0; i < got * d->canais; i++) {
      float v = d->tmp[i];
      if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
      out[i] = (int32_t)(v * 2147483647.0f);
    }
    return got;
  }
  return 0;
}
int mcdec_buscar(McDec *d, long long frame) {
  if (!d || frame < 0) return 0;
  if (d->tipo == 1) return drflac_seek_to_pcm_frame(d->flac, (drflac_uint64)frame);
  if (d->tipo == 2) return drwav_seek_to_pcm_frame(&d->wav, (drwav_uint64)frame);
  if (d->tipo == 3) return drmp3_seek_to_pcm_frame(&d->mp3, (drmp3_uint64)frame);
  return 0;
}
void mcdec_fechar(McDec *d) {
  if (!d) return;
  if (d->tipo == 1) drflac_close(d->flac);
  else if (d->tipo == 2) drwav_uninit(&d->wav);
  else if (d->tipo == 3) drmp3_uninit(&d->mp3);
  free(d->tmp);
  free(d->buf);
  free(d);
}

// MD5 context fed incrementally: md5.c is one-shot, so collect the PCM bytes.
int mcdec_flac_md5_confere(const void *dados, size_t n) {
  const unsigned char *b = dados;
  unsigned char alvo[16], zero[16] = { 0 }, got[16];
  McDecInfo in;
  McDec *d;
  unsigned char *pcm = NULL;
  size_t np = 0, cap = 0;
  int32_t blk[4096 * 8];
  int bytes, ok;
  long k;
  if (n < 42 || memcmp(b, "fLaC", 4) || (b[4] & 0x7f) != 0) return -1;
  memcpy(alvo, b + 8 + 18, 16);
  if (!memcmp(alvo, zero, 16)) return -1;
  if (!(d = mcdec_abrir(dados, n, &in)) || in.canais > 8) { mcdec_fechar(d); return 0; }
  bytes = (in.bits + 7) / 8;
  while ((k = mcdec_ler(d, blk, 4096)) > 0) {
    size_t add = (size_t)k * (size_t)in.canais * (size_t)bytes;
    if (np + add > cap) { size_t nc = (np + add) * 2; unsigned char *t = realloc(pcm, nc); if (!t) { free(pcm); mcdec_fechar(d); return 0; } pcm = t; cap = nc; }
    for (long i = 0; i < k * in.canais; i++) {
      int32_t v = blk[i] >> (32 - in.bits);   // back to the source's own width
      for (int j = 0; j < bytes; j++) pcm[np++] = (unsigned char)((uint32_t)v >> (8 * j));
    }
  }
  mcdec_fechar(d);
  md5_bytes(pcm, np, got);
  free(pcm);
  ok = !memcmp(got, alvo, 16);
  return ok;
}
#else
// LG webOS / other hosts: decoding is the TV media pipeline's job.
int    mcdec_disponivel(void) { return 0; }
McDec *mcdec_abrir(const void *dados, size_t n, McDecInfo *info) { (void)dados; (void)n; if (info) memset(info, 0, sizeof *info); return NULL; }
long   mcdec_ler(McDec *d, int32_t *out, long frames) { (void)d; (void)out; (void)frames; return 0; }
int    mcdec_buscar(McDec *d, long long frame) { (void)d; (void)frame; return 0; }
void   mcdec_fechar(McDec *d) { (void)d; }
int    mcdec_flac_md5_confere(const void *dados, size_t n) { (void)dados; (void)n; return -1; }
#endif
