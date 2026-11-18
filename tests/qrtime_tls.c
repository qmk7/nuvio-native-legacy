// QR TIME phase 8: certificate validation on every network path, the
// self-signed exception limited to private-network hosts, and credential
// files written owner-only.
//
//   bash tests/qrtime_tls.sh
#include "rede.h"
#include "dados.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

int main(int argc, char **argv) {
  char url[300];
  int st;
  char *b;
  if (argc < 4) return 2;
  const char *tls = argv[1], *ca = argv[2], *dir = argv[3];

  // ---- the generic (legacy) calls now verify ----
  snprintf(url, sizeof url, "%s/ok", tls);
  rede_discord_ca(NULL);
  b = rede_baixar_st(url, 5, NULL, &st);
  CONFERE(!b && st == 0, "self-signed server, no trust: rede_baixar_st refuses (was accepted before)");
  free(b);
  b = rede_postar_st(url, 5, NULL, "{}", &st);
  CONFERE(!b || st == 0, "same for rede_postar_st");
  free(b);
  rede_discord_ca(ca);
  b = rede_baixar_st(url, 5, NULL, &st);
  CONFERE(b && st == 200, "with the CA bundle that signs it: accepted");
  free(b);
  rede_discord_ca(NULL);

  // ---- rede_pedir: tls_pessoal only on a private host ----
  {
    RedePedido p;
    RedeResposta r;
    memset(&p, 0, sizeof p);
    p.url = url; p.tls_pessoal = 1;
    CONFERE(rede_pedir(&p, &r) && r.status == 200, "personal server on localhost (LAN): self-signed accepted, as before");
    rede_resposta_limpar(&r);
  }
  CONFERE(rede_host_privado("https://192.168.1.20:4533/rest") && rede_host_privado("http://10.0.0.5/") &&
          rede_host_privado("https://172.20.1.1/") && rede_host_privado("https://nas.local:8920/") &&
          rede_host_privado("https://[fd12:3456::1]:8096/") && rede_host_privado("https://[::1]/") &&
          rede_host_privado("https://jellyfin/") && rede_host_privado("https://media.home.arpa/"),
          "private addresses and LAN names are private");
  CONFERE(!rede_host_privado("https://jellyfin.example.com/") && !rede_host_privado("https://8.8.8.8/") &&
          !rede_host_privado("https://172.32.0.1/") && !rede_host_privado("https://192.169.0.1/") &&
          !rede_host_privado("https://[2001:db8::1]/") && !rede_host_privado("https://local.evil.com/") &&
          !rede_host_privado("https://10.0.0.5.evil.com/") && !rede_host_privado("https://user@public.example/") &&
          !rede_host_privado("ftp://10.0.0.1/") && !rede_host_privado(NULL),
          "public hosts (and look-alikes) are never private: always verified");
  CONFERE(rede_host_privado("https://user:pw@192.168.0.2/"), "userinfo does not confuse the host check");

  // ---- credential files are owner-only ----
  {
    struct stat s1, s2;
    char c[600];
    dados_iniciar(dir);
    dados_gravar("music-sources-p1.txt", "secret");
    dados_gravar("music-queue-p1.txt", "not secret");
    dados_caminho(c, sizeof c, "music-sources-p1.txt"); stat(c, &s1);
    dados_caminho(c, sizeof c, "music-queue-p1.txt"); stat(c, &s2);
    CONFERE((s1.st_mode & 077) == 0, "music-sources (Navidrome password): mode 0600");
    CONFERE(dados_e_secreto("trakt-p1.txt") && dados_e_secreto("youtube-account-p2.txt") && dados_e_secreto("lastfm-sessao-p1.txt")
            && dados_e_secreto("xtream-p1.txt") && dados_e_secreto("spotify.txt") && !dados_e_secreto("music-queue-p1.txt")
            && !dados_e_secreto("progresso.txt"), "the secret list covers tokens and keys, not plain data");
    CONFERE((s2.st_mode & 0777) != 0 && (s2.st_mode & S_IRUSR), "plain data is still written normally");
  }
  return falhas ? 1 : 0;
}
