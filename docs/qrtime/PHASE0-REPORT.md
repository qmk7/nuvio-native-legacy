# تقرير المرحلة 0 — الأساس والهوية

الفرع: `claude/qr-time-architecture-audit-toexj4`
التاريخ: 2026-11-16

القرارات المستخدمة (التوصيات من خطة البنية، القسم 9، لأنها لم تُعتمد صراحةً بعد):
البناء فوق الكود الحالي، معرّف الحزمة `com.qrtime.tv`، عدم حذف أي شيء، ترك كود Tizen خامدًا.

---

## 1. ما تم تنفيذه

### 1.1 هوية QR TIME وعزلها عن المشروع الأصلي

| التغيير | الملفات | السبب |
|---|---|---|
| معرّف حزمة جديد `com.qrtime.tv` واسم "QR TIME" | `deploy/app/appinfo.json`، `tools/arm.sh` | ليُثبَّت بجانب Nuvio Legacy دون أن يستبدله |
| ملف هوية مركزي | `src/qrtime_brand.h` (جديد)، `src/app_id.h` | مكان واحد للاسم والمعرّف وروابط التحديث والإعلانات |
| مجلد بيانات منفصل | (عبر `dados.c` الموجود، وضع "isolado") | كان سيشارك مجلد `~/.nuvio` مع Nuvio Legacy، أي الجلسات وبيانات الدخول |
| التحديث الذاتي يقرأ إصدارات `qmk7` | `src/atualizacao.c` | كان سيُنزّل حزمة Nuvio ويثبّتها فوق QR TIME |
| حارس: لا يُقبل ملف `.ipk` إلا إذا بدأ اسمه بـ `com.qrtime.tv_` | `src/atualizacao.c` | حماية إضافية حتى لو احتوى إصدار ما على حزمة تطبيق آخر |
| الإعلانات من `qrtime/notices.json` (فارغ حاليًا) | `src/avisos.c`، `qrtime/notices.json` | كانت تعرض إعلانات Nuvio |

ملاحظة عن الإرسال (telemetry): السجلات والقياسات تمر فقط عبر `nuvem.c` (Supabase)، وعنوانها
يُحدَّد وقت البناء. بدون إعدادات لا يُرسل شيء إلى خوادم المؤلف الأصلي.

### 1.2 إصلاح ثلاثة اختبارات كانت فاشلة مسبقًا

| الاختبار | السبب | الإصلاح |
|---|---|---|
| `epg_accounts` | ثلاثة أسباب: (1) `n++` على سطر `if` بلا أقواس في الماكرو `POE`؛ (2) تحذيرات خاصة بـ GCC لا يعرفها clang في macOS؛ (3) `stub_perfisessao.h` يعرّف دوال قوية تتكرر حين يضمّه ملفان | (1) فصل `n++` في سطر مستقل (`src/guia.c`)، ثم حذف المتغير الميت `sCorte`؛ (2) إضافة `-Wno-format-truncation -Wno-address` على GCC فقط؛ (3) جعل الدوال `weak`. أعدت تشغيل 9 اختبارات أخرى تستخدم الملف نفسه فنجحت |
| `stalker_portal` | الاختبار يعرّف دوال `perfis_sessao_*` الموجودة أصلًا في `stub_perfisessao.h` | حذف التعريف المكرر من الاختبار |
| `ajustes_iptv` | خلل حقيقي في الكود: إعداد "ذاكرة دليل البرامج" يعتمد على ذاكرة التلفاز، لكنه كان يُحفظ لكل ملف شخصي | إضافة `AJ_EPG_MEMORIA` إلى قائمة إعدادات الجهاز (`soNaTv` في `src/ajustes.c`) |

### 1.3 بناء حزمة LG قابل للتكرار

- `tools/qrtime/build-ipk.sh`: أمر واحد يقوم بما يلي:
  - ينزّل أدوات بناء webOS (SDK) المناسبة لمعالج الجهاز، ويتحقق من بصمة SHA-256 مثبّتة.
  - يثبّت `@webos-tools/cli@3.2.6`.
  - يبني الحزمة `build/ipk/com.qrtime.tv_<version>_arm.ipk`.
- `tools/ctr.sh`: وضع جديد `NUVIO_CTR=local` يبني بدون Docker.
- `tools/qrtime/sdk-image.sh`: يبني صورة Docker الأصلية على x86_64 أو arm64، مع دعم بروكسي HTTPS.
- `local.properties.example`: قائمة المفاتيح المطلوبة وقت البناء.

### 1.4 تشغيل الاختبارات على Linux

455 من 1198 سكربت اختبار كانت مكتوبة لـ macOS فقط (`-framework OpenGL`).
- `tools/qrtime/hostcc/cc`: غلاف يترجم أعلام الربط الخاصة بـ macOS دون تعديل الاختبارات نفسها.
- `tools/qrtime/run-tests.sh`: يشغّل الاختبارات على Linux.
- `tools/qrtime/gate-tests.txt`: الاختبارات التي يجب أن تنجح قبل أي دفع.

---

## 2. نتائج التحقق الفعلية

### 2.1 بناء حزمة LG

```
bash tools/qrtime/build-ipk.sh   →  rc=0  (حوالي دقيقتين)
build/ipk/com.qrtime.tv_2.0.8_arm.ipk  (48 MB)
```

فحص محتوى الحزمة:

| الفحص | النتيجة |
|---|---|
| `Package` | `com.qrtime.tv`، العنوان `QR TIME` |
| الملف التنفيذي | ELF 32-bit ARM EABI5، الصلاحيات `-rwxr-xr-x` |
| المكتبات المرتبطة | SDL2 و SDL2_image و SDL2_ttf و GLESv2 و EGL و libc وما شابه؛ **لا ربط مباشر بـ libcurl** (صحيح) |
| ملفات شخصية | لا توجد (يتحقق منها `arm.sh` تلقائيًا) |
| رابط التحديث | `api.github.com/repos/qmk7/nuvio-native-legacy/releases/latest` |
| مجلد البيانات | `/media/developer/temp/com.qrtime.tv` |

### 2.2 الاختبارات

**بوابة QR TIME** (`bash tools/qrtime/run-tests.sh`، 25 اختبارًا): **نجحت كلها، 25/25.**

| المجموعة | الاختبارات | النتيجة |
|---|---|---|
| البث المباشر / IPTV | `iptv_accounts` `ltv` `m3u` `aovivo` `xtepg` `epg_accounts` `epg_streaming` `guiaepg` `stalker_portal` `ajustes_iptv` `xtream` `xtepg_bounded` `guia_epg_estado` `guia_loading` | نجاح 14/14 |
| الهوية والتحديث والإعلانات | `atualizacao` `atualizacao_agenda` `avisos_corte` `dts_debug` | نجاح 4/4 |
| الأجندة ونطاق الإعدادات | `agenda` `ajustes_escopo` `ajustes_sync` `ajustes_nuvem` `ajustes_escala_migra` `ajustes_idioma_disco` `ajustes_memoria_migra` | نجاح 7/7 |

إضافةً إلى ذلك:
- `bash tools/linux.sh --build`: نجح (rc=0).
- بناء حزمة LG: نجح. بصمة SHA-256 للحزمة النهائية:
  `fd07ba22dc2f468f1020e4f87243f5531e5867500a8083549382063c265951ca`

**تراجع (regression) سبّبته ثم أصلحته** (أذكره للشفافية): عند تغيير الهوية إلى وضع "isolado"، صار
`dados.c` يضيف مجلدًا فرعيًا داخل `NUVIO_DADOS`، أي مجلد البيانات الصريح الذي يحدده المطوّر
أو الاختبار. كشف ذلك ستة اختبارات: `ajustes_escopo` و`ajustes_sync` و`ajustes_nuvem`
و`ajustes_escala_migra` و`ajustes_idioma_disco` و`ajustes_memoria_migra`.

الإصلاح: يُستخدم `NUVIO_DADOS` كما هو، ويبقى المجلد الفرعي لنسخ DTS التجريبية فقط (`NV_DTS_DEBUG`)،
وهذا ما كان الاختبار `dts_debug_data.c` ينص عليه أصلًا. أعدت تشغيل الستة بعد الإصلاح فنجحت كلها.

**اختبارات فاشلة مسبقًا** (تفشل بالطريقة نفسها على الـ commit الأصلي `7cac4f25`، وتحققت من ذلك بتشغيلها عليه):

| الاختبار | السبب |
|---|---|
| `ajustes_padroes` `ajustes_creditos` `ajustes_fonte` | الاختبارات تتوقع أن بعض الإعدادات خاصة بالجهاز، والكود يحفظها لكل ملف شخصي. الاختبار والكود متباعدان في المشروع الأصلي، ويحتاج القرار مراجعةً لكل إعداد |
| `addonurl` | ملف مصدر ناقص في سطر الربط (`ws_decl_ler`) |
| `musica` | `mus_alternar_guardado` معرّفة مرتين |
| `stalker_scheduler` | بدائل `app.c` قديمة |
| `atualizacao_android` | يعتمد على `-dead_strip` في macOS |

**غير مصنّف بعد:** `ajustes_categorias_ui` و`ajustes_lista_cabecalho` يحتاجان شاشة رسومية
(`XDG_RUNTIME_DIR`). كما لم تُشغَّل بقية الاختبارات (حوالي 1170 من أصل 1198) على Linux في هذه المرحلة.

---

## 3. ما لم يتم، أو لم يُختبر، بصراحة

1. **لم تُختبر الحزمة على تلفاز LG OLED B5.** لا يوجد تلفاز متصل بهذه البيئة.
   هذا الفحص مطلوب منك (القسم 4).
2. **الحزمة الحالية تنقصها أربع ميزات مقارنةً بإصدار Nuvio الرسمي:**
   - libass (عرض ترجمات ASS المتقدمة)؛ يبقى عارض الترجمة المبسّط المدمج.
   - تحويل صوت DTS (FFmpeg).
   - Dolby Vision profile 7 (Rust).
   - محرك P2P.

   السبب أن البيئة الحالية تمنع الوصول إلى `ffmpeg.org` و`savannah.gnu.org` و`github.com/libass`
   و`deb.debian.org`، وتمنع سحب صور Docker (Docker Hub أعاد 429). يمكن تفعيل هذه الميزات في بيئة
   يتوفر فيها الوصول، بالأمر نفسه مع `NUVIO_ASS_LIBASS=1` و`NUVIO_DTS_FFMPEG=1`.
3. **تغيّر اسم عميل خدمة الوسائط** إلى `com.webos.media.client.com.qrtime.tv`. هذا المسار
   تستخدمه أصلًا نسخ DTS التجريبية المعزولة، لكن يجب التأكد من أن التشغيل يعمل على B5.
4. **بقايا هوية Nuvio في الواجهة:**
   - `User-Agent` يعلن عن نفسه كـ `NuvioTV/1.0 (https://github.com/iqui27/...)`.
   - روابط داخل النصوص: صفحة إصدارات المشروع الأصلي، ودليل الإعداد عبر الهاتف، ورابط التبرع للمطوّر الأصلي.
   - الشعار والأيقونات.

   هذه جزء من المرحلة 8 (الهوية البصرية). روابط النسب للمطوّر الأصلي تحتاج قرارك.
5. **تسجيل الدخول ما زال إلزاميًا** (حساب Nuvio). بدون إعدادات Supabase تتوقف الحزمة عند
   شاشة الدخول. "وضع الضيف" هو الخطوة 0.5 التالية.
6. **رقم الإصدار** ما زال `2.0.8` (موروث). يجب الاتفاق على ترقيم QR TIME قبل أول إصدار،
   لأن التحديث الذاتي يقارن الأرقام.
7. **سبعة اختبارات فاشلة مسبقًا لم تُصلح بعد** (القسم 2.2). ليست في قائمة البوابة.

---

## 4. فحص مطلوب منك على التلفاز LG OLED B5

1. فعّل وضع المطوّر على التلفاز وثبّت الحزمة:
   `ares-install --device <tv> build/ipk/com.qrtime.tv_2.0.8_arm.ipk`
2. افتح التطبيق وتحقق من:
   - يظهر باسم **QR TIME** في قائمة التطبيقات.
   - يفتح ويصل إلى شاشة الدخول دون انهيار.
   - إذا كان Nuvio Legacy مثبتًا، يبقى كما هو ويعمل بشكل مستقل.
3. أرسل السجل: `ares-log --device <tv>`، أو ملف `/tmp/com.qrtime.tv.log`.
