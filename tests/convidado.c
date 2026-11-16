// Guest mode (src/convidado.c) against the real dados.c in a temp folder.
#include "convidado.h"
#include "dados.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  assert(argc == 2);
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(NULL);
  assert(!strcmp(dados_dir(), argv[1]));

  // Start-up policy.
  assert(!convidado_deve_entrar(1, 1, 1));  // a real session always wins
  assert(!convidado_deve_entrar(1, 0, 0));
  assert(convidado_deve_entrar(0, 0, 0));   // no account server: guest
  assert(convidado_deve_entrar(0, 1, 1));   // person chose guest
  assert(!convidado_deve_entrar(0, 1, 0));  // server, no choice: login screen
  puts("PASS: start-up policy (session wins; no server or chosen -> guest; else login)");

  // Fresh TV: not a guest, nothing on disk.
  assert(!convidado_ativo());
  char *b = dados_ler("convidado.txt");
  assert(!b);

  // Entering persists across a restart (cache dropped, file read again).
  assert(convidado_entrar());
  assert(convidado_ativo());
  convidado_teste_reiniciar();
  assert(convidado_ativo());
  puts("PASS: guest choice survives a restart");

  // Leaving removes the file, also after a restart.
  convidado_sair();
  assert(!convidado_ativo());
  convidado_teste_reiniciar();
  assert(!convidado_ativo());
  b = dados_ler("convidado.txt");
  assert(!b);
  puts("PASS: leaving guest mode is remembered");

  // Garbage in the file is not a yes.
  assert(dados_gravar("convidado.txt", "0\n"));
  convidado_teste_reiniciar();
  assert(!convidado_ativo());
  puts("convidado: ok");
  return 0;
}
