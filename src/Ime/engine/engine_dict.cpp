#include "Ime/engine/engine.h"
#include "Ime/engine/default_dict.h"
#include "common/file_util.h"
#include "common/logger.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <algorithm>
#include <span>

namespace qiqi {

namespace {

std::vector<std::wstring_view> SplitUtf16Codepoints(std::wstring_view text) {
    std::vector<std::wstring_view> chars;
    size_t i = 0;
    while (i < text.size()) {
        const wchar_t ch = gsl::at(text, i);
        if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < text.size()) {
            const wchar_t low = gsl::at(text, i + 1);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                chars.push_back(text.substr(i, 2));
                i += 2;
                continue;
            }
        }
        chars.push_back(text.substr(i, 1));
        ++i;
    }
    return chars;
}

} // namespace

void WubiEngine::AddEntry(std::wstring_view code, std::wstring_view word) {
    if (word.empty()) {
        return;
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return;
    }

    AppendUniqueWord(dict_[packed], word);
}

std::wstring WubiEngine::LookupCharacterCode(std::wstring_view ch) const {
    if (ch.empty()) {
        return {};
    }
    std::wstring best;
    for (const auto& mapped : mapped_dicts_) {
        std::wstring code = mapped->FindLongestCodeForWord(ch);
        if (code.size() > best.size()) {
            best = std::move(code);
            if (best.size() >= kMaxCodeLength) {
                return best;
            }
        }
    }
    for (const auto& [packed, words] : dict_) {
        if (std::find(words.begin(), words.end(), ch) != words.end()) {
            std::wstring code = UnpackWubiCode(packed);
            if (code.size() > best.size()) {
                best = std::move(code);
            }
        }
    }
    for (const auto& entry : kSortedBuiltinWubi86Dict) {
        if (entry.word == ch) {
            std::wstring code = UnpackWubiCode(entry.packed_code);
            if (code.size() > best.size()) {
                best = std::move(code);
            }
        }
    }
    return best;
}

std::wstring WubiEngine::EncodePhraseWubi86(std::wstring_view phrase) const {
    const auto chars = SplitUtf16Codepoints(phrase);
    if (chars.size() < 2) {
        return {};
    }

    auto resolve_char_code = [this](std::wstring_view ch) -> std::wstring {
        std::wstring c = LookupCharacterCode(ch);
        if (c.size() == 1) {
            c.push_back(c.front());
        }
        return c;
    };

    if (chars.size() == 2) {
        const std::wstring c1 = resolve_char_code(gsl::at(chars, 0));
        const std::wstring c2 = resolve_char_code(gsl::at(chars, 1));
        if (c1.size() < 2 || c2.size() < 2) {
            return {};
        }
        return std::wstring(c1.substr(0, 2)) + std::wstring(c2.substr(0, 2));
    }

    if (chars.size() == 3) {
        const std::wstring c1 = resolve_char_code(gsl::at(chars, 0));
        const std::wstring c2 = resolve_char_code(gsl::at(chars, 1));
        const std::wstring c3 = resolve_char_code(gsl::at(chars, 2));
        if (c1.empty() || c2.empty() || c3.size() < 2) {
            return {};
        }
        std::wstring res;
        res.push_back(c1.front());
        res.push_back(c2.front());
        res.append(c3.substr(0, 2));
        return res;
    }

    const std::wstring c1 = resolve_char_code(gsl::at(chars, 0));
    const std::wstring c2 = resolve_char_code(gsl::at(chars, 1));
    const std::wstring c3 = resolve_char_code(gsl::at(chars, 2));
    const std::wstring c_last = resolve_char_code(chars.back());
    if (c1.empty() || c2.empty() || c3.empty() || c_last.empty()) {
        return {};
    }
    return {c1.front(), c2.front(), c3.front(), c_last.front()};
}

bool WubiEngine::AddUserPhrase(std::wstring_view phrase, const std::filesystem::path& user_dict_path) {
    const std::wstring code = EncodePhraseWubi86(phrase);
    if (code.empty()) {
        return false;
    }
    AddEntry(code, phrase);
    QIQI_LOG_INFO("Added user phrase (code='{}', length={})",
                  WideToUtf8(code), SplitUtf16Codepoints(phrase).size());

    if (user_dict_path.empty()) {
        return true;
    }

    try {
        std::error_code ec;
        if (user_dict_path.has_parent_path()) {
            std::filesystem::create_directories(user_dict_path.parent_path(), ec);
        }
        bool wrote_ok = false;
        {
            const ScopedFileHandle file(::CreateFileW(
                user_dict_path.c_str(),
                FILE_APPEND_DATA,
                FILE_SHARE_READ,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
                nullptr));
            if (!file.IsValid() || ::GetFileType(file.Get()) != FILE_TYPE_DISK) {
                QIQI_LOG_WARN("AddUserPhrase: failed to open user dict '{}' for append (error={})",
                              WideToUtf8(user_dict_path.wstring()), ::GetLastError());
                return false;
            }
            const std::string line = WideToUtf8(code) + " " + WideToUtf8(phrase) + "\n";
            DWORD written = 0;
            const DWORD to_write = gsl::narrow_cast<DWORD>(line.size());
            wrote_ok = (::WriteFile(file.Get(), line.data(), to_write, &written, nullptr) != FALSE) &&
                       (written == to_write);
        }
        if (wrote_ok) {
            std::filesystem::path user_qdict_path = user_dict_path;
            user_qdict_path.replace_extension(L".qdict");
            if (WubiMappedDict::CompileTextFileToQdict(user_dict_path, user_qdict_path) &&
                user_dict_path == GetDefaultUserDictPath()) {
                std::string qdict_raw;
                if (ReadFileToUtf8String(user_qdict_path, qdict_raw)) {
                    SaveUserDictBinaryToRegistry(std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(qdict_raw.data()), qdict_raw.size()));
                }
            }
        }
        return wrote_ok;
    } catch (...) {
        return false;
    }
}

bool WubiEngine::LoadDictionaryFromBinaryBuffer(std::vector<uint8_t> qdict_bytes) {
    auto mapped = WubiMappedDict::LoadFromBinaryBuffer(std::move(qdict_bytes));
    if (!mapped) {
        return false;
    }
    mapped_dicts_.push_back(std::move(mapped));
    return true;
}

bool WubiEngine::LoadDictionaryFile(const std::filesystem::path& file_path) {
    auto mapped = WubiMappedDict::LoadFromTextOrCache(file_path);
    if (!mapped) {
        QIQI_LOG_WARN("Failed to load or map external dictionary file: {}",
                      WideToUtf8(file_path.wstring()));
        return false;
    }
    if (std::find(mapped_dicts_.begin(), mapped_dicts_.end(), mapped) == mapped_dicts_.end()) {
        QIQI_LOG_INFO("Attached external dictionary '{}' ({} codes, {} words, mapped={})",
                      WideToUtf8(file_path.wstring()),
                      mapped->CodeCount(),
                      mapped->WordCount(),
                      mapped->IsMemoryMapped());
        mapped_dicts_.push_back(std::move(mapped));
    }
    return true;
}

bool WubiEngine::HasModifiedMappedDictionary() const noexcept {
    for (const auto& dict : mapped_dicts_) {
        if (dict && dict->IsSourceModified()) {
            return true;
        }
    }
    return false;
}

void WubiEngine::PromotePhraseForCode(std::wstring_view code, std::wstring_view word) {
    if (!dynamic_phrase_freq_ || SplitUtf16Codepoints(word).size() < 2) {
        return;
    }
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return;
    }
    auto& list = promoted_phrases_[packed];
    std::erase(list, word);
    list.insert(list.begin(), std::wstring(word));
    if (!phrase_freq_path_.empty()) {
        std::ignore = SavePhraseFrequencies(phrase_freq_path_);
    }
}

bool WubiEngine::LoadPhraseFrequencies(const std::filesystem::path& file_path) {
    std::string utf8;
    if (!ReadFileToUtf8String(file_path, utf8)) {
        return false;
    }
    promoted_phrases_.clear();
    std::string_view sv(utf8);
    while (!sv.empty()) {
        const size_t nl = sv.find('\n');
        std::string_view line = (nl == std::string_view::npos) ? sv : sv.substr(0, nl);
        sv.remove_prefix(nl == std::string_view::npos ? sv.size() : nl + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const size_t sp = line.find(' ');
        if (sp == std::string_view::npos || sp == 0) {
            continue;
        }
        const uint32_t packed = PackWubiCode(Utf8ToWide(line.substr(0, sp)));
        if (packed == 0) {
            continue;
        }
        std::string_view rest = line.substr(sp + 1);
        auto& list = promoted_phrases_[packed];
        while (!rest.empty()) {
            const size_t first = rest.find_first_not_of(' ');
            if (first == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(first);
            const size_t next_sp = rest.find(' ');
            const std::wstring word = Utf8ToWide(
                next_sp == std::string_view::npos ? rest : rest.substr(0, next_sp));
            if (SplitUtf16Codepoints(word).size() >= 2) {
                AppendUniqueWord(list, word);
            }
            if (next_sp == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(next_sp + 1);
        }
    }
    return true;
}

bool WubiEngine::SavePhraseFrequencies(const std::filesystem::path& file_path) const {
    try {
        std::string out;
        for (const auto& [packed, words] : promoted_phrases_) {
            if (words.empty()) {
                continue;
            }
            out += WideToUtf8(UnpackWubiCode(packed));
            for (const auto& w : words) {
                out.push_back(' ');
                out += WideToUtf8(w);
            }
            out.push_back('\n');
        }
        return WriteFileAtomically(
            file_path,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(out.data()), out.size()));
    } catch (...) {
        return false;
    }
}

std::vector<std::wstring> WubiEngine::LookupExact(std::wstring_view code) const {
    const uint32_t packed = PackWubiCode(code);
    if (packed == 0) {
        return {};
    }

    std::vector<std::wstring> results;
    for (const auto& entry : kSortedBuiltinWubi86Dict) {
        if (entry.packed_code == packed) {
            AppendUniqueWord(results, entry.word);
        } else if (entry.packed_code > packed) {
            break;
        }
    }
    if (const auto it = dict_.find(packed); it != dict_.end()) {
        for (const auto& w : it->second) {
            AppendUniqueWord(results, w);
        }
    }
    for (const auto& mapped : mapped_dicts_) {
        mapped->LookupExact(packed, results);
    }

    if (dynamic_phrase_freq_ && results.size() > 1) {
        if (const auto pit = promoted_phrases_.find(packed); pit != promoted_phrases_.end()) {
            // Keep all single-character entries in their fixed positions;
            // reorder only multi-character phrase slots so promoted phrases appear first.
            std::vector<size_t> phrase_slots;
            std::vector<std::wstring> phrases;
            for (size_t i = 0; i < results.size(); ++i) {
                if (SplitUtf16Codepoints(gsl::at(results, i)).size() >= 2) {
                    phrase_slots.push_back(i);
                    phrases.push_back(gsl::at(results, i));
                }
            }
            if (phrases.size() > 1) {
                std::vector<std::wstring> ordered;
                ordered.reserve(phrases.size());
                for (const auto& fav : pit->second) {
                    if (std::find(phrases.begin(), phrases.end(), fav) != phrases.end()) {
                        ordered.push_back(fav);
                    }
                }
                for (auto& ph : phrases) {
                    if (std::find(ordered.begin(), ordered.end(), ph) == ordered.end()) {
                        ordered.push_back(std::move(ph));
                    }
                }
                for (size_t i = 0; i < phrase_slots.size(); ++i) {
                    gsl::at(results, gsl::at(phrase_slots, i)) = std::move(gsl::at(ordered, i));
                }
            }
        }
    }
    return results;
}

std::vector<std::wstring> WubiEngine::LookupCandidates(std::wstring_view code) const {
    const uint32_t exact_code = PackWubiCode(code);
    if (exact_code == 0) {
        return {};
    }

    constexpr size_t kMaxCandidateResults = 50;
    std::vector<std::wstring> results;

    // Wildcard query if 'z' is present
    const bool has_wildcard = (code.find(L'z') != std::wstring_view::npos) ||
                              (code.find(L'Z') != std::wstring_view::npos);
    if (has_wildcard) {
        for (const auto& entry : kSortedBuiltinWubi86Dict) {
            if (MatchesWildcardPattern(entry.packed_code, code)) {
                AppendUniqueWord(results, entry.word);
                if (results.size() >= kMaxCandidateResults) {
                    return results;
                }
            }
        }
        for (const auto& [k, words] : dict_) {
            if (MatchesWildcardPattern(k, code)) {
                for (const auto& w : words) {
                    AppendUniqueWord(results, w);
                    if (results.size() >= kMaxCandidateResults) {
                        return results;
                    }
                }
            }
        }
        for (const auto& mapped : mapped_dicts_) {
            mapped->LookupWildcard(code, results, kMaxCandidateResults);
            if (results.size() >= kMaxCandidateResults) {
                return results;
            }
        }
        return results;
    }

    // 1. Exact matches first
    results = LookupExact(code);
    if (results.size() >= kMaxCandidateResults) {
        results.resize(kMaxCandidateResults);
        return results;
    }

    // 2. Prefix matches in (exact_code, prefix_hi]
    const uint32_t prefix_hi = PackWubiPrefixUpperBound(code);
    for (const auto& entry : kSortedBuiltinWubi86Dict) {
        if (entry.packed_code <= exact_code) {
            continue;
        }
        if (entry.packed_code > prefix_hi) {
            break;
        }
        AppendUniqueWord(results, entry.word);
        if (results.size() >= kMaxCandidateResults) {
            return results;
        }
    }
    for (auto it = dict_.upper_bound(exact_code); it != dict_.end() && it->first <= prefix_hi; ++it) {
        for (const auto& w : it->second) {
            AppendUniqueWord(results, w);
            if (results.size() >= kMaxCandidateResults) {
                return results;
            }
        }
    }
    for (const auto& mapped : mapped_dicts_) {
        mapped->LookupPrefix(exact_code, exact_code, prefix_hi, results, kMaxCandidateResults);
        if (results.size() >= kMaxCandidateResults) {
            return results;
        }
    }

    return results;
}

} // namespace qiqi
