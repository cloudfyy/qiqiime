#include "engine/wubi_binary_dict.h"
#include "common/logger.h"

#include <algorithm>
#include <cstring>
#include <fstream>
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
    for (wchar_t ch : token) {
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
    while (pos < line.size() && line[pos] != L' ' && line[pos] != L'\t' && line[pos] != L'\r') {
        ++pos;
    }
    const std::wstring_view token = line.substr(0, pos);
    line.remove_prefix(pos);
    return token;
}

void AppendUniqueWord(std::vector<std::wstring>& list, std::wstring_view word) {
    if (std::find(list.begin(), list.end(), word) == list.end()) {
        list.emplace_back(word);
    }
}

std::filesystem::path GetCacheFilePath(const std::filesystem::path& txt_path) {
    // 1. Prefer adjacent .qdict file if it already exists or its directory is writable
    std::filesystem::path local_cache = txt_path;
    local_cache.replace_extension(L".qdict");
    std::error_code ec;
    if (std::filesystem::exists(local_cache, ec)) {
        return local_cache;
    }

    // 2. Otherwise use %LOCALAPPDATA%\QiQiIME\cache\<stem>.qdict
    const std::filesystem::path app_dir = log::GetAppDataDirectory();
    if (!app_dir.empty()) {
        const std::filesystem::path cache_dir = app_dir / L"cache";
        std::filesystem::create_directories(cache_dir, ec);
        if (!ec) {
            return cache_dir / (txt_path.stem().wstring() + L".qdict");
        }
    }
    return local_cache;
}

bool ParseTextDictIntoBinaryBuffer(
    const std::filesystem::path& txt_path,
    uint64_t src_size,
    uint64_t src_mtime,
    std::vector<uint8_t>& out_buffer) {
    std::ifstream file(txt_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    const auto file_size = file.tellg();
    std::string raw_utf8;
    if (file_size > 0) {
        raw_utf8.resize(static_cast<size_t>(file_size));
        file.seekg(0, std::ios::beg);
        file.read(raw_utf8.data(), file_size);
    }

    std::string_view utf8_view(raw_utf8);
    if (utf8_view.starts_with("\xEF\xBB\xBF")) {
        utf8_view.remove_prefix(3);
    }

    const std::wstring wide_content = log::Utf8ToWide(utf8_view);
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
        ci.first_word_idx = static_cast<uint32_t>(word_slices.size());
        ci.word_count = static_cast<uint32_t>(words.size());
        code_indices.push_back(ci);

        for (const auto word_sv : words) {
            DictWordSlice ws{};
            ws.char_offset = static_cast<uint32_t>(char_pool.size());
            ws.char_length = static_cast<uint32_t>(word_sv.size());
            word_slices.push_back(ws);
            char_pool.append(word_sv);
        }
    }

    DictBinaryHeader hdr{};
    hdr.magic = kDictMagic;
    hdr.version = kDictFormatVersion;
    hdr.source_size = src_size;
    hdr.source_mtime = src_mtime;
    hdr.code_count = static_cast<uint32_t>(code_indices.size());
    hdr.word_count = static_cast<uint32_t>(word_slices.size());
    hdr.char_count = static_cast<uint32_t>(char_pool.size());

    const size_t codes_bytes = code_indices.size() * sizeof(DictCodeIndex);
    const size_t words_bytes = word_slices.size() * sizeof(DictWordSlice);
    const size_t chars_bytes = char_pool.size() * sizeof(wchar_t);
    const size_t total_bytes = sizeof(DictBinaryHeader) + codes_bytes + words_bytes + chars_bytes;

    out_buffer.resize(total_bytes);
    uint8_t* ptr = out_buffer.data();
    std::memcpy(ptr, &hdr, sizeof(hdr));
    ptr += sizeof(hdr);
    if (codes_bytes > 0) {
        std::memcpy(ptr, code_indices.data(), codes_bytes);
        ptr += codes_bytes;
    }
    if (words_bytes > 0) {
        std::memcpy(ptr, word_slices.data(), words_bytes);
        ptr += words_bytes;
    }
    if (chars_bytes > 0) {
        std::memcpy(ptr, char_pool.data(), chars_bytes);
    }
    return true;
}

} // namespace

WubiMappedDict::~WubiMappedDict() noexcept {
    CloseMapping();
}

void WubiMappedDict::CloseMapping() noexcept {
    code_index_ = {};
    word_slices_ = {};
    char_pool_ = {};

    if (mapped_view_) {
        ::UnmapViewOfFile(mapped_view_);
        mapped_view_ = nullptr;
    }
    if (mapping_handle_) {
        ::CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
    }
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        ::CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
    }
}

bool WubiMappedDict::BindBufferView(
    const uint8_t* base,
    size_t total_bytes,
    uint64_t expected_src_size,
    uint64_t expected_src_mtime) noexcept {
    if (!base || total_bytes < sizeof(DictBinaryHeader)) {
        return false;
    }
    const auto* hdr = reinterpret_cast<const DictBinaryHeader*>(base);
    if (hdr->magic != kDictMagic || hdr->version != kDictFormatVersion) {
        return false;
    }
    if (expected_src_size != 0 &&
        (hdr->source_size != expected_src_size || hdr->source_mtime != expected_src_mtime)) {
        return false;
    }

    const size_t codes_bytes = static_cast<size_t>(hdr->code_count) * sizeof(DictCodeIndex);
    const size_t words_bytes = static_cast<size_t>(hdr->word_count) * sizeof(DictWordSlice);
    const size_t chars_bytes = static_cast<size_t>(hdr->char_count) * sizeof(wchar_t);
    const size_t required_bytes = sizeof(DictBinaryHeader) + codes_bytes + words_bytes + chars_bytes;
    if (total_bytes < required_bytes) {
        return false;
    }

    const uint8_t* codes_ptr = base + sizeof(DictBinaryHeader);
    const uint8_t* words_ptr = codes_ptr + codes_bytes;
    const uint8_t* chars_ptr = words_ptr + words_bytes;

    code_index_ = std::span<const DictCodeIndex>(
        reinterpret_cast<const DictCodeIndex*>(codes_ptr), hdr->code_count);
    word_slices_ = std::span<const DictWordSlice>(
        reinterpret_cast<const DictWordSlice*>(words_ptr), hdr->word_count);
    char_pool_ = std::span<const wchar_t>(
        reinterpret_cast<const wchar_t*>(chars_ptr), hdr->char_count);
    return true;
}

bool WubiMappedDict::OpenMappedFile(
    const std::filesystem::path& bin_path,
    uint64_t expected_src_size,
    uint64_t expected_src_mtime) noexcept {
    CloseMapping();

    file_handle_ = ::CreateFileW(
        bin_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER file_size{};
    if (!::GetFileSizeEx(file_handle_, &file_size) ||
        file_size.QuadPart < static_cast<LONGLONG>(sizeof(DictBinaryHeader))) {
        CloseMapping();
        return false;
    }

    mapping_handle_ = ::CreateFileMappingW(
        file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping_handle_) {
        CloseMapping();
        return false;
    }

    mapped_view_ = ::MapViewOfFile(mapping_handle_, FILE_MAP_READ, 0, 0, 0);
    if (!mapped_view_) {
        CloseMapping();
        return false;
    }

    if (!BindBufferView(
            static_cast<const uint8_t*>(mapped_view_),
            static_cast<size_t>(file_size.QuadPart),
            expected_src_size,
            expected_src_mtime)) {
        CloseMapping();
        return false;
    }
    return true;
}

std::shared_ptr<const WubiMappedDict> WubiMappedDict::LoadFromTextOrCache(
    const std::filesystem::path& txt_path) {
    std::error_code ec;
    if (!std::filesystem::exists(txt_path, ec)) {
        return nullptr;
    }

    const uint64_t src_size = std::filesystem::file_size(txt_path, ec);
    const auto mtime_tp = std::filesystem::last_write_time(txt_path, ec);
    const uint64_t src_mtime = static_cast<uint64_t>(mtime_tp.time_since_epoch().count());
    const std::wstring canonical_key = std::filesystem::weakly_canonical(txt_path, ec).wstring();

    {
        std::lock_guard<std::mutex> lock(g_dict_cache_mutex);
        const auto it = g_process_dict_cache.find(canonical_key);
        if (it != g_process_dict_cache.end() &&
            it->second.src_size == src_size &&
            it->second.src_mtime == src_mtime &&
            it->second.dict) {
            return it->second.dict;
        }
    }

    auto dict = std::make_shared<WubiMappedDict>();
    const std::filesystem::path cache_bin_path = GetCacheFilePath(txt_path);

    // 1. Try mapping existing binary cache directly (zero parse, zero heap copy, demand-paged)
    if (dict->OpenMappedFile(cache_bin_path, src_size, src_mtime)) {
        QIQI_LOG_INFO("Mapped binary dictionary cache '{}' ({} codes, {} words)",
                      log::WideToUtf8(cache_bin_path.wstring()),
                      dict->CodeCount(),
                      dict->WordCount());
        std::lock_guard<std::mutex> lock(g_dict_cache_mutex);
        g_process_dict_cache[canonical_key] = {src_size, src_mtime, dict};
        return dict;
    }

    // 2. Parse text dictionary and compile binary image
    std::vector<uint8_t> bin_buffer;
    if (!ParseTextDictIntoBinaryBuffer(txt_path, src_size, src_mtime, bin_buffer)) {
        return nullptr;
    }

    // Write binary cache to disk so this and all other processes can memory-map it
    {
        std::ofstream ofs(cache_bin_path, std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char*>(bin_buffer.data()),
                      static_cast<std::streamsize>(bin_buffer.size()));
            ofs.close();
        }
    }

    if (!dict->OpenMappedFile(cache_bin_path, src_size, src_mtime)) {
        // Fallback to flat in-memory buffer if file mapping is unavailable in sandbox
        dict->fallback_buffer_ = std::move(bin_buffer);
        if (!dict->BindBufferView(
                dict->fallback_buffer_.data(),
                dict->fallback_buffer_.size(),
                src_size,
                src_mtime)) {
            return nullptr;
        }
    }

    QIQI_LOG_INFO("Compiled & mapped dictionary '{}' -> '{}' ({} codes, {} words)",
                  log::WideToUtf8(txt_path.wstring()),
                  log::WideToUtf8(cache_bin_path.wstring()),
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
    return std::wstring_view(char_pool_.data() + slice.char_offset, slice.char_length);
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
    const size_t end_idx = std::min<size_t>(
        static_cast<size_t>(it->first_word_idx) + it->word_count, word_slices_.size());
    for (size_t i = it->first_word_idx; i < end_idx; ++i) {
        const std::wstring_view w = GetWordView(word_slices_[i]);
        if (!w.empty()) {
            AppendUniqueWord(out_words, w);
        }
    }
}

void WubiMappedDict::LookupPrefix(
    uint32_t exact_code,
    uint32_t prefix_lo,
    uint32_t prefix_hi,
    std::vector<std::wstring>& out_words,
    size_t max_results) const {
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
        const size_t end_idx = std::min<size_t>(
            static_cast<size_t>(it->first_word_idx) + it->word_count, word_slices_.size());
        for (size_t i = it->first_word_idx; i < end_idx; ++i) {
            const std::wstring_view w = GetWordView(word_slices_[i]);
            if (!w.empty()) {
                AppendUniqueWord(out_words, w);
                if (out_words.size() >= max_results) {
                    return;
                }
            }
        }
    }
}

void WubiMappedDict::LookupWildcard(
    std::wstring_view pattern,
    std::vector<std::wstring>& out_words,
    size_t max_results) const {
    if (pattern.empty() || code_index_.empty() || out_words.size() >= max_results) {
        return;
    }

    // Narrow scan range if the first character is not 'z'
    uint32_t lo = 0;
    uint32_t hi = 0xFFFFFFFFu;
    if (pattern[0] != L'z' && pattern[0] != L'Z') {
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
        const size_t end_idx = std::min<size_t>(
            static_cast<size_t>(it->first_word_idx) + it->word_count, word_slices_.size());
        for (size_t i = it->first_word_idx; i < end_idx; ++i) {
            const std::wstring_view w = GetWordView(word_slices_[i]);
            if (!w.empty()) {
                AppendUniqueWord(out_words, w);
                if (out_words.size() >= max_results) {
                    return;
                }
            }
        }
    }
}

} // namespace qiqi
