// QR TIME PROFILE PHOTO from the phone: the page lets the person pick a photo;
// the PHONE crops it square and shrinks it to 512 px JPEG (canvas), so the TV
// only receives a small JPEG. The TV checks it is a JPEG, writes it to the
// data folder as perfil-foto-p<N>-<time>.jpg (a new name each time, so the
// texture cache never shows the old one), removes the previous file, and the
// profile list shows it (perfis_fotos_locais). Only on this TV: the account's
// avatar on other devices is not changed.
#include "perfilfoto.h"
#include "celapps.h"
#include "dados.h"
#include "idioma.h"
#include "ilha.h"
#include "perfis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FOTO_MAX (900u * 1024u)

static int b64v(int c) {
  return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
}
int perfilfoto_salvar(int perfil, const unsigned char *jpg, size_t n) {
  char nome[64], ref[48], cam[600], *ant;
  FILE *f;
  if (perfil < 1 || !jpg || n < 64 || n > FOTO_MAX) return 0;
  if (jpg[0] != 0xFF || jpg[1] != 0xD8 || jpg[2] != 0xFF) return 0;   // JPEG only
  snprintf(nome, sizeof nome, "perfil-foto-p%d-%ld.jpg", perfil, (long)time(NULL));
  if (!dados_caminho(cam, sizeof cam, nome) || !(f = fopen(cam, "wb"))) return 0;
  if (fwrite(jpg, 1, n, f) != n) { fclose(f); remove(cam); return 0; }
  fclose(f);
  snprintf(ref, sizeof ref, "perfil-foto-p%d.txt", perfil);
  ant = dados_ler(ref);
  if (!dados_gravar(ref, nome)) { remove(cam); free(ant); return 0; }
  if (ant) {
    ant[strcspn(ant, "\r\n")] = 0;
    if (ant[0] && !strchr(ant, '/') && strcmp(ant, nome)) dados_apagar(ant);
    free(ant);
  }
  perfis_fotos_locais();
  printf("[perfilfoto] perfil %d: foto nova (%zu bytes)\n", perfil, n);
  fflush(stdout);
  return 1;
}
int perfilfoto_remover(int perfil) {
  char ref[48], *ant;
  snprintf(ref, sizeof ref, "perfil-foto-p%d.txt", perfil);
  if (!(ant = dados_ler(ref))) return 0;
  ant[strcspn(ant, "\r\n")] = 0;
  if (ant[0] && !strchr(ant, '/')) dados_apagar(ant);
  free(ant);
  dados_apagar(ref);
  return 1;
}

static void pedido(const LwPedido *p) {
  char *b64;
  unsigned char *o;
  size_t k = 0;
  unsigned v = 0;
  int bits = 0;
  const char *s;
  if (strcmp(p->rota, "foto")) return;
  if (!(b64 = malloc(p->n + 1))) return;
  if (!lanweb_campo(p->corpo, "jpg", b64, p->n + 1)) { free(b64); return; }
  s = strchr(b64, ',') ? strchr(b64, ',') + 1 : b64;   // "data:image/jpeg;base64,..."
  if (!(o = malloc(strlen(s) * 3 / 4 + 4))) { free(b64); return; }
  for (; *s; s++) {
    int x = b64v((unsigned char)*s);
    if (x < 0) continue;
    v = (v << 6) | (unsigned)x; bits += 6;
    if (bits >= 8) { bits -= 8; o[k++] = (unsigned char)(v >> bits); }
  }
  free(b64);
  if (perfilfoto_salvar(perfis_ativo(), o, k)) {
    ilha_avisar("perfilfoto", ILHA_OK, "aj_camera", i18n("Foto do perfil atualizada nesta TV"), 3500u, 0);
    lanweb_publicar("foto", "{\"ok\":1}");
  } else {
    lanweb_publicar("foto", "{\"erro\":1}");
  }
  free(o);
}
static char *pagina(void) {
  char corpo[1600], s[5][200];
  const char *k[5] = { "Escolher foto", "Enviar para a TV", "Pronto.", "Não foi possível usar esta foto.",
                       "A foto é cortada em quadrado e reduzida no celular antes de ir para a TV." };
  for (int i = 0; i < 5; i++) lanweb_escapar(s[i], sizeof s[i], i18n(k[i]));
  snprintf(corpo, sizeof corpo,
    "<label>%s</label><input id=\"f\" type=\"file\" accept=\"image/*\"><canvas id=\"c\" width=\"512\" height=\"512\" "
    "style=\"width:200px;height:200px;border-radius:50%%;display:block;margin:12px auto;background:#0d2048\"></canvas>"
    "<button id=\"env\">%s</button><p><small>%s</small></p><div id=\"msg\" class=\"msg\"></div>"
    "<script>var T={ok:\"%s\",erro:\"%s\"};</script>", s[0], s[1], s[4], s[2], s[3]);
  return celapps_pagina("Foto do perfil", corpo,
    "var c=document.getElementById('c'),x=c.getContext('2d'),tem=0;"
    "document.getElementById('f').onchange=function(){var f=this.files[0];if(!f)return;var u=URL.createObjectURL(f),i=new Image();"
    "i.onload=function(){var m=Math.min(i.width,i.height);x.drawImage(i,(i.width-m)/2,(i.height-m)/2,m,m,0,0,512,512);tem=1;URL.revokeObjectURL(u)};"
    "i.onerror=function(){msg(T.erro)};i.src=u};"
    "document.getElementById('env').onclick=function(){if(!tem)return;msg('\\u2026');var d=c.toDataURL('image/jpeg',0.86);"
    "post('foto',{jpg:d}).then(function(){setTimeout(function(){estado(function(e){msg(e.ok?T.ok:T.erro)})},700)})};");
}
void perfilfoto_iniciar(void) {
  CelAppDef d = { pagina, pedido, NULL, 1600u * 1024u };
  celapps_definir(CA_FOTO, &d);
}
