#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <filesystem>
#include <utility>
#include "strings.h"

/// <summary>Device configuration and reversible setup for the supported MBE USB-CAN cable.</summary>
namespace mbe
{
    using Text = std::wstring;

    /// <summary>Receives progress on the calling thread; GUI callers must marshal updates to their
    /// window.</summary>
    using Log = std::function<void(const Text &)>;
    namespace fs = std::filesystem;
    inline constexpr wchar_t HardwareId[] = L"USB\\VID_FEFE&PID_0001";
    inline constexpr wchar_t InterfaceGuid[] = L"{00E554CA-5C6C-441C-886F-CA3021EA8648}";

    /// <summary>An operation failure carrying a Unicode message suitable for display to the user.</summary>
    /// <remarks>Use text for the full diagnostic; what() is a fixed narrow-string fallback.</remarks>
    struct Error : std::exception
    {
        Text text;

        explicit Error(Text value) : text(std::move(value)) {}

        const char *what() const noexcept override
        {
            return "Windows setup operation failed";
        }
    };

    /// <summary>Throws Error with a Windows error code and its system description.</summary>
    /// <remarks>Capture GetLastError immediately if cleanup or other API calls will run first.</remarks>
    [[noreturn]] void fail(const Text &action, DWORD code = GetLastError());

    /// <summary>Compares identifiers using case-insensitive ordinal Windows comparison.</summary>
    bool equal(const Text &a, const Text &b);

    /// <summary>Encodes UTF-16 text as UTF-8, rejecting invalid Unicode.</summary>
    std::string utf8(const Text &value);

    /// <summary>Decodes UTF-8 text as UTF-16, rejecting invalid Unicode.</summary>
    Text wide(const std::string &value);

    /// <summary>Returns UTC time, optionally in a filename-safe format.</summary>
    Text timestamp(bool filename = false);

    /// <summary>Locates the shared setup data under Windows ProgramData.</summary>
    fs::path dataDirectory();

    /// <summary>Returns the device-specific version 1 XML backup path.</summary>
    /// <remarks>Hashes the uppercase UTF-8 instance ID so case variants resolve to the same backup.</remarks>
    fs::path backupPath(const Text &instance);

    /// <summary>Writes and flushes a complete file, replacing existing contents.</summary>
    void writeFile(const fs::path &path, const std::string &contents);

    /// <summary>Reads a non-empty file up to the backup size limit of one MiB.</summary>
    std::string readFile(const fs::path &path);

    /// <summary>A saved singular or multi-string interface registration, including whether it existed.</summary>
    /// <remarks>An absent value differs from an existing empty value and must be restored differently.</remarks>
    struct SavedValue
    {
        Text name;
        bool exists = false;
        DWORD kind = REG_NONE;
        std::vector<Text> data;

        /// <summary>Reports whether the saved data contains the Easimap interface GUID.</summary>
        bool containsMcl() const;
    };

    /// <summary>The original driver selection and registrations used to undo an installation.</summary>
    struct Snapshot
    {
        Text instance, inf, section, service, created;
        std::vector<SavedValue> values;
    };

    /// <summary>A point-in-time observation of a connected, supported USB cable.</summary>
    /// <remarks>Device presence can change; mutating operations resolve the instance again before
    /// changes.</remarks>
    struct Device
    {
        Text instance, service, inf, section;
        ULONG status = 0, problem = 0;
        bool interfaceActive = false;

        /// <summary>Requires a started, healthy WinUSB device and an active Easimap interface.</summary>
        bool ready() const;

        /// <summary>Checks for an active uninstall backup on disk.</summary>
        bool hasBackup() const;

        /// <summary>Reads whether either registration value contains the Easimap GUID.</summary>
        bool hasRegistration() const;

        /// <summary>Allows restoration from a backup or removal of a manually added registration.</summary>
        bool canUninstall() const;

        /// <summary>Returns the final component of the Windows device instance ID for display.</summary>
        Text serial() const;
    };

    /// <summary>Enumerates present USB devices matching the exact supported hardware ID.</summary>
    std::vector<Device> devices();

    /// <summary>Returns a fresh observation of a matching connected cable, or throws if it disappeared.</summary>
    Device find(const Text &instance);

    /// <summary>Checks for an active MCL device interface belonging to this exact cable instance.</summary>
    bool interfaceActive(const Text &instance);

    /// <summary>Selects an existing Windows INF's exact driver section for a connected supported cable.</summary>
    /// <param name="inf">An INF filename in Windows INF, or empty to restore an unconfigured device.</param>
    /// <param name="section">The exact driver section recorded in the backup or expected for WinUSB.</param>
    /// <param name="inspectOnly">When true, checks availability without applying the binding.</param>
    /// <returns>Whether an applied driver change requires a Windows restart.</returns>
    /// <remarks>Never installs a custom package or removes a shared Windows driver package.</remarks>
    bool bind(const Text &instance, const Text &inf, const Text &section, bool inspectOnly, const Log &log);

    /// <summary>Disables then re-enables the cable so Windows re-reads its interface registrations.</summary>
    /// <remarks>After successful disable, re-enable is always attempted. Failure asks for reconnection.</remarks>
    void restart(const Text &instance);

    /// <summary>Reads both plural and singular interface registration values without modifying them.</summary>
    std::vector<SavedValue> registrations(const Text &instance);

    /// <summary>Restores existence, type and data of the two saved interface registration values.</summary>
    /// <remarks>Restore after changing the driver: Windows can clear Device Parameters during binding.</remarks>
    void restoreRegistrations(const Text &instance, const std::vector<SavedValue> &values);

    /// <summary>Adds the MCL GUID to the plural value while preserving unrelated registrations.</summary>
    void addRegistration(const Text &instance);

    /// <summary>Removes only the MCL GUID from both registration values.</summary>
    void removeRegistration(const Text &instance);

    /// <summary>Reads and validates a version 1 backup for this exact cable instance.</summary>
    /// <remarks>Rejects invalid identity, structure and value types; leaves the file available for
    /// recovery.</remarks>
    Snapshot loadSnapshot(const Text &instance);

    /// <summary>Saves the original state before mutation, or loads an existing original baseline.</summary>
    /// <remarks>A temporary file and rename preserve the baseline across repairs and failed attempts.</remarks>
    Snapshot saveSnapshot(const Device &device);

    /// <summary>Compares registration existence, types and ordered data, independently of value order.</summary>
    bool equalValues(const std::vector<SavedValue> &a, const std::vector<SavedValue> &b);

    /// <summary>Configures the cable with inbox WinUSB and MCL registration, then verifies readiness.</summary>
    /// <returns>A user-facing result, including any requested restart or reconnection.</returns>
    /// <remarks>Requires administrator access and presence. Already-ready installation is a no-op.</remarks>
    Text install(const Text &instance, const Log &log);

    /// <summary>Restores the saved configuration, or removes manual MCL registration without a backup.</summary>
    /// <returns>A user-facing restoration result or a request to finish after restarting Windows.</returns>
    /// <remarks>Archives the backup only after verification. Refuses to overwrite an unrelated newer
    /// driver.</remarks>
    Text uninstall(const Text &instance, const Log &log);

    /// <summary>Formats a diagnostic observation; does not imply a vehicle or ECU is connected.</summary>
    Text describe(const Device &device);

    /// <summary>Exercises backup serialization and rejection checks without accessing devices or the
    /// registry.</summary>
    void selfTest(const Log &log);
} // namespace mbe
