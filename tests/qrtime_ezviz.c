// QR TIME EZVIZ alerts (src/ezviz.c): the token and alarm-list answers as
// EZVIZ documents them (no account here; the live API is unverified).
//   bash tests/qrtime_ezviz.sh
#include "ezviz.h"
#include "celapps.h"
#include <stdio.h>
#include <string.h>
// Stubs: the module's app-side neighbours.
void celapps_definir(int a, const CelAppDef *d) { (void)a; (void)d; }
char *celapps_pagina(const char *t, const char *c, const char *j) { (void)t; (void)c; (void)j; return NULL; }
int lanweb_campo(const char *c, const char *n, char *d, size_t m) { (void)c; (void)n; if (m) d[0] = 0; return 0; }
void lanweb_escapar(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s); }
const char *i18n(const char *s) { return s; }
void ilha_avisar(const char *c, int t, const char *i, const char *x, unsigned m, int k) { (void)c; (void)t; (void)i; (void)x; (void)m; (void)k; }
struct IlhaAvisoEx; void ilha_avisar_ex(const void *a) { (void)a; }
char *rede_postar_st(const char *u, int s, const char *const *c, const char *b, int *st) { (void)u; (void)s; (void)c; (void)b; *st = 0; return NULL; }
char *dados_ler(const char *n) { (void)n; return NULL; }
int dados_gravar(const char *n, const char *c) { (void)n; (void)c; return 1; }
int dados_apagar(const char *n) { (void)n; return 1; }
static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
int main(void) {
  char t[128], a[200];
  long long ex;
  EzAlarme al[4];
  CONFERE(ezviz_parse_token("{\"data\":{\"accessToken\":\"at.abc123\",\"expireTime\":1470810222045,\"areaDomain\":\"https://isgpopen.ezvizlife.com\"},\"code\":\"200\",\"msg\":\"Operation succeeded!\"}",
                            t, sizeof t, a, sizeof a, &ex) && !strcmp(t, "at.abc123") && !strcmp(a, "https://isgpopen.ezvizlife.com") && ex == 1470810222045LL,
          "token answer: token, region domain, expiry");
  CONFERE(!ezviz_parse_token("{\"code\":\"10017\",\"msg\":\"appKey not exist\"}", t, sizeof t, a, sizeof a, &ex), "wrong AppKey: refused");
  CONFERE(ezviz_parse_alarmes("{\"page\":{\"total\":2,\"page\":0,\"size\":5},\"data\":[{\"alarmId\":\"A1\",\"deviceSerial\":\"C12345678\",\"alarmName\":\"Doorbell\","
                              "\"alarmType\":10000,\"alarmTime\":1795372000000,\"alarmPicUrl\":\"https://x/p.jpg\",\"isEncrypt\":0},"
                              "{\"alarmId\":\"A0\",\"deviceSerial\":\"C12345678\",\"alarmName\":\"Motion\",\"alarmType\":10002,\"alarmTime\":1795371000000,\"alarmPicUrl\":\"\",\"isEncrypt\":1}],"
                              "\"code\":\"200\",\"msg\":\"ok\"}", al, 4) == 2
          && !strcmp(al[0].id, "A1") && !strcmp(al[0].nome, "Doorbell") && al[0].tipo == 10000 && !al[0].cifrada && al[1].cifrada,
          "alarm list: newest first, name, picture, encrypted flag");
  CONFERE(ezviz_parse_alarmes("{\"code\":\"10002\",\"msg\":\"accessToken expired\"}", al, 4) == -1, "expired token: error (token renewed next time)");
  CONFERE(ezviz_parse_alarmes("{\"page\":{\"total\":0},\"data\":[],\"code\":\"200\"}", al, 4) == 0, "no alarms");
  return falhas ? 1 : 0;
}
