// DolbyRouter - Zygisk native module
//
// Goal: for a per-app allow-list, make the app's audio *not* use a FAST track,
// because FAST tracks bypass the normal mixer's effect chain where Dolby DAP
// (libhwdap.so, Effect 19) is attached. See docs/02 for the full mechanism.
//
// Strategy (single chokepoint)
//   * Hook AudioTrack::createTrack_l() in libaudioclient.so and clear
//     AUDIO_OUTPUT_FLAG_FAST (and PRIMARY) from mFlags right before it is sent
//     to AudioFlinger. Every creation path (Java AudioTrack / OpenSL / AAudio
//     legacy) funnels through this one function.
//   * Crucially we do NOT touch set()/ctor() inputs. AudioTrack::set()
//     validates a negative notificationFrames as FAST-only, and OpenSL's
//     buffer-queue player passes notificationFrames = -numBuffers. Clearing
//     FAST (or coercion -N -> 0) at set() time either fails validation or
//     destroys the N-notifications-per-buffer refill cadence and makes OpenSL
//     playback stutter / slow down. Clearing inside createTrack_l() runs
//     *after* validation, so set() keeps its -N contract and only the final
//     output selection changes.
//   * Optionally hook aaudio::AudioGlobal_getMMapPolicy() to return 0 (NEVER)
//     so AAudio falls back to the legacy AudioTrack path (MMAP bypasses the
//     mixer entirely and would also bypass Dolby).
//
// Per-app config is read directly from the module directory in preAppSpecialize
// (zygote privilege + the module dir carries system_file context).

#include <jni.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <atomic>

#include <android/log.h>

#include "shadowhook.h"
#include "zygisk.hpp"
#include "abi.h"

#define LOG_TAG "DolbyRouter"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
static constexpr uint32_t kCfgMagic = 0x44524331u; // 'D','R','C','1'

struct DrConfig {
    uint32_t magic;
    int32_t strip_fast; // clear AUDIO_OUTPUT_FLAG_FAST
    int32_t block_mmap; // force AAudio MMAP policy -> NEVER
};

static std::atomic<bool> g_on{false};
static std::atomic<bool> g_strip_fast{false};
static std::atomic<bool> g_block_mmap{false};

// ---------------------------------------------------------------------------
// Symbol names (mangled) - verified against device libaudioclient.so
// ---------------------------------------------------------------------------
static const char *kLibAudioclient = "libaudioclient.so";
static const char *kLibAAudio = "libaaudio_internal.so";

static const char *kSymMmapGet = "_ZN6aaudio25AudioGlobal_getMMapPolicyEv";
static const char *kSymCreateTrackL = "_ZN7android10AudioTrack13createTrack_lEv";

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
static AudioGlobal_getMMapPolicy_t g_orig_mmap_get = nullptr;
static AudioTrack_createTrack_l_t g_orig_create = nullptr;

// NOTE: we hook in shadowhook UNIQUE mode, where the target jumps *directly* to
// our proxy - there is no hub trampoline pushing a stack frame. Therefore we
// must NOT use SHADOWHOOK_CALL_PREV()/SHADOWHOOK_STACK_SCOPE() (those walk the
// hub stack, which is null here and crashes). We simply call the saved `orig`
// function pointer.

static status_t create_track_proxy(void *p0) {
    if (g_on.load(std::memory_order_relaxed)) {
        // mFlags at this+0x378 (verified in the device disassembly:
        //   ldr w8,[x19,#0x378]; and w8,w8,#0xfffffffb; str w8,[x19,#0x378]).
        // This runs *before* the function body, hence before the client-side
        // FAST-denied test and before input.flags = mFlags.
        int32_t *mf = (int32_t *)((char *)p0 + kOffAudioTrackFlags);
        *mf &= ~(kFlagFast | kFlagPrimary);
    }
    return g_orig_create(p0);
}

static int32_t mmap_get_proxy() {
    int32_t was = g_orig_mmap_get();
    if (g_on.load(std::memory_order_relaxed)) {
        return 0; // NEVER -> force legacy AudioTrack path
    }
    return was;
}

static void hook_one(const char *tag, const char *lib, const char *sym,
                     void *proxy, void **orig) {
    void *stub = shadowhook_hook_sym_name_2(lib, sym, proxy, orig,
                                            SHADOWHOOK_HOOK_WITH_UNIQUE_MODE);
    if (stub == nullptr) {
        int err = shadowhook_get_errno();
        LOGE("hook %s FAILED errno=%d: %s", tag, err, shadowhook_to_errmsg(err));
    } else {
        LOGI("hook %s ok", tag);
    }
}

static void install_hooks() {
    int err = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false /* quiet */);
    if (err != 0) {
        LOGE("shadowhook_init errno=%d: %s", shadowhook_get_errno(),
             shadowhook_to_errmsg(shadowhook_get_errno()));
        return;
    }
    if (g_strip_fast.load()) {
        // Single chokepoint: covers every creation path (Java/OpenSL/AAudio
        // legacy) since they all funnel through createTrack_l().
        hook_one("AudioTrack::createTrack_l", kLibAudioclient, kSymCreateTrackL,
                 (void *)create_track_proxy, (void **)&g_orig_create);
    }
    if (g_block_mmap.load()) {
        // The AAudio C API lives in libaaudio.so, but the implementation of
        // AudioGlobal_getMMapPolicy is in libaaudio_internal.so, which is the
        // symbol that actually decides MMAP policy.
        hook_one("AudioGlobal_getMMapPolicy", kLibAAudio, kSymMmapGet,
                 (void *)mmap_get_proxy, (void **)&g_orig_mmap_get);
    }
}

// ---------------------------------------------------------------------------
// Root companion: decide whether `proc` is targeted.
//
// Source of truth is the DolbyRouter app's mirrored preferences
// (/data/data/com.dolbyrouter/shared_prefs/config.xml, world-readable). A flat
// "perapp.conf" in the module dir is kept as a manual/debug override. The
// companion runs as root, so it opens both by absolute path.
// ---------------------------------------------------------------------------
static constexpr const char *kConfigPath =
    "/data/adb/modules/dolbyrouter/" "perapp.conf";
static constexpr const char *kAppConfigPath =
    "/data/data/com.dolbyrouter/shared_prefs/config.xml";

static void read_all(int fd, void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, (char *)buf + off, n - off);
        if (r <= 0) break;
        off += (size_t)r;
    }
}

static void write_all(int fd, const void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = write(fd, (const char *)buf + off, n - off);
        if (r <= 0) break;
        off += (size_t)r;
    }
}

static bool read_file(const char *path, char *buf, size_t n) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    std::memset(buf, 0, n);
    ssize_t r = read(fd, buf, n - 1);
    close(fd);
    return r > 0;
}

// base name = strip ":service" suffix
static void proc_base(const char *proc, char *out, size_t n) {
    size_t i = 0;
    for (const char *p = proc; *p && *p != ':' && i < n - 1; ++p) out[i++] = *p;
    out[i] = '\0';
}

// flat "app=<name>" lines in perapp.conf
static bool conf_has_app(const char *cfg, const char *proc, const char *base) {
    const char *p = cfg;
    while ((p = std::strstr(p, "app=")) != nullptr) {
        p += 4;
        const char *end = p;
        while (*end && *end != '\n' && *end != '\r' && *end != ' ' && *end != '\t') ++end;
        size_t len = (size_t)(end - p);
        if (len && ((std::strlen(base) == len && !std::strncmp(p, base, len)) ||
                    (std::strlen(proc) == len && !std::strncmp(p, proc, len))))
            return true;
        p = end;
    }
    return false;
}

// mirrored SharedPreferences: <boolean name="scope_<pkg>" value="true" />
static bool xml_has_scope(const char *xml, const char *base) {
    const char *p = xml;
    while ((p = std::strstr(p, "name=\"scope_")) != nullptr) {
        const char *q = p + 12; // past name="scope_
        const char *e = q;
        while (*e && *e != '"') ++e;
        size_t len = (size_t)(e - q);
        if (len == std::strlen(base) && !std::strncmp(q, base, len)) {
            const char *v = std::strstr(e, "value=\"");
            return v != nullptr && !std::strncmp(v + 7, "true", 4);
        }
        p += 12;
    }
    return false;
}

static void companion_handler(int fd) {
    // request: process name (length-prefixed)
    int32_t name_len = 0;
    read_all(fd, &name_len, sizeof(name_len));
    char proc[256];
    proc[0] = '\0';
    if (name_len > 0 && name_len < (int32_t)sizeof(proc)) {
        read_all(fd, proc, (size_t)name_len);
        proc[name_len] = '\0';
    }
    char base[256];
    proc_base(proc, base, sizeof(base));

    DrConfig cfg{};
    cfg.magic = kCfgMagic;

    char conf[8192];
    bool clean = !read_file(kConfigPath, conf, sizeof(conf));

    char xml[8192];
    bool have_xml = read_file(kAppConfigPath, xml, sizeof(xml));

    bool enabled = true;
    if (!clean && std::strstr(conf, "enabled=0")) enabled = false;
    if (have_xml && std::strstr(xml, "name=\"enabled_global\"") &&
        std::strstr(xml, "name=\"enabled_global\" value=\"false\"") != nullptr)
        enabled = false;

    bool matched = conf_has_app(conf, proc, base) ||
                   (have_xml && xml_has_scope(xml, base));

    if (enabled && matched) {
        // Binary semantics (docs/03): force_normal == strip FAST + block MMAP.
        cfg.strip_fast = 1;
        cfg.block_mmap = 1;
    }
    LOGI("companion: proc='%s' enabled=%d matched=%d app_xml=%d conf=%d -> strip=%d mmap=%d",
         proc, enabled ? 1 : 0, matched ? 1 : 0, have_xml ? 1 : 0, clean ? 0 : 1,
         cfg.strip_fast, cfg.block_mmap);

    write_all(fd, &cfg, sizeof(cfg));
    close(fd);
}

REGISTER_ZYGISK_COMPANION(companion_handler)

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------
class DolbyRouterModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        const char *proc = nullptr;
        if (env_ != nullptr && args->nice_name != nullptr) {
            proc = env_->GetStringUTFChars(args->nice_name, nullptr);
        }

        int sock = api_->connectCompanion();
        DrConfig cfg{};
        if (sock >= 0) {
            int32_t len = (proc != nullptr) ? (int32_t)std::strlen(proc) : 0;
            if (len > 255) len = 255;
            write_all(sock, &len, sizeof(len));
            if (len > 0) write_all(sock, proc, (size_t)len);
            read_all(sock, &cfg, sizeof(cfg));
            close(sock);
        } else {
            LOGE("connectCompanion failed");
        }

        if (proc != nullptr) {
            env_->ReleaseStringUTFChars(args->nice_name, proc);
        }

        if (cfg.magic != kCfgMagic || (!cfg.strip_fast && !cfg.block_mmap)) {
            return; // not targeted
        }
        g_strip_fast.store(cfg.strip_fast != 0);
        g_block_mmap.store(cfg.block_mmap != 0);
        g_on.store(true);
        LOGI("activate '%s': strip_fast=%d block_mmap=%d",
             proc ? proc : "?", cfg.strip_fast, cfg.block_mmap);
        install_hooks();
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        (void)args;
    }

private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
};

REGISTER_ZYGISK_MODULE(DolbyRouterModule)
