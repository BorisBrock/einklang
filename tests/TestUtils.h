#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace TestUtils
{

// A fresh, empty directory below the system temp directory that is removed
// again (with everything in it) when the object goes out of scope.
class TempDir
{
public:
  TempDir()
  {
    std::random_device rd;
    const auto base = std::filesystem::temp_directory_path();
    do
    {
      mPath = base / ("einklang-test-" + std::to_string(rd()) + std::to_string(rd()));
    } while (std::filesystem::exists(mPath));
    std::filesystem::create_directories(mPath);
  }

  ~TempDir()
  {
    std::error_code ec;
    std::filesystem::remove_all(mPath, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::filesystem::path& Path() const { return mPath; }

  // Absolute path of `name` inside the directory, as a string.
  std::string File(std::string_view name) const { return (mPath / name).string(); }

  // Writes `content` to `name` inside the directory and returns its path.
  std::string Write(std::string_view name, std::string_view content) const
  {
    const auto path = File(name);
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path;
  }

private:
  std::filesystem::path mPath;
};

inline std::string ReadFile(const std::string& path)
{
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace TestUtils
