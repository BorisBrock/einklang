#pragma once

#include <string>
#include <string_view>

namespace JsUtils
{
  // Escapes backslashes and double-quotes so a string can be safely
  // embedded inside a JavaScript double-quoted string literal.
  inline std::string EscapeString(std::string_view text)
  {
    std::string result;
    result.reserve(text.size());
    for (const char c : text)
    {
      if (c == '\\' || c == '"')
        result += '\\';
      result += c;
    }
    return result;
  }
} // namespace JsUtils
