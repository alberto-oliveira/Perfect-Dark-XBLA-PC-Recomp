// Perfect Dark XBLA PC Recomp - handicap helpers for the pd_handicaps setting.
//
// pd_handicaps = "Name:percent, Name:percent" sets online handicaps by gamertag
// (10-1000% damage taken). PerfectDarkApp::TickNetHandicaps applies the list to
// the game's player configs during a match; online, the host sends its list to
// the joined PCs (EncodeBlob/DecodeBlob). Header-only, C++17.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rex/logging/api.h>
#include <rex/logging/macros.h>

namespace pd::handicap {

// Inverse of sub_821CB7F8 (N64 mp_handicap_to_value): damage-taken multiplier -> handicap byte.
// 10% -> 0, 100% -> 128, 1000% -> 255.
inline uint8_t ByteFromPercent(int percent) {
  const double v = std::clamp(percent, 10, 1000) / 100.0;
  const double h = v < 1.0 ? 127.0 * std::sqrt((v - 0.1) / 0.9)
                           : 128.0 + (std::sqrt((v + 2.0) / 3.0) - 1.0) * 127.0;
  return uint8_t(std::clamp<long>(std::lround(h), 0, 255));
}

// Strips surrounding spaces/tabs and lowercases (gamertags compare case-insensitively).
inline std::string LowerTrim(std::string s) {
  s.erase(0, s.find_first_not_of(" \t"));
  s.erase(s.find_last_not_of(" \t") + 1);
  for (char& c : s) c = char(std::tolower(uint8_t(c)));
  return s;
}

// "Zqhost:50, Zqclient:200" -> {zqhost: byte(50%), zqclient: byte(200%)}
inline std::unordered_map<std::string, uint8_t> Parse(const std::string& list) {
  std::unordered_map<std::string, uint8_t> out;
  std::stringstream ss(list);
  std::string entry;
  while (std::getline(ss, entry, ',')) {
    const size_t colon = entry.rfind(':');
    if (colon == std::string::npos) continue;
    const std::string name = LowerTrim(entry.substr(0, colon));
    const int percent = std::atoi(LowerTrim(entry.substr(colon + 1)).c_str());
    if (name.empty() || percent <= 0) {
      REXLOG_WARN("[handicap] ignoring bad pd_handicaps entry '{}'", entry);
      continue;
    }
    out[name] = ByteFromPercent(percent);
  }
  return out;
}

// The table's entries sorted by name, so every PC encodes and logs them in the
// same order (an unordered_map has no fixed order).
inline std::vector<std::pair<std::string, uint8_t>> Sorted(const std::unordered_map<std::string, uint8_t>& table) {
  std::vector<std::pair<std::string, uint8_t>> entries(table.begin(), table.end());
  std::sort(entries.begin(), entries.end());
  return entries;
}

// ---- The host's message to the joined PCs ---------------------------------------
//
// netplay::SetHostBlob(kBlobKey, ...): only the host's pd_handicaps counts online.
// Layout: version, number of entries, then for each entry the name's length, the
// name (already trimmed and lower-cased by Parse) and the handicap byte.
//   [1][2] [6]"zqhost"[0] [8]"zqclient"[128]

constexpr uint32_t kBlobKey = 0x48434150;  // "HCAP"
constexpr uint8_t kBlobVersion = 1;

// Entries that would go past max_size (the SDK keeps at most netplay::kMaxHostBlob
// bytes) are left out with a warning: at 15 characters a name takes 17 bytes, so
// 192 bytes hold 11 players.
inline std::vector<uint8_t> EncodeBlob(const std::unordered_map<std::string, uint8_t>& table, size_t max_size) {
  std::vector<uint8_t> blob = {kBlobVersion, 0};
  for (const auto& [name, value] : Sorted(table)) {
    const size_t length = std::min<size_t>(name.size(), 255);
    if (blob.size() + 1 + length + 1 > max_size || blob[1] == 255) {
      REXLOG_WARN("[handicap] the list is too long to send: '{}' and later names left out", name);
      break;
    }
    blob.push_back(uint8_t(length));
    blob.insert(blob.end(), name.begin(), name.begin() + length);
    blob.push_back(value);
    ++blob[1];
  }
  return blob;
}

// False if the message is from another version or cut short; *out is then left as it was.
inline bool DecodeBlob(const std::vector<uint8_t>& blob, std::unordered_map<std::string, uint8_t>* out) {
  if (blob.size() < 2 || blob[0] != kBlobVersion) return false;
  std::unordered_map<std::string, uint8_t> table;
  size_t at = 2;
  for (int i = 0; i < blob[1]; ++i) {
    if (at >= blob.size()) return false;
    const size_t length = blob[at++];
    if (at + length + 1 > blob.size()) return false;  // the name and its byte must both be there
    table[std::string(blob.begin() + at, blob.begin() + at + length)] = blob[at + length];
    at += length + 1;
  }
  *out = std::move(table);
  return true;
}

}  // namespace pd::handicap
