#include "qrcontasui.h"
#include "qrcontas.h"
#include "qrdados.h"
#include "gfx.h"
#include "text.h"
#include "layout.h"
#include "idioma.h"
#include "ilha.h"
#include "teclado.h"
#include "celapps.h"
#include "legtrad.h"
#include "songid.h"
#include "qrtex.h"
#include <stdio.h>
#include <string.h>

enum { ABA_PROV, ABA_DADOS, ABA_CEL, ABA_N };
static const char *const ABAS[ABA_N] = { "Provedores", "Dados e limpeza", "Celular" };
static GLuint texQr;
static char qrDe[160];
enum { Z_ABAS, Z_LISTA };
static QrcLinha linhas[QRC_MAX];
static int nLinhas, aba, zona, foco, botao, sair, pedido, tecladoTsdb;
static int confirmando = -1, botaoConf;   // QRR_* waiting; 0 = Cancel (default), 1 = confirm
static float refazer;
static unsigned long long cacheBytes;
static int cacheMedido;

static void texto(TxtEstilo s, const char *t, float x, float y, float a) { txt_desenhar_alpha(txt_linha(s, t, 240, 242, 248, 255), x, y, a); }
static void corta(TxtEstilo s, const char *t, float x, float y, float w, float a) { txt_desenhar_alpha(txt_linha_corta(s, t, 240, 242, 248, 255, w), x, y, a); }

// Buttons the focused provider row offers, in order.
enum { B_TESTAR, B_ALTERNAR, B_ATUALIZAR, B_ABRIR, B_CHAVE, B_N };
static int botoes(const QrcLinha *l, int *out) {
  int n = 0;
  if (l->pode & QRC_P_TESTAR) out[n++] = B_TESTAR;
  if (l->pode & QRC_P_ALTERNAR) out[n++] = B_ALTERNAR;
  if (l->pode & QRC_P_ATUALIZAR) out[n++] = B_ATUALIZAR;
  if (l->pode & QRC_P_CHAVE) out[n++] = B_CHAVE;
  if (l->pode & QRC_P_ABRIR) out[n++] = B_ABRIR;
  return n;
}
static const char *rotuloBotao(int b, const QrcLinha *l) {
  switch (b) {
    case B_TESTAR: return "Testar conexão";
    case B_ALTERNAR: return l->estado == QRC_E_DESLIGADO ? "Ligar" : "Desligar";
    case B_ATUALIZAR: return "Atualizar dados";
    case B_CHAVE: return "Chave pessoal";
    default: return "Abrir ajustes";
  }
}
static void montar(void) {
  nLinhas = qrc_montar(linhas, QRC_MAX);
  if (foco >= nLinhas) foco = nLinhas ? nLinhas - 1 : 0;
}
static int nItens(void) { return aba == ABA_PROV ? nLinhas : aba == ABA_CEL ? CA_N : QRR_N; }

void qrcui_abrir(void) {
  aba = ABA_PROV; zona = Z_LISTA; foco = 0; botao = 0; sair = 0; pedido = QRCUI_IR_NADA;
  confirmando = -1; tecladoTsdb = 0; cacheMedido = 0;
  montar();
  printf("[contas] aberto: %d provedor(es)\n", nLinhas);
  for (int i = 0; i < nLinhas; i++) printf("[contas]   %d tipo=%d estado=%d pode=%d\n", i, linhas[i].tipo, linhas[i].estado, linhas[i].pode);
  fflush(stdout);
}
int qrcui_quer_sair(void) { int v = sair; sair = 0; return v; }
int qrcui_pedido(void) { int v = pedido; pedido = QRCUI_IR_NADA; return v; }

static void abrirDe(const QrcLinha *l) {
  switch (l->tipo) {
    case QRC_IPTV: pedido = QRCUI_IR_IPTV; break;
    case QRC_TRAKT: pedido = QRCUI_IR_TRAKT; break;
    case QRC_MUSICA: case QRC_LASTFM: case QRC_SPOTIFY: pedido = QRCUI_IR_MUSICA; break;
    case QRC_YOUTUBE: pedido = QRCUI_IR_YOUTUBE; break;
    case QRC_ADDONS: pedido = QRCUI_IR_ADDONS; break;
    default: pedido = QRCUI_IR_AJUSTES; break;
  }
}
static void executar(void) {
  QrcLinha *l;
  int b[B_N], nb;
  if (foco < 0 || foco >= nLinhas) return;
  l = &linhas[foco];
  nb = botoes(l, b);
  if (!nb) return;
  if (botao >= nb) botao = nb - 1;
  switch (b[botao]) {
    case B_TESTAR:
      if (!qrc_testar(l)) ilha_avisar("qrcontas", ILHA_INFO, NULL, i18n("Já há um teste em andamento."), 2500u, 0);
      break;
    case B_ALTERNAR:
      if (qrc_alternar(l)) { ilha_avisar("qrcontas", ILHA_INFO, NULL, i18n(l->estado == QRC_E_DESLIGADO ? "Ligado." : "Desligado. Nada foi apagado."), 2500u, 0); montar(); }
      else ilha_avisar("qrcontas", ILHA_INFO, NULL, i18n("Não foi possível salvar."), 2500u, 0);
      break;
    case B_ATUALIZAR:
      if (qrc_atualizar(l)) ilha_avisar("qrcontas", ILHA_INFO, NULL, i18n("Atualizando. Os dados atuais ficam na tela até chegar a nova versão."), 3500u, 0);
      break;
    case B_CHAVE:
      tecladoTsdb = l->tipo;
      teclado_contexto(l->tipo == QRC_DEEPL ? "DeepL" : l->tipo == QRC_AUDD ? "AudD" : "TheSportsDB");
      if (l->tipo == QRC_DEEPL)
        teclado_abrir_com("Chave da API do DeepL", "Em deepl.com/pro-api (o plano gratuito serve). Vazio remove.", 80,
                          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-:", NULL);
      else if (l->tipo == QRC_AUDD)
        teclado_abrir_com("Token da API do AudD", "Em dashboard.audd.io. Vazio remove.", 64,
                          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", NULL);
      else
        teclado_abrir_com("Chave pessoal do TheSportsDB", "Vazio volta para a chave pública gratuita.", 40,
                          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", NULL);
      teclado_permitir_vazio(1);
      teclado_mascarar(1);
      break;
    default: abrirDe(l); break;
  }
}
static void resetar(int op) {
  int ok = qrdados_resetar(op);
  cacheMedido = 0;
  montar();
  ilha_avisar("qrcontas", ILHA_INFO, NULL, i18n(ok ? "Feito." : "Feito em parte. Tente de novo."), 3000u, 0);
}

void qrcui_evento(const SDL_Event *e) {
  SDL_Keycode k;
  int voltar, ok;
  if (teclado_aberto()) { teclado_evento(e); return; }
  if (e->type != SDL_KEYDOWN) return;
  k = e->key.keysym.sym;
  voltar = k == SDLK_AC_BACK || k == SDLK_ESCAPE || k == SDLK_BACKSPACE;
  ok = k == SDLK_RETURN || k == SDLK_KP_ENTER;
  if (confirmando >= 0) {
    // Cancel is focused first; Back always cancels.
    if (voltar) { confirmando = -1; return; }
    if (k == SDLK_LEFT) botaoConf = 0;
    if (k == SDLK_RIGHT) botaoConf = 1;
    if (ok) { int op = confirmando; confirmando = -1; printf("[contas] reset %d: %s\n", op, botaoConf == 1 ? "confirmado" : "cancelado"); fflush(stdout); if (botaoConf == 1) resetar(op); }
    return;
  }
  if (voltar) { if (zona == Z_LISTA) zona = Z_ABAS; else sair = 1; return; }
  if (zona == Z_ABAS) {
    if (k == SDLK_LEFT && aba > 0) { aba--; foco = 0; botao = 0; }
    else if (k == SDLK_RIGHT && aba + 1 < ABA_N) { aba++; foco = 0; botao = 0; }
    else if ((k == SDLK_DOWN || ok) && nItens()) zona = Z_LISTA;
    return;
  }
  if (k == SDLK_UP) { if (foco > 0) { foco--; botao = 0; } else zona = Z_ABAS; return; }
  if (k == SDLK_DOWN) { if (foco + 1 < nItens()) { foco++; botao = 0; } return; }
  if (aba == ABA_PROV) {
    int b[B_N], nb = foco < nLinhas ? botoes(&linhas[foco], b) : 0;
    if (k == SDLK_LEFT && botao > 0) botao--;
    else if (k == SDLK_RIGHT && botao + 1 < nb) botao++;
    else if (ok) executar();
    return;
  }
  if (aba == ABA_CEL) {
    // OK puts the page on the network (or, when it is up, makes a new link:
    // the old one stops working). Right also renews.
    if (ok || k == SDLK_RIGHT) {
      int ja = celapps_url(foco)[0] != 0;
      if (ja) { celapps_renovar(foco); ilha_avisar("qrcontas", ILHA_INFO, "aj_qr-code", i18n("Novo link: o anterior deixou de funcionar."), 3000u, 0); }
      else if (!celapps_abrir(foco)) ilha_avisar("qrcontas", ILHA_ERRO, "aj_smartphone", i18n("A TV não está numa rede local agora."), 3500u, 0);
      printf("[contas] celular %d: %s\n", foco, celapps_url(foco)[0] ? "no ar" : "fora");
      fflush(stdout);
    }
    return;
  }
  if (ok) { confirmando = foco; botaoConf = 0; printf("[contas] confirmar reset %d (foco em Cancelar)\n", foco); fflush(stdout); }
}

void qrcui_atualizar(float dt, Uint32 agora) {
  (void)agora;
  if (tecladoTsdb) {
    int r;
    if (teclado_aberto()) teclado_atualizar(dt, agora);
    r = teclado_resultado();
    if (r == TECLADO_PRONTO) {
      int okG = tecladoTsdb == QRC_DEEPL ? legtrad_definir_chave(teclado_texto()) :
                tecladoTsdb == QRC_AUDD ? songid_definir_chave(teclado_texto()) : qrc_tsdb_chave(teclado_texto());
      teclado_esquecer();
      ilha_avisar("qrcontas", ILHA_INFO, NULL, i18n(okG ? "Salvo neste aparelho." : "Valor inválido: use só letras, números, - _ ."), 3000u, 0);
      tecladoTsdb = 0; montar();
    } else if (r == TECLADO_CANCELOU) { teclado_esquecer(); tecladoTsdb = 0; }
  }
  // Test results arrive from workers; the list re-reads the modules' state.
  refazer += dt;
  if (refazer >= 0.5f) { refazer = 0; if (aba == ABA_PROV) montar(); }
  if (aba == ABA_DADOS && !cacheMedido) { cacheBytes = qrdados_cache_bytes(); cacheMedido = 1; }
}

static void estadoCor(int e, float *r, float *g, float *b) {
  switch (e) {
    case QRC_E_LIGADO: *r = 0.30f; *g = 0.80f; *b = 0.45f; break;
    case QRC_E_ERRO: *r = 0.95f; *g = 0.35f; *b = 0.35f; break;
    case QRC_E_DESLIGADO: *r = 0.55f; *g = 0.55f; *b = 0.60f; break;
    case QRC_E_TESTANDO: *r = 0.95f; *g = 0.75f; *b = 0.30f; break;
    case QRC_E_CONFIG: *r = 0.45f; *g = 0.65f; *b = 0.95f; break;
    default: *r = 0.40f; *g = 0.40f; *b = 0.45f; break;
  }
}
static const char *estadoTxt(int e) {
  switch (e) {
    case QRC_E_LIGADO: return "Conectado";
    case QRC_E_ERRO: return "Falhou";
    case QRC_E_DESLIGADO: return "Desligado";
    case QRC_E_TESTANDO: return "Testando…";
    case QRC_E_CONFIG: return "Configurado";
    default: return "Não configurado";
  }
}
static float botaoDesenhar(const char *t, float x, float y, int focado) {
  TxtLinha l = txt_linha(TXT_CAPTION, i18n(t), focado ? 20 : 240, focado ? 20 : 242, focado ? 24 : 248, 255);
  float w = (float)l.w + 32;
  gfx_cor((GfxRect){ x, y, w, 40 }, 0.5f, 1, 1, 1, focado ? 0.92f : 0.10f);
  txt_desenhar_alpha(l, x + 16, y + 6, 1);
  return w + 10;
}

static void desenharProvedores(float x, float y, float w) {
  float rh = 118, area = NV_TELA_H - 60 - y;
  int vis = (int)(area / rh), de;
  if (vis < 1) vis = 1;
  de = foco >= vis ? foco - vis + 1 : 0;
  for (int i = de; i < nLinhas && i < de + vis; i++) {
    QrcLinha *l = &linhas[i];
    int f = zona == Z_LISTA && i == foco, est = l->estado;
    float ry = y + (float)(i - de) * rh, cr, cg, cb;
    char msg[160];
    int rt = qrc_resultado(l, msg, sizeof msg);
    if (rt >= 0 && est != QRC_E_DESLIGADO) est = rt;
    if (f) gfx_cor((GfxRect){ x - 12, ry - 6, w + 24, rh - 10 }, 0.25f, 1, 1, 1, 0.10f);
    estadoCor(est, &cr, &cg, &cb);
    gfx_cor((GfxRect){ x, ry + 14, 14, 14 }, 0.5f, cr, cg, cb, 1);
    corta(TXT_BODY, l->traduzir ? i18n(l->nome) : l->nome, x + 30, ry + 2, w * 0.45f, f ? 1 : 0.9f);
    corta(TXT_CAPTION, i18n(estadoTxt(est)), x + 30 + w * 0.47f, ry + 8, w * 0.2f, 0.85f);
    corta(TXT_CAPTION, rt >= 0 && msg[0] ? i18n(msg) : i18n(l->detalhe), x + 30, ry + 46, f ? w * 0.42f : w - 40, 0.6f);
    if (f) {
      int b[B_N], nb = botoes(l, b);
      float bx = x + w * 0.45f;
      for (int j = 0; j < nb; j++) bx += botaoDesenhar(rotuloBotao(b[j], l), bx, ry + 42, j == botao);
    }
  }
}
static void desenharDados(float x, float y, float w) {
  char b[200];
  float rh = 104, area;
  int vis, de;
  snprintf(b, sizeof b, "%s %d%s  ·  %s %.1f MB", i18n("Versão dos dados:"), qrdados_esquema_disco(),
           qrdados_mais_novo() ? i18n(" (de uma versão mais nova do app, mantida)") : "", i18n("Cache:"), (double)cacheBytes / 1048576.0);
  corta(TXT_CAPTION, b, x, y, w, 0.75f);
  corta(TXT_CAPTION, i18n("Nada é apagado sem confirmação. Sair desta tela não apaga nada."), x, y + 36, w, 0.6f);
  y += 84;
  area = NV_TELA_H - 60 - y;
  vis = (int)(area / rh); if (vis < 1) vis = 1;
  de = foco >= vis ? foco - vis + 1 : 0;
  for (int i = de; i < QRR_N && i < de + vis; i++) {
    int f = zona == Z_LISTA && i == foco;
    float ry = y + (float)(i - de) * rh;
    if (f) gfx_cor((GfxRect){ x - 12, ry - 6, w + 24, rh - 10 }, 0.25f, 1, 1, 1, 0.10f);
    corta(TXT_BODY, i18n(qrdados_reset_nome(i)), x, ry, w * 0.7f, f ? 1 : 0.9f);
    corta(TXT_CAPTION, i18n(qrdados_reset_por_tv(i) ? "Nesta TV" : "Neste perfil"), x + w * 0.75f, ry + 6, w * 0.25f, 0.7f);
    corta(TXT_CAPTION, i18n(qrdados_reset_desc(i)), x, ry + 44, w, 0.6f);
  }
}
static void desenharCelular(float x, float y, float w) {
  float rh = 104, lw = w * 0.52f, qx = x + lw + 60, lado = 380;
  const char *u = celapps_url(foco);
  corta(TXT_CAPTION, i18n("O celular precisa estar no mesmo Wi-Fi da TV. Cada página tem seu próprio link."), x, y, w, 0.7f);
  y += 56;
  for (int i = 0; i < CA_N; i++) {
    int f = zona == Z_LISTA && i == foco, no = celapps_url(i)[0] != 0;
    float ry = y + (float)i * rh;
    if (f) gfx_cor((GfxRect){ x - 12, ry - 6, lw + 24, rh - 10 }, 0.25f, 1, 1, 1, 0.10f);
    gfx_icone((GfxRect){ x, ry + 4, 36, 36 }, celapps_icone(i), 1, 1, 1, f ? 1 : 0.8f);
    corta(TXT_BODY, i18n(celapps_nome(i)), x + 52, ry, lw - 180, f ? 1 : 0.9f);
    if (no) { gfx_cor((GfxRect){ x + lw - 110, ry + 12, 12, 12 }, 0.5f, 0.30f, 0.80f, 0.45f, 1); corta(TXT_CAPTION, i18n("No ar"), x + lw - 92, ry + 4, 100, 0.8f); }
    corta(TXT_CAPTION, i18n(celapps_desc(i)), x + 52, ry + 44, lw - 60, 0.6f);
  }
  if (zona != Z_LISTA) return;
  if (u[0]) {
    if (strcmp(qrDe, u)) { snprintf(qrDe, sizeof qrDe, "%s", u); qr_textura(&texQr, u); }
    if (texQr) gfx_textura((GfxRect){ qx, y, lado, lado }, texQr);
    corta(TXT_CAPTION, u, qx, y + lado + 16, w - lw - 60, 0.75f);
    txt_bloco_corta(TXT_CAPTION, i18n("Leia com a câmera do celular. OK ou direita: novo link (o anterior para de funcionar)."),
                    240, 242, 248, qx, y + lado + 60, w - lw - 60, 34, 0.6f, 3);
  } else {
    txt_bloco_corta(TXT_CALLOUT, i18n("Aperte OK para abrir esta página no celular."), 240, 242, 248, qx, y + 40, w - lw - 60, 44, 0.85f, 3);
  }
}
static void desenharConfirmacao(void) {
  float w = 900, h = 330, x = (NV_TELA_W - w) / 2, y = (NV_TELA_H - h) / 2;
  gfx_cor((GfxRect){ 0, 0, NV_TELA_W, NV_TELA_H }, 0, 0, 0, 0, 0.6f);
  gfx_cor((GfxRect){ x, y, w, h }, 0.06f, 0.10f, 0.105f, 0.12f, 1);
  corta(TXT_TITULO2, i18n(qrdados_reset_nome(confirmando)), x + 40, y + 34, w - 80, 1);
  txt_bloco_corta(TXT_BODY, i18n(qrdados_reset_desc(confirmando)), 240, 242, 248, x + 40, y + 110, w - 80, 40, 0.8f, 3);
  corta(TXT_CAPTION, i18n(qrdados_reset_por_tv(confirmando) ? "Vale para todos os perfis desta TV." : "Só o perfil atual."), x + 40, y + 222, w - 80, 0.65f);
  {
    float bx = x + 40;
    bx += botaoDesenhar("Cancelar", bx, y + 266, botaoConf == 0);
    botaoDesenhar("Apagar", bx, y + 266, botaoConf == 1);
  }
}
void qrcui_desenhar(Uint32 agora) {
  float x = NV_MARGEM_X, y = NV_MARGEM_Y, w = NV_TELA_W - 2 * NV_MARGEM_X;
  gfx_cor((GfxRect){ 0, 0, NV_TELA_W, NV_TELA_H }, 0, 0.055f, 0.058f, 0.068f, 1);
  texto(TXT_TITULO2, i18n("Contas, provedores e dados"), x, y, 1);
  y += 80;
  {
    float tx = x;
    for (int i = 0; i < ABA_N; i++) {
      TxtLinha l = txt_linha(TXT_CALLOUT, i18n(ABAS[i]), 240, 242, 248, 255);
      float lw = (float)l.w + 40;
      int f = zona == Z_ABAS && i == aba;
      gfx_cor((GfxRect){ tx, y, lw, 52 }, 0.5f, 1, 1, 1, i == aba ? (f ? 0.9f : 0.18f) : 0.06f);
      if (f) txt_desenhar_alpha(txt_linha(TXT_CALLOUT, i18n(ABAS[i]), 20, 20, 24, 255), tx + 20, y + 10, 1);
      else txt_desenhar_alpha(l, tx + 20, y + 10, i == aba ? 1 : 0.7f);
      tx += lw + 12;
    }
    y += 84;
  }
  if (aba == ABA_PROV) desenharProvedores(x, y, w);
  else if (aba == ABA_CEL) desenharCelular(x, y, w);
  else desenharDados(x, y, w);
  if (confirmando >= 0) desenharConfirmacao();
  if (teclado_aberto()) teclado_desenhar(agora);
}

int qrcui_teste_aba(void) { return aba; }
int qrcui_teste_n(void) { return nItens(); }
int qrcui_teste_foco(void) { return foco; }
int qrcui_teste_confirmando(void) { return confirmando; }
int qrcui_teste_botao(void) { return botaoConf; }
