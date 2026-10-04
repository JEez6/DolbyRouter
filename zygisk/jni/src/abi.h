// ABI-level declarations of the libaudioclient.so / libaaudio.so entry points we hook.
//
// We only need the argument *layout* to be correct so that the compiler places
// arguments in the same registers as the real Android functions, then we forward
// everything unchanged. Therefore all the "heavy" Android types are declared as
// opaque (incomplete) types - we never touch their contents.
//
// Verified against device libaudioclient.so (arm64, Android 15):
//   ALL FOUR AudioTrack entry points place `audio_output_flags_t` in x6
//   (7th slot, after `this`).
#pragma once

#include <cstddef>
#include <cstdint>

namespace android {
class IAudioTrackCallback;
class IMemory;
template <typename T> class sp;
template <typename T> class wp;
struct audio_offload_info_t;

// Full layout is needed so we can clear the LOW_LATENCY attribute bit. It is
// the true origin of AUDIO_OUTPUT_FLAG_FAST: AudioTrack::set() runs
// audio_flags_to_audio_output_flags(mAttributes.flags, &flags), and
// AUDIO_FLAG_LOW_LATENCY (0x100) -> AUDIO_OUTPUT_FLAG_FAST (0x4). Clearing the
// flags *argument* alone is useless because that conversion re-adds 0x4.
struct audio_attributes_t {
    int32_t content_type; // audio_content_type_t
    int32_t usage;        // audio_usage_t
    int32_t source;       // audio_source_t
    uint32_t flags;       // audio_flags_mask_t  <-- at offset 12
    char tags[256];       // AUDIO_ATTRIBUTES_TAGS_MAX_SIZE
};
namespace content {
struct AttributionSourceState;
}
} // namespace android

typedef int      audio_stream_type_t;
typedef int      audio_format_t;
typedef int      audio_output_flags_t;
typedef int      audio_session_t;
typedef int      audio_transfer_type_t;
typedef int      status_t;
typedef uint32_t audio_channel_mask_t;

// AUDIO_OUTPUT_FLAG_FAST
static constexpr audio_output_flags_t kFlagFast = 0x4;

// AUDIO_OUTPUT_FLAG_PRIMARY. AAudio's legacy path requests PRIMARY, which the
// policy routes to the PRIMARY|FAST output (AudioOut_D) that is fast-mixed and
// bypasses the Dolby effect chain. Clearing this lets the track fall to the
// deep-buffer output (AudioOut_15), where DAP_offload is attached.
static constexpr audio_output_flags_t kFlagPrimary = 0x2;

// AUDIO_FLAG_LOW_LATENCY (audio_attributes_t.flags). This is what set() turns
// into AUDIO_OUTPUT_FLAG_FAST via audio_flags_to_audio_output_flags().
static constexpr uint32_t kAttrFlagLowLatency = 0x100;

// ---------------------------------------------------------------------------
// AudioTrack::set(stream_type, unsigned, format, channel_mask, unsigned long,
//                 output_flags, const wp<IAudioTrackCallback>&, int,
//                 const sp<IMemory>&, bool, session, transfer,
//                 const offload_info*, const AttributionSourceState&,
//                 const attributes*, bool, int)
// ---------------------------------------------------------------------------
typedef status_t (*AudioTrack_set1_t)(
    void *, audio_stream_type_t, unsigned, audio_format_t, audio_channel_mask_t,
    unsigned long, audio_output_flags_t,
    const android::wp<android::IAudioTrackCallback> &, int,
    const android::sp<android::IMemory> &, bool, audio_session_t,
    audio_transfer_type_t, const android::audio_offload_info_t *,
    const android::content::AttributionSourceState &,
    const android::audio_attributes_t *, bool, int);

// ---------------------------------------------------------------------------
// AudioTrack::set(stream_type, unsigned, format, unsigned, unsigned long,
//                 output_flags, callback_fn, void*, int, const sp<IMemory>&,
//                 bool, session, transfer, const offload_info*, unsigned, int,
//                 const attributes*, bool, int)
// ---------------------------------------------------------------------------
typedef status_t (*AudioTrack_set2_t)(
    void *, audio_stream_type_t, unsigned, audio_format_t, unsigned,
    unsigned long, audio_output_flags_t, void (*)(int, void *, void *), void *, int,
    const android::sp<android::IMemory> &, bool, audio_session_t,
    audio_transfer_type_t, const android::audio_offload_info_t *, unsigned, int,
    const android::audio_attributes_t *, bool, int);

// ---------------------------------------------------------------------------
// AudioTrack::AudioTrack(stream_type, unsigned, format, channel_mask,
//                        const sp<IMemory>&, output_flags,
//                        const wp<IAudioTrackCallback>&, int, session, transfer,
//                        const offload_info*, const AttributionSourceState&,
//                        const attributes*, bool, int)
// ---------------------------------------------------------------------------
typedef void (*AudioTrack_ctor_sp_t)(
    void *, audio_stream_type_t, unsigned, audio_format_t, audio_channel_mask_t,
    const android::sp<android::IMemory> &, audio_output_flags_t,
    const android::wp<android::IAudioTrackCallback> &, int, audio_session_t,
    audio_transfer_type_t, const android::audio_offload_info_t *,
    const android::content::AttributionSourceState &,
    const android::audio_attributes_t *, bool, int);

// ---------------------------------------------------------------------------
// AudioTrack::AudioTrack(stream_type, unsigned, format, channel_mask,
//                        unsigned long, output_flags,
//                        const wp<IAudioTrackCallback>&, int, session, transfer,
//                        const offload_info*, const AttributionSourceState&,
//                        const attributes*, bool, int)
// ---------------------------------------------------------------------------
typedef void (*AudioTrack_ctor_wp_t)(
    void *, audio_stream_type_t, unsigned, audio_format_t, audio_channel_mask_t,
    unsigned long, audio_output_flags_t,
    const android::wp<android::IAudioTrackCallback> &, int, audio_session_t,
    audio_transfer_type_t, const android::audio_offload_info_t *,
    const android::content::AttributionSourceState &,
    const android::audio_attributes_t *, bool, int);

// ---------------------------------------------------------------------------
// AAudio:  int32_t aaudio::AudioGlobal_getMMapPolicy()   (C++ mangled)
// ---------------------------------------------------------------------------
typedef int32_t (*AudioGlobal_getMMapPolicy_t)();

// ---------------------------------------------------------------------------
// AudioTrack::createTrack_l()  (no args beyond `this`, returns status_t)
//
// This is the single chokepoint every track creation goes through, regardless
// of which set()/ctor() overload was used. `AudioTrack::mFlags` lives at
// this+0x378 on the device build (verified via disassembly of
// createTrack_l @0xb4f84: `ldr w8,[x19,#0x378]; and w8,w8,#0xfffffffb`).
// Clearing AUDIO_OUTPUT_FLAG_FAST here happens *after* set()/ctor() has already
// validated its inputs, so the OpenSL negative-notificationFrames contract is
// left intact (see dolby_router.cpp).
// ---------------------------------------------------------------------------
typedef status_t (*AudioTrack_createTrack_l_t)(void *);

static constexpr size_t kOffAudioTrackFlags = 0x378;
