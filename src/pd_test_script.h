// Helpers for the test pipe's scripts (perfect_dark_app.h TickTestPipe):
// the signal files that let several game copies on one PC wait for each other,
// and the timeouts of the commands that wait.
//
// Signals: every copy started by tools\Run-TestCopies.ps1 gets the same folder
// in pd_test_sync_dir. "signal hosting" creates an empty file named "hosting"
// there; "waitsignal hosting" in another copy holds its script until that file
// exists. Run-TestCopies empties the folder before starting the copies, so
// signals never carry over from an earlier run.

#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <rex/cvar.h>

namespace pd::testscript {

// The folder from pd_test_sync_dir; empty when signals are off.
inline std::filesystem::path SyncDir() {
  const std::string dir = rex::cvar::GetFlagByName("pd_test_sync_dir");
  return dir.empty() ? std::filesystem::path() : std::filesystem::path(dir);
}

// Creates the signal file. False when signals are off or the file can't be made.
inline bool Signal(const std::string& name) {
  const std::filesystem::path dir = SyncDir();
  if (dir.empty() || name.empty()) return false;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream file(dir / name);  // an empty file: its existence is the signal
  return bool(file);
}

// How many signal files have a name starting with `prefix`. "waitsignal ready 2"
// waits for two clients that each sent their own "ready-<copy>".
inline int CountSignals(const std::string& prefix) {
  const std::filesystem::path dir = SyncDir();
  if (dir.empty()) return 0;
  std::error_code ec;
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.path().filename().string().rfind(prefix, 0) == 0) ++count;  // starts with prefix
  }
  return count;
}

// "825FA134" or "0x825FA134" -> 0x825FA134; 0 when it isn't hex.
inline uint32_t ParseHex(const std::string& text) {
  return uint32_t(std::strtoul(text.c_str(), nullptr, 16));
}

// The timeout argument of a waiting command, in seconds (default 60).
inline int TimeoutSeconds(const std::string& text) {
  const int seconds = text.empty() ? 0 : std::atoi(text.c_str());
  return seconds > 0 ? seconds : 60;
}

// The start time of the command at the front of the queue: set the first time
// that command is looked at, cleared when it leaves the queue.
struct WaitClock {
  std::chrono::steady_clock::time_point started{};
  bool running = false;

  // Starts the clock if needed; true once `seconds` have passed since then.
  bool Expired(std::chrono::steady_clock::time_point now, int seconds) {
    if (!running) {
      running = true;
      started = now;
    }
    return now - started > std::chrono::seconds(seconds);
  }
  void Reset() { running = false; }
};

}  // namespace pd::testscript
