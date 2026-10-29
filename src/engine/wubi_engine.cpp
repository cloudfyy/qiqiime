#include "engine/wubi_engine.h"
#include "engine/wubi_default_dict.h"
#include "common/logger.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace qiqi {

namespace {

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), len);
    return result;
}

constexpr bool IsWubiCodeChar(wchar_t ch) noexcept {
    // Standard 86 Wubi uses 'a'-'y' for radicals and 'z' as wildcard/query key
    return ch >= L'a' && ch <= L'z';
}

} // namespace

WubiEngine::WubiEngine() {
    LoadBuiltinDictionary();
}

void WubiEngine::LoadBuiltinDictionary() {
    for (const auto& entry : kBuiltinWubi86Dict) {
        AddEntry(entry.code, entry.word);
    }
    QIQI_LOG_DEBUG("Loaded builtin Wubi86 dictionary: {} code keys", dict_.size());
}

void WubiEngine::AddEntry(std::wstring_view code, std::wstring_view word) {
    if (code.empty() || word.empty() || code.size() > kMaxCodeLength) {
        return;
    }
    std::wstring norm_code(code);
    for (auto& ch : norm_code) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
        if (!IsWubiCodeChar(ch)) {
            return;
        }
    }

    auto& list = dict_[norm_code];
    if (std::find(list.begin(), list.end(), word) == list.end()) {
        list.emplace_back(word);
    }
}

bool WubiEngine::LoadDictionaryFile(const std::filesystem::path& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        QIQI_LOG_WARN("Failed to open external dictionary file: {}",
                      log::WideToUtf8(file_path.wstring()));
        return false;
    }

    size_t loaded_words = 0;
    std::string line;
    while (std::getline(file, line)) {
        // Strip UTF-8 BOM on first line if present
        if (line.starts_with("\xEF\xBB\xBF")) {
            line.erase(0, 3);
        }

        std::wstring wline = Utf8ToWide(line);
        if (wline.empty()) {
            continue;
        }
        std::wistringstream iss(wline);
        std::wstring code;
        if (!(iss >> code) || code.empty() || code.starts_with(L'#')) {
            continue;
        }
        std::wstring word;
        while (iss >> word) {
            AddEntry(code, word);
            ++loaded_words;
        }
    }
    QIQI_LOG_INFO("Loaded external dictionary file '{}' ({} words, total {} code keys)",
                  log::WideToUtf8(file_path.wstring()), loaded_words, dict_.size());
    return true;
}

void WubiEngine::SetChineseMode(bool enabled) noexcept {
    chinese_mode_ = enabled;
    if (!chinese_mode_) {
        Reset();
    }
    try {
        QIQI_LOG_DEBUG("WubiEngine mode switched to {}", chinese_mode_ ? "Chinese" : "English");
    } catch (...) {
    }
}

void WubiEngine::ToggleChineseMode() noexcept {
    SetChineseMode(!chinese_mode_);
}

void WubiEngine::Reset() noexcept {
    raw_code_.clear();
    all_candidates_.clear();
    current_page_ = 0;
    selected_in_page_ = 0;
}

size_t WubiEngine::TotalPages() const noexcept {
    if (all_candidates_.empty()) {
        return 0;
    }
    return (all_candidates_.size() + page_size_ - 1) / page_size_;
}

std::vector<std::wstring> WubiEngine::CurrentPageCandidates() const {
    if (all_candidates_.empty()) {
        return {};
    }
    const size_t start = current_page_ * page_size_;
    if (start >= all_candidates_.size()) {
        return {};
    }
    const size_t end = std::min(start + page_size_, all_candidates_.size());
    return std::vector<std::wstring>(all_candidates_.begin() + static_cast<ptrdiff_t>(start),
                                     all_candidates_.begin() + static_cast<ptrdiff_t>(end));
}

std::vector<std::wstring> WubiEngine::LookupExact(std::wstring_view code) const {
    const auto it = dict_.find(std::wstring(code));
    if (it == dict_.end()) {
        return {};
    }
    return it->second;
}

std::vector<std::wstring> WubiEngine::LookupCandidates(std::wstring_view code) const {
    if (code.empty()) {
        return {};
    }

    std::vector<std::wstring> results;
    auto append_unique = [&results](const std::wstring& w) {
        if (std::find(results.begin(), results.end(), w) == results.end()) {
            results.push_back(w);
        }
    };

    // Wildcard query if 'z' is present
    const bool has_wildcard = code.find(L'z') != std::wstring_view::npos;
    if (has_wildcard) {
        for (const auto& [k, words] : dict_) {
            if (k.size() < code.size()) {
                continue;
            }
            bool match = true;
            for (size_t i = 0; i < code.size(); ++i) {
                if (code[i] != L'z' && code[i] != k[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                for (const auto& w : words) {
                    append_unique(w);
                    if (results.size() >= 50) {
                        return results;
                    }
                }
            }
        }
        return results;
    }

    // 1. Exact matches first
    if (const auto exact = dict_.find(std::wstring(code)); exact != dict_.end()) {
        for (const auto& w : exact->second) {
            append_unique(w);
        }
    }

    // 2. Prefix matches (sorted by code length then lexicographically)
    std::wstring prefix(code);
    for (auto it = dict_.lower_bound(prefix); it != dict_.end(); ++it) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) {
            break;
        }
        if (it->first == prefix) {
            continue;
        }
        for (const auto& w : it->second) {
            append_unique(w);
            if (results.size() >= 50) {
                return results;
            }
        }
    }

    return results;
}

void WubiEngine::RefreshCandidates() {
    all_candidates_ = LookupCandidates(raw_code_);
    current_page_ = 0;
    selected_in_page_ = 0;
}

KeyProcessResult WubiEngine::MakeCurrentStateResult(KeyActionType action, std::wstring committed) const {
    KeyProcessResult res;
    res.action = action;
    res.committed_text = std::move(committed);
    res.composition_text = raw_code_;
    res.page_candidates = CurrentPageCandidates();
    res.selected_index = selected_in_page_;
    res.current_page = current_page_;
    res.total_pages = TotalPages();
    return res;
}

constexpr std::optional<std::wstring_view> WubiEngine::MapChinesePunctuation(uint32_t vk, bool shift_down) noexcept {
    switch (vk) {
        case VK_OEM_COMMA:  return shift_down ? std::optional<std::wstring_view>(L"《") : std::optional<std::wstring_view>(L"，");
        case VK_OEM_PERIOD: return shift_down ? std::optional<std::wstring_view>(L"》") : std::optional<std::wstring_view>(L"。");
        case VK_OEM_1:      return shift_down ? std::optional<std::wstring_view>(L"：") : std::optional<std::wstring_view>(L"；");
        case VK_OEM_2:      return shift_down ? std::optional<std::wstring_view>(L"？") : std::optional<std::wstring_view>(L"、");
        case '1':           return shift_down ? std::optional<std::wstring_view>(L"！") : std::nullopt;
        case '4':           return shift_down ? std::optional<std::wstring_view>(L"￥") : std::nullopt;
        case '6':           return shift_down ? std::optional<std::wstring_view>(L"……") : std::nullopt;
        case '9':           return shift_down ? std::optional<std::wstring_view>(L"（") : std::nullopt;
        case '0':           return shift_down ? std::optional<std::wstring_view>(L"）") : std::nullopt;
        default:            return std::nullopt;
    }
}

bool WubiEngine::ShouldEatKey(uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down) const noexcept {
    if (!chinese_mode_ || ctrl_down || alt_down) {
        return false;
    }

    // Lowercase a-z starts or continues Wubi code
    if (!shift_down && vk >= 'A' && vk <= 'Z') {
        return true;
    }

    if (IsComposing()) {
        // While composing, IME owns navigation, commit, cancel, backspace, and number selection
        if (vk == VK_SPACE || vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_BACK ||
            vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT ||
            vk == VK_OEM_MINUS || vk == VK_OEM_PLUS) {
            return true;
        }
        if (!shift_down && vk >= '1' && vk <= '9') {
            return true;
        }
    }

    if (MapChinesePunctuation(vk, shift_down).has_value()) {
        return true;
    }

    return false;
}

KeyProcessResult WubiEngine::ProcessKey(uint32_t vk, bool shift_down, bool ctrl_down, bool alt_down) {
    if (!chinese_mode_ || ctrl_down || alt_down) {
        return {};
    }

    // 1. Handle Wubi letter input ('a' - 'z')
    if (!shift_down && vk >= 'A' && vk <= 'Z') {
        const wchar_t ch = static_cast<wchar_t>(vk - 'A' + L'a');
        std::wstring auto_committed;

        // Wubi 4-code rule: if already at 4 chars and user types 5th char,
        // commit current selected candidate first, then start new composition with 5th char.
        if (raw_code_.size() == kMaxCodeLength) {
            const auto page = CurrentPageCandidates();
            if (!page.empty()) {
                auto_committed = page[std::min(selected_in_page_, page.size() - 1)];
            } else {
                auto_committed = raw_code_;
            }
            Reset();
        }

        raw_code_.push_back(ch);
        RefreshCandidates();

        // Standard Wubi 86 4-code auto-commit:
        // When reaching 4 codes without wildcard 'z' and there is a unique exact match, commit immediately!
        if (raw_code_.size() == kMaxCodeLength && raw_code_.find(L'z') == std::wstring::npos) {
            const auto exact = LookupExact(raw_code_);
            if (exact.size() == 1) {
                std::wstring final_commit = auto_committed + exact.front();
                Reset();
                return MakeCurrentStateResult(KeyActionType::Commit, std::move(final_commit));
            }
        }

        if (!auto_committed.empty()) {
            return MakeCurrentStateResult(KeyActionType::Commit, std::move(auto_committed));
        }
        return MakeCurrentStateResult(KeyActionType::UpdateState);
    }

    // 2. Keys valid only while composing
    if (IsComposing()) {
        if (vk == VK_ESCAPE) {
            Reset();
            return MakeCurrentStateResult(KeyActionType::Cancel);
        }

        if (vk == VK_BACK) {
            raw_code_.pop_back();
            if (raw_code_.empty()) {
                Reset();
                return MakeCurrentStateResult(KeyActionType::Cancel);
            }
            RefreshCandidates();
            return MakeCurrentStateResult(KeyActionType::UpdateState);
        }

        if (vk == VK_SPACE) {
            const auto page = CurrentPageCandidates();
            std::wstring text = !page.empty()
                ? page[std::min(selected_in_page_, page.size() - 1)]
                : raw_code_;
            Reset();
            return MakeCurrentStateResult(KeyActionType::Commit, std::move(text));
        }

        if (vk == VK_RETURN) {
            // Enter commits raw ASCII code
            std::wstring text = raw_code_;
            Reset();
            return MakeCurrentStateResult(KeyActionType::Commit, std::move(text));
        }

        if (!shift_down && vk >= '1' && vk <= '9') {
            const size_t idx = static_cast<size_t>(vk - '1');
            const auto page = CurrentPageCandidates();
            if (idx < page.size()) {
                std::wstring text = page[idx];
                Reset();
                return MakeCurrentStateResult(KeyActionType::Commit, std::move(text));
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);
        }

        // Candidate navigation
        if (vk == VK_DOWN) {
            const auto page = CurrentPageCandidates();
            if (!page.empty() && selected_in_page_ + 1 < page.size()) {
                ++selected_in_page_;
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);
        }
        if (vk == VK_UP) {
            if (selected_in_page_ > 0) {
                --selected_in_page_;
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);
        }

        // Page navigation (- / + or PageUp / PageDown)
        if (vk == VK_NEXT || (!shift_down && vk == VK_OEM_PLUS)) {
            if (current_page_ + 1 < TotalPages()) {
                ++current_page_;
                selected_in_page_ = 0;
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);
        }
        if (vk == VK_PRIOR || (!shift_down && vk == VK_OEM_MINUS)) {
            if (current_page_ > 0) {
                --current_page_;
                selected_in_page_ = 0;
            }
            return MakeCurrentStateResult(KeyActionType::UpdateState);
        }
    }

    // 3. Chinese punctuation (if composing, commits current candidate + punctuation)
    if (auto punct = MapChinesePunctuation(vk, shift_down); punct.has_value()) {
        std::wstring committed;
        if (IsComposing()) {
            const auto page = CurrentPageCandidates();
            committed = !page.empty()
                ? page[std::min(selected_in_page_, page.size() - 1)]
                : raw_code_;
            Reset();
        }
        committed += *punct;
        return MakeCurrentStateResult(KeyActionType::Commit, std::move(committed));
    }

    return {};
}

} // namespace qiqi
