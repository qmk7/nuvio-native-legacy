// STALKER VOD AND SERIES (QR TIME): the Stalker/Ministra side of the IPTV
// library (iptvvod.c), like xtvod.h for Xtream.
//
// type=vod get_categories + get_ordered_list (category=*, paginated). An item
// with is_series=1 is a series whose "series" array lists its episode numbers
// (one season on these portals). `cmd` is what create_link needs at play time;
// it can carry the portal's own URL, so it is kept in memory only, never
// written to a cache or a log. Ids are the portal's own (provider-local).
#ifndef NV_STVOD_H
#define NV_STVOD_H
#include <stdint.h>
#include "xtvod.h"

#define STVOD_CMD 512
#define STVOD_EPS 256
typedef int (*StVodCb)(const XtVodItem *it, int serie, const char *cmd, const char *eps, void *u);

// One page of get_ordered_list (no network). Returns items delivered; *total
// and *porPagina from the page (0 when absent).
int stvod_parse_pagina(const char *json, const char *(*nomeCat)(const char *id, void *u), void *uCat,
                       StVodCb cb, void *u, int *total, int *porPagina);
// "1,2,3" from the item's series array: episode numbers, ascending. Returns
// how many were written to `out` (<= max).
int stvod_episodios(const char *eps, int *out, int max);
// Whole catalog of account `conta` (BLOCKS). Items delivered, or -1.
int stvod_listar(unsigned conta, StVodCb cb, void *u, uint64_t sessao);
#endif
