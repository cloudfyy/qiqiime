// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/test.h"

namespace qiqi::test::common_cases {

void TestVersionMechanism() {
    static_assert(version::kCurrentVersion >= version::SemanticVersion{1, 0, 0});
    static_assert(version::SemanticVersion{1, 0, 0} > version::SemanticVersion{0, 99, 7});
    QIQI_TEST_CHECK(!version::kVersion.empty());
    QIQI_TEST_CHECK(!version::kVersionW.empty());
    QIQI_TEST_CHECK(!version::kFullVersion.empty());
    QIQI_TEST_CHECK(!version::kFullVersionW.empty());
    QIQI_TEST_CHECK(!version::kGitCommit.empty());
    QIQI_TEST_CHECK(!version::kGitCommitW.empty());
    QIQI_TEST_CHECK(version::kArch == (sizeof(void*) == 8 ? "x64" : "x86"));
    QIQI_TEST_CHECK(version::kArchW == (sizeof(void*) == 8 ? L"x64" : L"x86"));
    QIQI_TEST_CHECK(version::kFullVersion.starts_with(version::kVersion));
}

void CheckStaticSoundWav(
    std::span<const uint8_t> wav, size_t sample_count,
    double start_frequency_hz, double end_frequency_hz, double amplitude) {
    QIQI_TEST_CHECK(wav.size() == 44 + sample_count * 2);
    codec::BinarySpanReader header(wav.first(44));
    QIQI_TEST_CHECK(header.ReadLe32() == 0x46464952u); // RIFF
    QIQI_TEST_CHECK(header.ReadLe32() == 36 + sample_count * 2);
    QIQI_TEST_CHECK(header.ReadLe32() == 0x45564157u); // WAVE
    QIQI_TEST_CHECK(header.ReadLe32() == 0x20746D66u); // fmt
    QIQI_TEST_CHECK(header.ReadLe32() == 16);
    QIQI_TEST_CHECK(header.ReadLeNumber(2) == 1);
    QIQI_TEST_CHECK(header.ReadLeNumber(2) == 1);
    QIQI_TEST_CHECK(header.ReadLe32() == 44100);
    QIQI_TEST_CHECK(header.ReadLe32() == 88200);
    QIQI_TEST_CHECK(header.ReadLeNumber(2) == 2);
    QIQI_TEST_CHECK(header.ReadLeNumber(2) == 16);
    QIQI_TEST_CHECK(header.ReadLe32() == 0x61746164u); // data
    QIQI_TEST_CHECK(header.ReadLe32() == sample_count * 2);
    QIQI_TEST_CHECK(header.Valid() && header.Empty());

    // Keep the legacy waveform formula independent of production constants.
    codec::BinarySpanReader samples(wav.subspan(44));
    double phase = 0.0;
    for (size_t i = 0; i < sample_count; ++i) {
        const double progress = static_cast<double>(i) / static_cast<double>(sample_count);
        const double frequency = start_frequency_hz + (end_frequency_hz - start_frequency_hz) * progress;
        const double envelope = std::exp(-4.5 * progress) * (1.0 - progress);
        const double value = std::sin(phase) * amplitude * envelope;
        phase += (6.28318530717958647692 * frequency) / 44100.0;
        const auto expected = gsl::narrow_cast<int16_t>(std::clamp(value * 32767.0, -32767.0, 32767.0));
        const auto actual = std::bit_cast<int16_t>(gsl::narrow_cast<uint16_t>(samples.ReadLeNumber(2)));
        QIQI_TEST_CHECK(actual == expected);
    }
    QIQI_TEST_CHECK(samples.Valid() && samples.Empty());
}

void TestStaticSoundWavs() {
    const auto key_click = qiqi::detail::GetKeyClickWav();
    CheckStaticSoundWav(key_click, 2646, 1400.0, 320.0, 0.55);
    // Guard against a nominally valid WAV whose energy dies out almost immediately.
    constexpr size_t kStartSample = 441; // 10 ms
    constexpr size_t kEndSample = 1764; // 40 ms
    codec::BinarySpanReader audible_samples(
        key_click.subspan(44 + kStartSample * 2, (kEndSample - kStartSample) * 2));
    double energy = 0.0;
    for (size_t i = kStartSample; i < kEndSample; ++i) {
        const auto pcm = std::bit_cast<int16_t>(
            gsl::narrow_cast<uint16_t>(audible_samples.ReadLeNumber(2)));
        const double normalized = static_cast<double>(pcm) / 32767.0;
        energy += normalized * normalized;
    }
    QIQI_TEST_CHECK(audible_samples.Valid() && audible_samples.Empty());
    QIQI_TEST_CHECK(std::sqrt(energy / static_cast<double>(kEndSample - kStartSample)) >= 0.04);
    CheckStaticSoundWav(qiqi::detail::GetEmptyCodeBeepWav(), 3748, 720.0, 360.0, 0.55);
    CheckStaticSoundWav(qiqi::detail::GetDupCodeBeepWav(), 1984, 660.0, 880.0, 0.40);
    const auto empty_wav = qiqi::detail::BuildStaticMonoWav<0>(1400.0, 320.0, 0.35);
    CheckStaticSoundWav(empty_wav, 0, 1400.0, 320.0, 0.35);
}

void TestI18nLocalization() {
    // 1. Direct language resolution
    QIQI_TEST_CHECK(ResolveUiLanguage(UiLanguage::ZhCn) == UiLanguage::ZhCn);
    QIQI_TEST_CHECK(ResolveUiLanguage(UiLanguage::EnUs) == UiLanguage::EnUs);
    const UiLanguage auto_resolved = ResolveUiLanguage(UiLanguage::Auto);
    QIQI_TEST_CHECK(auto_resolved == UiLanguage::ZhCn || auto_resolved == UiLanguage::EnUs);

    // 2. Bilingual string tables verification
    const UiStrings& zh = GetUiStrings(UiLanguage::ZhCn);
    const UiStrings& en = GetUiStrings(UiLanguage::EnUs);
    QIQI_TEST_CHECK(std::wstring_view(zh.candidate_header_prefix) == L"五笔 [");
    QIQI_TEST_CHECK(std::wstring_view(en.candidate_header_prefix) == L"Wubi [");
    QIQI_TEST_CHECK(std::wstring_view(zh.candidate_empty) == L"(无候选)");
    QIQI_TEST_CHECK(std::wstring_view(en.candidate_empty) == L"(No candidates)");
    QIQI_TEST_CHECK(std::wstring_view(zh.status_logo) == L"五笔");
    QIQI_TEST_CHECK(std::wstring_view(en.status_logo) == L"Wubi");
    QIQI_TEST_CHECK(std::wstring_view(zh.status_mode_cn) == L"中");
    QIQI_TEST_CHECK(std::wstring_view(en.status_mode_cn) == L"CN");
    QIQI_TEST_CHECK(std::wstring_view(zh.dlg_btn_ok) == L"确定");
    QIQI_TEST_CHECK(std::wstring_view(en.dlg_btn_ok) == L"OK");

    // 3. Test sound synthesizer and PlayImeSound (both live WAV synthesis and muted test mode)
    ResetLastPlayedSoundForTest();
    PlayImeSound(SoundEffectKind::None);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::None);
    PlayImeSound(SoundEffectKind::KeyClick);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::KeyClick);
    PlayImeSound(SoundEffectKind::EmptyCodeBeep);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::EmptyCodeBeep);
    PlayImeSound(SoundEffectKind::DupCodeBeep);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::DupCodeBeep);

    SetMuteAudioOutputForTest(true);
    PlayImeSound(SoundEffectKind::KeyClick);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::KeyClick);
    SetMuteAudioOutputForTest(false);
}

} // namespace qiqi::test::common_cases
