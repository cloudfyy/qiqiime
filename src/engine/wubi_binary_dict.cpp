#include "engine/wubi_binary_dict.h"
#include "common/file_util.h"
#include "common/logger.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

namespace qiqi {

namespace {

constexpr uint32_t kDictMagic = 0x49514257u;
constexpr uint32_t kDictFormatVersion = 1u;

struct ProcessDictCacheEntry {
    uint64_t src_size = 0;
    uint64_t src_mtime = 0;
    std::shared_ptr<const WubiMappedDict> dict;
};

std::mutex g_dict_cache_mutex;
std::map<std::wstring, ProcessDictCacheEntry> g_process_dict_cache;

constexpr bool IsWubiCodeChar(wchar_t ch) noexcept {
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

constexpr bool IsWubiCodeToken(std::wstring_view token) noexcept {
    if (token.empty() || token.size() > 4) {
        return false;
    }
    for (const wchar_t ch : token) {
        if (!IsWubiCodeChar(ch)) {
            return false;
        }
    }
    return true;
}

constexpr std::wstring_view NextToken(std::wstring_view& line) noexcept {
    while (!line.empty() && (line.front() == L' ' || line.front() == L'\t' || line.front() == L'\r')) {
        line.remove_prefix(1);
    }
    if (line.empty()) {
        return {};
    }
    size_t pos = 0;
    while (pos < line.size()) {
        const wchar_t ch = gsl::at(line, pos);
        if (ch == L' ' || ch == L'\t' || ch == L'\r') {
            break;
        }
        ++pos;
    }
    const std::wstring_view token = line.substr(0, pos);
    line.remove_prefix(pos);
    return token;
}

bool ParseTextDictIntoBinaryBuffer(
    const std::filesystem::path& txt_path,
    uint64_t src_size,
    uint64_t src_mtime,
    std::vector<uint8_t>& out_buffer) {
    std::string raw_utf8;
    if (!ReadFileToUtf8String(txt_path, raw_utf8)) {
        return false;
    }

    const std::wstring wide_content = Utf8ToWide(StripUtf8Bom(raw_utf8));
    std::wstring_view content_view(wide_content);

    std::map<uint32_t, std::vector<std::wstring_view>> grouped;
    bool in_yaml_header = false;

    while (!content_view.empty()) {
        const size_t nl = content_view.find(L'\n');
        std::wstring_view line = (nl == std::wstring_view::npos)
            ? content_view
            : content_view.substr(0, nl);
        content_view.remove_prefix(nl == std::wstring_view::npos ? content_view.size() : nl + 1);

        const std::wstring_view first = NextToken(line);
        if (first.empty() || first.starts_with(L'#')) {
            continue;
        }

        if (first == L"---") {
            in_yaml_header = true;
            continue;
        }
        if (in_yaml_header) {
            if (first == L"...") {
                in_yaml_header = false;
            }
            continue;
        }

        auto add_to_group = [&grouped](std::wstring_view code_tok, std::wstring_view word_tok) {
            const uint32_t packed = PackWubiCode(code_tok);
            if (packed == 0 || word_tok.empty()) {
                return;
            }
            auto& vec = grouped[packed];
            if (std::find(vec.begin(), vec.end(), word_tok) == vec.end()) {
                vec.push_back(word_tok);
            }
        };

        if (IsWubiCodeToken(first)) {
            for (std::wstring_view word = NextToken(line); !word.empty(); word = NextToken(line)) {
                add_to_group(first, word);
            }
        } else {
            const std::wstring_view second = NextToken(line);
            if (IsWubiCodeToken(second)) {
                add_to_group(second, first);
            }
        }
    }

    std::vector<DictCodeIndex> code_indices;
    code_indices.reserve(grouped.size());
    std::vector<DictWordSlice> word_slices;
    std::wstring char_pool;

    for (const auto& [packed_code, words] : grouped) {
        if (words.empty()) {
            continue;
        }
        DictCodeIndex ci{};
        ci.packed_code = packed_code;
        ci.first_word_idx = gsl::narrow_cast<uint32_t>(word_slices.size());
        ci.word_count = gsl::narrow_cast<uint32_t>(words.size());
        code_indices.push_back(ci);

        for (const auto word_sv : words) {
            DictWordSlice ws{};
            ws.char_offset = gsl::narrow_cast<uint32_t>(char_pool.size());
            ws.char_length = gsl::narrow_cast<uint32_t>(word_sv.size());
            word_slices.push_back(ws);
            char_pool.append(word_sv);
        }
    }

    DictBinaryHeader hdr{};
    hdr.magic = kDictMagic;
    hdr.version = kDictFormatVersion;
    hdr.source_size = src_size;
    hdr.source_mtime = src_mtime;
    hdr.code_count = gsl::narrow_cast<uint32_t>(code_indices.size());
    hdr.word_count = gsl::narrow_cast<uint32_t>(word_slices.size());
    hdr.char_count = gsl::narrow_cast<uint32_t>(char_pool.size());

    const size_t codes_bytes = code_indices.size() * sizeof(DictCodeIndex);
    const size_t words_bytes = word_slices.size() * sizeof(DictWordSlice);
    const size_t chars_bytes = char_pool.size() * sizeof(wchar_t);
    const size_t total_bytes = sizeof(DictBinaryHeader) + codes_bytes + words_bytes + chars_bytes;

    out_buffer.resize(total_bytes);
    std::span<uint8_t> out_span(out_buffer);
    std::memcpy(out_span.data(), &hdr, sizeof(hdr));
    out_span = out_span.subspan(sizeof(hdr));
    if (codes_bytes > 0) {
        std::memcpy(out_span.data(), code_indices.data(), codes_bytes);
        out_span = out_span.subspan(codes_bytes);
    }
    if (words_bytes > 0) {
        std::memcpy(out_span.data(), word_slices.data(), words_bytes);
        out_span = out_span.subspan(words_bytes);
    }
    if (chars_bytes > 0) {
        std::memcpy(out_span.data(), char_pool.data(), chars_bytes);
    }
    return true;
}

} // namespace

void WubiMappedDict::ClearProcessCache() noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_dict_cache_mutex);
        g_process_dict_cache.clear();
    } catch (...) {
    }
}

void WubiMappedDict::ResetViews() noexcept {
    code_index_ = {};
    word_slices_ = {};
    char_pool_ = {};
    mapped_view_.Reset();
    mapping_handle_.Reset();
    file_handle_.Reset();
}

bool WubiMappedDict::BindBufferView(
    std::span<const uint8_t> buffer,
    uint64_t expected_src_size,
    uint64_t expected_src_mtime) noexcept {
    if (buffer.size() < sizeof(DictBinaryHeader)) {
        QIQI_LOG_WARN("BindBufferView: buffer size ({} bytes) smaller than DictBinaryHeader", buffer.size());
        return false;
    }
    const auto* hdr = reinterpret_cast<const DictBinaryHeader*>(buffer.data());
    if (hdr->magic != kDictMagic || hdr->version != kDictFormatVersion) {
        QIQI_LOG_WARN("BindBufferView: invalid .qdict header (magic=0x{:08X}, version={})",
                      hdr->magic, hdr->version);
        return false;
    }
    if (expected_src_size != 0 && hdr->source_size != expected_src_size) {
        QIQI_LOG_DEBUG("BindBufferView: source_size mismatch (cached={}, current={})",
                       hdr->source_size, expected_src_size);
        return false;
    }
    if (expected_src_mtime != 0 && hdr->source_mtime != 0 && hdr->source_mtime != expected_src_mtime) {
        QIQI_LOG_DEBUG("BindBufferView: source_mtime mismatch (cached={}, current={})",
                       hdr->source_mtime, expected_src_mtime);
        return false;
    }

    const size_t codes_bytes = static_cast<size_t>(hdr->code_count) * sizeof(DictCodeIndex);
    const size_t words_bytes = static_cast<size_t>(hdr->word_count) * sizeof(DictWordSlice);
    const size_t chars_bytes = static_cast<size_t>(hdr->char_count) * sizeof(wchar_t);
    const size_t required_bytes = sizeof(DictBinaryHeader) + codes_bytes + words_bytes + chars_bytes;
    if (buffer.size() < required_bytes) {
        QIQI_LOG_WARN("BindBufferView: truncated .qdict buffer (actual={} bytes, required={} bytes)",
                      buffer.size(), required_bytes);
        return false;
    }

    const auto codes_span = buffer.subspan(sizeof(DictBinaryHeader), codes_bytes);
    const auto words_span = buffer.subspan(sizeof(DictBinaryHeader) + codes_bytes, words_bytes);
    const auto chars_span = buffer.subspan(sizeof(DictBinaryHeader) + codes_bytes + words_bytes, chars_bytes);

    code_index_ = std::span<const DictCodeIndex>(
        reinterpret_cast<const DictCodeIndex*>(codes_span.data()), hdr->code_count);
    word_slices_ = std::span<const DictWordSlice>(
        reinterpret_cast<const DictWordSlice*>(words_span.data()), hdr->word_count);
    char_pool_ = std::span<const wchar_t>(
        reinterpret_cast<const wchar_t*>(chars_span.data()), hdr->char_count);
    return true;
}

bool WubiMappedDict::OpenMappedFile(
    const std::filesystem::path& bin_path,
    uint64_t expected_src_size,
    uint64_t expected_src_mtime) noexcept {
    ResetViews();

    // Open with least privilege:
    // - GENERIC_READ only (no write/execute)
    // - FILE_SHARE_READ | FILE_SHARE_DELETE: disallows concurrent in-place write/truncation while mapped
    //   (preventing STATUS_IN_PAGE_ERROR), while allowing another process to atomically replace the cache
    //   directory entry via WriteFileAtomically (MoveFileExW MOVEFILE_REPLACE_EXISTING) when .txt is updated.
    // - SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS to prevent named-pipe token impersonation
    ScopedFileHandle file(::CreateFileW(
        bin_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
        nullptr));
    if (!file.IsValid() || ::GetFileType(file.Get()) != FILE_TYPE_DISK) {
        return false;
    }

    LARGE_INTEGER file_size{};
    constexpr LONGLONG kMaxDictBytes = 128LL * 1024LL * 1024LL; // 128 MB upper bound
    if (!::GetFileSizeEx(file.Get(), &file_size) ||
        file_size.QuadPart < gsl::narrow_cast<LONGLONG>(sizeof(DictBinaryHeader)) ||
        file_size.QuadPart > kMaxDictBytes) {
        QIQI_LOG_WARN("OpenMappedFile: invalid cache file size ({} bytes) for '{}'",
                      file_size.QuadPart, WideToUtf8(bin_path.native()));
        return false;
    }

    // Unnamed (anonymous) read-only file mapping (lpName == nullptr, PAGE_READONLY)
    ScopedKernelHandle mapping(::CreateFileMappingW(
        file.Get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
    if (!mapping.IsValid()) {
        QIQI_LOG_WARN("OpenMappedFile: CreateFileMappingW failed for '{}' (error={})",
                      WideToUtf8(bin_path.native()), ::GetLastError());
        return false;
    }

    // Map with FILE_MAP_READ only
    ScopedMappedView view(::MapViewOfFile(mapping.Get(), FILE_MAP_READ, 0, 0, 0));
    if (!view.IsValid()) {
        QIQI_LOG_WARN("OpenMappedFile: MapViewOfFile failed for '{}' (error={})",
                      WideToUtf8(bin_path.native()), ::GetLastError());
        return false;
    }

    const std::span<const uint8_t> mapped_span(
        static_cast<const uint8_t*>(view.Get()),
        gsl::narrow_cast<size_t>(file_size.QuadPart));
    if (!BindBufferView(mapped_span, expected_src_size, expected_src_mtime)) {
        QIQI_LOG_DEBUG("OpenMappedFile: cache header/timestamp mismatch for '{}', will rebuild",
                       WideToUtf8(bin_path.native()));
        return false;
    }

    // Keep `file_handle_` open with FILE_SHARE_READ to lock the backing file against concurrent
    // write/truncation while mapped; `mapping_handle_` can be released immediately or retained.
    file_handle_ = std::move(file);
    mapping_handle_ = std::move(mapping);
    mapped_view_ = std::move(view);
    return true;
}

bool WubiMappedDict::IsSourceModified() const noexcept {
    try {
        if (source_path_.empty()) {
            return false;
        }
        std::error_code ec;
        if (!std::filesystem::exists(source_path_, ec)) {
            return false;
        }
        const uint64_t cur_size = std::filesystem::file_size(source_path_, ec);
        const uint64_t cur_mtime = gsl::narrow_cast<uint64_t>(
            std::filesystem::last_write_time(source_path_, ec).time_since_epoch().count());
        if (ec) {
            return false;
        }
        return (cur_size != loaded_src_size_) || (cur_mtime != loaded_src_mtime_);
    } catch (...) {
        return false;
    }
}

std::shared_ptr<const WubiMappedDict> WubiMappedDict::LoadFromBinaryBuffer(
    std::vector<uint8_t> bin_buffer) {
    auto dict = std::make_shared<WubiMappedDict>();
    dict->fallback_buffer_ = std::move(bin_buffer);
    if (!dict->BindBufferView(dict->fallback_buffer_, 0, 0)) {
        return nullptr;
    }
    return dict;
}

bool WubiMappedDict::CompileTextFileToQdict(
    const std::filesystem::path& txt_path,
    const std::filesystem::path& out_qdict_path) {
    std::error_code ec;
    if (!std::filesystem::exists(txt_path, ec)) {
        return false;
    }
    const uint64_t src_size = std::filesystem::file_size(txt_path, ec);
    const uint64_t src_mtime = gsl::narrow_cast<uint64_t>(
        std::filesystem::last_write_time(txt_path, ec).time_since_epoch().count());
    if (ec) {
        return false;
    }
    std::vector<uint8_t> bin_buffer;
    if (!ParseTextDictIntoBinaryBuffer(txt_path, src_size, src_mtime, bin_buffer)) {
        return false;
    }
    return WriteFileAtomically(out_qdict_path, bin_buffer);
}

std::shared_ptr<const WubiMappedDict> WubiMappedDict::LoadFromTextOrCache(
    const std::filesystem::path& txt_path) {
    std::error_code ec;
    std::filesystem::path adjacent_qdict = txt_path;
    adjacent_qdict.replace_extension(L".qdict");

    const bool txt_exists = std::filesystem::exists(txt_path, ec);
    const bool qdict_exists = std::filesystem::exists(adjacent_qdict, ec);
    if (!txt_exists && !qdict_exists) {
        return nullptr;
    }

    const uint64_t src_size = txt_exists ? std::filesystem::file_size(txt_path, ec) : 0;
    const uint64_t src_mtime = txt_exists
        ? gsl::narrow_cast<uint64_t>(std::filesystem::last_write_time(txt_path, ec).time_since_epoch().count())
        : gsl::narrow_cast<uint64_t>(std::filesystem::last_write_time(adjacent_qdict, ec).time_since_epoch().count());
    const std::wstring canonical_key =
        std::filesystem::weakly_canonical(txt_exists ? txt_path : adjacent_qdict, ec).wstring();

    {
        std::lock_guard<std::mutex> lock(g_dict_cache_mutex);
        const auto it = g_process_dict_cache.find(canonical_key);
        if (it != g_process_dict_cache.end() &&
            it->second.src_size == src_size &&
            it->second.src_mtime == src_mtime &&
            it->second.dict) {
            QIQI_LOG_DEBUG("In-process dictionary cache hit for '{}'", WideToUtf8(canonical_key));
            return it->second.dict;
        }
    }

    auto dict = std::make_shared<WubiMappedDict>();
    dict->source_path_ = txt_exists ? txt_path : adjacent_qdict;
    dict->loaded_src_size_ = src_size;
    dict->loaded_src_mtime_ = src_mtime;

    std::filesystem::path fallback_cache;
    if (const std::filesystem::path app_dir = GetAppDataDirectory(); !app_dir.empty()) {
        const std::filesystem::path cache_dir = app_dir / L"cache";
        std::filesystem::create_directories(cache_dir, ec);
        if (!ec) {
            fallback_cache = cache_dir / (txt_path.stem().wstring() + L".qdict");
        }
    }

    // 1. Try mapping existing binary cache directly (prefer adjacent .qdict, then fallback %LOCALAPPDATA%\cache\.qdict)
    for (const auto& candidate : {adjacent_qdict, fallback_cache}) {
        if (!candidate.empty() && std::filesystem::exists(candidate, ec) &&
            dict->OpenMappedFile(candidate, src_size, txt_exists ? src_mtime : 0)) {
            QIQI_LOG_INFO("Mapped binary dictionary cache '{}' ({} codes, {} words)",
                          WideToUtf8(candidate.wstring()),
                          dict->CodeCount(),
                          dict->WordCount());
            std::lock_guard<std::mutex> lock(g_dict_cache_mutex);
            g_process_dict_cache[canonical_key] = {src_size, src_mtime, dict};
            return dict;
        }
    }

    if (!txt_exists) {
        return nullptr;
    }

    // 2. Parse text dictionary and compile binary image
    std::vector<uint8_t> bin_buffer;
    if (!ParseTextDictIntoBinaryBuffer(txt_path, src_size, src_mtime, bin_buffer)) {
        return nullptr;
    }

    // Write binary cache to disk atomically via an exclusive-write temporary file + MoveFileExW
    // (try adjacent .qdict first; if read-only such as Program Files, write to fallback %LOCALAPPDATA%\cache\.qdict).
    std::filesystem::path written_cache = adjacent_qdict;
    if (!WriteFileAtomically(written_cache, bin_buffer) && !fallback_cache.empty()) {
        written_cache = fallback_cache;
        std::ignore = WriteFileAtomically(written_cache, bin_buffer);
    }

    if (!dict->OpenMappedFile(written_cache, src_size, src_mtime)) {
        // Fallback to flat in-memory buffer if file mapping is unavailable in sandbox
        QIQI_LOG_WARN("Falling back to in-memory dictionary buffer for '{}'",
                      WideToUtf8(txt_path.wstring()));
        dict->fallback_buffer_ = std::move(bin_buffer);
        if (!dict->BindBufferView(dict->fallback_buffer_, src_size, src_mtime)) {
            return nullptr;
        }
    }

    QIQI_LOG_INFO("Compiled & mapped dictionary '{}' -> '{}' ({} codes, {} words)",
                  WideToUtf8(txt_path.wstring()),
                  WideToUtf8(written_cache.wstring()),
                  dict->CodeCount(),
                  dict->WordCount());

    {
        std::lock_guard<std::mutex> lock(g_dict_cache_mutex);
        g_process_dict_cache[canonical_key] = {src_size, src_mtime, dict};
    }
    return dict;
}

std::wstring_view WubiMappedDict::GetWordView(const DictWordSlice& slice) const noexcept {
    if (static_cast<size_t>(slice.char_offset) + slice.char_length > char_pool_.size()) {
        return {};
    }
    const auto sub = char_pool_.subspan(slice.char_offset, slice.char_length);
    return std::wstring_view(sub.data(), sub.size());
}

bool WubiMappedDict::AppendWordsForCodeEntry(
    const DictCodeIndex& entry,
    std::vector<std::wstring>& out_words,
    size_t max_results) const {
    const size_t end_idx = std::min<size_t>(
        static_cast<size_t>(entry.first_word_idx) + entry.word_count, word_slices_.size());
    for (size_t i = entry.first_word_idx; i < end_idx; ++i) {
        const std::wstring_view w = GetWordView(gsl::at(word_slices_, i));
        if (!w.empty()) {
            AppendUniqueWord(out_words, w);
            if (out_words.size() >= max_results) {
                return true;
            }
        }
    }
    return false;
}

void WubiMappedDict::LookupExact(
    uint32_t packed_code, std::vector<std::wstring>& out_words) const {
    if (packed_code == 0 || code_index_.empty()) {
        return;
    }
    const auto it = std::lower_bound(
        code_index_.begin(),
        code_index_.end(),
        packed_code,
        [](const DictCodeIndex& entry, uint32_t code) noexcept {
            return entry.packed_code < code;
        });
    if (it == code_index_.end() || it->packed_code != packed_code) {
        return;
    }
    AppendWordsForCodeEntry(*it, out_words, SIZE_MAX);
}

void WubiMappedDict::LookupPrefix(
    uint32_t exact_code,
    uint32_t prefix_lo,
    uint32_t prefix_hi,
    std::vector<std::wstring>& out_words,
    size_t max_results,
    std::wstring_view typed_prefix,
    std::vector<std::wstring>* out_display_labels) const {
    if (prefix_lo == 0 || code_index_.empty() || out_words.size() >= max_results) {
        return;
    }
    auto it = std::lower_bound(
        code_index_.begin(),
        code_index_.end(),
        prefix_lo,
        [](const DictCodeIndex& entry, uint32_t code) noexcept {
            return entry.packed_code < code;
        });

    for (; it != code_index_.end() && it->packed_code <= prefix_hi; ++it) {
        if (it->packed_code == exact_code) {
            continue;
        }
        const size_t prev_count = out_words.size();
        const bool reached_cap = AppendWordsForCodeEntry(*it, out_words, max_results);
        if (out_display_labels && out_words.size() > prev_count) {
            const std::wstring code_str = UnpackWubiCode(it->packed_code);
            const std::wstring_view suffix = (code_str.size() > typed_prefix.size())
                ? std::wstring_view(code_str).substr(typed_prefix.size())
                : std::wstring_view(code_str);
            for (size_t idx = prev_count; idx < out_words.size(); ++idx) {
                out_display_labels->push_back(
                    gsl::at(out_words, idx) + L"(~" + std::wstring(suffix) + L")");
            }
        }
        if (reached_cap) {
            return;
        }
    }
}

void WubiMappedDict::LookupWildcard(
    std::wstring_view pattern,
    std::vector<std::wstring>& out_words,
    size_t max_results,
    std::vector<std::wstring>* out_display_labels) const {
    if (pattern.empty() || code_index_.empty() || out_words.size() >= max_results) {
        return;
    }

    // Narrow scan range if the first character is not 'z'
    uint32_t lo = 0;
    uint32_t hi = 0xFFFFFFFFu;
    if (pattern.front() != L'z' && pattern.front() != L'Z') {
        const std::wstring_view first_char = pattern.substr(0, 1);
        lo = PackWubiCode(first_char);
        hi = PackWubiPrefixUpperBound(first_char);
    }

    auto it = std::lower_bound(
        code_index_.begin(),
        code_index_.end(),
        lo,
        [](const DictCodeIndex& entry, uint32_t code) noexcept {
            return entry.packed_code < code;
        });

    for (; it != code_index_.end() && it->packed_code <= hi; ++it) {
        if (!MatchesWildcardPattern(it->packed_code, pattern)) {
            continue;
        }
        const size_t prev_count = out_words.size();
        const bool reached_cap = AppendWordsForCodeEntry(*it, out_words, max_results);
        if (out_display_labels && out_words.size() > prev_count) {
            const std::wstring code_str = UnpackWubiCode(it->packed_code);
            for (size_t idx = prev_count; idx < out_words.size(); ++idx) {
                out_display_labels->push_back(gsl::at(out_words, idx) + L"(" + code_str + L")");
            }
        }
        if (reached_cap) {
            return;
        }
    }
}

std::wstring WubiMappedDict::FindLongestCodeForWord(std::wstring_view word) const {
    if (word.empty() || code_index_.empty()) {
        return {};
    }
    std::wstring best_code;
    for (const auto& entry : code_index_) {
        const size_t end_idx = std::min<size_t>(
            static_cast<size_t>(entry.first_word_idx) + entry.word_count, word_slices_.size());
        for (size_t i = entry.first_word_idx; i < end_idx; ++i) {
            if (GetWordView(gsl::at(word_slices_, i)) == word) {
                std::wstring candidate = UnpackWubiCode(entry.packed_code);
                if (candidate.size() > best_code.size()) {
                    best_code = std::move(candidate);
                    if (best_code.size() >= 4) {
                        return best_code;
                    }
                }
                break;
            }
        }
    }
    return best_code;
}

} // namespace qiqi
