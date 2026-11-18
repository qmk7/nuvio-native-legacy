// Music Center: Spotify import, metadata only (src/mcspotify.c), against
// tests/qrtime_music_mock.py (Spotify Web API shapes + the Navidrome mock):
// client-credentials token, a public playlist (paged), matching by artist and
// title to the library, the errors Spotify gives for private / Spotify-owned
// playlists and bad credentials. Plus the thread-safe local copies.
//
//   qrtime_mcspotify <data dir> <base url>
#include "mcspotify.h"
#include "mcfila.h"
#include "mcfontes.h"
#include "dados.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int perfil = 1;
uint64_t perfis_sessao_geracao(void) { return (uint64_t)perfil; }
int perfis_sessao_atual(uint64_t s) { return s == (uint64_t)perfil; }
int perfis_sessao_travar(uint64_t s) { return s == (uint64_t)perfil; }
void perfis_sessao_destravar(void) {}
int perfis_ativo(void) { return perfil; }

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

int main(int argc, char **argv) {
  char id[64], nome[160], c[300], a[300];
  static SpFaixa sp[300];
  static McFaixa ac[300];
  int n, achadas, total;
  McFonte f;
  if (argc < 3) return 2;
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(argv[1]);
  snprintf(c, sizeof c, "%s/spaccounts", argv[2]);
  snprintf(a, sizeof a, "%s/spapi/v1", argv[2]);
  mcsp_definir_base(c, a);

  CONFERE(mcsp_id_playlist("https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M?si=abc123", id, sizeof id) && !strcmp(id, "37i9dQZF1DXcBWIGoYBM5M"),
          "playlist id from a share link");
  CONFERE(mcsp_id_playlist("spotify:playlist:5QrtPublicMix00000001", id, sizeof id) && !strcmp(id, "5QrtPublicMix00000001"), "from a spotify: URI");
  CONFERE(!mcsp_id_playlist("https://example.com/hello", id, sizeof id), "not a playlist link");
  CONFERE(mcsp_mesmo("Blue Hour (Remastered 2011)", "blue hour") && mcsp_mesmo("Midnight Line - Live", "Midnight Line")
          && !mcsp_mesmo("Blue", "Blue Hour"), "title matching ignores case, punctuation, (remaster), - live");
  CONFERE(mcsp_playlist("5QrtPublicMix00000001", nome, sizeof nome, sp, 300, &n) == MC_ERRO_LOGIN, "without client id/secret: asks for them");
  mcsp_definir_chave("qrtspid", "wrongsecret");
  CONFERE(mcsp_playlist("5QrtPublicMix00000001", nome, sizeof nome, sp, 300, &n) == MC_ERRO_LOGIN, "bad secret: refused");
  mcsp_definir_chave(NULL, "qrtspsecret");
  CONFERE(mcsp_playlist("5QrtPublicMix00000001", nome, sizeof nome, sp, 300, &n) == MC_OK && n == 104 && !strcmp(nome, "QRT Road Mix"),
          "public playlist: name + 104 tracks over two pages (null and local items skipped)");
  CONFERE(!strcmp(sp[0].titulo, "Blue Hour") && !strcmp(sp[0].artista, "QRT Quartet") && sp[0].dur == 5 && !strcmp(sp[0].album, "Night Sessions"),
          "track metadata: title, first artist, album, duration");
  CONFERE(mcsp_playlist("37i9dQZF1DXcBWIGoYBM5M", nome, sizeof nome, sp, 300, &n) == MC_ERRO_NAO_SUPORTA,
          "Spotify-owned playlist (404 for new apps): reported, not faked");
  // Matching against the Navidrome mock.
  memset(&f, 0, sizeof f);
  f.tipo = MCS_SUBSONIC;
  snprintf(f.ss.url, sizeof f.ss.url, "%s", argv[2]);
  snprintf(f.ss.usuario, sizeof f.ss.usuario, "qrtuser");
  snprintf(f.ss.senha, sizeof f.ss.senha, "qrt pass+1");
  mcfontes_salvar(&f);
  CONFERE(mcsp_importar("https://open.spotify.com/playlist/5QrtPublicMix00000001", ac, 300, &achadas, &total, nome, sizeof nome) == MC_OK
          && total == 104 && achadas == 3, "import: 3 of 104 found in the library (title + artist)");
  CONFERE(!strncmp(ac[0].id, f.chave, 10) && !strcmp(ac[0].titulo, "Blue Hour") && !strcmp(ac[2].titulo, "Midnight Line"),
          "found tracks are the library's own (qualified ids), in playlist order");
  // The UI thread saves them; a worker reading the lists gets a consistent copy.
  {
    int li = mcl_criar("QRT Road Mix (Spotify)");
    char nomes[8][96];
    int fx[8];
    McFaixa cp[10];
    for (int i = 0; i < achadas; i++) mcl_adicionar(li, &ac[i]);
    CONFERE(mcl_copiar_nomes(nomes, fx, 8) == 1 && fx[0] == 3 && !strcmp(nomes[0], "QRT Road Mix (Spotify)"), "local playlist copy (worker side)");
    CONFERE(mcl_copiar(0, cp, 10) == 3 && !strcmp(cp[1].titulo, ac[1].titulo), "local playlist tracks copy");
    mcv_alternar(&ac[0]);
    CONFERE(mcv_copiar(cp, 10) == 1 && !strcmp(cp[0].id, ac[0].id), "local favourites copy");
  }
  {
    char *t = dados_ler("spotify.txt");
    CONFERE(t && strstr(t, "cliente=qrtspid"), "client id kept on this TV");
    free(t);
  }
  return falhas ? 1 : 0;
}
