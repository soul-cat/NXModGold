#include "save_export.hpp"

#include "fs_util.hpp"
#include "lib.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sshud::saveexport {

namespace {

constexpr const char* REQUEST_PATH = "sd:/config/sshud/export.request";
constexpr const char* EXPORT_ROOT = "sd:/config/sshud/export";
constexpr const char* SAVE_ROOT = "save:/";
constexpr int MAX_EXPORT_INDEX = 999;
constexpr int MAX_DEPTH = 2;  // save:/ 与其下一层（Restore_Points1 等）
constexpr size_t PATH_CAPACITY = 256;
constexpr size_t ENTRY_NAME_CAPACITY = 0x301;
constexpr int8_t ENTRY_TYPE_DIRECTORY = 0;

/// nn::fs::DirectoryEntry 的真实布局（0x310 字节，同 libnx FsDirectoryEntry）；
/// exlaunch 头文件里的定义多了 8 字节，不能直接拿来当 ReadDirectory 的缓冲
struct RawDirectoryEntry {
  char name[ENTRY_NAME_CAPACITY];
  uint8_t padding0[3];
  int8_t type;
  uint8_t padding1[3];
  int64_t size;
};
static_assert(sizeof(RawDirectoryEntry) == 0x310);

struct ExportStats {
  int files = 0;
  int failures = 0;
  int64_t bytes = 0;
};

/// 拼路径；超长返回 false
bool joinPath(char* out, const char* dir, const char* name) {
  const size_t length = std::strlen(dir);
  const bool hasSlash = length > 0 && dir[length - 1] == '/';
  const int written = std::snprintf(out, PATH_CAPACITY, "%s%s%s", dir, hasSlash ? "" : "/", name);
  return written >= 0 && static_cast<size_t>(written) < PATH_CAPACITY;
}

void exportDirectory(const char* from, const char* to, int depth, ExportStats& stats);

void exportEntry(const RawDirectoryEntry& entry, const char* from, const char* to, int depth, ExportStats& stats) {
  char source[PATH_CAPACITY];
  char target[PATH_CAPACITY];
  if (!joinPath(source, from, entry.name) || !joinPath(target, to, entry.name)) {
    ++stats.failures;
    Logging.Log("[sshud] export: path too long under %s", from);
    return;
  }
  if (entry.type == ENTRY_TYPE_DIRECTORY) {
    if (depth + 1 < MAX_DEPTH) exportDirectory(source, target, depth + 1, stats);
    return;
  }
  const Result rc = fsutil::copyFile(source, target);
  if (R_FAILED(rc)) {
    ++stats.failures;
    Logging.Log("[sshud] export %s failed (0x%x)", source, rc);
    return;
  }
  ++stats.files;
  stats.bytes += entry.size;
  Logging.Log("[sshud] export %s (%ld bytes)", source, static_cast<long>(entry.size));
}

/// 逐条读目录（每次 1 条），文件直接复制，子目录递归
void exportDirectory(const char* from, const char* to, int depth, ExportStats& stats) {
  nn::fs::CreateDirectory(to);
  nn::fs::DirectoryHandle handle{};
  const Result rc = nn::fs::OpenDirectory(&handle, from, nn::fs::OpenDirectoryMode_All);
  if (R_FAILED(rc)) {
    ++stats.failures;
    Logging.Log("[sshud] export: OpenDirectory %s failed (0x%x)", from, rc);
    return;
  }
  RawDirectoryEntry entry{};
  s64 count = 0;
  while (R_SUCCEEDED(nn::fs::ReadDirectory(&count, reinterpret_cast<nn::fs::DirectoryEntry*>(&entry), handle, 1)) &&
         count == 1) {
    exportEntry(entry, from, to, depth, stats);
  }
  nn::fs::CloseDirectory(handle);
}

/// 第一个还不存在的 export/<n>；都占满返回 false
bool pickTarget(char* out) {
  for (int index = 1; index <= MAX_EXPORT_INDEX; ++index) {
    std::snprintf(out, PATH_CAPACITY, "%s/%d", EXPORT_ROOT, index);
    if (!fsutil::isDirectory(out)) return true;
  }
  return false;
}

}  // namespace

void runOnce() {
  static bool s_hasRun = false;
  if (s_hasRun) return;
  s_hasRun = true;
  if (!fsutil::isFile(REQUEST_PATH)) return;
  char target[PATH_CAPACITY];
  nn::fs::CreateDirectory(EXPORT_ROOT);
  if (!pickTarget(target)) {
    Logging.Log("[sshud] save export: no free slot under %s", EXPORT_ROOT);
    return;
  }
  ExportStats stats;
  exportDirectory(SAVE_ROOT, target, 0, stats);
  nn::fs::DeleteFile(REQUEST_PATH);
  Logging.Log("[sshud] save export done: %d files, %ld bytes, %d failures -> %s", stats.files,
              static_cast<long>(stats.bytes), stats.failures, target);
}

}  // namespace sshud::saveexport
