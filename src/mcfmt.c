#include "mcfmt.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
static unsigned le16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }

static void copia(char *d, size_t dn, const char *s, size_t sn) {
  size_t k = sn < dn - 1 ? sn : dn - 1;
  memcpy(d, s, k);
  d[k] = 0;
  while (k && (d[k - 1] == ' ' || d[k - 1] == 0)) d[--k] = 0;
}
static void anoDe(McFaixa *f, const char *v) { int a = atoi(v); if (a > 1000 && a < 3000) f->ano = a; }
static void numDe(int *dst, const char *v) { int a = atoi(v); if (a > 0) *dst = a; }
// One "KEY=value" (Vorbis comment) or one named tag.
static void tagNomeada(McFaixa *f, const char *k, const char *v, size_t vn) {
  char t[256];
  copia(t, sizeof t, v, vn);
  if (!strcasecmp(k, "TITLE")) snprintf(f->titulo, sizeof f->titulo, "%s", t);
  else if (!strcasecmp(k, "ARTIST")) { if (!f->artista[0]) snprintf(f->artista, sizeof f->artista, "%s", t); }
  else if (!strcasecmp(k, "ALBUMARTIST")) snprintf(f->artista, sizeof f->artista, "%s", t);
  else if (!strcasecmp(k, "ALBUM")) snprintf(f->album, sizeof f->album, "%s", t);
  else if (!strcasecmp(k, "TRACKNUMBER")) numDe(&f->faixa, t);
  else if (!strcasecmp(k, "DISCNUMBER")) numDe(&f->disco, t);
  else if (!strcasecmp(k, "DATE") || !strcasecmp(k, "YEAR")) anoDe(f, t);
  else if (!strcasecmp(k, "GENRE")) snprintf(f->genero, sizeof f->genero, "%s", t);
}
static void vorbis(const unsigned char *p, size_t n, McFaixa *f) {
  size_t i = 0;
  uint32_t vl, cnt;
  if (n < 8) return;
  vl = le32(p); i = 4 + vl;
  if (i + 4 > n) return;
  cnt = le32(p + i); i += 4;
  for (uint32_t c = 0; c < cnt && i + 4 <= n; c++) {
    uint32_t l = le32(p + i);
    const char *s = (const char *)p + i + 4, *eq;
    i += 4;
    if (l > n - i) break;
    eq = memchr(s, '=', l);
    if (eq && eq - s < 32) {
      char k[32];
      copia(k, sizeof k, s, (size_t)(eq - s));
      tagNomeada(f, k, eq + 1, l - (size_t)(eq - s) - 1);
    }
    i += l;
  }
}

// ---------------------------------------------------------------- FLAC
static int flac(const unsigned char *b, size_t n, McFmt *m, McFaixa *f, size_t *precisa) {
  size_t i = 4;
  m->tipo = MCF_FLAC;
  snprintf(m->codec, sizeof m->codec, "FLAC");
  while (i + 4 <= n) {
    int ultimo = b[i] & 0x80, tipo = b[i] & 0x7f;
    uint32_t len = (uint32_t)b[i + 1] << 16 | (uint32_t)b[i + 2] << 8 | b[i + 3];
    const unsigned char *d = b + i + 4;
    if (i + 4 + len > n) {
      // Stream info is the first block; tags may sit after a big picture.
      if (tipo == 6 || tipo == 4 || tipo == 1 || tipo == 3) *precisa = i + 4 + len + 4096;
      break;
    }
    if (tipo == 0 && len >= 18) {
      m->taxa = (int)((uint32_t)d[10] << 12 | (uint32_t)d[11] << 4 | d[12] >> 4);
      m->canais = ((d[12] >> 1) & 7) + 1;
      m->bits = (((d[12] & 1) << 4) | (d[13] >> 4)) + 1;
      m->amostras = (long long)(d[13] & 15) << 32 | be32(d + 14);
      m->completo = 1;
    } else if (tipo == 4) vorbis(d, len, f);
    i += 4 + len;
    if (ultimo) break;
  }
  return MCF_FLAC;
}

// ---------------------------------------------------------------- WAV / AIFF
static int wav(const unsigned char *b, size_t n, long long total, McFmt *m, McFaixa *f, size_t *precisa) {
  size_t i = 12;
  int bloco = 0;
  (void)f;
  m->tipo = MCF_WAV;
  while (i + 8 <= n) {
    uint32_t len = le32(b + i + 4);
    if (!memcmp(b + i, "fmt ", 4) && i + 8 + 16 <= n) {
      unsigned fmt = le16(b + i + 8);
      m->canais = (int)le16(b + i + 10);
      m->taxa = (int)le32(b + i + 12);
      bloco = (int)le16(b + i + 20);
      m->bits = (int)le16(b + i + 22);
      if (fmt == 0xFFFE && len >= 40 && i + 8 + 26 <= n) fmt = le16(b + i + 8 + 24);   // WAVE_FORMAT_EXTENSIBLE sub-format
      snprintf(m->codec, sizeof m->codec, "%s", fmt == 1 ? "PCM" : fmt == 3 ? "PCM float" : fmt == 0x55 ? "MP3" : "WAV");
      m->completo = 1;
    } else if (!memcmp(b + i, "data", 4)) {
      long long dl = len;
      if ((dl == 0 || dl == 0xFFFFFFFFLL) && total > (long long)i + 8) dl = total - (long long)i - 8;   // streamed WAV
      if (bloco > 0) m->amostras = dl / bloco;
      break;
    } else if (!memcmp(b + i, "LIST", 4) && i + 12 <= n && !memcmp(b + i + 8, "INFO", 4)) {
      size_t j = i + 12, fim = i + 8 + len;
      if (fim > n) fim = n;
      while (j + 8 <= fim) {
        uint32_t l2 = le32(b + j + 4);
        const char *v = (const char *)b + j + 8;
        if (j + 8 + l2 > fim) break;
        if (!memcmp(b + j, "INAM", 4)) tagNomeada(f, "TITLE", v, l2);
        else if (!memcmp(b + j, "IART", 4)) tagNomeada(f, "ARTIST", v, l2);
        else if (!memcmp(b + j, "IPRD", 4)) tagNomeada(f, "ALBUM", v, l2);
        else if (!memcmp(b + j, "ICRD", 4)) tagNomeada(f, "DATE", v, l2);
        else if (!memcmp(b + j, "IGNR", 4)) tagNomeada(f, "GENRE", v, l2);
        else if (!memcmp(b + j, "ITRK", 4)) tagNomeada(f, "TRACKNUMBER", v, l2);
        j += 8 + l2 + (l2 & 1);
      }
    }
    i += 8 + len + (len & 1);
  }
  if (!m->completo && i > n) *precisa = i + 64;
  return MCF_WAV;
}
// 80-bit IEEE extended (AIFF sample rate).
static int ext80(const unsigned char *p) {
  int e = ((p[0] & 0x7f) << 8 | p[1]) - 16383 - 31;
  uint32_t mant = be32(p + 2);
  if (e > 0 || e < -31) return 0;
  return (int)(mant >> -e);
}
static int aiff(const unsigned char *b, size_t n, McFmt *m) {
  size_t i = 12;
  m->tipo = MCF_AIFF;
  snprintf(m->codec, sizeof m->codec, "PCM");
  while (i + 8 <= n) {
    uint32_t len = be32(b + i + 4);
    if (!memcmp(b + i, "COMM", 4) && i + 8 + 18 <= n) {
      m->canais = (int)be16(b + i + 8);
      m->amostras = be32(b + i + 10);
      m->bits = (int)be16(b + i + 14);
      m->taxa = ext80(b + i + 16);
      m->completo = 1;
      break;
    }
    i += 8 + len + (len & 1);
  }
  return MCF_AIFF;
}

// ---------------------------------------------------------------- MP3
static void id3Texto(McFaixa *f, const char *id, const unsigned char *d, size_t n) {
  char t[256];
  size_t k = 0;
  if (n < 2) return;
  if (d[0] == 0 || d[0] == 3) copia(t, sizeof t, (const char *)d + 1, n - 1);   // ISO-8859-1 (approx.) / UTF-8
  else {   // UTF-16 with or without BOM: keep the ASCII/Latin-1 range, enough for tags here
    int le = 1;
    size_t i = 1;
    if (n >= 3 && d[1] == 0xFE && d[2] == 0xFF) { le = 0; i = 3; }
    else if (n >= 3 && d[1] == 0xFF && d[2] == 0xFE) i = 3;
    if (d[0] == 2) le = 0;
    for (; i + 1 < n && k + 4 < sizeof t; i += 2) {
      unsigned c = le ? (unsigned)d[i] | (unsigned)d[i + 1] << 8 : (unsigned)d[i] << 8 | d[i + 1];
      if (!c) break;
      if (c < 0x80) t[k++] = (char)c;
      else if (c < 0x800) { t[k++] = (char)(0xC0 | c >> 6); t[k++] = (char)(0x80 | (c & 63)); }
      else { t[k++] = (char)(0xE0 | c >> 12); t[k++] = (char)(0x80 | (c >> 6 & 63)); t[k++] = (char)(0x80 | (c & 63)); }
    }
    t[k] = 0;
  }
  if (!strcmp(id, "TIT2")) snprintf(f->titulo, sizeof f->titulo, "%s", t);
  else if (!strcmp(id, "TPE1")) { if (!f->artista[0]) snprintf(f->artista, sizeof f->artista, "%s", t); }
  else if (!strcmp(id, "TPE2")) snprintf(f->artista, sizeof f->artista, "%s", t);
  else if (!strcmp(id, "TALB")) snprintf(f->album, sizeof f->album, "%s", t);
  else if (!strcmp(id, "TRCK")) numDe(&f->faixa, t);
  else if (!strcmp(id, "TPOS")) numDe(&f->disco, t);
  else if (!strcmp(id, "TYER") || !strcmp(id, "TDRC")) anoDe(f, t);
  else if (!strcmp(id, "TCON")) snprintf(f->genero, sizeof f->genero, "%s", t[0] == '(' && strchr(t, ')') && strchr(t, ')')[1] ? strchr(t, ')') + 1 : t);
}
static int id3(const unsigned char *b, size_t n, McFaixa *f, size_t *fimTag) {
  int ver = b[3];
  uint32_t tam = (uint32_t)(b[6] & 127) << 21 | (uint32_t)(b[7] & 127) << 14 | (uint32_t)(b[8] & 127) << 7 | (b[9] & 127);
  size_t i = 10, fim = 10 + tam + ((b[5] & 0x10) ? 10 : 0);
  *fimTag = fim;
  if (ver < 3) return 0;   // ID3v2.2 (3-char frames): skip the tags, keep the stream info
  if (b[5] & 0x40 && i + 4 <= n) i += ver == 4 ? ((uint32_t)(b[10] & 127) << 21 | (uint32_t)(b[11] & 127) << 14 | (uint32_t)(b[12] & 127) << 7 | (b[13] & 127)) : be32(b + 10) + 4;
  while (i + 10 <= n && i + 10 <= 10 + tam) {
    char id[5];
    uint32_t l = ver == 4 ? ((uint32_t)(b[i + 4] & 127) << 21 | (uint32_t)(b[i + 5] & 127) << 14 | (uint32_t)(b[i + 6] & 127) << 7 | (b[i + 7] & 127)) : be32(b + i + 4);
    memcpy(id, b + i, 4); id[4] = 0;
    if (!id[0] || l == 0) break;
    if (i + 10 + l > n) break;
    if (id[0] == 'T') id3Texto(f, id, b + i + 10, l);
    i += 10 + l;
  }
  return 1;
}
static int mpegFrame(const unsigned char *p, McFmt *m) {
  static const int KB1[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };   // MPEG1 Layer III
  static const int KB2[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };       // MPEG2/2.5 Layer III
  static const int TX[3] = { 44100, 48000, 32000 };
  int v, camada, br, sr;
  if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return 0;
  v = (p[1] >> 3) & 3; camada = (p[1] >> 1) & 3; br = p[2] >> 4; sr = (p[2] >> 2) & 3;
  if (v == 1 || camada != 1 || br == 0 || br == 15 || sr == 3) return 0;
  m->taxa = TX[sr] / (v == 3 ? 1 : v == 2 ? 2 : 4);
  m->kbps = v == 3 ? KB1[br] : KB2[br];
  m->canais = ((p[3] >> 6) & 3) == 3 ? 1 : 2;
  m->bits = 0;   // a lossy stream has no bit depth
  // Xing/Info (VBR) frame count -> duration.
  {
    int lado = v == 3 ? (m->canais == 1 ? 17 : 32) : (m->canais == 1 ? 9 : 17);
    const unsigned char *x = p + 4 + lado;
    if (!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4)) {
      if (x[7] & 1) m->amostras = (long long)be32(x + 8) * (v == 3 ? 1152 : 576);
      m->kbps = 0;   // the first frame's rate is not the file's in VBR
    }
  }
  m->completo = 1;
  return 1;
}
static int mp3(const unsigned char *b, size_t n, long long total, McFmt *m, McFaixa *f, size_t *precisa) {
  size_t i = 0;
  m->tipo = MCF_MP3;
  snprintf(m->codec, sizeof m->codec, "MP3");
  if (n >= 10 && !memcmp(b, "ID3", 3)) {
    size_t fim;
    id3(b, n, f, &fim);
    if (fim + 200 > n) { *precisa = fim + 4096; return MCF_MP3; }
    i = fim;
  }
  for (; i + 200 <= n; i++) if (mpegFrame(b + i, m)) break;
  if (m->completo && !m->amostras && m->kbps && total > (long long)i) m->amostras = (total - (long long)i) * 8 / m->kbps / 1000 * m->taxa;   // CBR estimate
  return MCF_MP3;
}

// ---------------------------------------------------------------- MP4 / M4A
typedef struct { McFmt *m; McFaixa *f; int escala; long long durMvhd; } Mp4;
static void ilst(const unsigned char *p, size_t n, McFaixa *f) {
  size_t i = 0;
  while (i + 8 <= n) {
    uint32_t l = be32(p + i);
    const unsigned char *t = p + i + 4;
    if (l < 8 || i + l > n) break;
    // Each item holds a "data" box: [len]["data"][type 4][locale 4][value].
    if (l >= 24 && !memcmp(p + i + 12, "data", 4)) {
      const unsigned char *v = p + i + 24;
      size_t vl = l - 24;
      if (!memcmp(t, "\xA9nam", 4)) tagNomeada(f, "TITLE", (const char *)v, vl);
      else if (!memcmp(t, "\xA9" "ART", 4)) tagNomeada(f, "ARTIST", (const char *)v, vl);
      else if (!memcmp(t, "aART", 4)) tagNomeada(f, "ALBUMARTIST", (const char *)v, vl);
      else if (!memcmp(t, "\xA9" "alb", 4)) tagNomeada(f, "ALBUM", (const char *)v, vl);
      else if (!memcmp(t, "\xA9" "day", 4)) tagNomeada(f, "DATE", (const char *)v, vl);
      else if (!memcmp(t, "\xA9gen", 4)) tagNomeada(f, "GENRE", (const char *)v, vl);
      else if (!memcmp(t, "trkn", 4) && vl >= 4) f->faixa = (int)be16(v + 2);
      else if (!memcmp(t, "disk", 4) && vl >= 4) f->disco = (int)be16(v + 2);
    }
    i += l;
  }
}
static void caixas(const unsigned char *p, size_t n, Mp4 *x, int prof) {
  size_t i = 0;
  while (i + 8 <= n && prof < 10) {
    uint64_t l = be32(p + i);
    size_t h = 8;
    const unsigned char *t = p + i + 4;
    if (l == 1 && i + 16 <= n) { l = (uint64_t)be32(p + i + 8) << 32 | be32(p + i + 12); h = 16; }
    if (l < h || i + l > n) break;
    if (!memcmp(t, "moov", 4) || !memcmp(t, "trak", 4) || !memcmp(t, "mdia", 4) || !memcmp(t, "minf", 4) ||
        !memcmp(t, "stbl", 4) || !memcmp(t, "udta", 4))
      caixas(p + i + h, (size_t)l - h, x, prof + 1);
    else if (!memcmp(t, "meta", 4)) caixas(p + i + h + 4, (size_t)l - h - 4, x, prof + 1);   // full box
    else if (!memcmp(t, "ilst", 4)) ilst(p + i + h, (size_t)l - h, x->f);
    else if (!memcmp(t, "mdhd", 4) && l >= h + 24) {
      const unsigned char *d = p + i + h;
      if (d[0] == 1 && l >= h + 36) { x->escala = (int)be32(d + 20); x->durMvhd = (long long)((uint64_t)be32(d + 24) << 32 | be32(d + 28)); }
      else { x->escala = (int)be32(d + 12); x->durMvhd = be32(d + 16); }
    } else if (!memcmp(t, "stsd", 4) && l >= h + 8 + 36) {
      const unsigned char *e = p + i + h + 8;   // first sample entry
      uint32_t el = be32(e);
      McFmt *m = x->m;
      if (!memcmp(e + 4, "mp4a", 4)) snprintf(m->codec, sizeof m->codec, "AAC");
      else if (!memcmp(e + 4, "alac", 4)) snprintf(m->codec, sizeof m->codec, "ALAC");
      else if (!memcmp(e + 4, "fLaC", 4)) snprintf(m->codec, sizeof m->codec, "FLAC");
      else if (!memcmp(e + 4, "Opus", 4)) snprintf(m->codec, sizeof m->codec, "Opus");
      else { char c[5]; memcpy(c, e + 4, 4); c[4] = 0; snprintf(m->codec, sizeof m->codec, "%s", c); }
      m->canais = (int)be16(e + 24);
      m->taxa = (int)(be32(e + 32) >> 16);
      // ALAC: the magic cookie (inside the entry's own "alac" box) has the true bit depth and rate.
      if (!memcmp(e + 4, "alac", 4) && el >= 36 + 36 && i + h + 8 + el <= n) {
        const unsigned char *c = e + 36;
        if (!memcmp(c + 4, "alac", 4)) { m->bits = c[12 + 5]; m->canais = c[12 + 9]; m->taxa = (int)be32(c + 12 + 20); }
      } else if (memcmp(e + 4, "mp4a", 4) && memcmp(e + 4, "Opus", 4)) m->bits = (int)be16(e + 26);
      m->completo = 1;
    }
    i += (size_t)l;
  }
}
static int m4a(const unsigned char *b, size_t n, long long total, McFmt *m, McFaixa *f, size_t *precisa) {
  Mp4 x = { m, f, 0, 0 };
  size_t i = 0;
  m->tipo = MCF_M4A;
  // Find moov at the top level; when it is after mdat, say how far to read.
  while (i + 8 <= n) {
    uint64_t l = be32(b + i);
    if (l == 1 && i + 16 <= n) l = (uint64_t)be32(b + i + 8) << 32 | be32(b + i + 12);
    if (l == 0) l = (uint64_t)(total > 0 ? total : (long long)n) - i;
    if (l < 8) break;
    if (!memcmp(b + i + 4, "moov", 4)) {
      if (i + l > n) { *precisa = (size_t)(i + l); return MCF_M4A; }
      caixas(b + i, (size_t)l, &x, 0);
      break;
    }
    if (i + l > n) {
      // moov at the end of the file (not "fast start"): a second read there.
      if (!memcmp(b + i + 4, "mdat", 4) && total > 0) *precisa = (size_t)-1;
      break;
    }
    i += (size_t)l;
  }
  if (x.escala > 0 && x.durMvhd > 0 && m->taxa > 0) m->amostras = x.durMvhd * m->taxa / x.escala;
  return MCF_M4A;
}

// ---------------------------------------------------------------- entry
int mcfmt_ler(const unsigned char *b, size_t n, long long total, McFmt *m, McFaixa *f, size_t *precisa) {
  int t = MCF_DESCONHECIDO;
  size_t pz = 0;
  memset(m, 0, sizeof *m);
  if (!precisa) precisa = &pz;
  *precisa = 0;
  if (n >= 4 && !memcmp(b, "fLaC", 4)) t = flac(b, n, m, f, precisa);
  else if (n >= 12 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WAVE", 4)) t = wav(b, n, total, m, f, precisa);
  else if (n >= 12 && !memcmp(b, "FORM", 4) && (!memcmp(b + 8, "AIFF", 4) || !memcmp(b + 8, "AIFC", 4))) t = aiff(b, n, m);
  else if (n >= 12 && !memcmp(b + 4, "ftyp", 4)) t = m4a(b, n, total, m, f, precisa);
  else if (n >= 4 && !memcmp(b, "OggS", 4)) {
    t = m->tipo = MCF_OGG;
    if (n >= 64 && !memcmp(b + 28, "\x01vorbis", 7)) { snprintf(m->codec, sizeof m->codec, "Vorbis"); m->canais = b[39]; m->taxa = (int)le32(b + 40); m->completo = 1; }
    else if (n >= 48 && !memcmp(b + 28, "OpusHead", 8)) { snprintf(m->codec, sizeof m->codec, "Opus"); m->canais = b[37]; m->taxa = 48000; m->completo = 1; }
  } else if (n >= 10 && (!memcmp(b, "ID3", 3) || (b[0] == 0xFF && (b[1] & 0xE0) == 0xE0))) t = mp3(b, n, total, m, f, precisa);
  if (*precisa && *precisa <= n) *precisa = 0;
  if (m->completo) {
    static const char *const SUF[] = { "", "flac", "wav", "mp3", "m4a", "ogg", "aiff" };
    f->taxa = m->taxa; f->bits = m->bits; f->canais = m->canais;
    if (m->kbps) f->kbps = m->kbps;
    if (m->amostras > 0 && m->taxa > 0) f->dur = (int)(m->amostras / m->taxa);
    if (!f->formato[0]) snprintf(f->formato, sizeof f->formato, "%s", SUF[t]);
    if (t == MCF_M4A && !strcmp(m->codec, "ALAC")) snprintf(f->formato, sizeof f->formato, "alac");
  }
  return t;
}
void mcfmt_descrever(const McFaixa *f, char *d, size_t n) {
  char fmt[16], q[64] = "", tx[24] = "", ch[16] = "";
  size_t k;
  d[0] = 0;
  if (!f->formato[0]) return;
  snprintf(fmt, sizeof fmt, "%s", f->formato);
  for (char *c = fmt; *c; c++) *c = (char)toupper((unsigned char)*c);
  if (!strcmp(fmt, "M4A")) snprintf(fmt, sizeof fmt, "AAC");
  if (f->taxa > 0) {
    if (f->taxa % 1000) snprintf(tx, sizeof tx, "%.1f kHz", f->taxa / 1000.0);
    else snprintf(tx, sizeof tx, "%d kHz", f->taxa / 1000);
  }
  if (f->bits > 0) snprintf(q, sizeof q, "%d-bit%s%s", f->bits, tx[0] ? " / " : "", tx);
  else if (f->kbps > 0) snprintf(q, sizeof q, "%d kbps%s%s", f->kbps, tx[0] ? " · " : "", tx);
  else snprintf(q, sizeof q, "%s", tx);
  if (f->canais == 1) snprintf(ch, sizeof ch, "1.0");
  else if (f->canais == 2) snprintf(ch, sizeof ch, "2.0");
  else if (f->canais == 6) snprintf(ch, sizeof ch, "5.1");
  else if (f->canais == 8) snprintf(ch, sizeof ch, "7.1");
  else if (f->canais > 0) snprintf(ch, sizeof ch, "%dch", f->canais);
  k = (size_t)snprintf(d, n, "%s", fmt);
  if (q[0] && k < n) k += (size_t)snprintf(d + k, n - k, " %s", q);
  if (ch[0] && k < n) snprintf(d + k, n - k, " · %s", ch);
}

int mcfmt_classe(const McFaixa *f) {
  static const char *const SEM_PERDA[] = { "flac", "wav", "aif", "aiff", "alac", "wv", "ape", NULL };
  static const char *const PERDA[] = { "mp3", "aac", "ogg", "oga", "opus", "wma", "mpc", NULL };
  int sem = 0;
  if (!f->formato[0]) return MCQ_DESCONHECIDA;
  for (int i = 0; SEM_PERDA[i]; i++) if (!strcasecmp(f->formato, SEM_PERDA[i])) sem = 1;
  // m4a can be AAC or ALAC: only a reported bit depth (lossless sources have one) says ALAC.
  if (!sem && !strcasecmp(f->formato, "m4a") && f->bits > 0) sem = 1;
  if (!sem) {
    for (int i = 0; PERDA[i]; i++) if (!strcasecmp(f->formato, PERDA[i])) return MCQ_PERDA;
    if (!strcasecmp(f->formato, "m4a")) return MCQ_PERDA;
    return MCQ_DESCONHECIDA;
  }
  return f->bits > 16 || f->taxa > 48000 ? MCQ_HIRES : MCQ_LOSSLESS;
}
const char *mcfmt_selo(int c) {
  return c == MCQ_HIRES ? "Hi-Res" : c == MCQ_LOSSLESS ? "Lossless" : c == MCQ_PERDA ? "Lossy" : "";
}
