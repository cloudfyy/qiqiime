// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hostapp.h"

#include <appmodel.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>

#include <algorithm>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>

namespace qiqi::host {
namespace {

namespace activation = winrt::Windows::ApplicationModel::Activation;
namespace search = winrt::Windows::ApplicationModel::Search;
namespace xaml = winrt::Windows::UI::Xaml;
namespace controls = winrt::Windows::UI::Xaml::Controls;

constexpr std::wstring_view kPersistentLogName = L"qiqi-uwp-host.log";

struct TokenCloser {
    void operator()(HANDLE token) const noexcept {
        if (token) {
            ::CloseHandle(token);
        }
    }
};

std::wstring_view HStringView(winrt::hstring const& value) noexcept {
    return {value.c_str(), value.size()};
}

template <typename Panel>
void AddControl(
    Panel const& panel,
    xaml::FrameworkElement const& element,
    double margin_top) {
    element.Margin(xaml::Thickness{0, margin_top, 0, 0});
    panel.Children().Append(element);
}

winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring>
EmptyStringVectorView() {
    return winrt::single_threaded_vector<winrt::hstring>().GetView();
}

} // namespace

HostApp::HostApp(std::shared_ptr<Diagnostics> diagnostics)
    : diagnostics_(std::move(diagnostics)) {}

void HostApp::OnLaunched(activation::LaunchActivatedEventArgs const& args) {
    ShowWindow(L"launch", args.PreviousExecutionState());
}

void HostApp::OnSearchActivated(activation::SearchActivatedEventArgs const& args) {
    ShowWindow(L"search-activation", args.PreviousExecutionState());
    input_.Text(args.QueryText());

    const auto with_details =
        args.try_as<activation::ISearchActivatedEventArgsWithLinguisticDetails>();
    if (with_details) {
        const auto details = with_details.LinguisticDetails();
        if (details) {
            ObserveQuery(
                L"search-activation",
                args.QueryText(),
                details.QueryTextAlternatives(),
                details.QueryTextCompositionStart(),
                details.QueryTextCompositionLength(),
                true);
            return;
        }
    }
    ObserveQuery(
        L"search-activation",
        args.QueryText(),
        EmptyStringVectorView(),
        0,
        0,
        false);
}

void HostApp::ShowWindow(
    std::wstring_view kind,
    activation::ApplicationExecutionState previous_state) {
    if (!window_ready_) {
        BuildWindow();
    }
    diagnostics_->Info(
        L"host.activation",
        std::format(
            L"kind={}, previous_state={}",
            kind,
            static_cast<int>(previous_state)));
    xaml::Window::Current().Activate();
}

void HostApp::BuildWindow() {
    controls::ScrollViewer scroller;
    scroller.VerticalScrollBarVisibility(
        controls::ScrollBarVisibility::Auto);

    controls::StackPanel panel;
    panel.Padding(xaml::Thickness{20, 20, 20, 20});
    panel.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);

    controls::TextBlock title;
    title.Text(L"QiQiIME UWP Search Contract Host");
    title.FontSize(28);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    AddControl(panel, title, 0);

    controls::TextBlock package_status;
    const PackageState package_state = ReadPackageState();
    package_status.Text(FormatPackageState(package_state));
    package_status.TextWrapping(xaml::TextWrapping::Wrap);
    AddControl(panel, package_status, 8);
    diagnostics_->Info(
        L"host.package",
        std::format(
            L"package={}, app_container={}, package_error={}, token_error={}",
            package_state.full_name.empty()
                ? L"<unavailable>"
                : package_state.full_name,
            package_state.is_app_container ? L"true" : L"false",
            package_state.package_error,
            package_state.token_error));

    controls::TextBlock description;
    description.Text(
        L"Use the TextBox for ordinary IME composition. The legacy Search "
        L"Contract is exploratory on modern Windows and may be unavailable.");
    description.TextWrapping(xaml::TextWrapping::Wrap);
    AddControl(panel, description, 12);

    input_.Header(winrt::box_value(
        L"IME input (text and candidate contents are never logged)"));
    input_.PlaceholderText(L"Type a code or Chinese text");
    input_.AcceptsReturn(true);
    input_.MinWidth(520);
    input_.MinHeight(96);
    input_.TextWrapping(xaml::TextWrapping::Wrap);
    input_.TextChanged([weak = get_weak()](auto&&, auto&&) {
        if (const auto self = weak.get()) {
            self->OnInputChanged();
        }
    });
    input_.GotFocus([weak = get_weak()](auto&&, auto&&) {
        if (const auto self = weak.get()) {
            self->diagnostics_->Info(L"host.focus", L"state=gained", DiagnosticArea::Input);
        }
    });
    input_.LostFocus([weak = get_weak()](auto&&, auto&&) {
        if (const auto self = weak.get()) {
            self->diagnostics_->Info(L"host.focus", L"state=lost", DiagnosticArea::Input);
        }
    });
    AddControl(panel, input_, 10);

    controls::StackPanel actions;
    actions.Orientation(controls::Orientation::Horizontal);
    open_search_.Content(winrt::box_value(L"Open legacy Search Pane"));
    open_search_.Click([weak = get_weak()](auto&&, auto&&) {
        if (const auto self = weak.get()) {
            self->OpenSearchPane();
        }
    });
    AddControl(actions, open_search_, 0);

    run_fixtures_.Content(winrt::box_value(L"Run synthetic fixtures"));
    run_fixtures_.IsEnabled(false);
    run_fixtures_.Click([weak = get_weak()](auto&&, auto&&) {
        if (const auto self = weak.get()) {
            self->RunFixtures();
        }
    });
    run_fixtures_.Margin(xaml::Thickness{12, 0, 0, 0});
    actions.Children().Append(run_fixtures_);
    AddControl(panel, actions, 8);

    fixture_status_.Text(L"Loading packaged test data...");
    fixture_status_.TextWrapping(xaml::TextWrapping::Wrap);
    AddControl(panel, fixture_status_, 12);

    observation_status_.Text(
        L"Waiting for an IME text observation or Search Contract event.");
    observation_status_.TextWrapping(xaml::TextWrapping::Wrap);
    AddControl(panel, observation_status_, 8);

    result_text_.TextWrapping(xaml::TextWrapping::Wrap);
    result_text_.IsTextSelectionEnabled(true);
    AddControl(panel, result_text_, 4);

    diagnostic_status_.FontSize(12);
    diagnostic_status_.Opacity(0.8);
    diagnostic_status_.TextWrapping(xaml::TextWrapping::Wrap);
    AddControl(panel, diagnostic_status_, 14);
    RefreshDiagnosticStatus();

    scroller.Content(panel);
    xaml::Window::Current().Content(scroller);
    window_ready_ = true;

    ConnectSearchPane();
    LoadFixtureSetAsync();
}

void HostApp::ConnectSearchPane() noexcept {
    try {
        search_pane_ = search::SearchPane::GetForCurrentView();
        query_changed_ = search_pane_.QueryChanged(
            winrt::auto_revoke,
            [weak = get_weak()](
                search::SearchPane const&,
                search::SearchPaneQueryChangedEventArgs const& args) {
                if (const auto self = weak.get()) {
                    self->OnQueryChanged(args);
                }
            });
        suggestions_requested_ = search_pane_.SuggestionsRequested(
            winrt::auto_revoke,
            [weak = get_weak()](
                search::SearchPane const&,
                search::SearchPaneSuggestionsRequestedEventArgs const& args) {
                if (const auto self = weak.get()) {
                    self->OnSuggestionsRequested(args);
                }
            });
        query_submitted_ = search_pane_.QuerySubmitted(
            winrt::auto_revoke,
            [weak = get_weak()](
                search::SearchPane const&,
                search::SearchPaneQuerySubmittedEventArgs const& args) {
                if (const auto self = weak.get()) {
                    self->OnQuerySubmitted(args);
                }
            });
        open_search_.IsEnabled(true);
        diagnostics_->Info(
            L"search.attach",
            L"legacy SearchPane handlers attached; OS support is still runtime-dependent");
    } catch (const winrt::hresult_error& error) {
        search_pane_ = nullptr;
        open_search_.IsEnabled(false);
        diagnostics_->Warning(
            L"search.attach",
            std::format(
                L"SearchPane unavailable hr=0x{:08X}",
                static_cast<uint32_t>(error.code())));
    }
}

winrt::fire_and_forget HostApp::LoadFixtureSetAsync() {
    const auto weak = get_weak();
    try {
        const auto text = co_await ReadPackagedFixtureTextAsync();
        auto fixture_set = ParseFixtureSet(text);
        if (const auto self = weak.get()) {
            self->fixture_set_ = std::move(fixture_set);
            self->fixture_set_ready_ = true;
            self->run_fixtures_.IsEnabled(true);
            self->fixture_status_.Text(std::format(
                L"Loaded {} records and {} fixtures.",
                self->fixture_set_.records.size(),
                self->fixture_set_.fixtures.size()));
            self->diagnostics_->Info(
                L"fixtures.load",
                std::format(
                    L"records={}, fixtures={}",
                    self->fixture_set_.records.size(),
                    self->fixture_set_.fixtures.size()));
            self->RunFixtures();
        }
    } catch (const winrt::hresult_error& error) {
        if (const auto self = weak.get()) {
            self->fixture_status_.Text(std::format(
                L"Could not load packaged test data (hr=0x{:08X}).",
                static_cast<uint32_t>(error.code())));
            self->diagnostics_->Error(
                L"fixtures.load",
                std::format(
                    L"packaged fixture load failed hr=0x{:08X}",
                    static_cast<uint32_t>(error.code())));
        }
    } catch (const std::bad_alloc&) {
        if (const auto self = weak.get()) {
            self->fixture_status_.Text(
                L"Could not load packaged test data (allocation failure).");
            self->diagnostics_->Error(
                L"fixtures.load",
                L"packaged fixture load allocation failed");
        }
    }
}

void HostApp::RunFixtures() noexcept {
    if (!fixture_set_ready_) {
        fixture_status_.Text(L"Packaged test data is not ready.");
        diagnostics_->Error(L"fixtures.run", L"test data unavailable");
        return;
    }

    size_t passed = 0;
    for (const auto& fixture : fixture_set_.fixtures) {
        try {
            auto alternatives = fixture.alternatives;
            const auto alternative_vector =
                winrt::single_threaded_vector<winrt::hstring>(
                    std::move(alternatives));
            const search::SearchQueryLinguisticDetails details(
                alternative_vector,
                fixture.composition_start,
                fixture.composition_length);
            if (details.QueryTextCompositionStart() != fixture.composition_start ||
                details.QueryTextCompositionLength() != fixture.composition_length) {
                diagnostics_->Error(
                    L"fixtures.run",
                    std::format(
                        L"id={} composition metadata round-trip failed",
                        HStringView(fixture.id)));
                continue;
            }
            const FixtureResult result =
                EvaluateFixture(fixture_set_, fixture);
            const bool ok = result.Passed();
            passed += ok ? 1 : 0;
            const std::wstring detail = std::format(
                L"id={}, expected_matches={}, actual_matches={}, passed={}",
                HStringView(result.id),
                result.expected_matches,
                result.actual_matches,
                ok ? L"true" : L"false");
            if (ok) {
                diagnostics_->Info(
                    L"fixtures.run",
                    detail,
                    DiagnosticArea::Input);
            } else {
                diagnostics_->Error(
                    L"fixtures.run",
                    detail,
                    DiagnosticArea::Input);
            }
        } catch (const winrt::hresult_error& error) {
            diagnostics_->Error(
                L"fixtures.run",
                std::format(
                    L"id={} synthetic details failed hr=0x{:08X}",
                    HStringView(fixture.id),
                    static_cast<uint32_t>(error.code())),
                DiagnosticArea::Input);
        } catch (const std::bad_alloc&) {
            diagnostics_->Error(
                L"fixtures.run",
                std::format(
                    L"id={} fixture allocation failed",
                    HStringView(fixture.id)),
                DiagnosticArea::Input);
        }
    }

    ran_fixtures_ = true;
    try {
        fixture_status_.Text(std::format(
            L"Synthetic fixtures: {} / {} passed.",
            passed,
            fixture_set_.fixtures.size()));
    } catch (const std::bad_alloc&) {
        fixture_status_.Text(L"Synthetic fixtures completed; status allocation failed.");
        diagnostics_->Error(
            L"fixtures.run",
            L"fixture summary allocation failed",
            DiagnosticArea::Input);
    }
    RefreshDiagnosticStatus();
}

void HostApp::OpenSearchPane() noexcept {
    try {
        if (!search_pane_) {
            throw winrt::hresult_error(E_NOINTERFACE);
        }
        if (input_.Text().empty()) {
            search_pane_.Show();
        } else {
            search_pane_.Show(input_.Text());
        }
        diagnostics_->Info(
            L"search.show",
            std::format(
                L"show requested query_length={}",
                input_.Text().size()));
    } catch (const winrt::hresult_error& error) {
        diagnostics_->Error(
            L"search.show",
            std::format(
                L"legacy SearchPane failed hr=0x{:08X}",
                static_cast<uint32_t>(error.code())));
        RefreshDiagnosticStatus();
    } catch (const std::bad_alloc&) {
        diagnostics_->Error(
            L"search.show",
            L"search diagnostic formatting allocation failed");
        RefreshDiagnosticStatus();
    }
}

void HostApp::OnInputChanged() noexcept {
    try {
        diagnostics_->Info(
            L"host.input",
            std::format(L"text_length={}", input_.Text().size()),
            DiagnosticArea::Input);
    } catch (const std::bad_alloc&) {
        diagnostics_->Error(
            L"host.input",
            L"diagnostic formatting allocation failed",
            DiagnosticArea::Input);
    }
}

void HostApp::OnQueryChanged(
    search::SearchPaneQueryChangedEventArgs const& args) noexcept {
    try {
        const auto details = args.LinguisticDetails();
        if (details) {
            ObserveQuery(
                L"search-query-changed",
                args.QueryText(),
                details.QueryTextAlternatives(),
                details.QueryTextCompositionStart(),
                details.QueryTextCompositionLength(),
                true);
        } else {
            ObserveQuery(
                L"search-query-changed",
                args.QueryText(),
                EmptyStringVectorView(),
                0,
                0,
                false);
        }
    } catch (const winrt::hresult_error& error) {
        diagnostics_->Error(
            L"search.query",
            std::format(
                L"QueryChanged failed hr=0x{:08X}",
                static_cast<uint32_t>(error.code())),
            DiagnosticArea::Input);
    } catch (const std::bad_alloc&) {
        diagnostics_->Error(
            L"search.query",
            L"QueryChanged diagnostic allocation failed",
            DiagnosticArea::Input);
    }
}

void HostApp::OnSuggestionsRequested(
    search::SearchPaneSuggestionsRequestedEventArgs const& args) noexcept {
    try {
        const auto request = args.Request();
        if (request.IsCanceled()) {
            diagnostics_->Info(
                L"search.suggestions",
                L"request canceled before suggestions");
            return;
        }
        const auto matches = MatchRecords(
            fixture_set_.records,
            latest_observation_);
        auto collection = request.SearchSuggestionCollection();
        size_t appended = 0;
        for (const auto index : matches) {
            if (appended >= 5) {
                break;
            }
            collection.AppendQuerySuggestion(
                fixture_set_.records[index].title);
            ++appended;
        }
        diagnostics_->Info(
            L"search.suggestions",
            std::format(
                L"matched={}, appended={}",
                matches.size(),
                appended),
            DiagnosticArea::Input);
    } catch (const winrt::hresult_error& error) {
        diagnostics_->Error(
            L"search.suggestions",
            std::format(
                L"SuggestionsRequested failed hr=0x{:08X}",
                static_cast<uint32_t>(error.code())),
            DiagnosticArea::Input);
    } catch (const std::bad_alloc&) {
        diagnostics_->Error(
            L"search.suggestions",
            L"suggestion handling allocation failed",
            DiagnosticArea::Input);
    }
}

void HostApp::OnQuerySubmitted(
    search::SearchPaneQuerySubmittedEventArgs const& args) noexcept {
    try {
        const auto with_details =
            args.try_as<search::ISearchPaneQuerySubmittedEventArgsWithLinguisticDetails>();
        if (with_details) {
            const auto details = with_details.LinguisticDetails();
            if (details) {
                ObserveQuery(
                    L"search-query-submitted",
                    args.QueryText(),
                    details.QueryTextAlternatives(),
                    details.QueryTextCompositionStart(),
                    details.QueryTextCompositionLength(),
                    true);
                return;
            }
        }
        ObserveQuery(
            L"search-query-submitted",
            args.QueryText(),
            EmptyStringVectorView(),
            0,
            0,
            false);
    } catch (const winrt::hresult_error& error) {
        diagnostics_->Error(
            L"search.submit",
            std::format(
                L"QuerySubmitted failed hr=0x{:08X}",
                static_cast<uint32_t>(error.code())),
            DiagnosticArea::Input);
    } catch (const std::bad_alloc&) {
        diagnostics_->Error(
            L"search.submit",
            L"QuerySubmitted diagnostic allocation failed",
            DiagnosticArea::Input);
    }
}

void HostApp::ObserveQuery(
    std::wstring_view stage,
    winrt::hstring const& query,
    winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> const& alternatives,
    uint32_t composition_start,
    uint32_t composition_length,
    bool has_linguistic_details) noexcept {
    try {
        Observation observation{
            .query = query,
            .composition_start = composition_start,
            .composition_length = composition_length,
            .has_linguistic_details = has_linguistic_details,
        };
        observation.alternatives.assign(
            alternatives.begin(),
            alternatives.end());
        latest_observation_ = std::move(observation);

        const bool valid_span =
            composition_start <= query.size() &&
            composition_length <= query.size() - composition_start;
        const auto matches = MatchRecords(
            fixture_set_.records,
            latest_observation_);
        std::wstring displayed;
        for (const auto index : matches) {
            if (!displayed.empty()) {
                displayed += L"\r\n";
            }
            displayed += HStringView(fixture_set_.records[index].id);
            displayed += L": ";
            displayed += HStringView(fixture_set_.records[index].title);
        }
        result_text_.Text(displayed.empty()
            ? L"No packaged fixture record matched."
            : winrt::hstring(displayed));

        const std::wstring status = std::format(
            L"{}: query_length={}, alternatives={}, composition_start={}, "
            L"composition_length={}, details={}, span_valid={}, matches={}",
            stage,
            query.size(),
            latest_observation_.alternatives.size(),
            composition_start,
            composition_length,
            has_linguistic_details ? L"true" : L"false",
            valid_span ? L"true" : L"false",
            matches.size());
        observation_status_.Text(status);
        diagnostics_->Info(
            L"search.observation",
            status,
            DiagnosticArea::Input);
    } catch (const std::bad_alloc&) {
        diagnostics_->Error(
            L"search.observation",
            L"observation allocation failed",
            DiagnosticArea::Input);
    }
}

void HostApp::RefreshDiagnosticStatus() noexcept {
    try {
        diagnostic_status_.Text(std::format(
            L"ETW registration error={}; persistent log={} at package "
            L"LocalState; persistent error=0x{:08X}; fixtures_run={}",
            diagnostics_->RegistrationError(),
            kPersistentLogName,
            static_cast<uint32_t>(diagnostics_->PersistentError()),
            ran_fixtures_ ? L"true" : L"false"));
    } catch (const std::bad_alloc&) {
        diagnostic_status_.Text(
            L"Diagnostic status unavailable (allocation failure).");
    }
}

PackageState HostApp::ReadPackageState() noexcept {
    PackageState state;

    uint32_t length = 0;
    LONG package_error = ::GetCurrentPackageFullName(
        &length,
        nullptr);
    if (package_error == ERROR_INSUFFICIENT_BUFFER && length != 0) {
        try {
            std::vector<wchar_t> name(length + 1, L'\0');
            package_error = ::GetCurrentPackageFullName(
                &length,
                name.data());
            if (package_error == ERROR_SUCCESS) {
                state.full_name.assign(name.data());
            }
        } catch (const std::bad_alloc&) {
            package_error = ERROR_OUTOFMEMORY;
        }
    }
    state.package_error = package_error;

    HANDLE token_raw = nullptr;
    if (!::OpenProcessToken(
            ::GetCurrentProcess(),
            TOKEN_QUERY,
            &token_raw)) {
        state.token_error = ::GetLastError();
        return state;
    }
    const std::unique_ptr<void, TokenCloser> token(token_raw);

    DWORD value = 0;
    DWORD returned = 0;
    if (!::GetTokenInformation(
            token.get(),
            TokenIsAppContainer,
            &value,
            sizeof(value),
            &returned)) {
        state.token_error = ::GetLastError();
        return state;
    }
    state.is_app_container = value != 0;
    return state;
}

std::wstring HostApp::FormatPackageState(const PackageState& state) {
    return std::format(
        L"PackageFullName: {}\r\n"
        L"TokenIsAppContainer: {} (token error={}; package error={})",
        state.full_name.empty() ? L"<unavailable>" : state.full_name,
        state.is_app_container ? L"true" : L"false",
        state.token_error,
        state.package_error);
}

} // namespace qiqi::host
