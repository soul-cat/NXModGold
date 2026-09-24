#include "save_import.hpp"

#include "lib.hpp"

#include <lib/nx/result.h>
#include <nn/fs.hpp>

#include <algorithm>
#include <cstdio>

namespace nn::fs {

Result DeleteFile(char const* path);
Result RenameFile(char const* from, char const* to);
Result CommitSaveData(char const* mountName);
Result GetEntryType(DirectoryEntryType* outType, char const* path);

}  // namespace nn::fs

namespace sshud::saveimport {

namespace {

constexpr int FIRST_SLOT = 2;
constexpr int LAST_SLOT = 4;
constexpr const char* SAVE_MOUNT = "save";
constexpr size_t PATH_CAPACITY = 96;
constexpr size_t COPY_CHUNK_SIZE = 16 * 1024;

char s_chunk[COPY_CHUNK_SIZE];

struct SlotPaths {
  char pending[PATH_CAPACITY];
  char done[PATH_CAPACITY];
  char backup[PATH_CAPACITY];
  char save[PATH_CAPACITY];
};

SlotPaths pathsFor(int slot) {
  SlotPaths paths{};
  std::snprintf(paths.pending, PATH_CAPACITY, "sd:/config/sshud/import/user%d.dat", slot);
  std::snprintf(paths.done, PATH_CAPACITY, "sd:/config/sshud/import/user%d.dat.done", slot);
  std::snprintf(paths.backup, PATH_CAPACITY, "sd:/config/sshud/import/backup-user%d.dat", slot);
  std::snprintf(paths.save, PATH_CAPACITY, "%s:/user%d.dat", SAVE_MOUNT, slot);
  return paths;
}

bool exists(const char* path) {
  nn::fs::DirectoryEntryType type{};
  return R_SUCCEEDED(nn::fs::GetEntryType(&type, path)) && type == nn::fs::DirectoryEntryType_File;
}

/// 按块把已打开的 src 写进新建的 to（先删后建，大小一致）
Result writeCopy(nn::fs::FileHandle src, long size, const char* to) {
  nn::fs::DeleteFile(to);
  Result rc = nn::fs::CreateFile(to, size);
  if (R_FAILED(rc)) return rc;
  nn::fs::FileHandle dst{};
  rc = nn::fs::OpenFile(&dst, to, nn::fs::OpenMode_Write);
  if (R_FAILED(rc)) return rc;
  for (long offset = 0; offset < size && R_SUCCEEDED(rc); offset += COPY_CHUNK_SIZE) {
    const auto length = static_cast<size_t>(std::min<long>(COPY_CHUNK_SIZE, size - offset));
    rc = nn::fs::ReadFile(src, offset, s_chunk, length);
    if (R_SUCCEEDED(rc)) rc = nn::fs::WriteFile(dst, offset, s_chunk, length, nn::fs::WriteOption::CreateOption(0));
  }
  if (R_SUCCEEDED(rc)) rc = nn::fs::FlushFile(dst);
  nn::fs::CloseFile(dst);
  return rc;
}

Result copyFile(const char* from, const char* to) {
  nn::fs::FileHandle src{};
  Result rc = nn::fs::OpenFile(&src, from, nn::fs::OpenMode_Read);
  if (R_FAILED(rc)) return rc;
  long size = 0;
  rc = nn::fs::GetFileSize(&size, src);
  if (R_SUCCEEDED(rc)) rc = writeCopy(src, size, to);
  nn::fs::CloseFile(src);
  return rc;
}

/// 返回失败步骤名；成功返回 nullptr
const char* importSlot(const SlotPaths& paths, Result& rc) {
  if (exists(paths.save)) {
    rc = copyFile(paths.save, paths.backup);
    if (R_FAILED(rc)) return "backup";
  }
  rc = copyFile(paths.pending, paths.save);
  if (R_FAILED(rc)) return "copy";
  rc = nn::fs::CommitSaveData(SAVE_MOUNT);
  if (R_FAILED(rc)) return "commit";
  nn::fs::DeleteFile(paths.done);
  rc = nn::fs::RenameFile(paths.pending, paths.done);
  return R_FAILED(rc) ? "rename" : nullptr;
}

}  // namespace

void runOnce() {
  static bool s_hasRun = false;
  if (s_hasRun) return;
  s_hasRun = true;
  for (int slot = FIRST_SLOT; slot <= LAST_SLOT; ++slot) {
    const SlotPaths paths = pathsFor(slot);
    if (!exists(paths.pending)) continue;
    Result rc = 0;
    const char* failedStep = importSlot(paths, rc);
    if (failedStep != nullptr) {
      Logging.Log("[sshud] save import slot %d failed at %s (0x%x)", slot, failedStep, rc);
    } else {
      Logging.Log("[sshud] save import slot %d done (old save backed up to %s)", slot, paths.backup);
    }
  }
}

}  // namespace sshud::saveimport
