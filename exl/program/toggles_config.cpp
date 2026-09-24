#include "toggles_config.hpp"

#include "lib.hpp"

#include <lib/nx/result.h>
#include <nn/fs.hpp>

#include <algorithm>
#include <cstdio>

namespace nn::fs {

Result DeleteFile(char const* path);
Result GetEntryType(DirectoryEntryType* outType, char const* path);

}  // namespace nn::fs

namespace sshud::toggles {

namespace {

constexpr const char* CONFIG_DIR = "sd:/config";
constexpr const char* SSHUD_DIR = "sd:/config/sshud";
constexpr const char* CONFIG_PATH = "sd:/config/sshud/config.ini";
constexpr size_t SUMMARY_CAPACITY = 128;

void logFailure(const char* step, Result rc) { Logging.Log("[sshud] config: %s failed (0x%x)", step, rc); }

bool configExists() {
  nn::fs::DirectoryEntryType type{};
  return R_SUCCEEDED(nn::fs::GetEntryType(&type, CONFIG_PATH)) && type == nn::fs::DirectoryEntryType_File;
}

/// 读整个文件（最多 capacity - 1 字节）；返回失败步骤名，成功返回 nullptr
const char* readConfig(char* text, size_t capacity, size_t& length, Result& rc) {
  nn::fs::FileHandle file{};
  rc = nn::fs::OpenFile(&file, CONFIG_PATH, nn::fs::OpenMode_Read);
  if (R_FAILED(rc)) return "OpenFile";
  long size = 0;
  rc = nn::fs::GetFileSize(&size, file);
  const char* failedStep = "GetFileSize";
  if (R_SUCCEEDED(rc)) {
    length = std::min(static_cast<size_t>(std::max(size, 0L)), capacity - 1);
    rc = nn::fs::ReadFile(file, 0, text, length);
    failedStep = "ReadFile";
  }
  nn::fs::CloseFile(file);
  return R_FAILED(rc) ? failedStep : nullptr;
}

/// 目录已存在时 CreateDirectory 报错，忽略即可；返回失败步骤名，成功返回 nullptr
const char* writeConfig(const char* text, size_t length, Result& rc) {
  nn::fs::CreateDirectory(CONFIG_DIR);
  nn::fs::CreateDirectory(SSHUD_DIR);
  nn::fs::DeleteFile(CONFIG_PATH);
  rc = nn::fs::CreateFile(CONFIG_PATH, static_cast<s64>(length));
  if (R_FAILED(rc)) return "CreateFile";
  nn::fs::FileHandle file{};
  rc = nn::fs::OpenFile(&file, CONFIG_PATH, nn::fs::OpenMode_Write);
  if (R_FAILED(rc)) return "OpenFile";
  rc = nn::fs::WriteFile(file, 0, text, length, nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
  nn::fs::CloseFile(file);
  return R_FAILED(rc) ? "WriteFile" : nullptr;
}

/// 一行汇总，如 "enemy_bars=1 boss_bar=1 damage_numbers=1 map_markers=0"
void logSettings(const Settings& settings) {
  char summary[SUMMARY_CAPACITY];
  size_t length = 0;
  summary[0] = '\0';
  for (size_t i = 0; i < FEATURE_COUNT; ++i) {
    const int written = std::snprintf(summary + length, SUMMARY_CAPACITY - length, "%s%s=%d", i == 0 ? "" : " ",
                                      FEATURES[i].key, settings.enabled[i] ? 1 : 0);
    if (written > 0) length += std::min(static_cast<size_t>(written), SUMMARY_CAPACITY - length - 1);
  }
  Logging.Log("[sshud] config: %s", summary);
}

}  // namespace

void loadSettings(Settings& settings) {
  if (!configExists()) {
    saveSettings(settings);
    Logging.Log("[sshud] config: created default");
    return;
  }
  char text[CONFIG_TEXT_CAPACITY];
  size_t length = 0;
  Result rc = 0;
  const char* failedStep = readConfig(text, sizeof(text), length, rc);
  if (failedStep != nullptr) return logFailure(failedStep, rc);
  parseSettings(text, length, settings);
  logSettings(settings);
}

void saveSettings(const Settings& settings) {
  char text[CONFIG_TEXT_CAPACITY];
  const size_t length = formatSettings(settings, text, sizeof(text));
  Result rc = 0;
  const char* failedStep = writeConfig(text, length, rc);
  if (failedStep != nullptr) logFailure(failedStep, rc);
}

}  // namespace sshud::toggles
