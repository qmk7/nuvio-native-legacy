// QR TIME product identity: one place for the name, the package id and every
// remote endpoint that decides what this build trusts (updates, notices).
//
// Why this exists: the base project updated itself from the upstream
// iqui27/nuvio-native-legacy releases and showed upstream notices. A QR TIME
// build left that way would download a Nuvio Legacy package and replace
// itself. Every such endpoint now comes from here, and each can be overridden
// at build time (-DQRT_UPDATE_REPO=..., -DQRT_NOTICES_URL=...).
#ifndef QRT_BRAND_H
#define QRT_BRAND_H

#define QRT_APP_NAME "QR TIME"

// Package id. Kept distinct from the upstream id so both apps can be installed
// on the same TV with separate data folders (see dados.c, "isolado").
#define QRT_APP_ID "com.qrtime.tv"

// GitHub repository that publishes QR TIME releases (owner/name).
#ifndef QRT_UPDATE_REPO
#define QRT_UPDATE_REPO "qmk7/nuvio-native-legacy"
#endif
// Branch that holds qrtime/notices.json.
#ifndef QRT_NOTICES_BRANCH
#define QRT_NOTICES_BRANCH "Qr-time-claude"
#endif

#define QRT_RELEASES_API  "https://api.github.com/repos/" QRT_UPDATE_REPO "/releases/latest"
#define QRT_RELEASES_PAGE "https://github.com/" QRT_UPDATE_REPO "/releases"
#ifndef QRT_NOTICES_URL
#define QRT_NOTICES_URL "https://raw.githubusercontent.com/" QRT_UPDATE_REPO "/" \
                        QRT_NOTICES_BRANCH "/qrtime/notices.json"
#endif

#endif
