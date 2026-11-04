#pragma once

#include "common/scoped_handle.h"
#include <filesystem>
#include <memory>
#include <string_view>

namespace qiqi::dict_manager {

inline constexpr std::wstring_view kWindowClass = L"QiQiIMEDictManagerWindow";

enum class ControlId {
    Reload = 200,
    Copy,
    Import,
    Export,
    Backup,
    Restore,
    ClearFreq,
    About,
    SourceLabel,
    SourceCombo,
    SearchLabel,
    SearchEdit,
    List,
    EditWordInput,
    EditCodeInput,
    InlineSave,
    NewWord,
    CancelEdit,
    AutoCode,
    SaveWord,
    Pin,
    Delete,
    Promote,
    Status,
    Toolbar,
    SelectAll,
    LanguageAuto,
    LanguageZh,
    LanguageEn,
    Exit,
    Separator,
};

class DictManagerWindow final {
public:
    DictManagerWindow();
    ~DictManagerWindow() noexcept;
    DictManagerWindow(const DictManagerWindow&) = delete;
    DictManagerWindow& operator=(const DictManagerWindow&) = delete;
    DictManagerWindow(DictManagerWindow&&) = delete;
    DictManagerWindow& operator=(DictManagerWindow&&) = delete;

    bool Create(
        HINSTANCE instance,
        const std::filesystem::path& user_dict_path = {},
        const std::filesystem::path& phrase_freq_path = {});
    HWND Handle() const noexcept { return window_.Get(); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ScopedWindow window_;
};

int RunDictManager(HINSTANCE instance, int show);

} // namespace qiqi::dict_manager
