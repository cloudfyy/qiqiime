#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

#include "common/logger.h"
#include "common/scoped_handle.h"

namespace qiqi {

enum class SoundEffectKind {
    None,
    KeyClick,
    EmptyCodeBeep,
    DupCodeBeep
};

namespace detail {

namespace wav_format {
inline constexpr size_t kChunkIdBytes = 4;
inline constexpr size_t kRiffSizeOffset = kChunkIdBytes;
inline constexpr size_t kWaveFormatIdOffset = kRiffSizeOffset + sizeof(uint32_t);
inline constexpr size_t kWaveFormatIdBytes = 2 * kChunkIdBytes;
inline constexpr size_t kFormatSizeOffset = kWaveFormatIdOffset + kWaveFormatIdBytes;
inline constexpr size_t kAudioFormatOffset = kFormatSizeOffset + sizeof(uint32_t);
inline constexpr size_t kChannelsOffset = kAudioFormatOffset + sizeof(uint16_t);
inline constexpr size_t kSampleRateOffset = kChannelsOffset + sizeof(uint16_t);
inline constexpr size_t kByteRateOffset = kSampleRateOffset + sizeof(uint32_t);
inline constexpr size_t kBlockAlignOffset = kByteRateOffset + sizeof(uint32_t);
inline constexpr size_t kBitsPerSampleOffset = kBlockAlignOffset + sizeof(uint16_t);
inline constexpr size_t kDataChunkOffset = kBitsPerSampleOffset + sizeof(uint16_t);
inline constexpr size_t kDataSizeOffset = kDataChunkOffset + kChunkIdBytes;
inline constexpr size_t kHeaderBytes = kDataSizeOffset + sizeof(uint32_t);
inline constexpr uint32_t kFormatChunkBytes = 16;
inline constexpr uint16_t kChannels = 1;
inline constexpr uint16_t kBitsPerSample = 16;
inline constexpr uint16_t kBlockAlign = kChannels * sizeof(int16_t);
inline constexpr uint32_t kSampleRate = 44100;
} // namespace wav_format

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
std::array<uint8_t, wav_format::kHeaderBytes + NumSamples * sizeof(int16_t)> BuildStaticMonoWav(
    double start_freq_hz,
    double end_freq_hz,
    double amplitude) noexcept {
    constexpr uint32_t kDataBytes = gsl::narrow_cast<uint32_t>(NumSamples * sizeof(int16_t));
    constexpr uint32_t kRiffSize = gsl::narrow_cast<uint32_t>(
        wav_format::kHeaderBytes - wav_format::kWaveFormatIdOffset) + kDataBytes;

    std::array<uint8_t, wav_format::kHeaderBytes + NumSamples * sizeof(int16_t)> wav{};
    auto write_u32 = [&wav](size_t offset, uint32_t val) noexcept {
        std::memcpy(&gsl::at(wav, offset), &val, sizeof(uint32_t));
    };
    auto write_u16 = [&wav](size_t offset, uint16_t val) noexcept {
        std::memcpy(&gsl::at(wav, offset), &val, sizeof(uint16_t));
    };

    std::memcpy(wav.data(), "RIFF", wav_format::kChunkIdBytes);
    write_u32(wav_format::kRiffSizeOffset, kRiffSize);
    std::memcpy(&gsl::at(wav, wav_format::kWaveFormatIdOffset), "WAVEfmt ", wav_format::kWaveFormatIdBytes);
    write_u32(wav_format::kFormatSizeOffset, wav_format::kFormatChunkBytes);
    write_u16(wav_format::kAudioFormatOffset, WAVE_FORMAT_PCM);
    write_u16(wav_format::kChannelsOffset, wav_format::kChannels);
    write_u32(wav_format::kSampleRateOffset, wav_format::kSampleRate);
    write_u32(wav_format::kByteRateOffset, wav_format::kSampleRate * wav_format::kBlockAlign);
    write_u16(wav_format::kBlockAlignOffset, wav_format::kBlockAlign);
    write_u16(wav_format::kBitsPerSampleOffset, wav_format::kBitsPerSample);
    std::memcpy(&gsl::at(wav, wav_format::kDataChunkOffset), "data", wav_format::kChunkIdBytes);
    write_u32(wav_format::kDataSizeOffset, kDataBytes);

    constexpr double kTwoPi = 6.28318530717958647692;
    constexpr double kEnvelopeDecay = 4.5;
    constexpr double kPcmMaximumMagnitude = static_cast<double>((std::numeric_limits<int16_t>::max)());
    double phase = 0.0;
    for (size_t i = 0; i < NumSamples; ++i) {
        const double progress = static_cast<double>(i) / static_cast<double>(NumSamples);
        const double freq = start_freq_hz + (end_freq_hz - start_freq_hz) * progress;
        const double env = std::exp(-kEnvelopeDecay * progress) * (1.0 - progress);
        const double sample_val = std::sin(phase) * amplitude * env;
        phase += (kTwoPi * freq) / static_cast<double>(wav_format::kSampleRate);

        const auto pcm = gsl::narrow_cast<int16_t>(
            std::clamp(sample_val * kPcmMaximumMagnitude, -kPcmMaximumMagnitude, kPcmMaximumMagnitude));
        std::memcpy(&gsl::at(wav, wav_format::kHeaderBytes + i * sizeof(int16_t)), &pcm, sizeof(int16_t));
    }
    return wav;
}

inline std::span<const uint8_t> GetKeyClickWav() noexcept {
    // Keep the decaying click audible beyond the first few milliseconds.
    constexpr size_t kSampleCount = 2646; // 60 ms at 44.1 kHz
    constexpr double kStartFrequencyHz = 1400.0;
    constexpr double kEndFrequencyHz = 320.0;
    constexpr double kAmplitude = 0.55;
    static const auto wav = BuildStaticMonoWav<kSampleCount>(kStartFrequencyHz, kEndFrequencyHz, kAmplitude);
    return wav;
}

inline std::span<const uint8_t> GetEmptyCodeBeepWav() noexcept {
    // 85 ms @ 44.1 kHz = 3,748 samples (7,540 bytes in static storage)
    constexpr size_t kSampleCount = 3748;
    constexpr double kStartFrequencyHz = 720.0;
    constexpr double kEndFrequencyHz = 360.0;
    constexpr double kAmplitude = 0.55;
    static const auto wav = BuildStaticMonoWav<kSampleCount>(kStartFrequencyHz, kEndFrequencyHz, kAmplitude);
    return wav;
}

inline std::span<const uint8_t> GetDupCodeBeepWav() noexcept {
    // 45 ms @ 44.1 kHz = 1,984 samples (4,012 bytes in static storage, gentle upward chime)
    constexpr size_t kSampleCount = 1984;
    constexpr double kStartFrequencyHz = 660.0;
    constexpr double kEndFrequencyHz = 880.0;
    constexpr double kAmplitude = 0.40;
    static const auto wav = BuildStaticMonoWav<kSampleCount>(kStartFrequencyHz, kEndFrequencyHz, kAmplitude);
    return wav;
}

// Keep winmm.dll loaded in a static ScopedModule RAII instance so that:
// 1. It is managed by RAII rather than a raw HMODULE;
// 2. It stays alive across asynchronous SND_ASYNC playback without premature FreeLibrary.
inline HMODULE GetWinmmModule() noexcept {
    static const ScopedModule winmm([]() noexcept {
        const HMODULE module = ::LoadLibraryW(L"winmm.dll");
        if (!module) {
            const DWORD error = ::GetLastError();
            QIQI_LOG_WARN("IME sound: LoadLibraryW(winmm.dll) failed (error={})", error);
        }
        return module;
    }());
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
        (kind == SoundEffectKind::EmptyCodeBeep)
            ? detail::GetEmptyCodeBeepWav()
            : ((kind == SoundEffectKind::DupCodeBeep) ? detail::GetDupCodeBeepWav() : detail::GetKeyClickWav());

    if (const HMODULE winmm = detail::GetWinmmModule()) {
        using PlaySoundWFn = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);
        const auto play_fn = reinterpret_cast<PlaySoundWFn>(::GetProcAddress(winmm, "PlaySoundW"));
        if (play_fn) {
            const BOOL played = play_fn(
                reinterpret_cast<LPCWSTR>(wav.data()),
                nullptr,
                SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
            if (!played) {
                // PlaySoundW reports failure via BOOL, not GetLastError.
                QIQI_LOG_WARN("IME sound: PlaySoundW returned FALSE (kind={})", static_cast<int>(kind));
            }
        } else {
            const DWORD error = ::GetLastError();
            QIQI_LOG_WARN("IME sound: GetProcAddress(PlaySoundW) failed (error={})", error);
        }
    }
}

} // namespace qiqi
