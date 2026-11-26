// Custom gun models: loose files that replace the game's packed ones (gun model research,
// docs/research/gun-models.md). With pd_file_override = true, every
// assets\DataFiles\override\file_XXXX.bin (XXXX = the file id in hex, as pdunpack
// names them) is used instead of PackedSegFile's entry XXXX. The files are the
// inflated N64 format, exactly what tools/models/pdunpack extracts.
//
// How the game reads a packed file (archive object 0x82652968):
//   sub_822549A0(archive, id)            size = table entry +4
//   sub_82254BE0(archive, id)            allocates `size` bytes itself, then loads
//   sub_822549C0(archive, id, dest, 0)   load: a prefetch-cache copy, else read+inflate
// Every one of them takes the size from the entry table (*(0x82652B24), entry for
// id at table + id*16 - 16: {offset, size, compressed size, -1}). So:
//   1. PatchTable writes each loose file's size into its entry: every buffer the game
//      allocates for it is then big enough, whatever path asks.
//   2. Serve copies the loose file into `dest` on the read path of sub_822549C0 (after
//      its cache lookup, with its lock held), so the game never reads the packed copy.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>

#include <rex/logging.h>

namespace pd::fileoverride {

inline constexpr uint32_t kTablePointer = 0x82652B24;  // u32: the archive's entry table
// The texture manager (0x82655BE0): +2864 = its 4J records (52 bytes, +20 inflated size).
inline constexpr uint32_t kTextureRecords = 0x82655BE0 + 2864;

// Textures work the same way: assets\DataFiles\override\tex_XXXX.bin (XXXX = texture
// number) holds texel data in the texture's own Xenos layout (tools/models/texpack.py
// makes it from a PNG). The texture manager inflates a texture from Textures.raw into
// GPU memory before pointing its D3D header at it; ServeTexture then copies the loose
// texels over the start of it (the base level; the smaller mip levels are left alone).
//   streamed:  sub_82263470 after `bl XMemDecompress` (0x82263618; r29 texels, r30 = number * 52)
//   resident:  sub_82262D60 after the same call (0x82262FF4; r28 texels, r30 = number * 52)
struct State {
  bool enabled = false;
  std::map<uint32_t, std::vector<uint8_t>> files;     // file id -> contents
  std::map<uint32_t, std::vector<uint8_t>> textures;  // texture number -> texels
  uint32_t patched_table = 0;                      // the table PatchTable last wrote to
  std::mutex mutex;                                // hooks run on loader threads too
};

inline State& Get() {
  static State state;
  return state;
}

inline uint32_t LoadBE32(const uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}
inline void StoreBE32(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(base + address, &value, 4);
}

// Reads every file_XXXX.bin in `folder` (called once at start-up).
inline void Load(const std::filesystem::path& folder, bool enabled) {
  State& s = Get();
  std::lock_guard<std::mutex> lock(s.mutex);
  s.enabled = enabled;
  if (!enabled) return;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
    const std::string name = entry.path().filename().string();
    unsigned id = 0;
    const bool is_file = std::sscanf(name.c_str(), "file_%x.bin", &id) == 1 && id != 0;
    const bool is_texture = !is_file && std::sscanf(name.c_str(), "tex_%x.bin", &id) == 1;
    if (!is_file && !is_texture) continue;
    FILE* f = _wfopen(entry.path().c_str(), L"rb");
    if (!f) continue;
    std::vector<uint8_t> data(size_t(std::filesystem::file_size(entry.path(), ec)));
    const size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    if (got != data.size() || data.empty()) continue;
    REXLOG_INFO("[gunmodel] override: {} 0x{:04X} from {} ({} bytes)", is_file ? "file" : "texture", id, name,
                data.size());
    (is_file ? s.files : s.textures)[id] = std::move(data);
  }
  REXLOG_INFO("[gunmodel] override: {} loose files and {} textures in {}", s.files.size(), s.textures.size(),
              folder.string());
}

// Step 1: the loose files' sizes into the game's entry table (once per table).
inline void PatchTable(uint8_t* base) {
  State& s = Get();
  if (!s.enabled || s.files.empty()) return;
  std::lock_guard<std::mutex> lock(s.mutex);
  const uint32_t table = LoadBE32(base, kTablePointer);
  if (!table || table == s.patched_table) return;
  for (const auto& [id, data] : s.files) {
    const uint32_t entry = table + id * 16 - 16;
    const uint32_t packed = LoadBE32(base, entry + 4);
    StoreBE32(base, entry + 4, uint32_t(data.size()));
    REXLOG_INFO("[gunmodel] override: file 0x{:04X} size {} -> {}", id, packed, data.size());
  }
  s.patched_table = table;
}

// Step 2: the read path of sub_822549C0. True = `dest` now holds the loose file.
inline bool Serve(uint8_t* base, uint32_t id, uint32_t dest) {
  State& s = Get();
  if (!s.enabled) return false;
  std::lock_guard<std::mutex> lock(s.mutex);
  const auto it = s.files.find(id);
  if (it == s.files.end() || !dest) {
    // TEMP_GUNMODEL: which packed files the game reads (e.g. to find a gun's third-person model).
    REXLOG_INFO("[gunmodel] load file 0x{:04X} (packed) to 0x{:08X}", id, dest);
    return false;
  }
  std::memcpy(base + dest, it->second.data(), it->second.size());
  REXLOG_INFO("[gunmodel] served file 0x{:04X} ({} bytes) to 0x{:08X}", id, it->second.size(), dest);
  return true;
}

// A texture was just inflated into GPU memory: copy the loose texels over its start.
// `record_offset` = texture number * 52 (the loop's offset into the record arrays);
// `inflated_size` = the record's size, so a too-big loose file can't overrun.
inline void ServeTexture(uint8_t* base, uint32_t record_offset, uint32_t texels) {
  State& s = Get();
  if (!s.enabled || s.textures.empty() || !texels) return;
  std::lock_guard<std::mutex> lock(s.mutex);
  const uint32_t inflated_size = LoadBE32(base, LoadBE32(base, kTextureRecords) + record_offset + 20);
  const uint32_t number = record_offset / 52;
  const auto it = s.textures.find(number);
  if (it == s.textures.end()) return;
  const size_t n = std::min<size_t>(it->second.size(), inflated_size);
  std::memcpy(base + texels, it->second.data(), n);
  REXLOG_INFO("[gunmodel] served texture 0x{:04X} ({} of {} bytes) to 0x{:08X}", number, n, inflated_size, texels);
}

}  // namespace pd::fileoverride
