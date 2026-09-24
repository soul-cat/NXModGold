#include "sd_logger.hpp"

#include <lib/log/svc_logger.hpp>
#include <lib/nx/result.h>
#include <nn/fs.hpp>

#include <cstdio>
#include <cstring>

namespace nn::fs {

Result DeleteFile(char const* path);

}  // namespace nn::fs

namespace sshud {

namespace {

constexpr const char* MOUNT_NAME = "sd";
constexpr const char* CONFIG_DIR = "sd:/config";
constexpr const char* LOG_DIR = "sd:/config/sshud";
constexpr const char* LOG_PATH = "sd:/config/sshud/log.txt";
constexpr size_t PENDING_CAPACITY = 16 * 1024;
constexpr size_t FAILURE_LINE_SIZE = 128;

enum class State : uint8_t { Buffering, Writing, Failed };

State s_state = State::Buffering;
char s_pending[PENDING_CAPACITY];
size_t s_pendingLength = 0;
nn::fs::FileHandle s_file{};
int64_t s_offset = 0;

/// 直接走 svc 日志报错，避免递归进 SdLogger
void reportFailure(const char* step, Result rc) {
  s_state = State::Failed;
  char line[FAILURE_LINE_SIZE];
  const int length = std::snprintf(line, sizeof(line), "[sshud] sd log disabled: %s failed (0x%x)", step, rc);
  exl::log::SvcLogger{}.LogRaw(std::string_view{line, static_cast<size_t>(length)});
}

/// 缓冲满了就丢弃后续行（只会在 start() 迟迟没被调用时发生）
void bufferLine(std::string_view text) {
  if (PENDING_CAPACITY - s_pendingLength < text.size() + 1) return;
  std::memcpy(s_pending + s_pendingLength, text.data(), text.size());
  s_pendingLength += text.size();
  s_pending[s_pendingLength++] = '\n';
}

bool writeBytes(const void* data, size_t size) {
  const Result rc = nn::fs::WriteFile(s_file, s_offset, data, size,
                                      nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
  if (R_FAILED(rc)) {
    reportFailure("WriteFile", rc);
    return false;
  }
  s_offset += static_cast<int64_t>(size);
  return true;
}

/// 每次启动一份新日志；目录已存在时 CreateDirectory 报错，忽略即可
Result openFreshLog() {
  nn::fs::CreateDirectory(CONFIG_DIR);
  nn::fs::CreateDirectory(LOG_DIR);
  nn::fs::DeleteFile(LOG_PATH);
  const Result rc = nn::fs::CreateFile(LOG_PATH, 0);
  if (R_FAILED(rc)) return rc;
  return nn::fs::OpenFile(&s_file, LOG_PATH, nn::fs::OpenMode_Write | nn::fs::OpenMode_Append);
}

}  // namespace

void SdLogger::LogRaw(std::string_view text) {
  if (s_state == State::Buffering) {
    bufferLine(text);
  } else if (s_state == State::Writing && writeBytes(text.data(), text.size())) {
    writeBytes("\n", 1);
  }
}

namespace sdlog {

void start() {
  if (s_state != State::Buffering) return;
  Result rc = nn::fs::MountSdCardForDebug(MOUNT_NAME);
  if (R_FAILED(rc)) return reportFailure("MountSdCardForDebug", rc);
  rc = openFreshLog();
  if (R_FAILED(rc)) return reportFailure("CreateFile/OpenFile", rc);
  s_state = State::Writing;
  if (writeBytes(s_pending, s_pendingLength)) s_pendingLength = 0;
}

}  // namespace sdlog

}  // namespace sshud
