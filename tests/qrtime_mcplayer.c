// Music Center queue, library store and player.
//
//   qrtime_mcplayer <data dir> logic
//       queue / shuffle / repeat / persistence / history / favourites / local
//       playlists / the "played" rule / resume, with a fake output
//   qrtime_mcplayer <data dir> <base url> <audio dir> <disk file>
//       the real catalog against tests/qrtime_music_mock.py and the SDL
//       backend on SDL's "disk" driver: what reached the output is compared
//       byte for byte with the decoded file (no conversion, no gap)
#include "mccat.h"
#include "mcdec.h"
#include "mcfila.h"
#include "mcfontes.h"
#include "mcplayer.h"
#include "mcoffline.h"
#include "mcsaida.h"
#include "dados.h"
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int perfil = 1;
uint64_t perfis_sessao_geracao(void) { return (uint64_t)perfil; }
int perfis_sessao_atual(uint64_t s) { return s == (uint64_t)perfil; }
int perfis_sessao_travar(uint64_t s) { return s == (uint64_t)perfil; }
void perfis_sessao_destravar(void) {}
int perfis_ativo(void) { return perfil; }

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

static McFaixa fx(const char *id, int dur) {
  McFaixa f;
  memset(&f, 0, sizeof f);
  snprintf(f.id, sizeof f.id, "%s", id);
  snprintf(f.titulo, sizeof f.titulo, "Title %s", id);
  snprintf(f.artista, sizeof f.artista, "Artist");
  f.dur = dur;
  return f;
}
static const char *idEm(int i) { const McFaixa *f = mcq_item(i); return f ? f->id : "-"; }

// ---------------------------------------------------------------- fake output
static double fPos, fDur;
static int fEstado = MCO_TOCANDO, fAbertos, fPausado;
static double fInicio;
static int fAbrir(const char *u, const char *c, const McFaixa *f, double ini) { (void)u; (void)c; fDur = f->dur; fPos = ini; fInicio = ini; fAbertos++; fEstado = MCO_TOCANDO; return 1; }
static void fPausar(int p) { fPausado = p; }
static void fBuscar(double s) { fPos = s; }
static void fParar(void) {}
static double fPosF(void) { return fPos; }
static double fDurF(void) { return fDur; }
static int fEstadoF(void) { return fEstado; }
static void fInfo(McSaidaInfo *o) { memset(o, 0, sizeof *o); }
static const McSaida FAKE = { "fake", fAbrir, NULL, fPausar, fBuscar, fParar, fPosF, fDurF, fEstadoF, fInfo, NULL };
static int urlFake(const McFaixa *f, char *u, size_t n, char *c, size_t nc) { snprintf(u, n, "fake://%s", f->id); c[0] = 0; (void)nc; return 1; }
static int nAgora, nTocou;
static void agoraFake(const McFaixa *f) { (void)f; nAgora++; }
static void tocouFake(const McFaixa *f, long long ms) { (void)f; (void)ms; nTocou++; }

static int logica(void) {
  McFaixa v[6];
  for (int i = 0; i < 6; i++) { char id[8]; snprintf(id, sizeof id, "t%d", i); v[i] = fx(id, 200); }
  // ---- queue ----
  mcq_definir(v, 4, 1);
  CONFERE(mcq_n() == 4 && mcq_atual() == 1 && !strcmp(mcq_faixa_atual()->id, "t1"), "set queue, start at the 2nd");
  mcq_tocar_a_seguir(&v[4]);
  CONFERE(!strcmp(idEm(2), "t4"), "play next goes right after the current track");
  mcq_adicionar(&v[5]);
  CONFERE(mcq_n() == 6 && !strcmp(idEm(5), "t5"), "add to queue goes to the end");
  CONFERE(!mcq_remover(1), "the current track cannot be removed");
  CONFERE(mcq_remover(0) && mcq_atual() == 0 && !strcmp(mcq_faixa_atual()->id, "t1") && mcq_n() == 5, "remove before the current keeps it current");
  CONFERE(mcq_mover(4, 1) && !strcmp(idEm(1), "t5") && mcq_atual() == 0, "move a track");
  // t1 t5 t4 t2 t3
  CONFERE(mcq_proxima(0) == 1 && mcq_proxima(0) == 2 && mcq_proxima(0) == 3 && mcq_proxima(0) == 4, "next x4");
  CONFERE(mcq_proxima(1) == -1, "end of the queue without repeat");
  mcq_repetir(MCR_TODAS);
  CONFERE(mcq_proxima(1) == 0, "repeat all wraps around");
  mcq_repetir(MCR_UMA);
  CONFERE(mcq_proxima(1) == 0 && mcq_proxima(0) == 1, "repeat one replays when the track ends; Next still moves");
  mcq_repetir(MCR_NAO);
  CONFERE(mcq_anterior() == 0, "previous");
  // ---- shuffle ----
  {
    int visto[5] = { 0 }, ok = 1;
    mcq_ir(2);
    mcq_aleatorio(1);
    CONFERE(mcq_atual() == 0 && !strcmp(mcq_faixa_atual()->id, "t4"), "shuffle on: the current track stays current, first");
    for (int i = 0; i < mcq_n(); i++) { int k = atoi(idEm(i) + 1); if (k < 1 || k > 5 || visto[k - 1]) ok = 0; else visto[k - 1] = 1; }
    CONFERE(ok && mcq_n() == 5, "shuffle is a permutation (nothing lost or doubled)");
    mcq_aleatorio(0);
    CONFERE(!strcmp(mcq_faixa_atual()->id, "t4") && !strcmp(idEm(0), "t1"), "shuffle off: original order back, same current track");
  }
  // ---- persistence + resume ----
  mcq_ir(3);
  mcq_repetir(MCR_TODAS);
  mcq_posicao(87.5);
  CONFERE(mcq_salvar(), "queue saved");
  mcq_limpar();
  mcq_repetir(MCR_NAO);
  CONFERE(mcq_carregar() && mcq_n() == 5 && mcq_atual() == 3 && !strcmp(mcq_faixa_atual()->id, "t2") && mcq_repeticao() == MCR_TODAS
          && mcq_posicao_salva() > 87 && mcq_posicao_salva() < 88, "queue, position, repeat and seconds survive a restart");
  {
    char lin[3000];
    McFaixa a = fx("dvabcdef12:/dav/Music/a\tb.flac", 61), b;
    snprintf(a.titulo, sizeof a.titulo, "Tab\there");
    a.bits = 24; a.taxa = 96000; a.canais = 2; snprintf(a.formato, sizeof a.formato, "flac");
    mcfila_linha(&a, lin, sizeof lin);
    CONFERE(mcfila_ler_linha(lin, &b) && b.bits == 24 && b.taxa == 96000 && !strcmp(b.titulo, "Tab here") && b.dur == 61, "track line round trip (tabs sanitized)");
  }
  // ---- history / favourites / local playlists ----
  mch_tocou(&v[0]); mch_tocou(&v[1]); mch_tocou(&v[0]);
  CONFERE(mch_n() == 2 && !strcmp(mch_item(0)->id, "t0"), "history: most recent first, no duplicates");
  CONFERE(mcv_alternar(&v[2]) == 1 && mcv_fav("t2") && mcv_alternar(&v[2]) == 0 && !mcv_fav("t2"), "local favourite on/off");
  mcv_alternar(&v[3]);
  {
    int l = mcl_criar("Evening");
    const McFaixa *p;
    CONFERE(l == 0 && mcl_adicionar(l, &v[0]) && mcl_adicionar(l, &v[1]) && mcl_adicionar(l, &v[2]), "local playlist created, 3 tracks");
    CONFERE(mcl_mover(l, 2, 0) && mcl_remover(l, 1) && mcl_faixas(l, &p) == 2 && !strcmp(p[0].id, "t2") && !strcmp(p[1].id, "t1"), "edit: move + remove");
    CONFERE(mcl_criar("bad\tname") == -1, "a name with a tab is refused");
    CONFERE(mcl_renomear(l, "Night"), "rename");
  }
  // Profile 2 sees none of it; profile 1 gets it back.
  perfil = 2;
  mcfila_perfil_trocou();
  CONFERE(mcq_n() == 0 && mch_n() == 0 && mcv_n() == 0 && mcl_n() == 0, "profile 2: own empty queue, history, favourites, playlists");
  perfil = 1;
  mcfila_perfil_trocou();
  CONFERE(mcq_n() == 5 && mch_n() == 2 && mcv_n() == 1 && mcl_n() == 1 && !strcmp(mcl_nome(0), "Night"), "profile 1: everything back from disk");

  // ---- player with the fake output ----
  {
    McGanchos g = { urlFake, agoraFake, tocouFake, NULL };
    unsigned t = 1000;
    mcq_repetir(MCR_NAO);
    mcp_definir_saida(&FAKE);
    mcp_definir_ganchos(&g);
    mcq_definir(v, 3, 0);
    CONFERE(mcp_tocar(0) && nAgora == 1 && mcp_estado() == MCP_CARREGANDO, "play: opens the output, 'now playing' once");
    mcp_atualizar(t += 16);
    CONFERE(mcp_estado() == MCP_TOCANDO, "playing");
    fPos = 99; mcp_atualizar(t += 16);
    CONFERE(nTocou == 0, "not 'played' before half the track");
    fPos = 101; mcp_atualizar(t += 16);
    CONFERE(nTocou == 1, "'played' at half the track (200 s)");
    fPos = 150; mcp_atualizar(t += 16);
    CONFERE(nTocou == 1, "counted once");
    mcp_anterior();
    CONFERE(fPos == 0 && mcq_atual() == 0, "previous after 3 s restarts the track");
    fPos = 1;
    mcp_anterior();
    CONFERE(mcq_atual() == 0 && fAbertos == 2, "previous at the start of the first track: reopen it");
    fEstado = MCO_FIM; mcp_atualizar(t += 16);
    CONFERE(mcq_atual() == 1 && fAbertos == 3, "the track ended: the next one opens");
    mcp_atualizar(t += 16);
    fPos = 120; mcp_atualizar(t += 11000);
    mcp_alternar_pausa();
    CONFERE(mcp_estado() == MCP_PAUSADO && fPausado == 1, "pause");
    CONFERE(mcq_posicao_salva() == 120, "pause saves the position");
    mcp_alternar_pausa();
    CONFERE(mcp_estado() == MCP_TOCANDO && fPausado == 0, "resume");
    fPos = 300; fEstado = MCO_FIM; mcp_atualizar(t += 16);
    fEstado = MCO_FIM; mcp_atualizar(t += 16);
    CONFERE(mcp_estado() == MCP_PARADO, "end of the queue: stopped");
    mcq_definir(v, 3, 0);
    mcp_tocar(0);
    fEstado = MCO_ERRO;
    mcp_atualizar(t += 16);
    CONFERE(mcq_atual() == 1, "an output error skips to the next track");
    fEstado = MCO_TOCANDO;
    // Resume: a fresh session continues where the queue was, paused.
    fPos = 77; mcp_atualizar(t += 16); mcp_atualizar(t += 11000);
    mcp_parar();
    mcq_limpar();
    fAbertos = 0;
    CONFERE(mcp_retomar() && fAbertos == 1 && fInicio == 75 && mcq_atual() == 1, "resume: same track, 2 s before the saved point");
    mcp_atualizar(t += 16);
    CONFERE(mcp_estado() == MCP_PAUSADO && fPausado == 1, "resume starts paused (no surprise sound)");
    mcp_parar();
  }
  return falhas;
}

// ---------------------------------------------------------------- real chain
static unsigned char *ler(const char *c, size_t *n) {
  FILE *f = fopen(c, "rb");
  unsigned char *b;
  long t;
  if (!f) { *n = 0; return NULL; }
  fseek(f, 0, SEEK_END); t = ftell(f); fseek(f, 0, SEEK_SET);
  b = malloc((size_t)t + 1);
  *n = fread(b, 1, (size_t)t, f);
  fclose(f);
  return b;
}
// PCM (s32) of a file, appended to `out`.
static size_t pcm(const char *arq, unsigned char **out, size_t at) {
  size_t n;
  unsigned char *b = ler(arq, &n);
  McDecInfo in;
  McDec *d = mcdec_abrir(b, n, &in);
  int32_t blk[4096 * 8];
  long k;
  free(b);
  while (d && (k = mcdec_ler(d, blk, 4096)) > 0) {
    size_t add = (size_t)k * (size_t)in.canais * 4;
    *out = realloc(*out, at + add);
    memcpy(*out + at, blk, add);
    at += add;
  }
  mcdec_fechar(d);
  return at;
}
static int esperar(int estadoAlvo, int ms) {
  for (int i = 0; i < ms / 10; i++) { mcp_atualizar(SDL_GetTicks()); if (mcp_estado() == estadoAlvo) return 1; SDL_Delay(10); }
  return 0;
}
static int real(const char *base, const char *audio, const char *disco) {
  McFonte s, d;
  McFaixa v[100];
  McAlbum al[20];
  static McFaixa big[70];
  int n = 0;
  char desc[120], arq[600];
  memset(&s, 0, sizeof s);
  s.tipo = MCS_SUBSONIC;
  snprintf(s.ss.url, sizeof s.ss.url, "%s", base);
  snprintf(s.ss.usuario, sizeof s.ss.usuario, "qrtuser");
  snprintf(s.ss.senha, sizeof s.ss.senha, "qrt pass+1");
  CONFERE(mccat_testar(MCS_SUBSONIC, s.ss.url, "qrtuser", "qrt pass+1", desc, sizeof desc) == MC_OK && strstr(desc, "Navidrome 0.53.3"),
          "connection test: Navidrome version shown");
  CONFERE(mccat_testar(MCS_SUBSONIC, s.ss.url, "qrtuser", "nope", desc, sizeof desc) == MC_ERRO_LOGIN, "connection test: wrong password");
  CONFERE(mcfontes_salvar(&s) && !strncmp(s.chave, "ss", 2) && strlen(s.chave) == 10, "Subsonic source saved with its key");
  memset(&d, 0, sizeof d);
  d.tipo = MCS_WEBDAV;
  snprintf(d.dv.url, sizeof d.dv.url, "%s/dav/Music/", base);
  snprintf(d.dv.usuario, sizeof d.dv.usuario, "davuser");
  snprintf(d.dv.senha, sizeof d.dv.senha, "davpass");
  CONFERE(mccat_testar(MCS_WEBDAV, d.dv.url, "davuser", "davpass", desc, sizeof desc) == MC_OK, "connection test: WebDAV");
  CONFERE(mcfontes_salvar(&d) && mcfontes_n() == 2, "WebDAV source saved");
  mcfontes_carregar();
  CONFERE(mcfontes_n() == 2, "sources reload from disk");
  {
    char *t = dados_ler("music-sources-p1.txt");
    CONFERE(t && strstr(t, "davpass"), "passwords kept on this TV (file), like the IPTV ones");
    free(t);
  }
  CONFERE(mccat_albuns("alphabeticalByName", 0, al, 20, &n) == MC_OK && n == 3 && !strncmp(al[0].id, s.chave, 10), "unified albums, qualified ids");
  CONFERE(mccat_album(al[2].id, NULL, v, 100, &n) == MC_OK && n == 2 && !strncmp(v[0].id, s.chave, 10) && strchr(v[0].id, ':'),
          "album tracks with qualified ids");
  {
    char u[600];
    mccat_capa(v[0].id, v[0].capa, 300, u, sizeof u);
    char u2[600];
    mccat_capa(v[0].id, v[0].capa, 300, u2, sizeof u2);
    CONFERE(u[0] && !strcmp(u, u2) && strstr(u, "getCoverArt"), "cover URL is stable (image cache works)");
  }
  // ---- play two FLACs of the same format: gapless and bit-exact ----
  {
    McFaixa dois[3];
    unsigned char *esperado = NULL, *saiu;
    size_t ne = 0, ns;
    McSaidaInfo info;
    int nb = 0;
    mccat_album(al[0].id, NULL, big, 70, &nb);   // "Big Box": every track is a16.flac
    dois[0] = big[0]; dois[1] = big[1]; dois[2] = v[1];   // + the 24/96 one (different format)
    mcp_definir_saida(mcsaida_sdl());
    mcp_definir_ganchos(mccat_ganchos());
    mcq_repetir(MCR_NAO);
    mcq_aleatorio(0);
    mcq_definir(dois, 2, 0);
    CONFERE(mcp_tocar(0), "play the Subsonic stream");
    CONFERE(esperar(MCP_TOCANDO, 10000), "downloaded, decoded, playing");
    mcp_saida(&info);
    CONFERE(info.medido && info.taxa == 44100 && info.bits == 32 && info.canais == 2 && !info.convertido,
            "output measured: 44.1 kHz, 32-bit container, 2 ch, no conversion");
    printf("info: %s | %s\n", info.backend, info.nota);
    for (int i = 0; i < 3000 && mcp_estado() != MCP_PARADO; i++) { mcp_atualizar(SDL_GetTicks()); SDL_Delay(10); }
    CONFERE(mcq_atual() == 1 && mcp_estado() == MCP_PARADO, "both tracks played to the end of the queue");
    SDL_Delay(300);
    snprintf(arq, sizeof arq, "%s/a16.flac", audio);
    ne = pcm(arq, &esperado, ne);
    ne = pcm(arq, &esperado, ne);
    saiu = ler(disco, &ns);
    {
      // The device writes silence until the first buffer: find the start, then
      // everything after it must be the two tracks exactly, with nothing between.
      size_t ini = 0;
      while (saiu && ini + 4096 <= ns && memcmp(saiu + ini, esperado, 4096)) ini += 4;
      CONFERE(saiu && ini + ne <= ns && !memcmp(saiu + ini, esperado, ne),
              "device got EXACTLY the decoded samples of both tracks, back to back (no gap, no change)");
      printf("info: output %zu bytes, music starts at %zu, expected %zu bytes\n", ns, ini, ne);
    }
    free(saiu);
    free(esperado);
    // A 24-bit / 96 kHz track: the device reopens at 96 kHz (own file: the disk driver truncates on open).
    {
      char d2[600];
      snprintf(d2, sizeof d2, "%s.96k", disco);
      setenv("SDL_DISKAUDIOFILE", d2, 1);
      mcq_definir(&dois[2], 1, 0);
      mcp_tocar(0);
      esperar(MCP_TOCANDO, 10000);
      mcp_saida(&info);
      CONFERE(info.taxa == 96000 && info.bits == 32 && !info.convertido, "the 24/96 track reopens the device at 96 kHz (no resampling)");
      mcp_parar();
    }
  }
  {
    char *t = dados_ler("music-history-p1.txt");
    CONFERE(t && strstr(t, "Track 05") && strstr(t, "Midnight Line"), "history recorded");
    free(t);
  }
  // ---- WebDAV: browse, tags, play with the auth header ----
  {
    DavItem p[20];
    int np, na;
    CONFERE(mccat_pasta(d.chave, "", p, 20, &np, v, 50, &na) == MC_OK && np == 2 && na == 0, "WebDAV root: folders");
    CONFERE(mccat_pasta(d.chave, p[0].href, p, 20, &np, v, 50, &na) == MC_OK && na == 3 && v[0].faixa == 1 && !strcmp(v[0].titulo, "Blue"),
            "WebDAV folder: tracks named from files (\"01 - Blue.flac\" -> 1, Blue)");
    mccat_tags(v, na);
    CONFERE(!strcmp(v[1].titulo, "With Cover") && v[1].bits == 24 && v[1].taxa == 96000 && !strcmp(v[2].titulo, "MP3 Title"), "tags read by Range");
    mcq_definir(v, 1, 0);
    mcp_tocar(0);
    CONFERE(esperar(MCP_TOCANDO, 10000), "WebDAV track plays (Basic auth sent with the download)");
    mcp_parar();
    CONFERE(mccat_favoritar(&v[0], 1) == MC_OK && mcv_fav(v[0].id), "WebDAV favourite kept locally");
  }
  // ---- offline copies ----
  {
    McFaixa nb2[5];
    int k = 0;
    char u[700];
    size_t na2, nb3;
    unsigned char *a2, *b3;
    mccat_album(al[2].id, NULL, nb2, 5, &k);   // Night Sessions: s1 (a16), s2 (a24)
    CONFERE(mco_baixar(&nb2[1]) == MCOF_OK && mco_caminho(nb2[1].id, u, sizeof u) && !strncmp(u, "file://", 7), "offline copy saved");
    snprintf(arq, sizeof arq, "%s/a24.flac", audio);
    a2 = ler(arq, &na2);
    b3 = ler(u + 7, &nb3);
    CONFERE(a2 && b3 && na2 == nb3 && !memcmp(a2, b3, na2), "the copy is the original file, byte for byte");
    free(a2); free(b3);
    CONFERE(mco_n() == 1 && mco_usado() == (long long)nb3, "offline list and space used");
    {
      McFaixa l;
      CONFERE(mco_item(0, &l) && !strcmp(l.titulo, "Midnight Line"), "offline list keeps the metadata");
    }
    mcq_definir(&nb2[1], 1, 0);
    mcp_tocar(0);
    CONFERE(esperar(MCP_TOCANDO, 10000), "the offline copy plays (from disk)");
    mcp_parar();
    CONFERE(mco_apagar(nb2[1].id) && !mco_caminho(nb2[1].id, u, sizeof u) && mco_n() == 0, "remove the offline copy");
    {   // a user without the download role: refused, nothing kept
      McFonte x;
      memset(&x, 0, sizeof x);
      x.tipo = MCS_SUBSONIC;
      snprintf(x.ss.url, sizeof x.ss.url, "%s", base);
      snprintf(x.ss.usuario, sizeof x.ss.usuario, "listener");
      snprintf(x.ss.senha, sizeof x.ss.senha, "listenpw");
      mcfontes_salvar(&x);
      snprintf(nb2[0].id, sizeof nb2[0].id, "%s:s1", x.chave);
      CONFERE(mco_baixar(&nb2[0]) == MCOF_SEM_PERMISSAO && mco_n() == 0, "server without download permission: refused, nothing saved");
      mcfontes_remover(x.chave);
    }
  }
  // ---- Party mode adds tracks before the queue runs out ----
  {
    mcq_definir(v, 0, 0);
    mcq_definir(big, 1, 0);
    mcp_festa(1);
    mcp_tocar(0);
    for (int i = 0; i < 500 && mcq_n() < 5; i++) { mcp_atualizar(SDL_GetTicks()); SDL_Delay(10); }
    CONFERE(mcq_n() > 5, "Party mode appended tracks from the server");
    mcp_festa(0);
    mcp_parar();
  }
  // ---- save a playlist: one server -> on the server ----
  {
    int srv = 0;
    McPlaylist pl[20];
    int npl = 0;
    CONFERE(mccat_salvar_playlist("From Queue", v, 0, &srv) == MC_OK, "empty save is harmless");
    mccat_album(al[2].id, NULL, v, 100, &n);
    CONFERE(mccat_salvar_playlist("Night Mix", v, n, &srv) == MC_OK && srv == 1, "a playlist of one server's tracks is saved on that server");
    CONFERE(mccat_playlists(pl, 20, &npl) == MC_OK && npl >= 2, "playlists: server + local");
  }
  return falhas;
}

int main(int argc, char **argv) {
  if (argc < 3) return 2;
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(argv[1]);
  if (!strcmp(argv[2], "logic")) return logica() ? 1 : 0;
  if (argc < 5) return 2;
  if (SDL_Init(SDL_INIT_AUDIO) != 0) { printf("FAIL: SDL audio (%s)\n", SDL_GetError()); return 1; }
  real(argv[2], argv[3], argv[4]);
  SDL_Quit();
  return falhas ? 1 : 0;
}
