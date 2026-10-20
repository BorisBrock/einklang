// iconrender — Renders the app icon from its SVG sources at build time.
//
// Usage: iconrender <icon.svg> <icon-small.svg> <max small size> <out dir> <size>...
//
// Writes einklang-<size>.png for every size and einklang.ico holding all of
// them. Sizes up to <max small size> come from the simplified SVG, which stays
// legible where the detailed one would turn to mush.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace
{
using Bytes = std::vector<unsigned char>;

NSVGimage* loadSvg(const char* path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return nullptr;
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return nsvgParse(text.data(), "px", 96.0f); // nsvgParse modifies its input
}

Bytes renderPng(NSVGrasterizer* rast, NSVGimage* svg, int size)
{
  Bytes rgba(static_cast<size_t>(size) * size * 4);
  nsvgRasterize(rast, svg, 0, 0, size / svg->width, rgba.data(), size, size, size * 4);

  Bytes png;
  stbi_write_png_to_func(
    [](void* ctx, void* data, int len) {
      auto* out = static_cast<Bytes*>(ctx);
      out->insert(out->end(), static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + len);
    },
    &png, size, size, 4, rgba.data(), size * 4);
  return png;
}

bool writeFile(const std::filesystem::path& path, const Bytes& data)
{
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(out);
}

void put16(Bytes& out, std::uint16_t v)
{
  out.push_back(v & 0xff);
  out.push_back(v >> 8);
}

void put32(Bytes& out, std::uint32_t v)
{
  put16(out, v & 0xffff);
  put16(out, v >> 16);
}

// ICO with PNG-compressed entries, which Windows supports since Vista
Bytes buildIco(const std::vector<std::pair<int, Bytes>>& images)
{
  Bytes ico;
  put16(ico, 0);                                          // reserved
  put16(ico, 1);                                          // type: icon
  put16(ico, static_cast<std::uint16_t>(images.size()));  // count

  auto offset = static_cast<std::uint32_t>(6 + 16 * images.size());
  for (const auto& [size, png] : images)
  {
    const auto dim = static_cast<unsigned char>(size >= 256 ? 0 : size); // 0 means 256
    ico.insert(ico.end(), {dim, dim, 0, 0});              // no palette, reserved
    put16(ico, 1);                                        // planes
    put16(ico, 32);                                       // bits per pixel
    put32(ico, static_cast<std::uint32_t>(png.size()));
    put32(ico, offset);
    offset += static_cast<std::uint32_t>(png.size());
  }
  for (const auto& [size, png] : images)
    ico.insert(ico.end(), png.begin(), png.end());
  return ico;
}
} // namespace

int main(int argc, char* argv[])
{
  if (argc < 6)
  {
    std::fprintf(stderr, "Usage: %s <icon.svg> <icon-small.svg> <max small size> <out dir> <size>...\n", argv[0]);
    return 2;
  }

  NSVGimage* detailed = loadSvg(argv[1]);
  NSVGimage* simple = loadSvg(argv[2]);
  if (!detailed || !simple || detailed->width <= 0 || simple->width <= 0)
  {
    std::fprintf(stderr, "iconrender: cannot load %s\n", detailed ? argv[2] : argv[1]);
    return 1;
  }
  const int maxSmall = std::atoi(argv[3]);
  const std::filesystem::path outDir = argv[4];
  std::filesystem::create_directories(outDir);

  NSVGrasterizer* rast = nsvgCreateRasterizer();
  std::vector<std::pair<int, Bytes>> images;
  for (int i = 5; i < argc; ++i)
  {
    const int size = std::atoi(argv[i]);
    Bytes png = renderPng(rast, size <= maxSmall ? simple : detailed, size);
    const auto path = outDir / ("einklang-" + std::to_string(size) + ".png");
    if (png.empty() || !writeFile(path, png))
    {
      std::fprintf(stderr, "iconrender: cannot write %s\n", path.string().c_str());
      return 1;
    }
    images.emplace_back(size, std::move(png));
  }
  nsvgDeleteRasterizer(rast);
  nsvgDelete(detailed);
  nsvgDelete(simple);

  if (!writeFile(outDir / "einklang.ico", buildIco(images)))
  {
    std::fprintf(stderr, "iconrender: cannot write einklang.ico\n");
    return 1;
  }
  return 0;
}
