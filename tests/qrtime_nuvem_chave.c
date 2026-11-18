// QR TIME: Supabase key handling. A publishable key (sb_publishable_...) goes
// only in `apikey`; a legacy anon JWT may also be the Bearer; a signed-in
// user's token is always the Bearer.
#include "nuvem.h"
#include "rede.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static char vistos[4][1400]; static int nv;
static void guardar(const char *const *cab) { nv = 0; for (int i = 0; cab && cab[i] && i < 4; i++) snprintf(vistos[nv++], sizeof vistos[0], "%s", cab[i]); }
static int tem(const char *s) { for (int i = 0; i < nv; i++) if (strstr(vistos[i], s)) return 1; return 0; }
char *rede_postar_st(const char *u, int t, const char *const *cab, const char *b, int *st) { (void)u; (void)t; (void)b; guardar(cab); *st = 200; return strdup("{}"); }
char *rede_baixar_st(const char *u, int t, const char *const *cab, int *st) { (void)u; (void)t; guardar(cab); *st = 200; return strdup("[]"); }
const char *rede_ultimo_erro(void) { return ""; }
int rede_pedir(const RedePedido *p, RedeResposta *r) { (void)p; (void)r; return 0; }
void rede_resposta_limpar(RedeResposta *r) { (void)r; }
static void configurar(const char *dir, const char *chave) {
  char p[600]; FILE *f;
  snprintf(p, sizeof p, "%s/nuvem.txt", dir);
  f = fopen(p, "w"); fprintf(f, "https://api.example.test\n%s\n\n", chave); fclose(f);
  nuvem_configurar(dir);
}
int main(int argc, char **argv) {
  int st; char *r;
  if (argc < 2) return 2;
  configurar(argv[1], "sb_publishable_TESTKEY");
  CONFERE(nuvem_pronta() && !nuvem_chave_jwt(), "publishable key recognised");
  r = nuvem_post("/auth/v1/signup", "{}", NULL, &st); free(r);
  CONFERE(tem("apikey: sb_publishable_TESTKEY") && !tem("Authorization"), "no user: publishable key only in apikey, no Bearer");
  r = nuvem_tabela("profiles", "select=id", NULL, &st); free(r);
  CONFERE(!tem("Authorization"), "table read without user: no Bearer either");
  r = nuvem_post("/rest/v1/rpc/x", "{}", "user-token", &st); free(r);
  CONFERE(tem("apikey: sb_publishable_TESTKEY") && tem("Authorization: Bearer user-token"), "signed in: the user's token is the Bearer");
  configurar(argv[1], "eyJhbGciOiJIUzI1NiJ9.test.sig");
  CONFERE(nuvem_chave_jwt(), "legacy anon JWT recognised");
  r = nuvem_post("/auth/v1/signup", "{}", NULL, &st); free(r);
  CONFERE(tem("Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.test.sig"), "legacy anon JWT: still sent as Bearer (old servers unchanged)");
  return falhas ? 1 : 0;
}
