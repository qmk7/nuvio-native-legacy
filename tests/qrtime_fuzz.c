// QR TIME phase 8: malformed playlists, API responses and saved files.
// Every parser that reads outside input gets valid samples, truncations at
// every length, byte flips and structural garbage, under ASan/UBSan. Passing
// means: no crash, no out-of-bounds access, counts within the caller's limit.
//
//   bash tests/qrtime_fuzz.sh
#include "subsonic.h"
#include "webdav.h"
#include "ytapi.h"
#include "mclrc.h"
#include "mcfmt.h"
#include "m3u.h"
#include "m3uvod.h"
#include "xmltv.h"
#include "agenda.h"
#include "traktfav.h"
#include "mcfila.h"
#include "mcfontes.h"
#include "js.h"
#include "dados.h"
#include "perfis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static unsigned semente = 12345;
static unsigned rnd(void) { semente = semente * 1103515245u + 12345u; return semente >> 8; }

// Mutations of one sample: every truncation, random byte flips, inserted
// structure characters, and a long repetition (deep nesting).
typedef int (*Alvo)(const char *txt, size_t n);
static int mutar(const char *base, Alvo f, int rodadas) {
  size_t n = strlen(base);
  char *b = malloc(n + 4096);
  int pior = 0;
  static const char estr[] = "{}[]\"\\:,<>/&;=#\n\r\t\0";
  for (size_t k = 0; k <= n; k++) {           // every prefix (a cut-off response)
    memcpy(b, base, k); b[k] = 0;
    pior |= f(b, k);
  }
  for (int r = 0; r < rodadas; r++) {
    memcpy(b, base, n + 1);
    int m = 1 + (int)(rnd() % 8);
    for (int i = 0; i < m && n; i++) {
      size_t at = rnd() % n;
      b[at] = (rnd() & 1) ? estr[rnd() % (sizeof estr - 1)] : (char)(rnd() & 0xff);
    }
    b[n] = 0;
    pior |= f(b, strlen(b));
  }
  for (int r = 0; r < 4; r++) {              // deep nesting / long tokens
    size_t m = 0;
    const char *rep = r == 0 ? "[" : r == 1 ? "{\"a\":" : r == 2 ? "<a>" : "\\\"";
    while (m + strlen(rep) < n + 4000) { memcpy(b + m, rep, strlen(rep)); m += strlen(rep); }
    b[m] = 0;
    pior |= f(b, m);
  }
  free(b);
  return pior;
}

// ---- targets (return nonzero on a contract violation) ----
static int fSubsonic(const char *t, size_t n) {
  McFaixa v[8]; int cod = 0; char msg[64];
  (void)n;
  ss_parse_estado(t, &cod, msg, sizeof msg);
  int k = ss_parse_faixas(t, "song", v, 8);
  int k2 = ss_parse_faixas(t, "entry", v, 8);
  return k > 8 || k2 > 8 || strlen(msg) >= sizeof msg;
}
static int fWebdav(const char *t, size_t n) {
  DavItem v[6]; (void)n;
  int k = dav_parse_lista(t, "/music/", v, 6);
  return k > 6;
}
static int fYoutube(const char *t, size_t n) {
  YtItem v[5]; char prox[40]; (void)n;
  int k = yt_parse_lista(t, v, 5, prox, sizeof prox);
  yt_parse_erro(t, 403);
  return k > 5 || strlen(prox) >= sizeof prox;
}
static int fLrc(const char *t, size_t n) {
  McLinha v[16]; int s = 0; (void)n;
  int k = mclrc_parse(t, v, 16, &s);
  for (int i = 0; i < k && i < 16; i++) if (strlen(v[i].texto) >= sizeof v[i].texto) return 1;
  return k > 16;
}
static int fAgenda(const char *t, size_t n) {
  AgEpCal v[4]; (void)n;
  return agenda_trakt_cal_parse(t, v, 4) > 4;
}
static int fTraktFav(const char *t, size_t n) {
  char ids[4][24]; (void)n;
  return traktfav_parse(t, ids, 4) > 4;
}
static int fM3u(const char *t, size_t n) {
  static M3uCanal v[6];
  char *c = malloc(n + 1), epg[256];
  int total = 0, k;
  memcpy(c, t, n + 1);
  k = m3u_processar(c, "http://origin.example/list.m3u", v, 6, epg, sizeof epg, &total);
  free(c);
  return k > 6;
}
static int vodCb(const XtVodItem *it, char kind, const char *serie, int t, int e, const char *url, void *u) {
  (void)it; (void)kind; (void)serie; (void)t; (void)e; (void)url; (*(int *)u)++; return 1;
}
static int fM3uVod(const char *t, size_t n) {
  char *c = malloc(n + 1);
  int cont = 0;
  memcpy(c, t, n + 1);
  m3uvod_parse(c, vodCb, &cont);
  free(c);
  return 0;
}
static void xCanal(const char *id, const char *const *nomes, int nn, void *u) { (void)id; (void)nomes; (void)nn; (void)u; }
static void xProg(const char *c, const char *a, const char *b, const char *t, void *u) { (void)c; (void)a; (void)b; (void)t; (void)u; }
static int fXmltv(const char *t, size_t n) {
  Xmltv x;
  memset(&x, 0, sizeof x);
  x.canal = xCanal; x.programa = xProg;
  if (!xmltv_iniciar(&x)) return 0;
  // fed in odd-sized pieces, as the network delivers it
  for (size_t i = 0; i < n; ) { size_t k = 1 + rnd() % 97; if (k > n - i) k = n - i; xmltv_alimentar(&x, t + i, k); i += k; }
  xmltv_fim(&x);
  xmltv_tempo(t);
  return 0;
}
static int fJs(const char *t, size_t n) {
  char s[32];
  const char *fim = t + n;
  (void)js_fim(t);
  js_texto(t, fim, "a", s, sizeof s);
  (void)js_num(t, fim, "a", 0);
  return strlen(s) >= sizeof s;
}
static int fFmt(const char *t, size_t n) {
  McFmt fm; McFaixa f; size_t precisa = 0;
  memset(&f, 0, sizeof f);
  mcfmt_ler((const unsigned char *)t, n, (long long)n, &fm, &f, &precisa);
  return 0;
}

int main(void) {
  dados_iniciar(getenv("NUVIO_DADOS"));
  perfis_definir_ativo(1);

  CONFERE(!mutar("{\"subsonic-response\":{\"status\":\"ok\",\"version\":\"1.16.1\",\"searchResult3\":{\"song\":[{\"id\":\"s1\","
                 "\"title\":\"T\\u00e9\",\"album\":\"A\",\"artist\":\"R\",\"duration\":180,\"suffix\":\"flac\",\"bitRate\":1000,"
                 "\"samplingRate\":96000,\"bitDepth\":24,\"coverArt\":\"c1\"},{\"id\":\"s2\",\"title\":\"x\"}]}}}", fSubsonic, 3000),
          "Subsonic JSON: truncated / corrupted / nested answers");
  CONFERE(!mutar("<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/music/</d:href></d:response>"
                 "<d:response><d:href>/music/A%20B/x.flac</d:href><d:propstat><d:prop><d:getcontentlength>123</d:getcontentlength>"
                 "<d:resourcetype/></d:prop></d:propstat></d:response></d:multistatus>", fWebdav, 3000),
          "WebDAV PROPFIND XML");
  CONFERE(!mutar("{\"nextPageToken\":\"CAoQAA\",\"items\":[{\"id\":{\"kind\":\"youtube#video\",\"videoId\":\"abcdefghijk\"},"
                 "\"snippet\":{\"title\":\"V\",\"channelTitle\":\"C\",\"thumbnails\":{\"high\":{\"url\":\"https://i.ytimg.com/x.jpg\"}}}}]}",
                 fYoutube, 3000), "YouTube Data API JSON");
  CONFERE(!mutar("[ar:A]\n[00:01.00]one\n[00:02.50][00:05.00]two\n[99:99.99]x\n[-1:00]y\nplain\n", fLrc, 3000), "LRC lyrics");
  CONFERE(!mutar("[{\"first_aired\":\"2026-11-20T02:00:00.000Z\",\"episode\":{\"season\":2,\"number\":5,\"title\":\"F\"},"
                 "\"show\":{\"title\":\"S\",\"ids\":{\"imdb\":\"tt9000001\"}}}]", fAgenda, 3000), "Trakt calendar JSON");
  CONFERE(!mutar("[{\"movie\":{\"ids\":{\"imdb\":\"tt0133093\",\"tmdb\":603}}},{\"show\":{\"ids\":{\"tmdb\":1396}}}]", fTraktFav, 3000),
          "Trakt favorites JSON");
  CONFERE(!mutar("#EXTM3U x-tvg-url=\"http://e/epg.xml\"\n#EXTINF:-1 tvg-id=\"a\" tvg-logo=\"http://l/a.png\" group-title=\"G\",Name A\n"
                 "#EXTVLCOPT:http-user-agent=UA\nhttp://h/live/1.ts\n#EXTINF:-1,B\n../rel/2.m3u8\n", fM3u, 3000),
          "M3U playlist (live)");
  CONFERE(!mutar("#EXTM3U\n#EXTINF:-1 group-title=\"Movies\",Film (2020)\nhttp://h/movie/u/p/1.mkv\n"
                 "#EXTINF:-1,Show S01E02\nhttp://h/series/u/p/2.mp4\n", fM3uVod, 3000), "M3U playlist (VOD / series)");
  CONFERE(!mutar("<?xml version=\"1.0\"?><tv><channel id=\"a\"><display-name>A</display-name><icon src=\"http://i\"/></channel>"
                 "<programme channel=\"a\" start=\"20261118100000 +0300\" stop=\"20261118110000 +0300\"><title>T</title></programme></tv>",
                 fXmltv, 3000), "XMLTV guide (streamed in pieces)");
  CONFERE(!mutar("{\"a\":\"x\\\"y\\u0041\",\"b\":[1,{\"c\":2}],\"a\":3}", fJs, 3000), "JSON helpers (js_fim / js_texto / js_num)");
  {
    // a minimal FLAC header (fLaC + STREAMINFO) and a WAV header, then mutated
    static const char flac[] = "fLaC\x80\x00\x00\x22\x10\x00\x10\x00\x00\x00\x10\x00\x00\x10\x0b\xb8\x03\x70\x00\x00\x00\x00\x00\x00"
                               "0123456789abcdef";
    static const char wav[] = "RIFF\x24\x08\x00\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x02\x00\x44\xac\x00\x00\x10\xb1\x02\x00\x04\x00\x10\x00data";
    CONFERE(!mutar(flac, fFmt, 3000) && !mutar(wav, fFmt, 3000) && !mutar("ID3\x04\x00\x00\x00\x00\x00\x10TIT2", fFmt, 3000),
            "audio headers (FLAC / WAV / ID3)");
  }
  // ---- corrupted saved files: loaded without crashing, nothing invented ----
  {
    static const char *const lixo[] = {
      "", "\n\n\n", "\t\t\t\t\t\t\t\t\t\t\t\t\t\t", "x\ty", "1\t2\t3\t4\t5\t6\t7\t8\t9\t10\t11\t12\t13\t14\t15\t16\t17\t18\t19\t20\n",
      "\xff\xfe\x00garbage", NULL };
    char grande[70000];
    memset(grande, 'A', sizeof grande - 1); grande[sizeof grande - 1] = 0;
    for (int i = 0; lixo[i]; i++) {
      dados_gravar("music-queue-p1.txt", lixo[i]);
      dados_gravar("music-favorites-p1.txt", lixo[i]);
      dados_gravar("music-playlists-p1.txt", lixo[i]);
      dados_gravar("music-history-p1.txt", lixo[i]);
      dados_gravar("music-sources-p1.txt", lixo[i]);
      mcfila_perfil_trocou();
      mcq_carregar(); (void)mcv_n(); (void)mcl_n(); (void)mch_n();
      mcfontes_carregar(); (void)mcfontes_n();
    }
    dados_gravar("music-queue-p1.txt", grande);
    dados_gravar("music-sources-p1.txt", grande);
    mcfila_perfil_trocou(); mcq_carregar(); mcfontes_carregar();
    CONFERE(mcfontes_n() == 0, "corrupted music files: loaded without a crash, no invented source");
  }
  return falhas ? 1 : 0;
}
