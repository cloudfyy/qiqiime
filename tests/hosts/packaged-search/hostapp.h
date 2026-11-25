// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "diagnostics.h"
#include "fixtures.h"
#include "tokeninfo.h"
#include "brokerprobe.h"

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Search.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.h>

#include <memory>
#include <string>

namespace qiqi::host {

struct PackageState {
    std::wstring full_name;
    LONG package_error = ERROR_SUCCESS;
    token::State token;
};

class HostApp : public winrt::Windows::UI::Xaml::ApplicationT<HostApp> {
public:
    explicit HostApp(std::shared_ptr<Diagnostics> diagnostics);

    void OnLaunched(
        winrt::Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const& args);
    void OnSearchActivated(
        winrt::Windows::ApplicationModel::Activation::SearchActivatedEventArgs const& args);

private:
    void ShowWindow(
        std::wstring_view kind,
        winrt::Windows::ApplicationModel::Activation::ApplicationExecutionState previous_state);
    void BuildWindow();
    void ConnectSearchPane() noexcept;
    winrt::fire_and_forget LoadFixtureSetAsync();
    void RunFixtures() noexcept;
    void OpenSearchPane() noexcept;
    void ProbeConfigBroker() noexcept;
    void OnInputChanged() noexcept;
    void OnQueryChanged(
        winrt::Windows::ApplicationModel::Search::SearchPaneQueryChangedEventArgs const& args) noexcept;
    void OnSuggestionsRequested(
        winrt::Windows::ApplicationModel::Search::SearchPaneSuggestionsRequestedEventArgs const& args) noexcept;
    void OnQuerySubmitted(
        winrt::Windows::ApplicationModel::Search::SearchPaneQuerySubmittedEventArgs const& args) noexcept;
    void ObserveQuery(
        std::wstring_view stage,
        winrt::hstring const& query,
        winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> const& alternatives,
        uint32_t composition_start,
        uint32_t composition_length,
        bool has_linguistic_details) noexcept;
    void RefreshDiagnosticStatus() noexcept;

    static PackageState ReadPackageState() noexcept;
    static std::wstring FormatPackageState(const PackageState& state);

    std::shared_ptr<Diagnostics> diagnostics_;
    FixtureSet fixture_set_;
    Observation latest_observation_;
    winrt::Windows::ApplicationModel::Search::SearchPane search_pane_{nullptr};
    winrt::Windows::ApplicationModel::Search::SearchPane::QueryChanged_revoker
        query_changed_;
    winrt::Windows::ApplicationModel::Search::SearchPane::SuggestionsRequested_revoker
        suggestions_requested_;
    winrt::Windows::ApplicationModel::Search::SearchPane::QuerySubmitted_revoker
        query_submitted_;
    winrt::Windows::UI::Xaml::Controls::TextBox input_;
    winrt::Windows::UI::Xaml::Controls::Button open_search_;
    winrt::Windows::UI::Xaml::Controls::Button run_fixtures_;
    winrt::Windows::UI::Xaml::Controls::Button probe_broker_;
    winrt::Windows::UI::Xaml::Controls::TextBlock fixture_status_;
    winrt::Windows::UI::Xaml::Controls::TextBlock observation_status_;
    winrt::Windows::UI::Xaml::Controls::TextBlock result_text_;
    winrt::Windows::UI::Xaml::Controls::TextBlock broker_status_;
    winrt::Windows::UI::Xaml::Controls::TextBlock diagnostic_status_;
    bool window_ready_ = false;
    bool fixture_set_ready_ = false;
    bool ran_fixtures_ = false;
};

} // namespace qiqi::host
