// QR TIME guest mode in the real Settings screen (src/ajustes.c):
// the account row signs IN for a guest, signs OUT for everyone else, and the
// "Add IPTV" shortcut lands on the Xtream row.
#include "../src/ajustes.c"
#include <assert.h>
static void key(SDL_Keycode k) {
  SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.keysym.sym=k;ajustes_evento(&e);
}
static int linha(int op) {
  for(int i=0;i<AJ_N_TELA;i++)if(TELA[i].tipo==IT_OPC && TELA[i].op==op)return i;
  assert(!"Settings row missing");return -1;
}
static void focarLinha(int op) {
  int i=linha(op);assert(visivel(i) && focavel(i));
  ajPonteiroItem(i,0);assert(focoOp==op);
}
int main(void) {
  setvbuf(stdout,NULL,_IONBF,0);
  assert(getenv("NUVIO_DADOS"));assert(SDL_Init(SDL_INIT_TIMER)==0);
  dados_iniciar(getenv("NUVIO_DADOS"));ajustes_dir(getenv("NUVIO_DADOS"));
  ajustes_iniciar();layout_modo_definir(1);layout_tela_definir(1080,1920);gfx_opacidade_grupo=0;
  assert(dados_gravar("aviso-legal.txt","1\n"));
  assert(!sessao_logada());

  // Not a guest (fresh TV, no session): the row keeps its sign-out meaning.
  assert(!convidado_ativo());
  assert(!strcmp(rotuloOpcao(AJ_SAIR),"Sair da conta"));
  assert(pedeConfirmacao(AJ_SAIR));
  assert(efeitoOpcao(AJ_SAIR)!=NULL);
  puts("PASS: without guest mode the account row is still 'Sair da conta', two OKs");

  // Guest: sign in, one OK, nothing erased.
  assert(convidado_entrar());
  assert(dados_gravar("xtream-p1.txt","sentinel\n"));
  assert(!strcmp(rotuloOpcao(AJ_SAIR),"Entrar com uma conta"));
  assert(!pedeConfirmacao(AJ_SAIR));
  assert(efeitoOpcao(AJ_SAIR)==NULL);
  assert(strstr(ajudaOpcaoBase(AJ_SAIR),"conectar uma conta"));
  assert(!ajustes_quer_sair());
  focarLinha(AJ_SAIR);key(SDLK_RETURN);
  assert(!convidado_ativo());
  assert(ajustes_quer_sair());
  convidado_teste_reiniciar();assert(!convidado_ativo());   // file removed too
  { char *x=dados_ler("xtream-p1.txt");assert(x && !strcmp(x,"sentinel\n"));free(x); }
  puts("PASS: guest row 'Entrar com uma conta' leaves guest mode with one OK, keeps IPTV data");

  // Shortcut from the guest card on an empty Home.
  ajustes_abrir_no_iptv();
  ajustes_iniciar();
  assert(focoOp==AJ_IPTV_ADD_XTREAM);
  puts("PASS: ajustes_abrir_no_iptv lands on 'Adicionar conta Xtream'");
  puts("convidado_ajustes: ok");
  return 0;
}
