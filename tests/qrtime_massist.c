// massist.c: Music Assistant answers -> items/players, and the commands sent.
//   bash tests/qrtime_massist.sh
#include "massist.h"
#include "dados.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int falhas;
#define CONFERE(c, m) do { if (c) printf("PASS: %s\n", m); else { printf("FAIL: %s\n", m); falhas++; } } while (0)
static char ultimoUrl[300], ultimoCorpo[2000], ultimoToken[200];
static const char *resposta;
static int statusResp = 200;
static char *falso(const char *url, const char *corpo, const char *token, int *st) {
  snprintf(ultimoUrl, sizeof ultimoUrl, "%s", url);
  snprintf(ultimoCorpo, sizeof ultimoCorpo, "%s", corpo);
  snprintf(ultimoToken, sizeof ultimoToken, "%s", token);
  *st = statusResp;
  return strdup(resposta ? resposta : "null");
}
int main(void) {
  MaItem it[16];
  MaPlayer pl[8];
  int n = 0;
  char u[256], k[256];
  setenv("NUVIO_DADOS", getenv("MA_DIR"), 1);
  dados_iniciar(NULL);
  massist_teste_transporte(falso);
  CONFERE(massist_buscar("x", it, 16, &n) == MA_SEM_CONFIG, "no address yet: nothing sent");
  CONFERE(massist_gravar("192.168.100.104:8095/", "tok123"), "saved");
  CONFERE(massist_ler(u, sizeof u, k, sizeof k) && !strcmp(u, "http://192.168.100.104:8095") && !strcmp(k, "tok123"), "address normalised, token kept");
  resposta = "{\"tracks\":[{\"item_id\":\"abc\",\"provider\":\"ytmusic\",\"name\":\"Kifak Inta\",\"uri\":\"ytmusic://track/abc\","
             "\"media_type\":\"track\",\"duration\":245,\"artists\":[{\"name\":\"Fairuz\",\"item_id\":\"f1\"}],"
             "\"metadata\":{\"images\":[{\"type\":\"thumb\",\"path\":\"https://lh3.googleusercontent.com/x\",\"provider\":\"ytmusic\",\"remotely_accessible\":true}]}}],"
             "\"artists\":[{\"item_id\":\"f1\",\"provider\":\"ytmusic\",\"name\":\"Fairuz\",\"uri\":\"ytmusic://artist/f1\",\"media_type\":\"artist\"}],"
             "\"albums\":[],\"playlists\":[]}";
  CONFERE(massist_buscar("Fairuz \"live\"", it, 16, &n) == MA_OK && n == 2, "search: tracks and artists");
  CONFERE(!strcmp(it[0].nome, "Kifak Inta") && !strcmp(it[0].sub, "Fairuz") && it[0].dur == 245 &&
          !strcmp(it[0].uri, "ytmusic://track/abc") && !strncmp(it[0].imagem, "https://lh3", 11), "track: name, artist, length, uri, picture");
  CONFERE(!strcmp(ultimoUrl, "http://192.168.100.104:8095/api") && !strcmp(ultimoToken, "tok123") &&
          strstr(ultimoCorpo, "\"command\":\"music/search\"") && strstr(ultimoCorpo, "Fairuz \\\"live\\\""), "POST /api with the token, query escaped");
  resposta = "{\"message_id\":\"1\",\"result\":[{\"player_id\":\"media_player.q990f\",\"display_name\":\"Soundbar\",\"state\":\"idle\",\"available\":true},"
             "{\"player_id\":\"ma_group_all\",\"name\":\"All speakers\",\"type\":\"group\",\"available\":true},"
             "{\"player_id\":\"echo1\",\"name\":\"Echo\",\"available\":false}]}";
  CONFERE(massist_players(pl, 8, &n) == MA_OK && n == 3 && !strcmp(pl[0].nome, "Soundbar") && pl[1].grupo && !pl[2].disponivel,
          "players (wrapped result): names, group, unavailable");
  resposta = "[{\"item_id\":\"12\",\"provider\":\"library\",\"name\":\"Liked\",\"uri\":\"library://playlist/12\",\"media_type\":\"playlist\",\"owner\":\"me\"}]";
  CONFERE(massist_biblioteca("playlists", 0, it, 16, &n) == MA_OK && n == 1 && !strcmp(it[0].sub, "me") &&
          strstr(ultimoCorpo, "music/playlists/library_items"), "library playlists");
  CONFERE(massist_faixas_de(&it[0], it + 1, 8, &n) == MA_OK && strstr(ultimoCorpo, "playlist_tracks") &&
          strstr(ultimoCorpo, "\"item_id\":\"12\"") && strstr(ultimoCorpo, "\"provider_instance_id_or_domain\":\"library\""), "playlist tracks asked by id + provider");
  resposta = "null";
  CONFERE(massist_tocar("media_player.q990f", "ytmusic://track/abc", 1) == MA_OK && strstr(ultimoCorpo, "player_queues/play_media") &&
          strstr(ultimoCorpo, "\"queue_id\":\"media_player.q990f\"") && strstr(ultimoCorpo, "\"radio_mode\":true"), "play on a speaker");
  CONFERE(massist_comando("echo1", "next") == MA_OK && strstr(ultimoCorpo, "players/cmd/next"), "next");
  CONFERE(massist_comando("echo1", "rm -rf") != MA_OK, "unknown command refused");
  statusResp = 401;
  CONFERE(massist_players(pl, 8, &n) == MA_TOKEN, "401: token problem");
  statusResp = 200; resposta = "{\"message_id\":\"1\",\"error_code\":5,\"details\":\"x\"}";
  CONFERE(massist_players(pl, 8, &n) == MA_SERVIDOR, "server error answer");
  return falhas ? 1 : 0;
}
