# QR TIME — Persistent data schema

Schema version: **2** (`QRD_ESQUEMA` in `src/qrdados.h`), recorded in
`qrtime-schema.txt` (`schema=<N>`).

## Where data lives

- All files sit in the app's data folder (`dados_dir()`; on the LG it is the
  app's own private folder). Writes are atomic: `<file>.tmp`, then `rename`.
- `progresso.txt` is also `fsync`ed, so it survives a power cut.
- The IPK never carries any of these files. `tools/arm.sh` strips them and
  checks the built package (see "Packaging" below).

## Scope rules

| Suffix | Scope |
|---|---|
| `-p<N>` | One profile (`perfis_ativo()`). Another profile never reads or writes it. |
| no suffix | This TV, shared by every profile (API keys, caches, offline copies). |

## Upgrade rules (`qrdados_esquema_iniciar`, at every start)

1. **First QR TIME start** (no `qrtime-schema.txt`): the version is recorded.
   The base app's files are already in the v1 layout, so nothing is converted.
2. **Older version on disk:** the steps run **one version at a time**, and the
   new version is written after each step.
   - A failed step stops at the last good version. Nothing is removed, and the
     next start retries that step only.
3. **Newer version on disk** (an older app installed over newer data): nothing
   is converted, downgraded or deleted. `qrdados_mais_novo()` reports it.
4. **Steps so far:**
   - v1: marks the base app's old first-run cards as seen.
   - v2: writes `qrtime-quality.txt=pendente`. After the settings load,
     `qrdados_qualidade_aplicar` moves the playback options **still at their
     factory value** to the best this TV chain takes (4K, prefer DV/HDR, DV and
     Atmos on, "Qualidade máxima" priority, no size limit, DV in MKV, 256 MB
     read-ahead), then writes `feito`. A value the person chose stays.
5. **An upgrade step may rewrite or add, never delete.** A format change is
   read-compatible both ways where possible. Example: the music sources'
   on/off column is an optional 7th field.
   - Old files load as switched on.
   - An older app reads the 6 fields it knows, and the password stays intact.

## User-controlled operations (Settings → Accounts, providers and data)

Nothing is removed on screen exit or automatically. Every reset needs a
confirmation, with **Cancel** focused by default.

| Reset | Removes | Scope |
|---|---|---|
| Clear cache | Regenerable files only, by allowlist (`qrdados_e_cache`): catalogue/guide/metadata caches, `cache/` images, trailers, extras, subtitles. Unknown names are kept as user data. | TV |
| Delete offline music | `mo-*` copies + `music-offline.txt` | TV |
| Clear music history and queue | `music-history-p<N>`, `music-queue-p<N>` | profile |
| Delete local favourites and playlists | `music-favorites-p<N>`, `music-playlists-p<N>` (server playlists untouched) | profile |
| Unfollow all series | `agenda-follows-p<N>` | profile |
| Reset Sport Center | `esporte-p<N>` | profile |
| Forget what was sent to Trakt | `trakt-sent-p<N>` (nothing is deleted on Trakt) | profile |
| Disconnect YouTube and Last.fm | `youtube-account-p<N>`, `lastfm-sessao-p<N>`, `lastfm-pendentes-p<N>` | profile |
| Delete API keys | `youtube.txt`, `lastfm.txt`, `spotify.txt` | TV |
| Forget series that skip on their own | `skip-series-p<N>` | profile |

**Cache recovery:**
- A cache file that fails to parse is treated as a miss. The parsers are
  fuzzed (`tests/qrtime_fuzz.sh`).
- A missing cache file is fetched again.

## QR TIME files

S = secret (written `0600`, never logged, `dados_e_secreto`).
R = regenerable.

| File | Scope | Owner | Format | |
|---|---|---|---|---|
| `qrtime-schema.txt` | TV | `qrdados.c` | `schema=N` | |
| `livetv-contas-p<N>.txt`, `xtream-p<N>.txt`, `m3u-p<N>.txt`, `stalker-p<N>.txt` | profile | `iptvfontes.c`, `xtream.c`, `m3u.c`, `stalker.c` | provider accounts (URLs, users, passwords, MAC) | S |
| `ltv-*.bin`, `ltv-indice*.txt`, `m3u-cache-p<N>.txt`, `epg-*`, `catalogo-*.bin` | profile/TV | live TV / guide / VOD caches | binary / text | R |
| `music-sources-p<N>.txt` | profile | `mcfontes.c` | `tipo\tchave\tnome\turl\tusuario\tsenha[\toff]` | S |
| `music-queue-p<N>.txt`, `music-history-p<N>.txt`, `music-favorites-p<N>.txt`, `music-playlists-p<N>.txt` | profile | `mcfila.c` | tab-separated tracks | |
| `music-settings-p<N>.txt` | profile | `mcui.c` | `festa=0/1` (the pre-phase-8 TV-wide `music-settings.txt` is read only as a fallback) | |
| `music-offline.txt`, `mo-<hash>.<ext>` | TV | `mcoffline.c` | index + audio copies (2 GiB cap) | |
| `lastfm.txt` | TV | `mclfm.c` | API key + secret | S |
| `lastfm-sessao-p<N>.txt`, `lastfm-pendentes-p<N>.txt` | profile | `mclfm.c` | session key; scrobbles waiting for the network | S |
| `spotify.txt` | TV | `mcspotify.c` | client id + secret (public metadata only) | S |
| `youtube.txt` | TV | `ytapi.c` | Data API key + OAuth client | S |
| `youtube-account-p<N>.txt` | profile | `ytapi.c` | OAuth tokens | S |
| `trakt-p<N>.txt` | profile | `traktauth.c` | OAuth tokens | S |
| `trakt-sent-p<N>.txt` | profile | `traktdup.c` | `account-fingerprint\tcanonical-id\twhen` (FNV of the token; the token is never stored) | |
| `agenda-p<N>.txt`, `lembretes-p<N>.txt` | profile | `agenda.c` | Agenda cache, reminders | |
| `agenda-follows-p<N>.txt` | profile | `agenda.c` | `imdb\ttitle\tposter` | |
| `esporte-p<N>.txt` | profile | `esporte.c` | leagues, teams, event→channel links, hide-scores flag | |
| `skip-series-p<N>.txt` | profile | `pularserie.c` | `imdb\tmask\tintro presses\trecap presses` (mask 1 = intro, 2 = recap skip on their own) | |
| `qrtime-quality.txt` | TV | `qrdados.c` | `pendente` / `feito` (schema v2 max-quality defaults) | |

The base app's own files are listed by `tools/arm.sh`:
- `ARQ_DE_PESSOA`
- `GLOB_DE_PESSOA`
- `ACERVO_DE_PESSOA`

They follow the same scope rules. Their formats are documented in each owner
module.

## Isolation between providers and profiles

- Each IPTV account, music source and add-on keeps its own keyed entries:
  - IPTV: account `id`
  - music: `ss…`/`dv…` key, plus a qualified track id `ss1234abcd:<id>`
- A corrupted or failing provider:
  - can't overwrite another provider's entries;
  - is skipped, not merged.
- A switched-off music source is never contacted.
- Profile files are chosen by `perfis_ativo()` at the moment of the read or
  write. Each module re-reads on `*_perfil_trocou`.
- Tested in:
  - `qrtime_dados` (resets are per profile)
  - `qrtime_agenda7`, `qrtime_traktid`, `qrtime_music_ui`, `qrtime_esporte`
    (per-module isolation)

## Packaging

Before an IPK is published, `tools/arm.sh`:
1. stages a clean copy of the app;
2. removes every personal file (the lists above, including the QR TIME
   globs);
3. unpacks the built `.ipk` and refuses to finish if any of them is inside.
