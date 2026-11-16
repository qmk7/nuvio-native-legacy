# QR TIME: architecture audit and implementation plan

Status: **draft for approval**. No product code has been changed. This document is the
output of the first-task audit described in the QR TIME master specification.

Audit date: 2026-11-16. Base commit: `7cac4f25` (identical on `Qr-time-claude` and
`claude/qr-time-architecture-audit-toexj4`).

---

## 0. Executive summary

1. **Don't rewrite this codebase. Build QR TIME on it.** The repository is a mature native C
   TV client: about 844 files in `src/`, about 302,000 lines of C, and about 2,500 test files.
   It already has a working LG webOS video pipeline (LS2 to `com.webos.media`, hardware video
   plane, DTS through FFmpeg, Dolby Vision profile 7, libass), plus Live TV with
   Xtream/M3U/Stalker, an EPG, Trakt, an Agenda, search, Continue Watching and profiles.
   Rewriting this "from scratch" would throw away the most expensive and most
   device-specific work (the webOS media path). It would also break the spec's own rule:
   "preserve the stable native C architecture and the LG webOS media pipeline".
   **What "a clean implementation" should mean:** QR TIME gets a clean product identity, clean
   module boundaries for all new work, a clean reproducible build, and a clean baseline of
   verified behaviour. It should not mean deleting and retyping working subsystems.
2. **The baseline builds.** `tools/linux.sh --build` compiles the whole tree on Linux
   (rc=0, 306 warnings, about 3 minutes). The webOS ARM build is containerised
   (`tools/Dockerfile` + `tools/arm.sh`).
3. **Large functional gaps** against the spec:
   - IPTV **VOD/Series** do not exist. Xtream and Stalker are live-only.
   - **Sport Center** does not exist.
   - **YouTube** browsing/playback does not exist. On LG there are only IMDb MP4 trailers and
     a "Where to watch" launcher.
   - The **Music Center** does not exist. There are only 30-second soundtrack previews.
   - Content from all sources is not merged into one **unified library**.
4. **Blocking identity risk.** The binary self-updates from `iqui27/nuvio-native-legacy`
   releases (`src/atualizacao.c:132`). It also pulls announcements from that upstream repo
   (`src/avisos.c:65`). A QR TIME build left as-is would update itself back into
   *Nuvio Legacy*. This must be fixed before any QR TIME package goes to a TV.
5. **Account dependency.** The app cannot get past the login screen without a Nuvio
   (Supabase) account and build-time server config (`src/app.c:3860`, `tools/env.sh`). QR TIME
   needs a decision on its account model (see §9).

---

## 1. Audit of the existing system

### 1.1 Repository and branches

| Item | Finding |
|---|---|
| Remote | `qmk7/nuvio-native-legacy`, a fork of `iqui27/nuvio-native-legacy`, GPL-3.0 |
| Branches | `Qr-time-claude` and `claude/qr-time-architecture-audit-toexj4`, both at `7cac4f25`. No other branches were fetched in this clone. |
| Language | C99/C11 (Portuguese identifiers), small Rust crates (`rust/mkvcore`, DV7 converter), Kotlin host for Android (`android/`), .NET host for Tizen (`tizen-tpk/`) |
| Targets in tree | LG webOS (`NV_WEBOS`), Android (`NV_ANDROID`), Samsung TPK/WGT (`NV_TPK`, Emscripten), VIDAA, Linux/macOS desktop preview |
| Docs | Mostly Portuguese: `docs/`, `RELATORIO-*.md`, `docs/issues/MAPA.md` (live map) |
| Repo hygiene | A committed `guia.o` (378 KB object file) sits at the root, along with several report files at the root |

### 1.2 Native entry point and lifecycle (webOS)

| Concern | Location | Notes |
|---|---|---|
| Package manifest | `deploy/app/appinfo.json` | `type: native`, `main: nuvio-proto`, `nativeLifeCycleInterfaceVersion: 2`, id `space.nuvio.native.legacy`, v2.0.8 |
| Process entry | `src/main.c:929` `main()` | Arg parsing, data dir, SDL init (`:1050`), GL context, main loop |
| Event loop | `src/main.c:1881` `SDL_PollEvent` | Remote key remapping (`remapCanal`, CH+/-), Magic Remote pointer workaround (`:1326`) |
| Background/foreground | `src/main.c:1916-1974` | `FOCUS_LOST`/`HIDDEN` go to `player_ciclo_evento(SDL_APP_WILLENTERBACKGROUND)`; `DIDENTERFOREGROUND` resumes the updater and frame pacing |
| Low memory | `src/main.c:2369`, `memlog.c`, `app.c` `aliviarMemoria` | Evicts textures, libass, mkvass and the heap |
| App identity | `src/app_id.h` | `NV_APP_ID` and the LS2 media client name are tied to the package id |

### 1.3 Screen router and navigation

- `src/app.h` / `app.c` (7.5k lines) is the screen router. There is one current `Tela` plus a
  return stack: `LOGIN, ESCOLHA_PERFIL, HOME, EXPLORAR, GUIA, BUSCA, BIBLIOTECA, PERFIL,
  AJUSTES, DIAGNOSTICO, PLAYER, SOCIAL, ADDONS, AGENDA, LIVETV_DIAG, PLUGINS`.
- `src/menu.c` is the sidebar (`MENU_INICIO … MENU_AJUSTES`). It is an overlay layer that takes the D-pad.
- `src/focus.c` and per-screen focus models; `ponteiro.c` handles the Magic Remote pointer.
- Each screen exposes `x_iniciar / x_evento / x_atualizar / x_desenhar / x_quer_sair`. This
  contract is good and new modules should follow it.

### 1.4 Rendering

- `gfx.c` (5.2k lines): GLES2 primitives, rounded rects, blur/glass, the "furo" (hole punch) for the video plane.
- `text.c`: SDL_ttf glyph cache, bidi (`bidi.c`), 30+ languages (`idioma_*.h`).
- `tex_cache.c` (4.8k lines): poster/backdrop texture cache with a budget set by device RAM
  (`NV_TEX_MB_FIXO` for the high-cache variant), plus `cachedisco.c` on disk and `jpegrapido.c`/`webp.c` for decoding.
- `layout.h`, `uisizing.h`, `escala.h`: a 1920×1080 base canvas, scaled.

### 1.5 Player and media pipeline

- `player.c` (7.4k lines) is the UI/OSD and state machine. `aovivo.c` is the Live TV OSD.
- `video.c` (4k lines) is the **webOS pipeline**: LS2 calls to `luna://com.webos.media/*`
  (load/play/seek/subscribe), opened with `dlopen(libluna-service2)`. The video lives on a
  hardware plane behind GL.
- Supporting modules:
  - Subtitles and tracks: `legenda*.c`, `mkvass.c`, `assrender.c` (libass), `faixas*.c`.
  - Audio and video formats: `src/dts/*` (DTS to PCM through FFmpeg), Rust DV7 to 8.1.
  - Streaming helpers: `proxyts.c` (TS proxy), `moviebuffer.c`.
- Screensaver suppression goes through `com.webos.service.tvpower`.

### 1.6 Networking

- `rede.c` (4k lines) opens **libcurl with `dlopen`** (`.so.5`, then `.so.4`). Linking libcurl
  directly breaks on the TV (see `tools/arm.sh` header).
- There are async primitives: `RedeGrupo`/`RedeJob` with cancellation (`rede.h:106-114`) and
  worker threads per module (`fio1.c`). The UI thread never blocks on network, by convention.
- `negcache`, `redesaude.c` and `logrede.c` provide negative caching, health tracking and
  request logging (credentials are redacted per module).

### 1.7 Storage, settings, accounts

- `dados.c` finds a **writable** data directory by probing candidates at runtime.
- Per-profile files: `xtream-p<N>.txt`, `stalker-p<N>.txt`, `m3u-p<N>.txt`, `trakt-p<N>.txt`, …
  `tools/arm.sh` strips them from packages and checks the result.
- `ajustes.c` (11.4k lines) holds about 184 options in 11 categories, with device-only versus
  per-profile scope.
- `sessao.c`, `login.c`, `perfis.c`, `sync.c`, `nuvem.c` handle the Nuvio account (Supabase).
  Profiles, addons and progress sync to it. **Login is mandatory.**

### 1.8 Feature inventory against the spec

| Spec area | Existing modules | State |
|---|---|---|
| Live TV / IPTV | `guia.c` (7.9k), `ltv.c`, `iptvfontes.c`, `aovivo.c`, `livecat.c`, `guiafiltro.c`, `guialembrete.c` | **Exists**. Per-source groups, favorites, zapping, mini-guide overlay, reminders |
| Xtream Codes | `xtream.c` | **Live only.** No `get_vod_*` / `get_series*` |
| M3U | `m3u.c`, `m3u_parse.c`, `ltv.c` | **Live only.** Catch-up attributes are parsed |
| Stalker | `stalker.c` | **Live only** (`type=itv`) |
| Multi-provider | `iptvfontes.c` | **Exists**. Multiple accounts per profile, ordering, enable/disable |
| EPG | `epg.c`, `xmltv.c` (streaming, bounded memory), `xtepg.c` (short EPG) | **Exists** |
| Catch-up | `xtream.c:564` (`tv_archive`), `ltv.c:419` | **Fields parsed.** Playback flow not verified |
| Movies / VOD (addons) | `addons.c`, `catalogo.c`, `streams.c`, `detail.c`, `episodios.c` | **Exists** (Stremio/Nuvio addon protocol) |
| Series and episodes | `episodios.c`, `metaep.c`, `temporadas_grafico.c` | **Exists** (addon/TMDB) |
| Nuvio/Stremio add-ons | `addons.c`, `addonsui.c`, `plugins*.c` (QuickJS) | **Exists** |
| Unified library | `biblioteca.c`, `salvos.c`, `catalogo.c` | **Partial.** Addon titles only; no IPTV VOD |
| Sport Center | none (only channel categorisation) | **Missing** |
| YouTube | `trailer*.c`; `ondever.c:407` can launch `youtube.leanback.v4` | **Missing** (trailers only) |
| Music Center | `musica.c`, `musicaui.c` (soundtrack, iTunes 30 s previews) | **Missing** as a standalone player |
| Trakt | `trakt.c`, `traktauth.c` (device code), `traktscrobble.c`, `simkl*.c` | **Exists** |
| Agenda / Calendar | `agenda.c`, `agendaui.c` (TMDB next episode) | **Exists** for series. No sports or EPG events |
| Search | `busca.c`, `spotlight.c`, `buscanorm.c` | **Exists.** Live TV is not part of global search |
| Favorites | `salvos.c`, `guia.c` favourite channels | **Exists**, as separate lists |
| History / Continue Watching | `progresso.c`, `continuar.c`, `watchstate*.c`, `visto*.c` | **Exists**. Addon titles only |
| Settings | `ajustes*.c/inc` | **Exists** |
| Profiles / parental | `perfis.c`, `perfilinfantil.c`; `parental.c` is a content advisory only | **Partial.** No PIN lock for IPTV groups |
| Media servers | `plex.c`, `jellyfin.c` (and Emby) | **Exists** (bonus; reusable for music) |

### 1.9 Baseline evidence (this audit, Linux host)

| Check | Result |
|---|---|
| `bash tools/linux.sh --build` | **PASS**: rc=0, 306 warnings, server config empty (expected) |
| `tests/iptv_accounts.sh`, `ltv.sh`, `m3u.sh`, `aovivo.sh`, `xtepg.sh`, `epg_streaming.sh`, `guiaepg.sh` | **PASS** |
| `tests/epg_accounts.sh` | FAIL: `-Werror=misleading-indentation` at `src/guia.c:2899` (newer GCC) |
| `tests/stalker_portal.sh` | FAIL: stale test stub, `redefinition of perfis_sessao_geracao` |
| `tests/ajustes_iptv.sh` | FAIL: assertion `AJ_EPG_MEMORIA` default/scope (`tests/ajustes_iptv.c:179`) |
| `tests/agenda.sh`, `stalker_scheduler.sh`, `musica.sh` | Not runnable on Linux: they use macOS `-framework` |

Conclusion: the suite is **not green on Linux**. These are pre-existing problems, not caused by
QR TIME work. Phase 0 must triage them so that later regressions can be detected.

---

## 2. Reference projects

| Project | Language | Useful behaviour to adopt (behaviour only, no code copy) |
|---|---|---|
| `homelessbrian/NuvioPlusIPTV` (inspected via fork `esenciable/NuvioPlusIPTV`, GPL-3.0) | Kotlin / Compose, Android TV | **Sports hub** (ESPN `site.api.espn.com/apis/site/v2/sports/` + TheSportsDB; matches games to the user's channels via EPG/name with "SHOWING IT / MAYBE" confidence; Fix Channel memory; reminders with auto-switch). **On Demand** (Xtream movies/series with categories, recently added, sort; matched to addon metadata so they open on the normal details page; duplicates merged with a version picker; "On Demand" tab in the stream list). **Xtream account info** (expiry, connections). **Catch-up** with time shift. **Channel management** (hide/reorder/rename/renumber, custom groups). **PIN-locked groups**. **Start page** choice. **Home row** of favourite channels with what's on now. |
| `qmk7/NuvioplusLg` | — | Your own repo; not inspected in depth in this pass. Ask whether it holds behaviour that should carry over. |
| Upstream `iqui27/nuvio-native-legacy` | C | The base itself. Keep merges from upstream possible by keeping QR TIME changes in new modules and small hooks. |

Licence: GPL-3.0 on both sides, so they are compatible. QR TIME stays GPL-3.0, keeps upstream
copyright notices, and must not use Nuvio branding as its own.

---

## 3. Target architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│ Shell: main.c (lifecycle, input) → app.c router → screens            │
│   Home · Live TV · Movies · Series · Sports · Search · Library ·     │
│   Agenda · YouTube · Music · Settings · Player                       │
├──────────────────────────────────────────────────────────────────────┤
│ UI kit: gfx / text / tex_cache / focus / layout / theme tokens (new: │
│   marca.c, tema.h)                                                   │
├──────────────────────────────────────────────────────────────────────┤
│ Domain services                                                      │
│   conteudo.c   unified ContentItem + provider refs + ID matcher (NEW)│
│   biblioteca / salvos / progresso / watchstate (extend to refs)      │
│   ltv / guia / epg (Live) · iptvvod (NEW) · esporte (NEW)            │
│   agenda (+sports, +EPG reminders) · musicacentro (NEW) · yt (NEW)   │
├──────────────────────────────────────────────────────────────────────┤
│ Provider adapters (one per protocol; no UI, no SDL)                  │
│   addons.c (Stremio/Nuvio) · xtream.c + xtvod.c (NEW) ·              │
│   stalker.c + stkvod.c (NEW) · m3u.c (+VOD classification) ·         │
│   plex/jellyfin (video + music) · trakt/simkl · tmdb (extras.c) ·    │
│   espn/thesportsdb (NEW) · youtube data api (NEW) · radio (NEW)      │
├──────────────────────────────────────────────────────────────────────┤
│ Platform: rede.c (async, cancel) · dados.c (storage) · video.c       │
│   (webOS LS2 media) · luna (app launch, power) · registro (logs)     │
└──────────────────────────────────────────────────────────────────────┘
```

### 3.1 Rules for new code

- **Adapter boundary.** An adapter only does protocol work: it parses, builds URLs, and
  resolves playback. It returns plain structs and never touches SDL or global UI state. A
  test can drive it with a recorded HTTP fixture, following the `livecat_postar_teste` /
  `LiveCatPostar` injection pattern.
- **Identity.** A `ContentItem` holds `ids {imdb, tmdb, tvdb, trakt}`, and each of these is
  filled **only** from an authoritative source. Provider items live in
  `ProviderRef {kind, account_id, provider_item_id, container_ext}`. A provider ID is never
  written into an `imdb`/`tmdb` field. Xtream `get_vod_info` often returns `tmdb_id`, and
  only that value may fill `ids.tmdb`. Title+year matching produces a *suggested* link with a
  confidence score, stored separately.
- **Threads.** Network work runs on `RedeJob` with a `RedeGrupo`, so a whole screen's requests
  can be cancelled at once when it closes. Results reach the draw thread through a
  `*_passo()` poll, as `xtepg_passo` does today.
- **Secrets.** Credentials are kept in per-profile files with mode 0600 and go through the
  existing redaction in logs. They are added to `ARQ_DE_PESSOA`/`GLOB_DE_PESSOA` in
  `tools/arm.sh` **in the same commit** that creates the file.
- **Storage.** Writes are atomic (`tmp` + `rename`), as `cat_gravar_cache` does, and use
  `dados_dir()`.
- **Bounded memory.** Every list has a cap per platform with a visible "truncated" notice,
  never a silent cut (see `ltv_teto_canais`).
- **Tests.** Every adapter gets a host test with fixtures, and every screen gets a `_shot`
  test.

---

## 4. Module-by-module implementation plan

Each phase ends with a **gate**: the host build passes, the related tests pass, the ARM IPK
builds, and the result is verified on the **LG OLED B5** by the owner. A feature is "done"
only after the B5 workflow check.

### Phase 0: foundation and identity (blocks everything else)

| # | Work | Files |
|---|---|---|
| 0.1 | New package identity, e.g. `com.qrtime.tv` (to be decided). Name "QR TIME", icons and splash. This lets it install **alongside** Nuvio Legacy. | `deploy/app/appinfo.json`, `src/app_id.h`, `tools/arm.sh` (`APP_ID`), `tools/hb-repo.sh`, `repo.json` |
| 0.2 | Repoint the **self-updater** and **announcements** to `qmk7/...`, or disable them until QR TIME releases exist. Audit `telemetria`/`registro_envio` endpoints and turn off any upload to upstream servers. | `src/atualizacao.c:132-134`, `src/avisos.c:65`, `src/registro*.c`, `src/telemetria.c` |
| 0.3 | Reproducible build: pin the SDK tarball and its SHA-256, and add an **x86_64 host SDK** path or QEMU instructions (the current image needs `linux/arm64`). Pin `@webos-tools/cli` for `ares-package`, replacing the hard-coded `../NuvioWeb-0.3.38-beta` path. Add `local.properties.example`. | `tools/Dockerfile`, `tools/arm.sh`, new `tools/qrtime-build.sh` |
| 0.4 | CI: host build plus the triaged test subset on every push to the QR TIME branch. Fix or quarantine, with a written reason, the Linux-only failures from §1.9 (not by disabling assertions). | `.github/workflows/qrtime.yml`, `tests/*` |
| 0.5 | **Account model**: a guest/local mode so an IPTV-only user can reach Home without a Nuvio account (decision D3). | `app.c`, `login.c`, `sessao.c` |
| 0.6 | Leave Tizen, VIDAA and WGT code dormant: don't build, test or release it. Nothing is deleted. | — |

### Phase 1: Live TV hardening on the B5

- Verify the Xtream, M3U and Stalker flows end to end on the B5: add account, guide, zap,
  mini-guide, reminders and EPG.
- Finish and verify the catch-up flow (Xtream `timeshift` URL, M3U `catchup-source`).
- Add Xtream account info (expiry date, active connections) and a stream-format option
  (TS/HLS).
- Add PIN-locked groups, extending `perfilinfantil`.

### Phase 2: IPTV VOD and Series

| Module | Responsibility |
|---|---|
| `xtvod.c` (new) | `get_vod_categories`, `get_vod_streams`, `get_vod_info`, `get_series_categories`, `get_series`, `get_series_info`. URLs: `/movie/U/P/<id>.<ext>`, `/series/U/P/<id>.<ext>`. Pagination and caching per account. |
| `stkvod.c` (new) | Stalker `type=vod` categories, ordered list, seasons and episodes, `create_link` |
| `m3u` extension | Classify entries as live, movie or series (by `group-title`, URL path `/movie/` `/series/`, `tvg-type`) |
| `iptvvod.c` (new) | Aggregate across accounts, recently added, sort, cache, and duplicate merge with a version picker |
| Integration | VOD items open on the existing `detail.c` (with TMDB metadata when `tmdb_id` is known). Add an "IPTV" tab in the `streams.c` source list. Progress goes through `progresso.c` with the provider ref as the key. |

### Phase 3: unified library and home

- `conteudo.c`: the `ContentItem`/`ProviderRef` model and matcher, persisted per profile.
- Continue Watching merges addon, IPTV VOD and media-server progress. Favorites gain a
  "Channels" section. Search adds Live TV and IPTV VOD results with configurable order.
- New home rows (see §8).

### Phase 4: Sport Center

- `esporte.c` adapter: the ESPN site API scoreboard per league, plus TheSportsDB as an optional
  extra, with a cache of 60 s while games are live and 15 min otherwise.
- `esportematch.c`: matches events to channels using EPG titles, channel names and team
  aliases, and returns a confidence level (SHOWING/MAYBE). User corrections ("Fix Channel")
  are remembered.
- `esporteui.c`: My Teams, Live & Upcoming, an All Events day strip, and the game page.
  Reminders reuse `lembrete.c`/`guialembrete.c`; auto-switch is opt-in.

### Phase 5: Trakt and Agenda extensions

- The Agenda gains sports fixtures and EPG reminders next to series episodes.
- Trakt watchlist/history for IPTV VOD **only when the item has a verified IMDb/TMDB/Trakt
  id**. Scrobbling uses the same rule.

### Phase 6: YouTube (see risk R4)

- Browse with the YouTube Data API v3 (search, trending, subscriptions, channel). This needs
  an API key and has a quota.
- Playback launches the **LG YouTube app** (`youtube.leanback.v4`) with a `contentTarget`
  deep link through `luna://com.webos.applicationManager/launch`, a path `ondever.c` already
  uses. In-app playback through scraped stream URLs is **not** proposed (ToS, fragility).

### Phase 7: Music Center (see risk R5)

- `musicacentro.c`: sources are (a) Jellyfin/Plex/Emby music libraries through the existing
  adapters, (b) internet radio through the radio-browser.info directory, (c) USB/local files
  (`usbsonda.c`), and (d) the existing soundtrack previews.
- Playback is audio-only through `video.c`, with a now-playing screen and a queue. Music stops
  when the app goes to the background: webOS does not let native apps keep running there.

### Phase 8: design system and branding

- Theme tokens (`tema.h`: colour, type scale, radii, focus ring, motion) replace scattered
  constants on touched screens. Add a QR TIME logo, an icon set and app icons at every webOS
  size. The home hero and focus states are revised.
- Delivered as mock-ups first for approval, then implemented screen by screen with `_shot`
  tests.

### Phase 9: Android APK (secondary)

- Uses the existing `android/` SDL2 + Media3 host and the `android-release` skill. It starts
  only after the webOS gate for Phases 0–4 has passed.

---

## 5. Dependencies and reproducible webOS IPK build

| Dependency | Source / version | Notes |
|---|---|---|
| Cross toolchain + sysroot | openlgtv `buildroot-nc4` release `webos-a38c582`, `arm-webos-linux-gnueabi` | Current image uses the **aarch64-hosted** SDK (`--platform linux/arm64`). On x86_64 CI, use the x86_64 SDK tarball or qemu-user. Pin SHA-256. |
| SDL2, SDL2_image, SDL2_ttf, GLESv2, EGL | TV firmware (linked against sysroot) | LG ships its own SDL 2.0.5-webos; do not bundle |
| libcurl | TV firmware, **dlopen** at runtime | Do not link |
| libluna-service2 | TV firmware, dlopen | — |
| zlib | sysroot | — |
| libass + harfbuzz + fribidi + freetype | Built statically in image (`tools/build-ass-arm.sh`) | `--exclude-libs,ALL` is needed to avoid symbol clashes with the system FreeType |
| FFmpeg (DTS decode) | `tools/build-dts-ffmpeg.sh`, static | LGPL-2.1; licence copied into the package |
| Rust 1.88.0 + `armv7-unknown-linux-gnueabi` | DV7 converter, mkvcore | Pinned in Dockerfile and `Cargo.lock` |
| P2P engine (libtorrent, OpenSSL) | Optional (`NUVIO_P2P_MOTOR=none` to skip) | Recommend **off** for QR TIME v1 |
| `ares-package` | `@webos-tools/cli` (pin version) | Replaces the `../NuvioWeb-0.3.38-beta` path |
| Build-time config | `local.properties` (not committed) | TMDB key, Trakt id/secret, optional Supabase, plus new keys: YouTube API, TheSportsDB |

**Reproducible procedure (target for Phase 0.3):**

```
docker build --platform linux/arm64 -t qrtime-webos-sdk tools/   # or x86_64 variant
cp local.properties.example local.properties && edit
NUVIO_P2P_MOTOR=none bash tools/arm.sh --ipk --build
# outputs <appid>_<ver>_arm.ipk; script verifies: no personal files, binary mode 755,
# all configured keys present in the binary
```

Install on the B5: Developer Mode (`ares-install`) or Homebrew Channel. A rooted TV is not
required.

---

## 6. Technical risks and unavailable APIs

| ID | Risk | Impact | Mitigation |
|---|---|---|---|
| R1 | Self-updater and announcements point at upstream | QR TIME would replace itself with Nuvio Legacy | Phase 0.2, done before any device install |
| R2 | Mandatory Nuvio/Supabase login; using Nuvio's backend under a new brand may not be permitted | No Home without an account; possible ToS/ownership issue | Guest mode, with an optional Nuvio account link (D3) |
| R3 | No physical B5 in the build environment | Cannot claim "done" from CI | Owner runs a scripted acceptance checklist per phase on the B5. Logs come back through the existing diagnostic upload or `ares-log`. |
| R4 | **No native YouTube playback API on webOS for third-party native apps.** Stream scraping breaks often and violates YouTube ToS. | No in-app YouTube player | Data API for browsing plus a launch of the official LG YouTube app. Shown as "opens in YouTube". |
| R5 | **No licensed full-track music catalogue API** usable in a free TV app (Spotify/Apple require their SDKs or apps) | A "music streaming" scope isn't feasible | Music from the user's own servers, internet radio and USB (D5) |
| R6 | ESPN site API is undocumented and may change; TheSportsDB needs a key for higher limits | Sports data could break | Adapter isolation, cache, graceful "scores unavailable" state, fixture tests |
| R7 | webOS native apps do not run in the background | No recording or music while the app is closed | Document it. Recording is out of scope for v1 (D6); catch-up covers past shows. |
| R8 | Large Xtream VOD catalogues (50k+ items) on 32-bit ARM with limited RAM | OOM, slow UI | Paged category loading, arena strings (the `ltv.c` pattern), disk cache, visible caps |
| R9 | Codebase size (files of 8–11k lines) and Portuguese identifiers | Regression risk | New code in new modules; small hooks in `app.c`/`home.c`; host tests and `_shot` per change |
| R10 | Test suite partly macOS-only and partly failing on Linux | No regression signal in CI | Phase 0.4 triage |
| R11 | Mixing provider IDs with IMDb/TMDB/Trakt | Wrong scrobbles, corrupted history | `ProviderRef` separation (§3.1), tests that assert no cross-fill |
| R12 | webOS 25 on the B5 is newer than the C9 the project was tuned on | Unknown media or permission behaviour | Early Phase 1 smoke test on the B5. Upstream issues #224/#225 show webOS 25 runs the native app once the binary mode is 755. |

---

## 7. Requirements traceability matrix

Status: **E** = exists, **P** = partial, **M** = missing. "B5" means the acceptance check runs
on the TV.

| ID | Requirement (spec) | Module(s) | Test(s) | Acceptance criteria | Status |
|---|---|---|---|---|---|
| R-PL1 | Primary target LG webOS, native IPK | `appinfo.json`, `tools/arm.sh`, `video.c` | ARM build + package check | IPK installs on B5 via dev mode and launches to Home | E (as Nuvio) |
| R-PL2 | Main test TV: LG OLED B5 | all | B5 checklist per phase | Each phase checklist signed off on B5 | M (process) |
| R-PL3 | No Tizen development | `tizen-tpk/`, `NV_TPK` | — | No Tizen build/test/release in QR TIME CI | E (dormant) |
| R-PL4 | Android APK secondary, after webOS stable | `android/` | `android-release` flow | APK built only after the Phase 0–4 gate | P |
| R-PL5 | Native C/SDL2, no WebView | whole tree | grep gate in CI (no browser/webview launch for core flows) | Core features run in the native process | E |
| R-F1 | Live TV / IPTV | `guia.c`, `ltv.c`, `aovivo.c`, `epg.c` | `ltv.sh`, `guiaepg.sh`, `aovivo.sh` + B5 | Add source → guide shows channels with now/next; zap ≤ debounce; Back returns | E |
| R-F2 | Movies / VOD | `addons.c`, `detail.c`, `xtvod.c`* | `xtvod.sh`* + B5 | Browse → details → play → progress saved | P |
| R-F3 | Series and episodes | `episodios.c`, `xtvod.c`*, `stkvod.c`* | `xtvod_series.sh`* + B5 | Seasons/episodes listed; next episode autoplay | P |
| R-F4 | Multiple IPTV providers | `iptvfontes.c` | `iptv_accounts.sh` | ≥3 accounts of mixed types coexist; enable/disable/reorder | E |
| R-F5 | Xtream Codes | `xtream.c`, `xtvod.c`* | `iptv_accounts.sh`, `xtvod.sh`* | Live + VOD + series + account info | P |
| R-F6 | M3U playlists | `m3u.c`, `ltv.c` | `m3u.sh` | Live + VOD classification; headers honoured | P |
| R-F7 | Stalker portals | `stalker.c`, `stkvod.c`* | `stalker_portal.sh` (fix), `stkvod.sh`* | Live + VOD via MAC; token refresh | P |
| R-F8 | Nuvio/Stremio add-on catalogs | `addons.c`, `addonsui.c` | existing addon tests | Install by URL, catalogs on Home, streams resolve | E |
| R-F9 | Unified content library | `conteudo.c`*, `biblioteca.c` | `conteudo.sh`* | Same film from addon + IPTV appears once with both sources | M |
| R-F10 | Sport Center | `esporte*.c`* | `esporte.sh`* (fixtures), `_shot` | Today's games for followed teams; OK plays matched channel | M |
| R-F11 | YouTube | `youtube.c`*, `ondever.c` | `youtube.sh`* (fixtures) + B5 | Search/browse in app; OK opens video in LG YouTube app; Back returns to QR TIME | M |
| R-F12 | Music Center | `musicacentro.c`*, `jellyfin.c`, `plex.c` | `musicacentro.sh`* + B5 | Browse library/radio; play, pause, next, queue; now-playing screen | M |
| R-F13 | Trakt integration | `trakt*.c` | `trakt_*.sh` | Device-code link; scrobble only with verified IDs | E |
| R-F14 | Agenda / Calendar | `agenda.c`, `agendaui.c` | `agenda.sh` (port to Linux) | Series + sports + EPG reminders in one timeline | P |
| R-F15 | Search | `busca.c`, `spotlight.c` | busca tests | Results across addons, IPTV VOD, live channels | P |
| R-F16 | Favorites | `salvos.c`, `guia.c` | salvos tests | Titles and channels; persistent across restart | P |
| R-F17 | Watch history / Continue Watching | `progresso.c`, `continuar.c` | progress tests | IPTV VOD resumes at saved position; CW row merged | P |
| R-F18 | User settings | `ajustes*.c` | `ajustes_*.sh` | QR TIME settings persist per device/profile as declared | E |
| R-A1 | Async network, UI never blocks | `rede.c` (`RedeJob`) | frame-time test (`fluidez_*`) | No frame > 100 ms caused by I/O on B5 | E |
| R-A2 | Safe memory, cancellation, logging, caching | `rede.c`, `tex_cache.c`, `registro.c` | `tools/sanitiza.sh` (ASan/UBSan/TSan) | Sanitizer clean on new modules | P |
| R-A3 | Remote/D-pad/Back/OK/focus, Magic Remote | `focus.c`, `ponteiro.c`, `main.c` | `_shot` focus tests + B5 | Every new screen fully usable by D-pad and pointer | E (per screen) |
| R-A4 | Preserve Live TV, no regressions | Live TV modules | Live TV test subset in CI | Subset green on every push | P (see §1.9) |
| R-A5 | Never confuse provider IDs with IMDb/TMDB/Trakt | `conteudo.c`* | `conteudo_ids.sh`* | Test proves no provider id lands in an `imdb`/`tmdb`/`trakt` field | M |
| R-A6 | No secrets in logs | `registro.c`, per-adapter redaction | log redaction test | Captured logs contain no password/token/MAC | P |
| R-A7 | Persistent, safe preferences and progress | `dados.c`, atomic writes | restart tests | Data survives kill -9 mid-write | E |
| R-A8 | No feature called complete without a tested workflow | process | B5 checklist | Checklist evidence per feature in `docs/qrtime/evidence/` | M |
| R-A9 | No fake data as proof | process | review rule | Tests use recorded real-shape fixtures; B5 uses real accounts | — |
| R-D1 | Premium TV-first UI, consistent identity, logo, icons | `tema.h`*, `marca.c`*, `gfx.c` | `_shot` per screen | Owner approves mock-ups, then screens match | M |
| R-D2 | Coherent home screen | `home.c` | `home.sh`, `_shot` | §8 row structure, configurable | P |

`*` = new module or new test.

---

## 8. Proposed navigation and home screen

**Sidebar** (the left rail; the order is configurable and items can be hidden):

1. Home
2. Live TV (guide)
3. Movies (addons, IPTV VOD and media servers merged)
4. Series
5. Sports
6. Search
7. Library (Favorites · Continue Watching · History · Channels)
8. Agenda
9. YouTube
10. Music
11. Settings, with the profile avatar at the top of the rail

The start page can be set to Home, Live TV, Sports or the last screen used.

**Home rows** (top to bottom by default; each can be toggled and reordered):

1. Hero: featured title or a live sports event.
2. Continue Watching, merged across all sources.
3. On Now: favourite channels with a live programme bar.
4. Sports Today: followed teams; games that are live now show the score unless Hide Scores is on.
5. Up Next: the next episode from Trakt/Agenda.
6. Recently Added from IPTV providers.
7. Add-on catalogs (the existing rows).
8. Trending (TMDB).
9. Music: recently played and favourite stations.

**Global keys:**

| Key | Action |
|---|---|
| CH+/- | Zap channels while Live TV is playing |
| Back | Returns through the screen stack. On Home it asks before exiting. |
| Red | Search |
| Green | Live TV |
| Yellow | Sports |
| Blue | Mini-guide or the menu |

---

## 9. Decisions needed before implementation

| ID | Decision | Recommendation |
|---|---|---|
| D1 | Rewrite versus evolve | **Evolve.** Clean identity and clean new modules on top of the proven pipeline |
| D2 | Package id and coexistence with Nuvio Legacy | `com.qrtime.tv` (new id, installs alongside) |
| D3 | Account model | Guest/local mode by default; optional Nuvio account link only if you are allowed to use that backend |
| D4 | API keys (TMDB, Trakt, YouTube Data API, TheSportsDB) | You register QR TIME's own client ids; they go in `local.properties` and are never committed |
| D5 | Music Center scope | Media servers + internet radio + USB (no streaming-service catalogue) |
| D6 | Recording | Out of v1 (catch-up instead) |
| D7 | YouTube | Browse in app, play in the official LG YouTube app |
| D8 | Primary branch | Which of `Qr-time-claude` or this audit branch is the QR TIME trunk |
| D9 | Tizen/VIDAA/WGT code | Leave dormant and unbuilt; delete nothing |
| D10 | Upstream merges | Keep QR TIME hooks small so upstream fixes can still be merged |

Once approved, Phase 0 starts. None of its steps are destructive: they add files and change
identity strings, and each step is committed separately and can be reverted on its own.
