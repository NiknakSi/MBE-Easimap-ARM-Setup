#include "setup.h"
#include <iostream>

namespace
{
    void checkResources()
    {
        // Exercise the compiled table, including the complete long/Unicode captions.
        for (UINT id = IDS_FIRST_STRING; id <= IDS_LAST_STRING; ++id)
        {
            if (mbe::textResource(id).empty())
            {
                throw std::runtime_error("Empty string resource");
            }
        }

        SetLastError(ERROR_ACCESS_DENIED);
        const auto footer = mbe::textResource(IDS_FOOTER);
        if (GetLastError() != ERROR_ACCESS_DENIED || footer.find(L'\u00b7') == std::wstring::npos)
        {
            throw std::runtime_error("Resource lookup lost Unicode or the Windows error code");
        }

        const std::wstring serial = L"\u00e9 {0}";
        const auto label = mbe::formatResource(IDS_MBE_USB_CAN_INTERFACE, {serial});
        if (GetLastError() != ERROR_ACCESS_DENIED || label != L"MBE USB-CAN interface  (" + serial + L")")
        {
            throw std::runtime_error("Resource formatting altered an insert or the Windows error code");
        }

        bool rejected = false;
        try
        {
            mbe::formatResource(IDS_MBE_USB_CAN_INTERFACE, {});
        }
        catch (const std::runtime_error &)
        {
            rejected = true;
        }
        if (!rejected)
        {
            throw std::runtime_error("Resource formatter accepted a missing argument");
        }

        // Loading a caption inside fail(...) must retain the error that preceded the call.
        SetLastError(ERROR_ACCESS_DENIED);
        try
        {
            mbe::fail(mbe::textResource(IDS_CREATE_OUTPUT_FILE));
        }
        catch (const mbe::Error &error)
        {
            if (error.text.find(L"(5)") == std::wstring::npos)
            {
                throw std::runtime_error("Error caption lookup changed the reported Windows error");
            }
        }

        std::wcout
            << L"PASS: embedded string table, Unicode, message inserts and Windows error preservation.\n";
    }
} // namespace

/// <summary>Runs the pure backup-format checks; no connected cable or administrator access is
/// needed.</summary>
int wmain()
{
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com))
    {
        std::wcerr << L"Could not initialize Windows services.\n";
        return 1;
    }
    int result = 0;
    try
    {
        checkResources();
        mbe::selfTest([](const mbe::Text &message) {
            std::wcout << message << L'\n';
        });
    }
    catch (const mbe::Error &error)
    {
        std::wcerr << L"FAIL: " << error.text << L'\n';
        result = 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    CoUninitialize();
    return result;
}
