#include "setup.h"
#include <bcrypt.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <algorithm>
#include <memory>
#include <sstream>
#include <iomanip>

namespace mbe
{
    namespace
    {
        /// <summary>Releases a file handle automatically, including when an operation throws.</summary>
        struct Handle
        {
            HANDLE value = INVALID_HANDLE_VALUE;

            explicit Handle(HANDLE h) : value(h) {}

            ~Handle()
            {
                if (value != INVALID_HANDLE_VALUE && value)
                {
                    CloseHandle(value);
                }
            }
        };

        /// <summary>Releases a locally owned COM interface when its scope ends.</summary>
        template <class T> struct Com
        {
            T *value = nullptr;

            ~Com()
            {
                if (value)
                {
                    value->Release();
                }
            }

            T **put()
            {
                return &value;
            }

            T *operator->()
            {
                return value;
            }
        };

        void check(HRESULT hr, const Text &action)
        {
            if (FAILED(hr))
            {
                fail(action, static_cast<DWORD>(hr));
            }
        }

        struct XmlNode
        {
            Text name, text;
            std::vector<XmlNode> children;
        };

        XmlNode readNode(IXmlReader *reader, unsigned depth, size_t &nodes)
        {
            if (depth > 12 || ++nodes > 16384)
            {
                throw Error(textResource(IDS_SAVED_CONFIGURATION_IS_TOO_COMPLEX));
            }
            const wchar_t *name = nullptr;
            check(reader->GetLocalName(&name, nullptr), textResource(IDS_READ_SAVED_XML_NAME));
            XmlNode node;
            node.name = name;
            const wchar_t *ns = nullptr;
            check(reader->GetNamespaceUri(&ns, nullptr), textResource(IDS_READ_SAVED_XML_NAMESPACE));
            if (ns && *ns)
            {
                throw Error(textResource(IDS_SAVED_CONFIGURATION_HAS_AN_UNSUPPORTED_NAMESPACE));
            }
            if (reader->IsEmptyElement())
            {
                return node;
            }
            XmlNodeType type{};
            HRESULT hr;
            while ((hr = reader->Read(&type)) == S_OK)
            {
                if (type == XmlNodeType_EndElement)
                {
                    return node;
                }
                if (type == XmlNodeType_Element)
                {
                    node.children.push_back(readNode(reader, depth + 1, nodes));
                }
                else if (type == XmlNodeType_Text || type == XmlNodeType_CDATA ||
                         type == XmlNodeType_Whitespace)
                {
                    const wchar_t *value = nullptr;
                    UINT size = 0;
                    check(reader->GetValue(&value, &size), textResource(IDS_READ_SAVED_XML_TEXT));
                    node.text.append(value, size);
                }
            }
            check(hr, textResource(IDS_READ_SAVED_CONFIGURATION_XML));
            throw Error(textResource(IDS_SAVED_CONFIGURATION_IS_INCOMPLETE));
        }

        const XmlNode &child(const XmlNode &parent, const Text &name)
        {
            const XmlNode *found = nullptr;
            for (const auto &node : parent.children)
            {
                if (node.name == name)
                {
                    if (found)
                    {
                        throw Error(textResource(IDS_SAVED_CONFIGURATION_CONTAINS_DUPLICATE_FIELDS));
                    }
                    found = &node;
                }
            }
            if (!found)
            {
                throw Error(formatResource(IDS_SAVED_CONFIGURATION_IS_MISSING, {name}));
            }
            return *found;
        }

        Text field(const XmlNode &parent, const Text &name)
        {
            const auto &n = child(parent, name);
            if (!n.children.empty())
            {
                throw Error(textResource(IDS_SAVED_CONFIGURATION_CONTAINS_INVALID_FIELDS));
            }
            return n.text;
        }

        Snapshot parse(const std::string &bytes, const Text &instance)
        {
            if (bytes.empty() || bytes.size() > 1024 * 1024)
            {
                throw Error(textResource(IDS_SAVED_CONFIGURATION_HAS_AN_INVALID_SIZE));
            }
            Com<IStream> stream;
            stream.value = SHCreateMemStream(reinterpret_cast<const BYTE *>(bytes.data()),
                                             static_cast<UINT>(bytes.size()));
            if (!stream.value)
            {
                throw Error(textResource(IDS_COULD_NOT_OPEN_THE_SAVED_CONFIGURATION));
            }
            Com<IXmlReader> reader;
            check(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void **>(reader.put()), nullptr),
                  textResource(IDS_CREATE_XML_READER));
            check(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit),
                  textResource(IDS_CONFIGURE_XML_READER));
            check(reader->SetInput(stream.value), textResource(IDS_OPEN_SAVED_XML));
            XmlNode root;
            bool found = false;
            XmlNodeType type{};
            HRESULT hr;
            size_t nodes = 0;
            while ((hr = reader->Read(&type)) == S_OK)
            {
                if (type == XmlNodeType_Element)
                {
                    if (found)
                    {
                        throw Error(textResource(IDS_SAVED_CONFIGURATION_HAS_MULTIPLE_ROOTS));
                    }
                    root = readNode(reader.value, 0, nodes);
                    found = true;
                }
            }
            check(hr, textResource(IDS_READ_SAVED_XML));
            if (!found || root.name != L"Snapshot" || field(root, L"Version") != L"1")
            {
                throw Error(textResource(IDS_SAVED_CONFIGURATION_VERSION_IS_INVALID_IT_HAS_BEEN));
            }
            Snapshot s;
            s.instance = field(root, L"Instance");
            s.inf = field(root, L"PreviousInf");
            s.section = field(root, L"PreviousSection");
            s.service = field(root, L"PreviousService");
            s.created = field(root, L"CreatedUtc");
            if (!equal(s.instance, instance))
            {
                throw Error(textResource(IDS_SAVED_CONFIGURATION_BELONGS_TO_A_DIFFERENT_DEVICE_IT));
            }
            const auto &values = child(root, L"Values");
            if (values.children.size() != 2)
            {
                throw Error(textResource(IDS_SAVED_REGISTRATION_LIST_IS_INVALID));
            }
            for (const auto &node : values.children)
            {
                if (node.name != L"SavedValue")
                {
                    throw Error(textResource(IDS_SAVED_REGISTRATION_LIST_IS_INVALID));
                }
                SavedValue v;
                v.name = field(node, L"Name");
                auto exists = field(node, L"Exists");
                if (exists != L"true" && exists != L"false")
                {
                    throw Error(textResource(IDS_SAVED_REGISTRATION_STATE_IS_INVALID));
                }
                v.exists = exists == L"true";
                auto kind = field(node, L"Kind");
                v.kind = kind == L"String" ? REG_SZ : kind == L"MultiString" ? REG_MULTI_SZ : REG_NONE;
                if (v.exists && v.kind == REG_NONE)
                {
                    throw Error(textResource(IDS_SAVED_REGISTRATION_TYPE_IS_INVALID));
                }
                const auto &data = child(node, L"Data");
                for (const auto &item : data.children)
                {
                    if (item.name != L"string" || !item.children.empty() ||
                        item.text.find(L'\0') != Text::npos)
                    {
                        throw Error(textResource(IDS_SAVED_REGISTRATION_DATA_IS_INVALID));
                    }
                    v.data.push_back(item.text);
                }
                if (v.exists && v.kind == REG_SZ && v.data.size() != 1)
                {
                    throw Error(textResource(IDS_SAVED_STRING_REGISTRATION_IS_INVALID));
                }
                s.values.push_back(std::move(v));
            }
            if (!((s.values[0].name == L"DeviceInterfaceGUIDs" &&
                   s.values[1].name == L"DeviceInterfaceGUID") ||
                  (s.values[1].name == L"DeviceInterfaceGUIDs" &&
                   s.values[0].name == L"DeviceInterfaceGUID")))
            {
                throw Error(textResource(IDS_SAVED_REGISTRATION_NAMES_ARE_INVALID));
            }
            return s;
        }

        std::string escape(const Text &text)
        {
            std::string result;
            for (auto c : text)
            {
                if (c < 32 && c != L'\t' && c != L'\r' && c != L'\n')
                {
                    throw Error(textResource(IDS_SAVED_SETTING_CONTAINS_AN_UNSUPPORTED_CHARACTER));
                }
            }
            for (auto c : utf8(text))
            {
                switch (c)
                {
                case '&':
                    result += "&amp;";
                    break;
                case '<':
                    result += "&lt;";
                    break;
                case '>':
                    result += "&gt;";
                    break;
                case '\r':
                    result += "&#13;";
                    break;
                default:
                    result += c;
                }
            }
            return result;
        }

        std::string serialize(const Snapshot &s)
        {
            std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Snapshot>\n";
            auto element = [&](const char *name, const Text &value) {
                xml += "  <" + std::string(name) + ">" + escape(value) + "</" + name + ">\n";
            };
            element("Version", L"1");
            element("Instance", s.instance);
            element("PreviousInf", s.inf);
            element("PreviousSection", s.section);
            element("PreviousService", s.service);
            element("CreatedUtc", s.created);
            xml += "  <Values>\n";
            for (const auto &v : s.values)
            {
                xml += "    <SavedValue>\n";
                element("Name", v.name);
                element("Exists", v.exists ? L"true" : L"false");
                element("Kind", v.kind == REG_SZ         ? L"String"
                                : v.kind == REG_MULTI_SZ ? L"MultiString"
                                                         : L"Unknown");
                xml += "      <Data>\n";
                for (const auto &t : v.data)
                {
                    element("string", t);
                }
                xml += "      </Data>\n    </SavedValue>\n";
            }
            return xml + "  </Values>\n</Snapshot>\n";
        }

        void archive(const Text &instance)
        {
            auto path = backupPath(instance);
            auto destination = fs::path(path.wstring() + L".restored-" + timestamp(true));
            if (!MoveFileExW(path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
            {
                fail(textResource(IDS_ARCHIVE_RESTORED_CONFIGURATION));
            }
        }
    } // namespace

    [[noreturn]] void fail(const Text &action, DWORD code)
    {
        wchar_t *message = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
        Text text = formatResource(IDS_FAILED, {action, std::to_wstring(code)});
        if (message)
        {
            text += formatResource(IDS_SYSTEM_ERROR_DETAIL, {Text(message)});
            LocalFree(message);
        }
        throw Error(text);
    }

    bool equal(const Text &a, const Text &b)
    {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                    static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    }

    std::string utf8(const Text &value)
    {
        if (value.empty())
        {
            return {};
        }
        int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (!size)
        {
            fail(textResource(IDS_ENCODE_UNICODE_TEXT));
        }
        std::string result(size, 0);
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                 result.data(), size, nullptr, nullptr))
        {
            fail(textResource(IDS_ENCODE_UNICODE_TEXT));
        }
        return result;
    }

    Text wide(const std::string &value)
    {
        if (value.empty())
        {
            return {};
        }
        int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
        if (!size)
        {
            fail(textResource(IDS_DECODE_UNICODE_TEXT));
        }
        Text result(size, 0);
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                 result.data(), size))
        {
            fail(textResource(IDS_DECODE_UNICODE_TEXT));
        }
        return result;
    }

    Text timestamp(bool filename)
    {
        SYSTEMTIME t{};
        GetSystemTime(&t);
        wchar_t value[64]{};
        if (filename)
        {
            swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u-%03u", t.wYear, t.wMonth, t.wDay, t.wHour,
                       t.wMinute, t.wSecond, t.wMilliseconds);
        }
        else
        {
            swprintf_s(value, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.wYear, t.wMonth, t.wDay, t.wHour,
                       t.wMinute, t.wSecond, t.wMilliseconds);
        }
        return value;
    }

    fs::path dataDirectory()
    {
        wchar_t *folder = nullptr;
        check(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &folder),
              textResource(IDS_FIND_SHARED_APPLICATION_DATA));
        fs::path result = fs::path(folder) / L"MBE-Easimap-ARM-Setup";
        CoTaskMemFree(folder);
        return result;
    }

    fs::path backupPath(const Text &instance)
    {
        Text upper = instance;
        for (auto &c : upper)
        {
            if (c >= L'a' && c <= L'z')
            {
                // USB instance IDs use ASCII; normalize case before hashing the backup filename.
                c -= L'a' - L'A';
            }
        }
        auto bytes = utf8(upper);
        BYTE hash[32]{};
        auto rc = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<BYTE *>(bytes.data()),
                             static_cast<ULONG>(bytes.size()), hash, sizeof(hash));
        if (rc < 0)
        {
            fail(textResource(IDS_COMPUTE_CONFIGURATION_IDENTIFIER), static_cast<DWORD>(rc));
        }
        Text name;
        const wchar_t digits[] = L"0123456789ABCDEF";
        for (auto b : hash)
        {
            name += digits[b >> 4];
            name += digits[b & 15];
        }
        return dataDirectory() / L"Backups" / (name + L".xml");
    }

    void writeFile(const fs::path &path, const std::string &contents)
    {
        Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
        if (file.value == INVALID_HANDLE_VALUE)
        {
            fail(textResource(IDS_CREATE_OUTPUT_FILE));
        }
        DWORD written = 0;
        if (!WriteFile(file.value, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) ||
            written != contents.size())
        {
            fail(textResource(IDS_WRITE_OUTPUT_FILE));
        }
        if (!FlushFileBuffers(file.value))
        {
            fail(textResource(IDS_FLUSH_OUTPUT_FILE));
        }
    }

    std::string readFile(const fs::path &path)
    {
        Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.value == INVALID_HANDLE_VALUE)
        {
            fail(textResource(IDS_OPEN_SAVED_CONFIGURATION));
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.value, &size))
        {
            fail(textResource(IDS_READ_SAVED_CONFIGURATION_SIZE));
        }
        if (size.QuadPart <= 0 || size.QuadPart > 1024 * 1024)
        {
            throw Error(textResource(IDS_SAVED_CONFIGURATION_HAS_AN_INVALID_SIZE));
        }
        std::string result(static_cast<size_t>(size.QuadPart), 0);
        DWORD read = 0;
        if (!ReadFile(file.value, result.data(), static_cast<DWORD>(result.size()), &read, nullptr) ||
            read != result.size())
        {
            fail(textResource(IDS_READ_SAVED_CONFIGURATION));
        }
        return result;
    }

    Snapshot loadSnapshot(const Text &instance)
    {
        return parse(readFile(backupPath(instance)), instance);
    }

    // Never replace the original baseline when repairing a failed or incomplete installation.
    Snapshot saveSnapshot(const Device &d)
    {
        auto path = backupPath(d.instance);
        if (fs::exists(path))
        {
            return loadSnapshot(d.instance);
        }
        Snapshot s;
        s.instance = d.instance;
        s.inf = d.inf;
        s.section = d.section;
        s.service = d.service;
        s.created = timestamp();
        s.values = registrations(d.instance);
        fs::create_directories(path.parent_path());
        auto temp = fs::path(path.wstring() + L".tmp");
        writeFile(temp, serialize(s));
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            fail(textResource(IDS_SAVE_ORIGINAL_CONFIGURATION));
        }
        return s;
    }

    bool equalValues(const std::vector<SavedValue> &a, const std::vector<SavedValue> &b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        return std::all_of(a.begin(), a.end(), [&](const SavedValue &v) {
            return std::any_of(b.begin(), b.end(), [&](const SavedValue &w) {
                return v.name == w.name && v.exists == w.exists &&
                       (!v.exists || (v.kind == w.kind && v.data == w.data));
            });
        });
    }

    Text install(const Text &instance, const Log &log)
    {
        auto d = find(instance);
        if (d.ready())
        {
            return textResource(IDS_ALREADY_READY_FOR_EASIMAP_NO_CHANGES_WERE_NEEDED);
        }
        if (!equal(d.service, L"WINUSB"))
        {
            mbe::bind(instance, L"winusb.inf", L"WINUSB", true, log);
        }
        saveSnapshot(d);
        log(textResource(IDS_SAVED_THE_PREVIOUS_CABLE_CONFIGURATION));
        bool reboot = false;
        if (!equal(d.service, L"WINUSB"))
        {
            log(textResource(IDS_SELECTING_MICROSOFT_S_BUILT_IN_WINUSB_DRIVER));
            reboot = mbe::bind(instance, L"winusb.inf", L"WINUSB", false, log);
        }
        find(instance);
        addRegistration(instance);
        log(textResource(IDS_REGISTERED_THE_EASIMAP_INTERFACE));
        if (reboot)
        {
            return textResource(IDS_SETUP_SAVED_RESTART_WINDOWS_RECONNECT_THE_CABLE_AND);
        }
        restart(instance);
        for (int i = 0; i < 20; ++i)
        {
            Sleep(250);
            try
            {
                if (find(instance).ready())
                {
                    return textResource(IDS_READY_FOR_EASIMAP_OPEN_EASIMAP_TO_CHECK_THE);
                }
            }
            catch (const Error &)
            {
                if (i == 19)
                {
                    throw;
                }
            }
        }
        throw Error(textResource(IDS_SETUP_WAS_APPLIED_BUT_WINDOWS_HAS_NOT_ACTIVATED));
    }

    Text uninstall(const Text &instance, const Log &log)
    {
        auto d = find(instance);
        if (!d.canUninstall())
        {
            return textResource(IDS_THERE_IS_NO_EASIMAP_SETUP_TO_UNINSTALL);
        }
        if (d.hasBackup())
        {
            auto s = loadSnapshot(instance);
            bool same = equal(d.inf, s.inf) && equal(d.section, s.section);
            if (!same && !equal(d.service, L"WINUSB") && !d.inf.empty())
            {
                throw Error(textResource(IDS_ANOTHER_DRIVER_HAS_BEEN_INSTALLED_SINCE_SETUP_THE));
            }
            if (!same && !s.inf.empty())
            {
                mbe::bind(instance, s.inf, s.section, true, log);
            }
            bool reboot = false;
            if (!same)
            {
                log(textResource(IDS_RESTORING_THE_PREVIOUS_DRIVER_SELECTION));
                reboot = mbe::bind(instance, s.inf, s.section, false, log);
            }
            // Driver switches may clear Device Parameters. Restore registrations after switching.
            restoreRegistrations(instance, s.values);
            log(textResource(IDS_RESTORED_THE_PREVIOUS_INTERFACE_REGISTRATION));
            if (!reboot && equal(s.service, L"WINUSB"))
            {
                restart(instance);
            }
            if (reboot)
            {
                return textResource(IDS_RESTORATION_NEEDS_A_WINDOWS_RESTART_RESTART_RECONNECT_THE);
            }
            Sleep(500);
            auto after = find(instance);
            if (!equal(after.inf, s.inf) || !equal(after.section, s.section))
            {
                throw Error(textResource(IDS_WINDOWS_HAS_NOT_RESTORED_THE_SAVED_DRIVER_SELECTION));
            }
            if (!equalValues(registrations(instance), s.values))
            {
                throw Error(textResource(IDS_SAVED_REGISTRATION_WAS_NOT_RESTORED_THE_BACKUP_IS));
            }
            if (std::none_of(s.values.begin(), s.values.end(),
                             [](const SavedValue &v) {
                                 return v.containsMcl();
                             }) &&
                after.interfaceActive)
            {
                throw Error(textResource(IDS_WINDOWS_HAS_NOT_REMOVED_THE_EASIMAP_INTERFACE_YET));
            }
            archive(instance);
            return textResource(IDS_UNINSTALLED_THE_CABLE_S_PREVIOUS_CONFIGURATION_HAS_BEEN);
        }
        log(textResource(IDS_THIS_CABLE_WAS_CONFIGURED_OUTSIDE_THIS_UTILITY_KEEPING));
        removeRegistration(instance);
        restart(instance);
        Sleep(500);
        auto result = find(instance);
        if (result.hasRegistration() || result.interfaceActive)
        {
            throw Error(textResource(IDS_RECONNECT_THE_CABLE_AND_RETRY_UNINSTALL_TO_FINISH));
        }
        return textResource(IDS_EASIMAP_REGISTRATION_REMOVED_THE_EXISTING_WINDOWS_USB_DRIVER);
    }

    void selfTest(const Log &log)
    {
        const Text id = L"USB\\VID_FEFE&PID_0001\\07937625";
        Snapshot s;
        s.instance = id;
        s.inf = L"winusb.inf";
        s.section = L"WINUSB";
        s.service = L"WINUSB";
        s.created = timestamp();
        s.values = {
            {L"DeviceInterfaceGUIDs",
             true,
             REG_MULTI_SZ,
             {InterfaceGuid, L"{11111111-2222-3333-4444-555555555555}", L"Unicode \u00e9 & < >\r\n", L"   "}},
            {L"DeviceInterfaceGUID", true, REG_SZ, {L""}}};
        auto restored = parse(serialize(s), id);
        if (!equalValues(s.values, restored.values) || restored.inf != s.inf ||
            restored.section != s.section || restored.service != s.service)
        {
            throw Error(textResource(IDS_SNAPSHOT_ROUND_TRIP_FAILED));
        }
        auto mustReject = [&](std::string xml, Text instance) {
            bool rejected = false;
            try
            {
                parse(xml, instance);
            }
            catch (const Error &)
            {
                rejected = true;
            }
            if (!rejected)
            {
                throw Error(textResource(IDS_INVALID_SNAPSHOT_WAS_ACCEPTED));
            }
        };
        mustReject(serialize(s), L"USB\\VID_FEFE&PID_0001\\OTHER");
        auto wrong = serialize(s);
        auto pos = wrong.find("<Version>1");
        wrong.replace(pos, 10, "<Version>2");
        mustReject(wrong, id);
        mustReject(
            "<!DOCTYPE Snapshot [<!ENTITY x SYSTEM 'file:///C:/Windows/win.ini'>]><Snapshot>&x;</Snapshot>",
            id);
        mustReject("<Snapshot>", id);
        s.values[0].exists = false;
        s.values[0].kind = REG_NONE;
        s.values[0].data.clear();
        if (!equalValues(s.values, parse(serialize(s), id).values))
        {
            throw Error(textResource(IDS_ABSENT_REGISTRATION_ROUND_TRIP_FAILED));
        }
        log(textResource(IDS_BACKUP_TESTS_PASSED));
    }
} // namespace mbe
