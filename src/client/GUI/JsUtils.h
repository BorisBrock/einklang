#pragma once

#include <format>
#include <string>
#include <string_view>

namespace JsUtils
{
  // Escapes backslashes, double-quotes and control characters so a string
  // can be safely embedded inside a JavaScript or JSON double-quoted string.
  inline std::string EscapeString(std::string_view text)
  {
    std::string result;
    result.reserve(text.size());
    for (const char c : text)
    {
      if (c == '\\' || c == '"')
      {
        result += '\\';
        result += c;
      }
      else if (static_cast<unsigned char>(c) < 0x20)
      {
        result += std::format("\\u{:04x}", static_cast<unsigned char>(c));
      }
      else
      {
        result += c;
      }
    }
    return result;
  }
} // namespace JsUtils
