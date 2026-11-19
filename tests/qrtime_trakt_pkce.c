// QR TIME: linking Trakt without a client secret (PKCE, RFC 7636). The
// package has TRAKT_CLIENT_ID only: the device-code flow (needs the secret) is
// replaced by Trakt's authorization page + the pasted code + code_verifier.
#include "traktauth.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

static char definido[300], corpoToken[1200];
static int nDefinir, ativo, statusToken = 200, nPost;
int trakt_definir(const char *tk, const char *cli) { (void)cli; snprintf(definido, sizeof definido, "%s", tk ? tk : ""); nDefinir++; ativo = 1; return 1; }
int trakt_ativo(void) { return ativo; }
int trakt_recusada(void) { return 0; }
void trakt_sessao_morta(void) {}
void trakt_esquecer(void) { ativo = 0; }
const char *nuvem_trakt_cliente(void) { return "client-id-test"; }
const char *nuvem_trakt_segredo(void) { return ""; }   // the case: no secret
char *dados_ler(const char *n) { (void)n; return NULL; }
int dados_gravar(const char *n, const char *c) { (void)n; (void)c; return 1; }
int dados_apagar(const char *n) { (void)n; return 1; }
char *rede_postar_st_retry(const char *url, int s, const char *const *cab, const char *corpo, int *st, int *retry) {
  (void)s; (void)cab; if (retry) *retry = 0;
  nPost++;
  if (strstr(url, "/oauth/token")) {
    snprintf(corpoToken, sizeof corpoToken, "%s", corpo ? corpo : "");
    *st = statusToken;
    if (statusToken == 200) return strdup("{\"access_token\":\"tok-pkce\",\"refresh_token\":\"ref-pkce\",\"created_at\":1800000000,\"expires_in\":7776000}");
    return strdup("{\"error\":\"invalid_grant\"}");
  }
  *st = 404; return strdup("{}");
}
char *rede_postar_st(const char *u, int s, const char *const *c, const char *b, int *st) { int r; return rede_postar_st_retry(u, s, c, b, st, &r); }
int sessao_logada(void) { return 0; }
int sync_estado(void) { return 2; }
int sync_empurrar_credencial(const char *p, const char *j) { (void)p; (void)j; return 0; }
void desc_repetir(void) {}
const char *i18n(const char *s) { return s; }
const char *idioma_mes_data(int m, const char *n) { (void)m; return n; }

static void passos(int n) { for (int i = 0; i < n; i++) { traktauth_passo((unsigned)(i * 50 + 1000)); usleep(20000); } }

int main(int argc, char **argv) {
  char v[80], ch[64];
  (void)argc; (void)argv;
  // ---- verifier / challenge (the shell script recomputes the SHA-256) ----
  CONFERE(traktauth_pkce_desafio(v, sizeof v, ch, sizeof ch) && strlen(v) == 64 && strlen(ch) == 43, "verifier 64 chars, challenge 43 chars");
  { int ok = 1; for (const char *p = v; *p; p++) ok &= !!strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~", *p);
    for (const char *p = ch; *p; p++) ok &= !!strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", *p);
    CONFERE(ok, "verifier uses the RFC 7636 alphabet; challenge is base64url without padding"); }
  printf("VECTOR %s %s\n", v, ch);
  { char v2[80], ch2[64]; traktauth_pkce_desafio(v2, sizeof v2, ch2, sizeof ch2); CONFERE(strcmp(v, v2) != 0, "a new verifier every time"); }

  // ---- the flow ----
  traktauth_comecar();
  passos(10);
  CONFERE(traktauth_estado() == TRA_AGUARDANDO && traktauth_pkce(), "no secret: PKCE authorization opened (not 'package without keys')");
  CONFERE(strstr(traktauth_url(), "https://trakt.tv/oauth/authorize?response_type=code&client_id=client-id-test") != NULL
          && strstr(traktauth_url(), "code_challenge_method=S256") && strstr(traktauth_url(), "redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob"),
          "the QR is Trakt's authorize page with the S256 challenge and the oob redirect");
  CONFERE(nPost == 0, "nothing sent before the code is pasted");
  CONFERE(!traktauth_pkce_codigo("bad code!") && !traktauth_pkce_codigo("ab"), "invalid code refused locally");
  statusToken = 401;
  CONFERE(traktauth_pkce_codigo("WRONGCODE1"), "a code is accepted for exchange");
  passos(10);
  CONFERE(traktauth_estado() == TRA_AGUARDANDO && traktauth_pkce() && strstr(traktauth_erro(), "recusou"),
          "Trakt refuses the code: still waiting, error shown, can paste again");
  statusToken = 200;
  CONFERE(traktauth_pkce_codigo("  abc123DEF  "), "the right code (spaces from the phone trimmed)");
  passos(10);
  CONFERE(strstr(corpoToken, "\"code\":\"abc123DEF\"") && strstr(corpoToken, "\"grant_type\":\"authorization_code\"")
          && strstr(corpoToken, "\"code_verifier\":\"") && !strstr(corpoToken, "client_secret")
          && strstr(corpoToken, "\"redirect_uri\":\"urn:ietf:wg:oauth:2.0:oob\""),
          "token request: code + code_verifier + oob redirect, no client_secret");
  CONFERE(traktauth_estado() == TRA_LIGADO && !strcmp(definido, "tok-pkce"), "linked: the token is applied");
  CONFERE(!traktauth_pkce(), "PKCE state cleared after linking");
  return falhas ? 1 : 0;
}
