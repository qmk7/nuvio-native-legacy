#ifndef NV_APP_ID_H
#define NV_APP_ID_H
#include "qrtime_brand.h"
/* Package metadata and the runtime must be built with the same identity. */
/* Upstream Nuvio Legacy identity. QR TIME never uses it as its own: when
 * NV_APP_ID differs from it, dados.c keeps a separate data folder, so QR TIME
 * and Nuvio Legacy can live on the same TV without sharing sessions or
 * credentials. */
#define NV_APP_ID_PRODUCTION "space.nuvio.native.legacy"
#ifndef NV_APP_ID
#ifdef NV_DTS_DEBUG
#error "NV_DTS_DEBUG requires an isolated NV_APP_ID package identity"
#endif
#define NV_APP_ID QRT_APP_ID
#endif
#if defined(NV_APP_ID_DEFAULT)
#define NV_LS_MEDIA_CLIENT "com.webos.media.client.nuvio"
#else
#define NV_LS_MEDIA_CLIENT "com.webos.media.client." NV_APP_ID
#endif
#endif
