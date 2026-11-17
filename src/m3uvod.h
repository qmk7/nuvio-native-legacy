// M3U VOD (QR TIME): movies and series inside an M3U playlist, for the IPTV
// library (iptvvod.c). M3U has no VOD protocol, so this is by the list's own
// structure, and says so:
//   - an entry is VOD when its URL is a video FILE (.mp4 .mkv .avi .mov .m4v
//     .wmv) or an Xtream-style /movie/ or /series/ path; live streams (.m3u8,
//     .ts, no extension) stay in the Live TV guide;
//   - a VOD entry whose name has "S01E02" (or "1x02") is an episode of the
//     series named before it; any other VOD entry is a movie.
// URLs carry the provider's credentials: they are handed to the caller in
// memory and never written to a cache or a log here.
#ifndef NV_M3UVOD_H
#define NV_M3UVOD_H
#include <stddef.h>
#include <stdint.h>
#include "xtvod.h"

// kind 'm' movie or 'e' episode (`serie` = series name, `t`/`e` set).
typedef int (*M3uVodCb)(const XtVodItem *it, char kind, const char *serie, int t, int e,
                        const char *url, void *u);
// Episode marker in a name: "Show S01E02", "Show 1x02". Writes the series
// name before it. 1 when found.
int m3uvod_episodio(const char *nome, char *serie, size_t tam, int *t, int *e);
// 1 when the URL is a VOD file by the rules above.
int m3uvod_url_vod(const char *url);
// Parses a playlist text (modified in place). Returns VOD entries delivered.
int m3uvod_parse(char *texto, M3uVodCb cb, void *u);
// Downloads account `conta`'s playlist and parses it (BLOCKS). -1 no answer.
int m3uvod_listar(unsigned conta, M3uVodCb cb, void *u, uint64_t sessao);
#endif
