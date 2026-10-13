// Development-only, explicitly destructive test cycle on a connected development cable.
// Not part of the shipped application. Saves and restores entry settings and backup.
#include "setup.h"
#include <iostream>
#include <algorithm>
using namespace mbe;

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3 || Text(argv[1]) != L"--run-live-device-cycle")
    {
        std::wcerr << L"Use --run-live-device-cycle followed by the exact instance ID.\n";
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Log log = [](const Text &text) {
        std::wcout << text << L"\n";
    };
    auto require = [](bool condition, const Text &message) {
        if (!condition)
        {
            throw Error(message);
        }
    };
    Text id = argv[2];
    Device entry;
    std::vector<SavedValue> entryValues;
    fs::path path;
    std::string entryBackup;
    bool prepared = false, hadBackup = false;
    int result = 1;
    try
    {
        entry = find(id);
        entryValues = registrations(id);
        path = backupPath(id);
        hadBackup = fs::exists(path);
        if (hadBackup)
        {
            entryBackup = readFile(path);
        }
        // Check the baseline can be restored before changing anything.
        if (!entry.inf.empty())
        {
            mbe::bind(id, entry.inf, entry.section, true, log);
        }
        prepared = true;
        if (hadBackup && !DeleteFileW(path.c_str()))
        {
            fail(L"Temporarily remove active test baseline");
        }
        mbe::bind(id, L"", L"", false, log);
        const Text unrelated = L"{11111111-2222-3333-4444-555555555555}";
        std::vector<SavedValue> baseline = {
            {L"DeviceInterfaceGUIDs", true, REG_MULTI_SZ, {unrelated}},
            {L"DeviceInterfaceGUID", true, REG_SZ, {L"{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}"}}};
        restoreRegistrations(id, baseline);
        require(find(id).inf.empty(), L"Initial null driver was not applied.");
        log(install(id, log));
        require(find(id).ready(), L"Fresh native install was not ready.");
        auto saved = loadSnapshot(id);
        require(saved.inf.empty() && equalValues(saved.values, baseline),
                L"Snapshot lost the previous null driver or registrations.");
        auto first = readFile(path);
        log(install(id, log));
        require(readFile(path) == first, L"Repeated install replaced the baseline.");
        auto active = registrations(id);
        require(active[0].containsMcl() && std::find(active[0].data.begin(), active[0].data.end(),
                                                     unrelated) != active[0].data.end(),
                L"Install lost an unrelated GUID.");
        require(active[1].data == baseline[1].data, L"Install changed the singular GUID.");
        log(uninstall(id, log));
        require(find(id).inf.empty() && !find(id).interfaceActive,
                L"Uninstall did not restore the null driver or remove the interface.");
        require(equalValues(registrations(id), baseline),
                L"Uninstall failed to restore both registry value types exactly.");
        require(!fs::exists(path), L"Successful uninstall did not archive its backup.");
        log(uninstall(id, log));
        require(equalValues(registrations(id), baseline), L"Repeated uninstall changed the device.");
        // A manually configured cable must keep its Microsoft binding on uninstall.
        mbe::bind(id, L"winusb.inf", L"WINUSB", false, log);
        addRegistration(id);
        restart(id);
        Sleep(500);
        require(find(id).ready(), L"Manual test setup was not ready.");
        log(uninstall(id, log));
        auto manual = find(id);
        require(equal(manual.inf, L"winusb.inf") && !manual.interfaceActive,
                L"Manual uninstall replaced the shared WinUSB binding.");
        require(equalValues(registrations(id), baseline), L"Manual uninstall lost unrelated registrations.");
        log(L"PASS: native fresh install, repeated-install baseline, GUID preservation, exact uninstall, "
            L"repeated uninstall, manual-registration removal.");
        result = 0;
    }
    catch (const Error &e)
    {
        std::wcerr << L"FAIL: " << e.text << L"\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
    }
    if (prepared)
    {
        try
        {
            mbe::bind(id, entry.inf, entry.section, false, log);
            restoreRegistrations(id, entryValues);
            if (equal(entry.service, L"WINUSB"))
            {
                restart(id);
            }
            if (hadBackup)
            {
                writeFile(path, entryBackup);
            }
            else if (fs::exists(path) && !DeleteFileW(path.c_str()))
            {
                fail(L"Remove temporary test backup");
            }
            Sleep(500);
            auto restored = find(id);
            require(equal(restored.inf, entry.inf) && equal(restored.section, entry.section) &&
                        equalValues(registrations(id), entryValues),
                    L"Entry configuration restoration failed.");
            require(restored.ready() == entry.ready(), L"Entry ready state restoration failed.");
            log(L"PASS: restored entry driver, registrations, ready state and original active backup.");
        }
        catch (const Error &e)
        {
            std::wcerr << L"RESTORATION FAILED: " << e.text << L"\n";
            result = 1;
        }
        catch (const std::exception &e)
        {
            std::cerr << "RESTORATION FAILED: " << e.what() << "\n";
            result = 1;
        }
    }
    CoUninitialize();
    return result;
}
