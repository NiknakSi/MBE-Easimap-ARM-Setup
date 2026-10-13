#include "setup.h"
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <algorithm>
#include <memory>

namespace mbe
{
    namespace
    {
        const GUID mcl = {0x00e554ca, 0x5c6c, 0x441c, {0x88, 0x6f, 0xca, 0x30, 0x21, 0xea, 0x86, 0x48}};

        /// <summary>Owns a SetupAPI device-information set and releases it on every exit path.</summary>
        struct DeviceSet
        {
            HDEVINFO handle;

            explicit DeviceSet(const GUID *guid = nullptr, DWORD flags = DIGCF_PRESENT | DIGCF_ALLCLASSES)
                : handle(SetupDiGetClassDevsW(guid, guid ? nullptr : L"USB", nullptr, flags))
            {
                if (handle == INVALID_HANDLE_VALUE)
                {
                    fail(textResource(IDS_FIND_CONNECTED_DEVICES));
                }
            }

            ~DeviceSet()
            {
                SetupDiDestroyDeviceInfoList(handle);
            }

            DeviceSet(const DeviceSet &) = delete;
        };

        /// <summary>Owns a registry key; copying ownership is deliberately prohibited.</summary>
        struct Key
        {
            HKEY handle = nullptr;
            Key() = default;

            ~Key()
            {
                if (handle)
                {
                    RegCloseKey(handle);
                }
            }

            Key(const Key &) = delete;
        };

        Text enumPath(const Text &instance)
        {
            return L"SYSTEM\\CurrentControlSet\\Enum\\" + instance;
        }

        Text parameterPath(const Text &instance)
        {
            return enumPath(instance) + L"\\Device Parameters";
        }

        void openKey(Key &key, const Text &path, REGSAM access = KEY_READ, bool optional = true)
        {
            auto rc =
                RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, access | KEY_WOW64_64KEY, &key.handle);
            if (optional && rc == ERROR_FILE_NOT_FOUND)
            {
                return;
            }
            if (rc != ERROR_SUCCESS)
            {
                fail(textResource(IDS_READ_CABLE_REGISTRY_SETTINGS), rc);
            }
        }

        Text stringValue(HKEY key, const wchar_t *name)
        {
            if (!key)
            {
                return {};
            }
            DWORD type = 0, size = 0;
            auto rc = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
            if (rc == ERROR_FILE_NOT_FOUND)
            {
                return {};
            }
            if (rc != ERROR_SUCCESS)
            {
                fail(textResource(IDS_READ_REGISTRY_STRING), rc);
            }
            if (type != REG_SZ && type != REG_EXPAND_SZ)
            {
                throw Error(textResource(IDS_CABLE_HAS_AN_UNEXPECTED_DRIVER_SETTING_FORMAT));
            }
            std::vector<wchar_t> value(size / sizeof(wchar_t) + 1, 0);
            rc = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(value.data()), &size);
            if (rc != ERROR_SUCCESS)
            {
                fail(textResource(IDS_READ_REGISTRY_STRING), rc);
            }
            return value.data();
        }

        std::vector<Text> splitMulti(const wchar_t *data, size_t count)
        {
            std::vector<Text> values;
            size_t pos = 0;
            while (pos < count && data[pos])
            {
                size_t end = pos;
                while (end < count && data[end])
                {
                    ++end;
                }
                if (end == count)
                {
                    throw Error(textResource(IDS_INTERFACE_REGISTRATION_IS_NOT_CORRECTLY_TERMINATED));
                }
                values.emplace_back(data + pos, end - pos);
                pos = end + 1;
            }
            return values;
        }

        std::vector<wchar_t> property(HDEVINFO set, SP_DEVINFO_DATA &info, DWORD prop)
        {
            DWORD type = 0, size = 0;
            if (!SetupDiGetDeviceRegistryPropertyW(set, &info, prop, &type, nullptr, 0, &size))
            {
                auto rc = GetLastError();
                if (rc == ERROR_INVALID_DATA || rc == ERROR_NOT_FOUND)
                {
                    return {0, 0};
                }
                if (rc != ERROR_INSUFFICIENT_BUFFER)
                {
                    fail(textResource(IDS_MEASURE_USB_DEVICE_PROPERTY), rc);
                }
            }
            std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 2, 0);
            if (!SetupDiGetDeviceRegistryPropertyW(set, &info, prop, &type,
                                                   reinterpret_cast<BYTE *>(buffer.data()), size, &size))
            {
                fail(textResource(IDS_READ_USB_DEVICE_PROPERTY));
            }
            return buffer;
        }

        Text instanceId(HDEVINFO set, SP_DEVINFO_DATA &info)
        {
            DWORD size = 0;
            SetupDiGetDeviceInstanceIdW(set, &info, nullptr, 0, &size);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                fail(textResource(IDS_MEASURE_CABLE_IDENTIFIER));
            }
            std::vector<wchar_t> value(size, 0);
            if (!SetupDiGetDeviceInstanceIdW(set, &info, value.data(), size, nullptr))
            {
                fail(textResource(IDS_READ_CABLE_IDENTIFIER));
            }
            return value.data();
        }

        bool supported(HDEVINFO set, SP_DEVINFO_DATA &info)
        {
            auto p = property(set, info, SPDRP_HARDWAREID);
            auto ids = splitMulti(p.data(), p.size());
            return std::any_of(ids.begin(), ids.end(), [](const Text &id) {
                return equal(id, HardwareId);
            });
        }

        SP_DEVINFO_DATA selected(DeviceSet &set, const Text &instance)
        {
            for (DWORD i = 0;; ++i)
            {
                SP_DEVINFO_DATA info{};
                info.cbSize = sizeof(info);
                if (!SetupDiEnumDeviceInfo(set.handle, i, &info))
                {
                    if (GetLastError() == ERROR_NO_MORE_ITEMS)
                    {
                        break;
                    }
                    fail(textResource(IDS_READ_CONNECTED_DEVICES));
                }
                if (!equal(instanceId(set.handle, info), instance))
                {
                    continue;
                }
                ULONG status = 0, problem = 0;
                if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) != CR_SUCCESS)
                {
                    throw Error(textResource(IDS_CABLE_IS_NO_LONGER_AVAILABLE));
                }
                if (!supported(set.handle, info))
                {
                    throw Error(textResource(IDS_THIS_DEVICE_IS_NOT_THE_SUPPORTED_MBE_CABLE));
                }
                return info;
            }
            throw Error(textResource(IDS_CABLE_IS_NO_LONGER_CONNECTED_CONNECT_IT_AND));
        }

        Key &createParameters(Key &key, const Text &instance)
        {
            auto rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, parameterPath(instance).c_str(), 0, nullptr, 0,
                                      KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key.handle, nullptr);
            if (rc != ERROR_SUCCESS)
            {
                fail(textResource(IDS_OPEN_CABLE_INTERFACE_SETTINGS), rc);
            }
            return key;
        }

        void setValue(HKEY key, const SavedValue &value)
        {
            LSTATUS rc;
            if (!value.exists)
            {
                rc = RegDeleteValueW(key, value.name.c_str());
                if (rc == ERROR_FILE_NOT_FOUND)
                {
                    return;
                }
            }
            else
            {
                if (value.kind != REG_SZ && value.kind != REG_MULTI_SZ)
                {
                    throw Error(textResource(IDS_INVALID_SAVED_REGISTRATION_FORMAT));
                }
                std::vector<wchar_t> data;
                for (const auto &s : value.data)
                {
                    data.insert(data.end(), s.begin(), s.end());
                    data.push_back(0);
                }
                if (value.kind == REG_MULTI_SZ)
                {
                    data.push_back(0);
                }
                if (data.empty())
                {
                    data.push_back(0);
                }
                rc = RegSetValueExW(key, value.name.c_str(), 0, value.kind,
                                    reinterpret_cast<const BYTE *>(data.data()),
                                    static_cast<DWORD>(data.size() * sizeof(wchar_t)));
            }
            if (rc != ERROR_SUCCESS)
            {
                fail(textResource(IDS_WRITE_CABLE_INTERFACE_SETTINGS), rc);
            }
        }
    } // namespace

    bool SavedValue::containsMcl() const
    {
        return std::any_of(data.begin(), data.end(), [](const Text &s) {
            return equal(s, InterfaceGuid);
        });
    }

    bool Device::ready() const
    {
        return equal(service, L"WINUSB") && problem == 0 && (status & DN_STARTED) != 0 && interfaceActive;
    }

    bool Device::hasBackup() const
    {
        return fs::exists(backupPath(instance));
    }

    bool Device::hasRegistration() const
    {
        auto v = registrations(instance);
        return std::any_of(v.begin(), v.end(), [](const SavedValue &s) {
            return s.containsMcl();
        });
    }

    bool Device::canUninstall() const
    {
        return hasBackup() || hasRegistration();
    }

    Text Device::serial() const
    {
        return instance.substr(instance.find_last_of(L'\\') + 1);
    }

    std::vector<Device> devices()
    {
        DeviceSet set;
        std::vector<Device> result;
        for (DWORD i = 0;; ++i)
        {
            SP_DEVINFO_DATA info{};
            info.cbSize = sizeof(info);
            if (!SetupDiEnumDeviceInfo(set.handle, i, &info))
            {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                {
                    break;
                }
                fail(textResource(IDS_READ_USB_DEVICE_LIST));
            }
            if (!supported(set.handle, info))
            {
                continue;
            }
            Device d;
            d.instance = instanceId(set.handle, info);
            if (CM_Get_DevNode_Status(&d.status, &d.problem, info.DevInst, 0) != CR_SUCCESS)
            {
                continue;
            }
            d.service = property(set.handle, info, SPDRP_SERVICE).data();
            Key key;
            openKey(key, enumPath(d.instance), KEY_READ, false);
            auto driver = stringValue(key.handle, L"Driver");
            if (!driver.empty())
            {
                Key dk;
                openKey(dk, L"SYSTEM\\CurrentControlSet\\Control\\Class\\" + driver);
                d.inf = stringValue(dk.handle, L"InfPath");
                d.section = stringValue(dk.handle, L"InfSection");
            }
            d.interfaceActive = interfaceActive(d.instance);
            result.push_back(std::move(d));
        }
        return result;
    }

    Device find(const Text &instance)
    {
        auto list = devices();
        for (const auto &d : list)
        {
            if (equal(d.instance, instance))
            {
                return d;
            }
        }
        throw Error(textResource(IDS_NO_MATCHING_CABLE_IS_CONNECTED_CONNECT_IT_TO));
    }

    bool interfaceActive(const Text &instance)
    {
        DeviceSet set(&mcl, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        for (DWORD i = 0;; ++i)
        {
            SP_DEVICE_INTERFACE_DATA iface{};
            iface.cbSize = sizeof(iface);
            if (!SetupDiEnumDeviceInterfaces(set.handle, nullptr, &mcl, i, &iface))
            {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                {
                    return false;
                }
                fail(textResource(IDS_READ_EASIMAP_INTERFACE_LIST));
            }
            DWORD size = 0;
            SP_DEVINFO_DATA info{};
            info.cbSize = sizeof(info);
            SetupDiGetDeviceInterfaceDetailW(set.handle, &iface, nullptr, 0, &size, &info);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
                size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
            {
                fail(textResource(IDS_MEASURE_EASIMAP_INTERFACE_PATH));
            }
            std::vector<BYTE> buffer(size, 0);
            auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(set.handle, &iface, detail, size, nullptr, &info))
            {
                fail(textResource(IDS_READ_EASIMAP_INTERFACE));
            }
            if (equal(instanceId(set.handle, info), instance) && (iface.Flags & SPINT_ACTIVE))
            {
                return true;
            }
        }
    }

    bool bind(const Text &instance, const Text &inf, const Text &section, bool inspectOnly, const Log &log)
    {
        DeviceSet set;
        auto info = selected(set, instance);
        BOOL reboot = FALSE;
        if (inf.empty())
        {
            if (inspectOnly)
            {
                return false;
            }
            if (!DiInstallDevice(nullptr, set.handle, &info, nullptr,
                                 DIIDFLAG_NOFINISHINSTALLUI | DIIDFLAG_INSTALLNULLDRIVER, &reboot))
            {
                fail(textResource(IDS_RESTORE_THE_PREVIOUS_UNCONFIGURED_DEVICE));
            }
            return reboot != FALSE;
        }
        if (fs::path(inf).filename() != fs::path(inf) || !equal(fs::path(inf).extension().wstring(), L".inf"))
        {
            throw Error(textResource(IDS_SAVED_DRIVER_FILENAME_IS_INVALID));
        }
        wchar_t windows[MAX_PATH]{};
        if (!GetWindowsDirectoryW(windows, MAX_PATH))
        {
            fail(textResource(IDS_FIND_WINDOWS_DIRECTORY));
        }
        auto path = fs::path(windows) / L"INF" / inf;
        if (!fs::exists(path))
        {
            throw Error(formatResource(IDS_REQUIRED_WINDOWS_DRIVER_PACKAGE_IS_NOT_AVAILABLE, {inf}));
        }
        SP_DEVINSTALL_PARAMS_W p{};
        p.cbSize = sizeof(p);
        if (!SetupDiGetDeviceInstallParamsW(set.handle, &info, &p))
        {
            fail(textResource(IDS_READ_INSTALLATION_PARAMETERS));
        }
        // Match Device Manager's manual selection from one existing INF, including excluded drivers.
        p.Flags |= DI_ENUMSINGLEINF | DI_QUIETINSTALL;
        p.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
        p.FlagsEx &= ~DI_FLAGSEX_FILTERSIMILARDRIVERS;
        if (path.wstring().size() >= MAX_PATH)
        {
            throw Error(textResource(IDS_DRIVER_PATH_IS_TOO_LONG));
        }
        wcscpy_s(p.DriverPath, path.c_str());
        if (!SetupDiSetDeviceInstallParamsW(set.handle, &info, &p))
        {
            fail(textResource(IDS_SELECT_WINDOWS_DRIVER_PACKAGE));
        }
        if (!SetupDiBuildDriverInfoList(set.handle, &info, SPDIT_CLASSDRIVER))
        {
            fail(textResource(IDS_READ_DRIVERS_IN_WINDOWS_PACKAGE));
        }

        struct DriverList
        {
            HDEVINFO set;
            SP_DEVINFO_DATA *info;

            ~DriverList()
            {
                SetupDiDestroyDriverInfoList(set, info, SPDIT_CLASSDRIVER);
            }
        } cleanup{set.handle, &info};

        for (DWORD i = 0;; ++i)
        {
            SP_DRVINFO_DATA_W driver{};
            driver.cbSize = sizeof(driver);
            if (!SetupDiEnumDriverInfoW(set.handle, &info, SPDIT_CLASSDRIVER, i, &driver))
            {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                {
                    break;
                }
                fail(textResource(IDS_READ_DRIVER_CHOICE));
            }
            DWORD size = 0;
            SetupDiGetDriverInfoDetailW(set.handle, &info, &driver, nullptr, 0, &size);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size < sizeof(SP_DRVINFO_DETAIL_DATA_W))
            {
                fail(textResource(IDS_MEASURE_DRIVER_DETAILS));
            }
            std::vector<BYTE> buffer(size, 0);
            auto detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_W *>(buffer.data());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDriverInfoDetailW(set.handle, &info, &driver, detail, size, nullptr))
            {
                fail(textResource(IDS_READ_DRIVER_DETAILS));
            }
            log(formatResource(IDS_DRIVER_CHOICE,
                               {driver.Description, detail->InfFileName, detail->SectionName}));
            if (!equal(fs::path(detail->InfFileName).filename().wstring(), inf) ||
                !equal(detail->SectionName, section))
            {
                continue;
            }
            if (inspectOnly)
            {
                return false;
            }
            if (!DiInstallDevice(nullptr, set.handle, &info, &driver, DIIDFLAG_NOFINISHINSTALLUI, &reboot))
            {
                fail(textResource(IDS_INSTALL_THE_SELECTED_WINDOWS_DRIVER));
            }
            return reboot != FALSE;
        }
        throw Error(formatResource(IDS_EXPECTED_DRIVER_SECTION_WAS_NOT_FOUND_IN, {inf, section}));
    }

    void restart(const Text &instance)
    {
        DeviceSet set;
        auto info = selected(set, instance);
        auto rc = CM_Disable_DevNode(info.DevInst, 0);
        if (rc != CR_SUCCESS)
        {
            throw Error(formatResource(IDS_WINDOWS_COULD_NOT_RESTART_THE_CABLE_DISABLE_RESULT,
                                       {std::to_wstring(rc)}));
        }
        // After a successful disable, always attempt to re-enable, even if the node is being removed.
        rc = CM_Enable_DevNode(info.DevInst, 0);
        if (rc != CR_SUCCESS)
        {
            throw Error(
                formatResource(IDS_WINDOWS_COULD_NOT_RE_ENABLE_THE_CABLE_RESULT, {std::to_wstring(rc)}));
        }
    }

    std::vector<SavedValue> registrations(const Text &instance)
    {
        Key key;
        openKey(key, parameterPath(instance));
        std::vector<SavedValue> result;
        for (const auto *name : {L"DeviceInterfaceGUIDs", L"DeviceInterfaceGUID"})
        {
            SavedValue v;
            v.name = name;
            DWORD size = 0;
            auto rc = key.handle ? RegQueryValueExW(key.handle, name, nullptr, &v.kind, nullptr, &size)
                                 : ERROR_FILE_NOT_FOUND;
            if (rc == ERROR_FILE_NOT_FOUND)
            {
                v.kind = REG_NONE;
                result.push_back(v);
                continue;
            }
            if (rc != ERROR_SUCCESS)
            {
                fail(textResource(IDS_READ_INTERFACE_REGISTRATION), rc);
            }
            if (v.kind != REG_SZ && v.kind != REG_MULTI_SZ)
            {
                throw Error(textResource(IDS_CABLE_HAS_AN_UNEXPECTED_INTERFACE_REGISTRATION_FORMAT_NO));
            }
            if (size % sizeof(wchar_t) != 0 || size > 1024 * 1024)
            {
                throw Error(textResource(IDS_CABLE_HAS_INVALID_INTERFACE_REGISTRATION_DATA));
            }
            std::vector<wchar_t> data(size / sizeof(wchar_t) + 2, 0);
            rc = RegQueryValueExW(key.handle, name, nullptr, &v.kind, reinterpret_cast<BYTE *>(data.data()),
                                  &size);
            if (rc != ERROR_SUCCESS)
            {
                fail(textResource(IDS_READ_INTERFACE_REGISTRATION), rc);
            }
            v.exists = true;
            if (v.kind == REG_SZ)
            {
                v.data.emplace_back(data.data());
            }
            else
            {
                v.data = splitMulti(data.data(), data.size());
            }
            result.push_back(std::move(v));
        }
        return result;
    }

    void restoreRegistrations(const Text &instance, const std::vector<SavedValue> &values)
    {
        find(instance);
        Key key;
        createParameters(key, instance);
        for (const auto &v : values)
        {
            setValue(key.handle, v);
        }
    }

    void addRegistration(const Text &instance)
    {
        auto values = registrations(instance);
        auto v = values.at(0);
        v.exists = true;
        v.kind = REG_MULTI_SZ;
        if (!v.containsMcl())
        {
            v.data.emplace_back(InterfaceGuid);
        }
        find(instance);
        Key key;
        createParameters(key, instance);
        setValue(key.handle, v);
    }

    void removeRegistration(const Text &instance)
    {
        auto values = registrations(instance);
        for (auto &v : values)
        {
            if (v.containsMcl())
            {
                v.data.erase(std::remove_if(v.data.begin(), v.data.end(),
                                            [](const Text &s) {
                                                return equal(s, InterfaceGuid);
                                            }),
                             v.data.end());
                v.exists = !v.data.empty();
            }
        }
        restoreRegistrations(instance, values);
    }

    Text describe(const Device &d)
    {
        return formatResource(IDS_DEVICE_STATUS,
                              {d.instance, textResource(d.ready() ? IDS_REPORT_TRUE : IDS_REPORT_FALSE),
                               d.service, d.inf, d.section, std::to_wstring(d.problem),
                               textResource(d.interfaceActive ? IDS_REPORT_TRUE : IDS_REPORT_FALSE),
                               textResource(d.hasBackup() ? IDS_REPORT_TRUE : IDS_REPORT_FALSE)});
    }
} // namespace mbe
