// Perfect Dark XBLA PC Recomp - handicap helpers for the pd_handicaps setting.
//
// pd_handicaps = "Name:percent, Name:percent" sets online handicaps by gamertag
// (10-1000% damage taken). PerfectDarkApp::TickNetHandicaps applies the list to
// the game's player configs during a match. Header-only, C++17.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>

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

}  // namespace pd::handicap
