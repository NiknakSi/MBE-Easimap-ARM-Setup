#pragma once
#include <windows.h>
#include <initializer_list>
#include <string>
#include "resources/resource.h"

namespace mbe
{
    /// <summary>Loads a complete UTF-16 string from the current executable's string table.</summary>
    /// <remarks>Preserves GetLastError so loading an error caption does not hide the original
    /// Windows failure. The app and test executables each embed the same strings.rc.</remarks>
    /// <param name="id">An IDS_ identifier from resources/resource.h.</param>
    /// <returns>An owning copy, with no fixed-buffer truncation.</returns>
    std::wstring textResource(UINT id);

    /// <summary>Inserts text arguments into a resource containing {0} through {9} placeholders.</summary>
    /// <remarks>Arguments can be reordered or repeated in the resource. Their contents are copied
    /// literally, including braces. Invalid or missing inserts throw rather than read past the
    /// argument list. Literal braces in a template are written {{ and }}.</remarks>
    /// <param name="id">An IDS_ identifier for the complete message template.</param>
    /// <param name="arguments">Text values in zero-based placeholder order.</param>
    /// <returns>The formatted message, preserving the caller's Windows error code.</returns>
    std::wstring formatResource(UINT id, std::initializer_list<std::wstring> arguments);
} // namespace mbe
