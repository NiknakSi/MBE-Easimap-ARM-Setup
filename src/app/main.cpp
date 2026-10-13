#include "setup.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>
#include <mutex>
#include <memory>
#include <algorithm>

namespace
{
    using namespace mbe;

    constexpr UINT ProgressMessage = WM_APP + 1, CompleteMessage = WM_APP + 2;
    constexpr int CableId = 101, InstallId = 102, UninstallId = 103, RefreshId = 104;
    fs::path logPath;
    std::mutex logMutex;

    void writeLog(const Text &text)
    {
        std::lock_guard<std::mutex> guard(logMutex);
        fs::create_directories(logPath.parent_path());
        HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            fail(textResource(IDS_OPEN_DIAGNOSTIC_WRITELOG));
        }
        auto line = utf8(timestamp() + L" " + text + L"\r\n");
        DWORD written = 0;
        BOOL ok = WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        DWORD error = GetLastError();
        CloseHandle(file);
        if (!ok || written != line.size())
        {
            fail(textResource(IDS_WRITE_DIAGNOSTIC_WRITELOG), error);
        }
    }

    Text exceptionText()
    {
        try
        {
            throw;
        }
        catch (const Error &e)
        {
            return e.text;
        }
        catch (const fs::filesystem_error &e)
        {
            return formatResource(IDS_FILE_OPERATION_FAILED, {wide(e.what())});
        }
        catch (const std::exception &e)
        {
            return wide(e.what());
        }
        catch (...)
        {
            return textResource(IDS_UNEXPECTED_ERROR);
        }
    }

    void safeLog(const Text &text) noexcept
    {
        try
        {
            writeLog(text);
        }
        catch (...)
        {
        }
    }

    void report(const Text &path, const Text &text)
    {
        writeLog(text);
        if (!path.empty())
        {
            writeFile(path, utf8(text + formatResource(IDS_LOG_PATH, {logPath.wstring()})));
        }
    }

    /// <summary>Worker result whose ownership passes to the window on successful PostMessage.</summary>
    struct Completion
    {
        bool success;
        Text message;
    };

    /// <summary>Native controls and layout; mutable state is owned by the Windows UI thread.</summary>
    struct Window
    {
        HWND hwnd = nullptr, heading = nullptr, intro = nullptr, cables = nullptr, status = nullptr,
             description = nullptr, installButton = nullptr, uninstallButton = nullptr,
             refreshButton = nullptr, details = nullptr, footer = nullptr;
        HFONT normal = nullptr, bold = nullptr, large = nullptr, footerFont = nullptr;
        HICON bigIcon = nullptr, smallIcon = nullptr;
        UINT dpi = 96;
        bool busy = false;
        std::vector<Device> list;

        ~Window()
        {
            for (auto font : {normal, bold, large, footerFont})
            {
                if (font)
                {
                    DeleteObject(font);
                }
            }
            if (bigIcon)
            {
                DestroyIcon(bigIcon);
            }
            if (smallIcon)
            {
                DestroyIcon(smallIcon);
            }
        }

        int px(int value) const
        {
            return MulDiv(value, static_cast<int>(dpi), 96);
        }

        HWND control(const wchar_t *type, const Text &text, DWORD style, int id = 0, DWORD ex = 0)
        {
            HWND child = CreateWindowExW(ex, type, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                                         hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         GetModuleHandleW(nullptr), nullptr);
            if (!child)
            {
                fail(textResource(IDS_CREATE_SETUP_CONTROL));
            }
            return child;
        }

        /// <summary>Recreates fonts and icon sizes for the current monitor DPI.</summary>
        void fonts()
        {
            auto make = [&](int points, int weight) {
                HFONT f = CreateFontW(-MulDiv(points, static_cast<int>(dpi), 72), 0, 0, 0, weight, FALSE,
                                      FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                if (!f)
                {
                    fail(textResource(IDS_CREATE_DISPLAY_FONT));
                }
                return f;
            };
            auto n = make(10, FW_NORMAL), b = make(10, FW_BOLD), l = make(17, FW_BOLD),
                 s = make(9, FW_NORMAL);
            for (auto c :
                 {intro, cables, description, installButton, uninstallButton, refreshButton, details})
            {
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(n), TRUE);
            }
            SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(l), TRUE);
            SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(b), TRUE);
            SendMessageW(footer, WM_SETFONT, reinterpret_cast<WPARAM>(s), TRUE);
            for (auto old : {normal, bold, large, footerFont})
            {
                if (old)
                {
                    DeleteObject(old);
                }
            }
            normal = n;
            bold = b;
            large = l;
            footerFont = s;
            auto load = [&](int size) {
                return static_cast<HICON>(
                    LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, 0));
            };
            if (bigIcon)
            {
                DestroyIcon(bigIcon);
            }
            if (smallIcon)
            {
                DestroyIcon(smallIcon);
            }
            bigIcon = load(GetSystemMetricsForDpi(SM_CXICON, dpi));
            smallIcon = load(GetSystemMetricsForDpi(SM_CXSMICON, dpi));
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        }

        /// <summary>Places controls using logical 96-DPI units, scaled once to the monitor DPI.</summary>
        void layout()
        {
            RECT client{};
            GetClientRect(hwnd, &client);
            int x = px(27), width = client.right - px(54);
            auto place = [&](HWND c, int y, int height) {
                MoveWindow(c, x, px(y), width, px(height), TRUE);
            };
            place(heading, 27, 43);
            place(intro, 74, 49);
            place(cables, 124, 160);
            place(status, 165, 33);
            place(description, 204, 66);
            for (int i = 0; i < 3; ++i)
            {
                MoveWindow(i == 0   ? installButton
                           : i == 1 ? uninstallButton
                                    : refreshButton,
                           x + px(i * 132), px(274), px(125), px(36), TRUE);
            }
            int bottom = client.bottom - px(54);
            MoveWindow(details, x, px(321), width, std::max(px(40), bottom - px(321)), TRUE);
            MoveWindow(footer, x, client.bottom - px(46), width, px(32), TRUE);
            InvalidateRect(hwnd, nullptr, TRUE);
        }

        void create(HWND window)
        {
            hwnd = window;
            dpi = GetDpiForWindow(hwnd);
            heading = control(L"STATIC", textResource(IDS_HEADING), SS_LEFT);
            intro = control(L"STATIC", textResource(IDS_INTRO), SS_LEFT);
            cables = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, CableId);
            status = control(L"STATIC", textResource(IDS_CHECKING_CABLE), SS_LEFT);
            description = control(L"STATIC", L"", SS_LEFT);
            installButton =
                control(L"BUTTON", textResource(IDS_INSTALL), BS_PUSHBUTTON | WS_TABSTOP, InstallId);
            uninstallButton =
                control(L"BUTTON", textResource(IDS_UNINSTALL), BS_PUSHBUTTON | WS_TABSTOP, UninstallId);
            refreshButton =
                control(L"BUTTON", textResource(IDS_REFRESH), BS_PUSHBUTTON | WS_TABSTOP, RefreshId);
            details =
                control(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
                        0, WS_EX_CLIENTEDGE);
            SendMessageW(details, EM_SETLIMITTEXT, 1024 * 1024, 0);
            footer = control(L"STATIC", textResource(IDS_FOOTER), SS_LEFT);
            fonts();
            layout();
            refresh();
            SetTimer(hwnd, 1, 2000, nullptr);
            writeLog(formatResource(IDS_NATIVE_ARM64_WINDOW_DPI, {std::to_wstring(dpi)}));
        }

        const Device *selected() const
        {
            auto i = SendMessageW(cables, CB_GETCURSEL, 0, 0);
            return i >= 0 && static_cast<size_t>(i) < list.size() ? &list[static_cast<size_t>(i)] : nullptr;
        }

        void update()
        {
            const auto *d = selected();
            EnableWindow(installButton, !busy && d && !d->ready());
            EnableWindow(uninstallButton, !busy && d && d->canUninstall());
            if (busy)
            {
                return;
            }
            Text title, body;
            if (!d)
            {
                title = list.empty() ? textResource(IDS_NO_CABLE) : textResource(IDS_SELECT_CABLE);
                body = list.empty() ? textResource(IDS_NO_CABLE_DESCRIPTION)
                                    : textResource(IDS_SELECT_CABLE_DESCRIPTION);
            }
            else
            {
                title = d->ready()       ? textResource(IDS_READY)
                        : d->hasBackup() ? textResource(IDS_SETUP_NEEDS_ATTENTION)
                                         : textResource(IDS_SETUP_REQUIRED);
                body = d->ready() ? textResource(IDS_READY_DESCRIPTION)
                                  : textResource(IDS_SETUP_REQUIRED_DESCRIPTION);
                if (d->canUninstall())
                {
                    body += d->hasBackup() ? textResource(IDS_UNINSTALL_RESTORE_DESCRIPTION)
                                           : textResource(IDS_UNINSTALL_KEEP_DRIVER_DESCRIPTION);
                }
            }
            SetWindowTextW(status, title.c_str());
            SetWindowTextW(description, body.c_str());
        }

        /// <summary>Refreshes cable observations without resetting an unchanged dropdown selection.</summary>
        void refresh()
        {
            if (busy)
            {
                return;
            }
            try
            {
                Text previous;
                if (auto *d = selected())
                {
                    previous = d->instance;
                }
                auto next = devices();
                // Avoid closing a dropdown or resetting keyboard selection on every timer tick.
                bool unchanged = next.size() == list.size();
                if (unchanged)
                {
                    for (size_t i = 0; i < next.size(); ++i)
                    {
                        if (next[i].instance != list[i].instance)
                        {
                            unchanged = false;
                        }
                    }
                }
                list = std::move(next);
                if (!unchanged)
                {
                    SendMessageW(cables, CB_RESETCONTENT, 0, 0);
                    int chosen = -1;
                    for (size_t i = 0; i < list.size(); ++i)
                    {
                        auto label = formatResource(IDS_MBE_USB_CAN_INTERFACE, {list[i].serial()});
                        SendMessageW(cables, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                        if (equal(previous, list[i].instance))
                        {
                            chosen = static_cast<int>(i);
                        }
                    }
                    if (chosen < 0 && list.size() == 1)
                    {
                        chosen = 0;
                    }
                    SendMessageW(cables, CB_SETCURSEL, chosen, 0);
                }
                update();
            }
            catch (...)
            {
                SetWindowTextW(status, textResource(IDS_CABLE_CHECK_FAILED).c_str());
                auto error = exceptionText();
                SetWindowTextW(description, error.c_str());
                EnableWindow(installButton, FALSE);
                EnableWindow(uninstallButton, FALSE);
            }
        }

        void append(const Text &text)
        {
            auto line = text + L"\r\n";
            auto len = GetWindowTextLengthW(details);
            SendMessageW(details, EM_SETSEL, len, len);
            SendMessageW(details, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
        }

        /// <summary>Runs setup on a worker and posts progress/results back to the window.</summary>
        /// <remarks>The busy flag keeps the window alive until the completion message is handled.</remarks>
        void operation(bool remove)
        {
            const auto *d = selected();
            if (!d || busy)
            {
                return;
            }
            if (remove && MessageBoxW(hwnd,
                                      (d->hasBackup() ? textResource(IDS_CONFIRM_RESTORE)
                                                      : textResource(IDS_CONFIRM_REMOVE_REGISTRATION))
                                          .c_str(),
                                      textResource(IDS_UNINSTALL_TITLE).c_str(),
                                      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            {
                return;
            }
            Text instance = d->instance;
            busy = true;
            for (auto c : {installButton, uninstallButton, refreshButton, cables})
            {
                EnableWindow(c, FALSE);
            }
            SetWindowTextW(status,
                           (remove ? textResource(IDS_UNINSTALLING) : textResource(IDS_INSTALLING)).c_str());
            SetWindowTextW(description, textResource(IDS_KEEP_CONNECTED).c_str());
            try
            {
                std::thread([window = hwnd, instance, remove] {
                    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    // Each posted allocation is owned by the sender until posting succeeds, then by the UI.
                    auto progress = [window](const Text &text) {
                        writeLog(text);
                        auto value = std::make_unique<Text>(text);
                        if (PostMessageW(window, ProgressMessage, 0, reinterpret_cast<LPARAM>(value.get())))
                        {
                            value.release();
                        }
                    };
                    auto result = std::make_unique<Completion>();
                    try
                    {
                        result->message =
                            remove ? mbe::uninstall(instance, progress) : mbe::install(instance, progress);
                        result->success = true;
                        progress(result->message);
                    }
                    catch (...)
                    {
                        result->success = false;
                        result->message = exceptionText();
                        safeLog(result->message);
                    }
                    CoUninitialize();
                    if (PostMessageW(window, CompleteMessage, 0, reinterpret_cast<LPARAM>(result.get())))
                    {
                        result.release();
                    }
                }).detach();
            }
            catch (...)
            {
                busy = false;
                EnableWindow(cables, TRUE);
                EnableWindow(refreshButton, TRUE);
                refresh();
                throw;
            }
        }

        LRESULT message(UINT msg, WPARAM w, LPARAM l)
        {
            switch (msg)
            {
            case WM_SIZE:
                if (heading)
                {
                    layout();
                }
                return 0;
            case WM_GETMINMAXINFO: {
                auto info = reinterpret_cast<MINMAXINFO *>(l);
                RECT r{0, 0, px(640), px(490)};
                AdjustWindowRectExForDpi(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, FALSE,
                                         0, dpi);
                info->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
                return 0;
            }
            case WM_DPICHANGED: {
                dpi = HIWORD(w);
                fonts();
                auto r = reinterpret_cast<RECT *>(l);
                SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                layout();
                safeLog(formatResource(IDS_WINDOW_DPI_CHANGED_TO, {std::to_wstring(dpi)}));
                return 0;
            }
            case WM_TIMER:
                refresh();
                return 0;
            case WM_COMMAND:
                if (LOWORD(w) == CableId && HIWORD(w) == CBN_SELCHANGE)
                {
                    update();
                }
                else if (HIWORD(w) == BN_CLICKED)
                {
                    if (LOWORD(w) == InstallId)
                    {
                        operation(false);
                    }
                    else if (LOWORD(w) == UninstallId)
                    {
                        operation(true);
                    }
                    else if (LOWORD(w) == RefreshId)
                    {
                        refresh();
                    }
                }
                return 0;
            case ProgressMessage: {
                std::unique_ptr<Text> value(reinterpret_cast<Text *>(l));
                append(*value);
                return 0;
            }
            case CompleteMessage: {
                std::unique_ptr<Completion> result(reinterpret_cast<Completion *>(l));
                if (!result->success)
                {
                    append(formatResource(IDS_OPERATION_DID_NOT_COMPLETE, {result->message}));
                    result->message += formatResource(IDS_DETAILS_WERE_SAVED_TO, {logPath.wstring()});
                }
                MessageBoxW(hwnd, result->message.c_str(), textResource(IDS_APP_TITLE).c_str(),
                            MB_OK | (result->success ? MB_ICONINFORMATION : MB_ICONERROR));
                busy = false;
                EnableWindow(refreshButton, TRUE);
                EnableWindow(cables, TRUE);
                refresh();
                return 0;
            }
            case WM_CTLCOLORSTATIC: {
                auto dc = reinterpret_cast<HDC>(w);
                SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
                SetTextColor(dc, reinterpret_cast<HWND>(l) == footer ? GetSysColor(COLOR_GRAYTEXT)
                                                                     : GetSysColor(COLOR_BTNTEXT));
                if (reinterpret_cast<HWND>(l) == details)
                {
                    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
                    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
                }
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            case WM_CLOSE:
                if (busy)
                {
                    MessageBoxW(hwnd, textResource(IDS_WAIT_FOR_COMPLETION).c_str(),
                                textResource(IDS_APP_TITLE).c_str(), MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                KillTimer(hwnd, 1);
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, w, l);
        }
    };

    // Exceptions must not escape a native Windows callback into the OS message dispatcher.
    LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
    {
        auto *window = reinterpret_cast<Window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE)
        {
            window = static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            window->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }
        if (!window)
        {
            return DefWindowProcW(hwnd, msg, w, l);
        }
        try
        {
            if (msg == WM_CREATE)
            {
                window->create(hwnd);
                return 0;
            }
            return window->message(msg, w, l);
        }
        catch (...)
        {
            auto error = exceptionText();
            safeLog(error);
            MessageBoxW(hwnd, error.c_str(), textResource(IDS_APP_TITLE).c_str(), MB_OK | MB_ICONERROR);
            return msg == WM_CREATE ? -1 : 0;
        }
    }

    int gui(HINSTANCE instance)
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
        if (!InitCommonControlsEx(&controls))
        {
            fail(textResource(IDS_INITIALIZE_WINDOWS_CONTROLS));
        }
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = windowProc;
        wc.lpszClassName = L"MbeEasimapArmNative";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        if (!RegisterClassExW(&wc))
        {
            fail(textResource(IDS_REGISTER_SETUP_WINDOW));
        }
        Window window;
        window.dpi = GetDpiForSystem();
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
        RECT r{0, 0, window.px(640), window.px(490)};
        AdjustWindowRectExForDpi(&r, style, FALSE, 0, window.dpi);
        int width = r.right - r.left, height = r.bottom - r.top;
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, textResource(IDS_APP_TITLE).c_str(), style,
                                    work.left + (work.right - work.left - width) / 2,
                                    work.top + (work.bottom - work.top - height) / 2, width, height, nullptr,
                                    nullptr, instance, &window);
        if (!hwnd)
        {
            fail(textResource(IDS_CREATE_SETUP_WINDOW));
        }
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        MSG msg{};
        BOOL rc;
        while ((rc = GetMessageW(&msg, nullptr, 0, 0)) > 0)
        {
            if (!IsDialogMessageW(hwnd, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (rc < 0)
        {
            fail(textResource(IDS_READ_WINDOW_MESSAGES));
        }
        return static_cast<int>(msg.wParam);
    }
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, wchar_t *, int)
{
    using namespace mbe;
    int count = 0;
    wchar_t **args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args)
    {
        MessageBoxW(nullptr, textResource(IDS_COULD_NOT_READ_APPLICATION_ARGUMENTS).c_str(),
                    textResource(IDS_APP_TITLE).c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }

    struct ArgsCleanup
    {
        wchar_t **value;

        ~ArgsCleanup()
        {
            LocalFree(value);
        }
    } cleanArgs{args};

    auto option = [&](const Text &name) {
        for (int i = 2; i < count; ++i)
        {
            if (args[i] == name)
            {
                if (i + 1 == count)
                {
                    throw Error(formatResource(IDS_MISSING_VALUE_FOR, {name}));
                }
                return Text(args[i + 1]);
            }
        }
        return Text();
    };
    Text resultPath;
    HANDLE mutex = nullptr;
    bool acquired = false;
    int result = 1;
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try
    {
        logPath = dataDirectory() / L"Logs" /
                  (L"native-" + timestamp(true) + L"-" + std::to_wstring(GetCurrentProcessId()) + L".log");
        resultPath = option(L"--result");
        mutex = CreateMutexW(nullptr, TRUE, L"Global\\MBE-Easimap-ARM-Setup");
        auto mutexError = GetLastError();
        if (!mutex)
        {
            fail(textResource(IDS_OPEN_SETUP_INSTANCE_LOCK), mutexError);
        }
        if (mutexError == ERROR_ALREADY_EXISTS)
        {
            throw Error(textResource(IDS_ALREADY_RUNNING));
        }
        acquired = true;
        if (!IsUserAnAdmin())
        {
            throw Error(textResource(IDS_ADMIN_REQUIRED));
        }
        if (FAILED(com))
        {
            fail(textResource(IDS_INITIALIZE_WINDOWS_SERVICES), static_cast<DWORD>(com));
        }
        writeLog(textResource(IDS_STARTUP_LOG));
        if (count == 1)
        {
            result = gui(instance);
        }
        else
        {
            Text command = args[1];
            if (command == L"--self-test")
            {
                selfTest(writeLog);
                report(resultPath, textResource(IDS_PASS_NATIVE_BACKUP_VALIDATION_SELF_TESTS));
                result = 0;
            }
            else
            {
                if (command != L"--status" && command != L"--install" && command != L"--uninstall" &&
                    command != L"--probe-drivers" && command != L"--validate-backup")
                {
                    throw Error(textResource(IDS_UNKNOWN_COMMAND));
                }
                auto list = devices();
                auto requested = option(L"--device");
                if (!requested.empty())
                {
                    list.erase(std::remove_if(list.begin(), list.end(),
                                              [&](const Device &d) {
                                                  return !equal(d.instance, requested);
                                              }),
                               list.end());
                }
                if (command == L"--status")
                {
                    Text status =
                        list.empty() ? textResource(IDS_NO_MATCHING_CABLE_CONNECTED_INSTALL_DISABLED) : L"";
                    for (const auto &d : list)
                    {
                        if (!status.empty())
                        {
                            status += L"\r\n";
                        }
                        status += describe(d);
                    }
                    report(resultPath, status);
                    result = 0;
                }
                else
                {
                    if (list.size() != 1)
                    {
                        throw Error(list.empty()
                                        ? textResource(IDS_NO_MATCHING_CABLE_CONNECTED_NO_CHANGES_MADE)
                                        : textResource(IDS_SELECT_ONE_CABLE_WITH_DEVICE));
                    }
                    auto id = list[0].instance;
                    Text message;
                    if (command == L"--probe-drivers")
                    {
                        mbe::bind(id, L"winusb.inf", L"WINUSB", true, writeLog);
                        message = textResource(IDS_BUILT_IN_WINUSB_DRIVER_SELECTION_VERIFIED_WITHOUT_CHANGES);
                    }
                    else if (command == L"--validate-backup")
                    {
                        auto saved = loadSnapshot(id);
                        message = formatResource(IDS_SAVED_CONFIGURATION_VALIDATED_FOR,
                                                 {saved.instance, saved.inf, saved.service});
                    }
                    else
                    {
                        message = command == L"--install" ? install(id, writeLog) : uninstall(id, writeLog);
                    }
                    report(resultPath, message + L"\r\n" + describe(find(id)));
                    result = 0;
                }
            }
        }
    }
    catch (...)
    {
        auto error = exceptionText();
        safeLog(error);
        if (count == 1)
        {
            MessageBoxW(nullptr, error.c_str(), textResource(IDS_APP_TITLE).c_str(), MB_OK | MB_ICONERROR);
        }
        else
        {
            try
            {
                report(resultPath, formatResource(IDS_ERROR_REPORT, {error}));
            }
            catch (...)
            {
            }
        }
    }
    if (acquired)
    {
        ReleaseMutex(mutex);
    }
    if (mutex)
    {
        CloseHandle(mutex);
    }
    if (SUCCEEDED(com))
    {
        CoUninitialize();
    }
    return result;
}
