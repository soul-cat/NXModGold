#include "save_import.hpp"

#include "fs_util.hpp"
#include "lib.hpp"

#include <cstdio>

namespace sshud::saveimport {

namespace {

constexpr int FIRST_SLOT = 1;
constexpr int LAST_SLOT = 4;
constexpr const char* SAVE_MOUNT = "save";
constexpr size_t PATH_CAPACITY = 96;

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

/// 返回失败步骤名；成功返回 nullptr
const char* importSlot(const SlotPaths& paths, Result& rc) {
  if (fsutil::isFile(paths.save)) {
    rc = fsutil::copyFile(paths.save, paths.backup);
    if (R_FAILED(rc)) return "backup";
  }
  rc = fsutil::copyFile(paths.pending, paths.save);
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
    if (!fsutil::isFile(paths.pending)) continue;
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
