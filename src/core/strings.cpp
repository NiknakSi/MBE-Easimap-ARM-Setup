#include "strings.h"
#include <stdexcept>

namespace
{
    // Caption lookup can run while constructing fail(...). Do not disturb its default error code.
    struct LastErrorGuard
    {
        DWORD saved = GetLastError();

        ~LastErrorGuard()
        {
            SetLastError(saved);
        }
    };
} // namespace

namespace mbe
{
    std::wstring textResource(UINT id)
    {
        LastErrorGuard guard;
        const wchar_t *value = nullptr;
        const int length = LoadStringW(GetModuleHandleW(nullptr), id, reinterpret_cast<wchar_t *>(&value), 0);
        if (length == 0)
        {
            // A missing compiled resource cannot supply its own translated error message.
            throw std::runtime_error("Missing string resource: " + std::to_string(id));
        }

        // LoadString's pointer is length-delimited and may not have a terminating null character.
        return std::wstring(value, static_cast<size_t>(length));
    }

    std::wstring formatResource(UINT id, std::initializer_list<std::wstring> arguments)
    {
        LastErrorGuard guard;
        const auto pattern = textResource(id);
        std::wstring result;

        for (size_t i = 0; i < pattern.size(); ++i)
        {
            const auto ch = pattern[i];
            if ((ch == L'{' || ch == L'}') && i + 1 < pattern.size() && pattern[i + 1] == ch)
            {
                result += ch;
                ++i;
            }
            else if (ch == L'{')
            {
                if (i + 2 >= pattern.size() || pattern[i + 1] < L'0' || pattern[i + 1] > L'9' ||
                    pattern[i + 2] != L'}')
                {
                    throw std::runtime_error("Invalid string resource insert: " + std::to_string(id));
                }

                const auto index = static_cast<size_t>(pattern[i + 1] - L'0');
                if (index >= arguments.size())
                {
                    throw std::runtime_error("Missing string resource argument: " + std::to_string(id));
                }

                result += arguments.begin()[index];
                i += 2;
            }
            else if (ch == L'}')
            {
                throw std::runtime_error("Unmatched string resource brace: " + std::to_string(id));
            }
            else
            {
                result += ch;
            }
        }

        return result;
    }
} // namespace mbe
