#include "m3uvod.h"
#include "m3u.h"
#include "rede.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int m3uvod_url_vod(const char *url) {
  static const char *const ext[] = { ".mp4", ".mkv", ".avi", ".mov", ".m4v", ".wmv", NULL };
  const char *q, *fim;
  size_t n;
  if (!url || (strncmp(url, "http://", 7) && strncmp(url, "https://", 8))) return 0;
  q = strchr(url, '?');
  fim = q ? q : url + strlen(url);
  n = (size_t)(fim - url);
  for (int i = 0; ext[i]; i++) {
    size_t k = strlen(ext[i]);
    if (n > k && !strncasecmp(fim - k, ext[i], k)) return 1;
  }
  return strstr(url, "/movie/") != NULL || strstr(url, "/series/") != NULL;
}

int m3uvod_episodio(const char *nome, char *serie, size_t tam, int *t, int *e) {
  for (const char *p = nome; p && *p; p++) {
    int a = 0, b = 0, k = 0;
    const char *q = p;
    if ((*q == 'S' || *q == 's') && isdigit((unsigned char)q[1]) && (q == nome || !isalnum((unsigned char)q[-1]))) {
      q++;
      while (isdigit((unsigned char)*q) && k < 4) { a = a * 10 + (*q++ - '0'); k++; }
      if (*q == ' ') q++;
      if ((*q == 'E' || *q == 'e') && isdigit((unsigned char)q[1])) {
        q++; k = 0;
        while (isdigit((unsigned char)*q) && k < 5) { b = b * 10 + (*q++ - '0'); k++; }
      } else continue;
    } else if (isdigit((unsigned char)*q) && (q == nome || q[-1] == ' ')) {
      while (isdigit((unsigned char)*q) && k < 3) { a = a * 10 + (*q++ - '0'); k++; }
      if ((*q != 'x' && *q != 'X') || !isdigit((unsigned char)q[1])) continue;
      q++; k = 0;
      while (isdigit((unsigned char)*q) && k < 4) { b = b * 10 + (*q++ - '0'); k++; }
    } else continue;
    if (isalnum((unsigned char)*q) || b <= 0) continue;
    {
      size_t n = (size_t)(p - nome);
      while (n && (nome[n - 1] == ' ' || nome[n - 1] == '-' || nome[n - 1] == '|' || nome[n - 1] == ':')) n--;
      if (!n || n >= tam) return 0;
      memcpy(serie, nome, n);
      serie[n] = 0;
    }
    *t = a; *e = b;
    return 1;
  }
  return 0;
}

// Attribute of an #EXTINF line: key="value".
static void atributo(const char *linha, const char *chave, char *dst, size_t tam) {
  char padrao[32];
  const char *p, *f;
  dst[0] = 0;
  snprintf(padrao, sizeof padrao, "%s=\"", chave);
  p = strstr(linha, padrao);
  if (!p) return;
  p += strlen(padrao);
  f = strchr(p, '"');
  if (!f || (size_t)(f - p) >= tam) return;
  memcpy(dst, p, (size_t)(f - p));
  dst[f - p] = 0;
}
// The title: after the last comma outside quotes.
static const char *tituloExtinf(const char *linha) {
  const char *t = NULL;
  int aspas = 0;
  for (const char *p = linha; *p; p++) {
    if (*p == '"') aspas = !aspas;
    else if (*p == ',' && !aspas) t = p + 1;
  }
  while (t && *t == ' ') t++;
  return t;
}
static uint64_t fnv(const char *s) {
  uint64_t h = 1469598103934665603ULL;
  for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ULL;
  return h;
}

int m3uvod_parse(char *texto, M3uVodCb cb, void *u) {
  char *p = texto, *extinf = NULL;
  int n = 0;
  while (p && *p) {
    char *fim = strpbrk(p, "\r\n"), *linha = p;
    if (fim) { *fim = 0; p = fim + 1; while (*p == '\r' || *p == '\n') p++; }
    else p = NULL;
    while (*linha == ' ' || *linha == '\t') linha++;
    if (!strncmp(linha, "#EXTINF", 7)) { extinf = linha; continue; }
    if (!*linha || *linha == '#') continue;
    if (extinf && m3uvod_url_vod(linha)) {
      XtVodItem it;
      const char *nome = tituloExtinf(extinf);
      char serie[160];
      int t = 0, e = 0;
      memset(&it, 0, sizeof it);
      if (nome && *nome) {
        snprintf(it.nome, sizeof it.nome, "%s", nome);
        snprintf(it.sid, sizeof it.sid, "%016llx", (unsigned long long)fnv(linha));
        atributo(extinf, "tvg-logo", it.poster, sizeof it.poster);
        if (strncmp(it.poster, "http://", 7) && strncmp(it.poster, "https://", 8)) it.poster[0] = 0;
        atributo(extinf, "group-title", it.categoria, sizeof it.categoria);
        {
          const char *q = strrchr(linha, '.'), *qq = strchr(linha, '?');
          if (q && (!qq || q < qq) && strlen(q + 1) < sizeof it.ext) {
            size_t k = qq ? (size_t)(qq - q - 1) : strlen(q + 1);
            if (k < sizeof it.ext) { memcpy(it.ext, q + 1, k); it.ext[k] = 0; }
            for (char *c = it.ext; *c; c++) *c = (char)tolower((unsigned char)*c);
          }
        }
        it.ano = xtvod_ano_do_nome(it.nome);
        n++;
        if (m3uvod_episodio(it.nome, serie, sizeof serie, &t, &e)) {
          if (cb && !cb(&it, 'e', serie, t, e, linha, u)) break;
        } else if (cb && !cb(&it, 'm', NULL, 0, 0, linha, u)) break;
      }
    }
    extinf = NULL;
  }
  return n;
}

int m3uvod_listar(unsigned conta, M3uVodCb cb, void *u, uint64_t sessao) {
  char lista[2048], epg[1024];
  char *corpo;
  int n;
  (void)sessao;
  if (!m3u_fonte_urls(conta, lista, sizeof lista, epg, sizeof epg) || !lista[0]) return -1;
  if (strncmp(lista, "http://", 7) && strncmp(lista, "https://", 8)) return -1;
  corpo = rede_baixar_com(lista, 90, NULL);
  if (!corpo) return -1;
  n = m3uvod_parse(corpo, cb, u);
  free(corpo);
  printf("[m3uvod] conta %u: %d titulo(s) VOD na lista\n", conta, n);
  return n;
}
