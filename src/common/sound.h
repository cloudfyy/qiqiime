#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <gsl/gsl>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

#include "common/scoped_handle.h"

namespace qiqi {

enum class SoundEffectKind {
    None,
    KeyClick,
    EmptyCodeBeep
};

namespace detail {

inline SoundEffectKind& LastPlayedSoundStorage() noexcept {
    static SoundEffectKind last = SoundEffectKind::None;
    return last;
}

inline bool& MuteAudioOutputForTestStorage() noexcept {
    static bool mute = false;
    return mute;
}

// Synthesize a 16-bit mono 44.1 kHz RIFF WAVE image in a fixed-size stack/static std::array
// (zero heap allocation, noexcept, no try/catch needed).
template <size_t NumSamples>
std::array<uint8_t, 44 + NumSamples * sizeof(int16_t)> BuildStaticMonoWav(
    double start_freq_hz,
    double end_freq_hz,
    double amplitude) noexcept {
    constexpr uint32_t kSampleRate = 44100;
    constexpr uint32_t kDataBytes = gsl::narrow_cast<uint32_t>(NumSamples * sizeof(int16_t));
    constexpr uint32_t kRiffSize = 36u + kDataBytes;

    std::array<uint8_t, 44 + NumSamples * sizeof(int16_t)> wav{};
    auto write_u32 = [&wav](size_t offset, uint32_t val) noexcept {
        std::memcpy(&gsl::at(wav, offset), &val, sizeof(uint32_t));
    };
    auto write_u16 = [&wav](size_t offset, uint16_t val) noexcept {
        std::memcpy(&gsl::at(wav, offset), &val, sizeof(uint16_t));
    };

    // RIFF header
    std::memcpy(wav.data(), "RIFF", 4);
    write_u32(4, kRiffSize);
    std::memcpy(&gsl::at(wav, 8), "WAVEfmt ", 8);
    write_u32(16, 16);                              // PCM chunk size
    write_u16(20, 1);                               // PCM format = 1
    write_u16(22, 1);                               // Channels = 1 (mono)
    write_u32(24, kSampleRate);                     // Sample rate
    write_u32(28, kSampleRate * sizeof(int16_t));   // Byte rate
    write_u16(32, sizeof(int16_t));                 // Block align
    write_u16(34, 16);                              // Bits per sample
    std::memcpy(&gsl::at(wav, 36), "data", 4);
    write_u32(40, kDataBytes);

    constexpr double kTwoPi = 6.28318530717958647692;
    double phase = 0.0;
    for (size_t i = 0; i < NumSamples; ++i) {
        const double progress = static_cast<double>(i) / static_cast<double>(NumSamples);
        const double freq = start_freq_hz + (end_freq_hz - start_freq_hz) * progress;
        const double env = std::exp(-4.5 * progress) * (1.0 - progress);
        const double sample_val = std::sin(phase) * amplitude * env;
        phase += (kTwoPi * freq) / static_cast<double>(kSampleRate);

        const auto pcm = gsl::narrow_cast<int16_t>(std::clamp(sample_val * 32767.0, -32767.0, 32767.0));
        std::memcpy(&gsl::at(wav, 44 + i * sizeof(int16_t)), &pcm, sizeof(int16_t));
    }
    return wav;
}

inline std::span<const uint8_t> GetKeyClickWav() noexcept {
    // 14 ms @ 44.1 kHz = 617 samples (1,278 bytes in static storage)
    static const auto wav = BuildStaticMonoWav<617>(1400.0, 320.0, 0.35);
    return wav;
}

inline std::span<const uint8_t> GetEmptyCodeBeepWav() noexcept {
    // 85 ms @ 44.1 kHz = 3,748 samples (7,540 bytes in static storage)
    static const auto wav = BuildStaticMonoWav<3748>(720.0, 360.0, 0.55);
    return wav;
}

// Keep winmm.dll loaded in a static ScopedModule RAII instance so that:
// 1. It is managed by RAII rather than a raw HMODULE;
// 2. It stays alive across asynchronous SND_ASYNC playback without premature FreeLibrary.
inline HMODULE GetWinmmModule() noexcept {
    static const ScopedModule winmm(::LoadLibraryW(L"winmm.dll"));
    return winmm.Get();
}

} // namespace detail

inline void SetMuteAudioOutputForTest(bool mute) noexcept {
    detail::MuteAudioOutputForTestStorage() = mute;
}

inline SoundEffectKind GetLastPlayedSoundForTest() noexcept {
    return detail::LastPlayedSoundStorage();
}

inline void ResetLastPlayedSoundForTest() noexcept {
    detail::LastPlayedSoundStorage() = SoundEffectKind::None;
}

// Play synthesized IME sound effect asynchronously without blocking the TSF keystroke thread.
// Fully noexcept with zero heap allocations and no try/catch needed.
inline void PlayImeSound(SoundEffectKind kind) noexcept {
    if (kind == SoundEffectKind::None) {
        return;
    }
    detail::LastPlayedSoundStorage() = kind;
    if (detail::MuteAudioOutputForTestStorage()) {
        return;
    }

    const std::span<const uint8_t> wav =
        (kind == SoundEffectKind::EmptyCodeBeep) ? detail::GetEmptyCodeBeepWav() : detail::GetKeyClickWav();

    if (const HMODULE winmm = detail::GetWinmmModule()) {
        using PlaySoundWFn = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);
        const auto play_fn = reinterpret_cast<PlaySoundWFn>(::GetProcAddress(winmm, "PlaySoundW"));
        if (play_fn) {
            play_fn(
                reinterpret_cast<LPCWSTR>(wav.data()),
                nullptr,
                SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        }
    }
}

} // namespace qiqi
