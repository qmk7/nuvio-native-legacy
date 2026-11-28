// QR TIME "What's this song?": the player button's background job and the
// result card (title, artist, album; "Search on YouTube" opens the official
// YouTube app with a search). See songid.h.
#include "songid.h"
#include "idioma.h"
#include "ilha.h"
#include "ondever.h"
#include "player.h"
#include <stdio.h>
#include <string.h>

static char busca[340];
#include <SDL.h>
static Uint32 desde;
// Every answer is a notice (ilha), not a player toast: it is seen even when the
// player was closed while listening.
static void avisar(const char *t, unsigned ms) { ilha_avisar("songid", ILHA_INFO, "aj_music", i18n(t), ms, 0); }
void songidui_iniciar(const char *url, double seg) {
  if (!songid_tem_chave()) { avisar("Falta a chave do AudD (Celular > Configurar música).", 4500u); return; }
  if (songid_rodando()) { avisar("Ainda ouvindo a música…", 2500u); return; }
  if (!url || !url[0]) { avisar("Não disponível nesta fonte (só arquivos MKV).", 4500u); return; }
  if (songid_iniciar(url, seg)) { desde = SDL_GetTicks(); avisar("Ouvindo a música… (uns segundos)", 6000u); }
  else avisar("Não foi possível começar. Tente de novo.", 3500u);
}
void songidui_passo(void) {
  SidMusica m;
  int r = songid_resultado(&m);
  if (r < 0) {
    if (desde && songid_rodando() && SDL_GetTicks() - desde > 15000u) { desde = 0; avisar("Ainda ouvindo… a fonte está lenta.", 6000u); }
    return;
  }
  desde = 0;
  if (r != SID_OK) {
    const char *t = r == SID_NAO_ACHOU ? "Música não reconhecida. Tente num trecho sem diálogo." :
                    r == SID_NAO_MKV ? "Não disponível nesta fonte (só arquivos MKV)." :
                    r == SID_CODEC || r == SID_SEM_AUDIO ? "O áudio desta fonte não pode ser enviado." :
                    r == SID_SEM_CHAVE ? "Falta a chave do AudD (Celular > Configurar música)." :
                    r == SID_REDE ? "Sem resposta da rede." : "O AudD recusou o pedido (confira a chave).";
    printf("[musica-id] resultado %d\n", r);
    fflush(stdout);
    avisar(t, 5000u);
    return;
  }
  {
    IlhaModal md;
    IlhaAvisoEx a;
    char txt[400];
    memset(&md, 0, sizeof md); memset(&a, 0, sizeof a);
    snprintf(busca, sizeof busca, "%s %s", m.artista, m.titulo);
    snprintf(md.kicker, sizeof md.kicker, "%s", i18n("Esta música"));
    snprintf(md.titulo, sizeof md.titulo, "%s", m.titulo);
    snprintf(md.linha, sizeof md.linha, "%s", m.artista);
    snprintf(md.nota, sizeof md.nota, "%s", m.album);
    snprintf(md.icone, sizeof md.icone, "aj_music");
    md.nBotoes = 2;
    snprintf(md.botao[0], sizeof md.botao[0], "%s", i18n("Procurar no YouTube"));
    snprintf(md.botao[1], sizeof md.botao[1], "%s", i18n("Fechar"));
    snprintf(txt, sizeof txt, "%s — %s", m.titulo, m.artista);
    a.chave = "songid"; a.tipo = ILHA_ACENTO; a.icone = "aj_music"; a.texto = txt; a.ms = 12000u; a.modal = &md; a.tecla = 1;
    ilha_avisar_ex(&a);
  }
}
void songidui_acao(int botao) { if (botao == 1 && busca[0]) ondever_youtube_busca(busca); }
