// Guest add-ons (src/addonslocal.c) over real HTTP against the local fixture.
// addons.c is replaced by a small in-memory list with the same contract
// (addons_definir_lista, addons_esquecer, addons_n/base/nome/ativo).
//
//   qrtime_addonslocal <data dir> <base url>
#include "addonslocal.h"
#include "addons.h"
#include "dados.h"
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

static AddonRemoto lista[64];
static int nLista, esquecida;
int addons_definir_lista(const AddonRemoto *l, int n) { memcpy(lista, l, sizeof *l * (size_t)n); nLista = n; return 1; }
void addons_esquecer(void) { nLista = 0; esquecida++; }
int addons_n(void) { return nLista; }
const char *addons_base(int i) { return i >= 0 && i < nLista ? lista[i].url : ""; }
const char *addons_nome(int i) { return i >= 0 && i < nLista ? lista[i].nome : ""; }
int addons_ativo(int i) { return i >= 0 && i < nLista ? lista[i].ativo : 0; }

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static int esperar(char *nome, size_t n) {
  int e = ADL_TRABALHANDO;
  for (int i = 0; i < 300 && e == ADL_TRABALHANDO; i++) { e = addonslocal_estado(nome, n); if (e == ADL_TRABALHANDO) usleep(20000); }
  return e;
}

int main(int argc, char **argv) {
  char b[512], u[600], nome[64];
  if (argc != 3) { fprintf(stderr, "usage: %s <data dir> <base url>\n", argv[0]); return 2; }
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(NULL);

  CONFERE(addonslocal_base("https://a.example/x/manifest.json", b, sizeof b) && !strcmp(b, "https://a.example/x"), "manifest URL -> base");
  CONFERE(addonslocal_base("stremio://a.example/k/manifest.json", b, sizeof b) && !strcmp(b, "https://a.example/k"), "stremio:// -> https");
  CONFERE(addonslocal_base("  https://a.example/x/  ", b, sizeof b) && !strcmp(b, "https://a.example/x"), "spaces and trailing slash");
  CONFERE(!addonslocal_base("ftp://a.example/x", b, sizeof b) && !addonslocal_base("hello", b, sizeof b) &&
          !addonslocal_base("https://a b.example/x", b, sizeof b), "not an http(s) add-on address");

  addonslocal_carregar();
  CONFERE(nLista == 0, "no saved list: no add-ons");
  snprintf(u, sizeof u, "%s/addon/manifest.json", argv[2]);
  CONFERE(addonslocal_adicionar(u) && esperar(nome, sizeof nome) == ADL_OK && !strcmp(nome, "QRT Test Addon") &&
          nLista == 1 && lista[0].ativo, "valid manifest: added and applied, named from the manifest");
  CONFERE(addonslocal_adicionar(u) && esperar(nome, sizeof nome) == ADL_REPETIDO && nLista == 1, "the same add-on twice: refused");
  snprintf(u, sizeof u, "%s/notaddon/manifest.json", argv[2]);
  CONFERE(addonslocal_adicionar(u) && esperar(nome, sizeof nome) == ADL_FALHOU && nLista == 1, "an HTML page is not an add-on");
  snprintf(u, sizeof u, "%s/addon2", argv[2]);
  CONFERE(addonslocal_adicionar(u) && esperar(nome, sizeof nome) == ADL_OK && nLista == 2, "base URL without manifest.json works");
  { char *f = dados_ler("addons-local-p1.txt");
    CONFERE(f && strstr(f, "QRT Test Addon") && strstr(f, "QRT Second Addon"), "saved per profile");
    free(f); }

  lista[0].ativo = 0; addonslocal_salvar();
  nLista = 0; addonslocal_carregar();
  CONFERE(nLista == 2 && !lista[0].ativo && lista[1].ativo, "on/off survives a reload");
  CONFERE(addonslocal_remover_desligados() == 1 && nLista == 1 && !strcmp(lista[0].nome, "QRT Second Addon"),
          "remove switched-off add-ons");
  lista[0].ativo = 0; addonslocal_salvar();
  CONFERE(addonslocal_remover_desligados() == 1 && nLista == 0 && esquecida > 0, "removing the last one empties the list");

  perfil = 2; nLista = 0;
  addonslocal_carregar();
  CONFERE(nLista == 0, "profile 2 has its own (empty) list");
  return falhas ? 1 : 0;
}
