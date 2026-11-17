// YouTube section data layer (src/ytapi.c): parsers, error mapping, the Data
// API calls and the OAuth device flow against the local fixture
// (tests/qrtime_iptv_mock.py: /yt/v3/..., /goauth/...). The fixture copies
// the official response shapes; it proves our handling, not Google's service.
//
//   qrtime_youtube <data dir> <base url>          (fixture running)
//   qrtime_youtube <data dir> <base url> offline  (fixture down)
#include "ytapi.h"
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

static YtItem v[60];
static const YtItem *achar(int n, const char *id) { for (int i = 0; i < n; i++) if (!strcmp(v[i].id, id)) return &v[i]; return NULL; }

int main(int argc, char **argv) {
  char api[200], oauth[200], prox[64], d[16], cod[32], url[128];
  int n = 0, e, iv = 0;
  if (argc < 3) return 2;
  setenv("NUVIO_DADOS", argv[1], 1);
  snprintf(api, sizeof api, "%s/yt/v3", argv[2]);
  snprintf(oauth, sizeof oauth, "%s/goauth", argv[2]);
  yt_definir_base(api, oauth);
  dados_iniciar(argv[1]);

  if (argc > 3 && !strcmp(argv[3], "offline")) {
    yt_carregar();
    yt_definir_chave("QRTYTKEY");
    yt_definir_cliente("qrt-client.apps.googleusercontent.com", "qrt-secret");
    e = yt_em_alta("SA", NULL, v, 25, &n, prox, sizeof prox);
    CONFERE(e == YT_ERRO_REDE && n == 0, "offline: network error reported, no items");
    e = yt_login_iniciar(cod, sizeof cod, url, sizeof url, &iv);
    CONFERE(e == YT_ERRO_REDE, "offline: sign-in start reports the network");
    CONFERE(yt_login_conferir() <= 0, "offline: polling does not sign in");
    return falhas ? 1 : 0;
  }

  // ---- parsers ----
  yt_duracao("PT4M13S", d, sizeof d); CONFERE(!strcmp(d, "4:13"), "duration 4:13");
  yt_duracao("PT1H2M3S", d, sizeof d); CONFERE(!strcmp(d, "1:02:03"), "duration 1:02:03");
  yt_duracao("PT10M", d, sizeof d); CONFERE(!strcmp(d, "10:00"), "duration 10:00");
  yt_duracao("P1DT1M", d, sizeof d); CONFERE(!strcmp(d, "24:01:00"), "duration over a day");
  yt_duracao("P0D", d, sizeof d); CONFERE(!d[0], "live (P0D) has no duration");
  CONFERE(yt_id_valido("dQw4w9WgXcQ") && !yt_id_valido("dQw4w9WgXc") && !yt_id_valido("dQw4w9WgXc\"") && !yt_id_valido(NULL),
          "video id validation (11 chars, URL-safe base64)");
  CONFERE(yt_parse_erro("{\"error\":{\"errors\":[{\"reason\":\"quotaExceeded\"}]}}", 403) == YT_ERRO_COTA, "quotaExceeded -> quota");
  CONFERE(yt_parse_erro("{\"error\":{\"errors\":[{\"reason\":\"keyInvalid\"}]}}", 400) == YT_ERRO_CHAVE, "keyInvalid -> key");
  CONFERE(yt_parse_erro("{\"error\":{\"code\":401}}", 401) == YT_ERRO_LOGIN, "401 -> sign-in");
  CONFERE(yt_parse_lista("<html>", v, 5, prox, sizeof prox) == -1, "an HTML page is not a list");
  CONFERE(yt_parse_lista("{\"items\":[]}", v, 5, prox, sizeof prox) == 0, "empty list");

  // ---- no key: nothing fake ----
  yt_carregar();
  CONFERE(!yt_tem_chave() && !yt_tem_cliente() && !yt_logado(), "fresh install: no key, no client, no account");
  e = yt_em_alta("SA", NULL, v, 25, &n, prox, sizeof prox);
  CONFERE(e == YT_SEM_CHAVE && n == 0, "without a key: YT_SEM_CHAVE and no items");
  CONFERE(!yt_definir_chave("bad key with spaces"), "a key with spaces is refused");
  yt_definir_chave("WRONGKEY");
  e = yt_em_alta("SA", NULL, v, 25, &n, prox, sizeof prox);
  CONFERE(e == YT_ERRO_CHAVE, "a key the API rejects -> key error");
  yt_definir_chave("QUOTAKEY");
  e = yt_buscar("pasta", NULL, v, 25, &n, prox, sizeof prox);
  CONFERE(e == YT_ERRO_COTA, "quota exceeded -> quota error");

  // ---- with the key ----
  CONFERE(yt_definir_chave("QRTYTKEY") && yt_tem_chave(), "key saved");
  e = yt_em_alta("sa", NULL, v, 25, &n, prox, sizeof prox);
  CONFERE(e == YT_OK && n == 3 && !strcmp(prox, "PAGE2"), "trending: 3 videos and a next page");
  {
    const YtItem *a = achar(n, "qrtVideo001"), *b = achar(n, "qrtVideo002"), *c = achar(n, "qrtVideo003");
    CONFERE(a && a->tipo == YT_VIDEO && !strcmp(a->titulo, "QRT Trending One") && !strcmp(a->canal, "QRT Channel")
            && !strcmp(a->duracao, "4:13") && a->views == 1534000 && !strcmp(a->publicado, "2026-11-10"), "trending item fields");
    CONFERE(a && !strncmp(a->thumb, "https://i.ytimg.com/", 20), "thumbnail: the https one, never plain http");
    CONFERE(b && !strcmp(b->duracao, "1:02:03") && b->views == 2500000000LL, "long video, big view count");
    CONFERE(c && !strcmp(c->duracao, "AO VIVO"), "live broadcast marked live");
  }
  e = yt_em_alta("SA", "PAGE2", v, 25, &n, prox, sizeof prox);
  CONFERE(e == YT_OK && n == 1 && !prox[0], "trending page 2, last page");
  e = yt_buscar("Pasta at home", NULL, v, 25, &n, prox, sizeof prox);
  CONFERE(e == YT_OK && n == 2 && achar(n, "qrtSearch01") && !achar(n, "qrtSearch01")->duracao[0], "search: 2 results (no duration yet)");
  CONFERE(yt_detalhes(v, n) == YT_OK && !strcmp(achar(n, "qrtSearch02")->duracao, "10:00") && achar(n, "qrtSearch02")->views > 0,
          "details fill durations and views");
  e = yt_buscar("nothing like this", NULL, v, 25, &n, prox, sizeof prox);
  CONFERE(e == YT_OK && n == 0, "search with no results");
  {
    YtItem one;
    CONFERE(yt_video("qrtVideo001", &one) == YT_OK && !strcmp(one.desc, "Description of QRT Trending One"), "video page");
    CONFERE(yt_video("qrtGone0000", &one) == YT_INDISPONIVEL, "removed/private video -> unavailable");
    CONFERE(yt_video("../../x", &one) == YT_INDISPONIVEL, "a malformed id never reaches the API");
  }
  e = yt_canal_videos("UCqrtChannel0000000001", v, 25, &n);
  CONFERE(e == YT_OK && n == 1 && !strcmp(v[0].id, "qrtUpload01"), "channel page: its uploads playlist (1 unit, no search)");

  // ---- account ----
  e = yt_inscricoes(NULL, v, 50, &n, prox, sizeof prox);
  CONFERE(e == YT_ERRO_LOGIN && n == 0, "subscriptions without an account -> sign-in needed");
  e = yt_login_iniciar(cod, sizeof cod, url, sizeof url, &iv);
  CONFERE(e == YT_SEM_CHAVE, "sign-in without an OAuth client -> asks for it");
  yt_definir_cliente("wrong.apps.googleusercontent.com", "x");
  e = yt_login_iniciar(cod, sizeof cod, url, sizeof url, &iv);
  CONFERE(e == YT_ERRO_CHAVE, "an unknown OAuth client is reported");
  yt_definir_cliente("qrt-client.apps.googleusercontent.com", NULL);
  yt_definir_cliente(NULL, "qrt-secret");
  CONFERE(yt_tem_cliente(), "client id and secret saved separately (NULL keeps the other half)");
  e = yt_login_iniciar(cod, sizeof cod, url, sizeof url, &iv);
  CONFERE(e == YT_OK && !strcmp(cod, "QRT-WXYZ") && !strcmp(url, "https://www.google.com/device") && iv == 1, "device code and URL");
  CONFERE(yt_login_conferir() == 0, "first poll: authorization pending");
  CONFERE(yt_login_conferir() == 1 && yt_logado(), "second poll: signed in");
  {
    char *t = dados_ler("youtube-account-p1.txt");
    CONFERE(t && strstr(t, "renovar=QRTREFRESH"), "refresh token kept in this profile's file");
    free(t);
  }
  e = yt_inscricoes(NULL, v, 50, &n, prox, sizeof prox);
  CONFERE(e == YT_OK && n == 1 && v[0].tipo == YT_CANAL && !strcmp(v[0].id, "UCqrtChannel0000000001"), "subscriptions: the channel");
  e = yt_playlists(v, 50, &n);
  CONFERE(e == YT_OK && n == 1 && v[0].tipo == YT_PLAYLIST && v[0].itens == 2, "playlists: one, 2 items");
  e = yt_playlist_itens("PLqrtMine01", NULL, v, 25, &n, prox, sizeof prox);
  CONFERE(e == YT_OK && n == 2 && !achar(n, "qrtGone0000"), "playlist items: the deleted video is skipped");
  CONFERE(achar(n, "qrtVideo001") && !strcmp(achar(n, "qrtVideo001")->canal, "QRT Channel"), "playlist item shows the video's channel, not the owner");

  // Expired access token: refreshed with the refresh token.
  dados_gravar("youtube-account-p1.txt", "renovar=QRTREFRESH\nacesso=OLD\nate=1\n");
  yt_carregar();
  e = yt_playlists(v, 50, &n);
  {
    char *t = dados_ler("youtube-account-p1.txt");
    CONFERE(e == YT_OK && n == 1 && t && strstr(t, "acesso=QRTACCESS2"), "expired access token refreshed");
    free(t);
  }
  // A revoked refresh token signs the profile out instead of failing forever.
  dados_gravar("youtube-account-p1.txt", "renovar=REVOKED\nacesso=OLD\nate=1\n");
  yt_carregar();
  e = yt_playlists(v, 50, &n);
  CONFERE(e == YT_ERRO_LOGIN && !yt_logado(), "revoked refresh token -> signed out, sign-in needed");

  // Profiles: the account belongs to the profile that signed in.
  dados_gravar("youtube-account-p1.txt", "renovar=QRTREFRESH\nacesso=QRTACCESS\nate=9999999999\n");
  yt_carregar();
  perfil = 2;
  CONFERE(!yt_logado() && yt_tem_chave(), "profile 2: no account, same TV key");
  perfil = 1;
  CONFERE(yt_logado(), "profile 1: still signed in");
  // Profile changed while the phone was signing in: the token is discarded.
  yt_login_iniciar(cod, sizeof cod, url, sizeof url, &iv);
  yt_login_conferir();
  perfil = 3;
  CONFERE(yt_login_conferir() == -1 && !yt_logado(), "sign-in finished on another profile -> discarded");
  perfil = 1;
  yt_sair();
  CONFERE(!yt_logado() && !dados_ler("youtube-account-p1.txt"), "sign out removes the profile's tokens");
  return falhas ? 1 : 0;
}
