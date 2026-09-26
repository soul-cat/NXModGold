#include "sd_logger.hpp"

#include <lib/log/svc_logger.hpp>
#include <lib/nx/result.h>
#include <nn/fs.hpp>

#include <algorithm>
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
constexpr float FLUSH_INTERVAL_SECONDS = 2.0f;

enum class State : uint8_t { Buffering, Writing, Failed };

State s_state = State::Buffering;
char s_pending[PENDING_CAPACITY];
size_t s_pendingLength = 0;
int64_t s_offset = 0;
float s_lastFlushTime = 0.0f;

/// 直接走 svc 日志报错，避免递归进 SdLogger
void reportFailure(const char* step, Result rc) {
  s_state = State::Failed;
  char line[FAILURE_LINE_SIZE];
  const int written = std::snprintf(line, sizeof(line), "[sshud] sd log disabled: %s failed (0x%x)", step, rc);
  const size_t length = std::min(static_cast<size_t>(std::max(written, 0)), sizeof(line) - 1);
  exl::log::SvcLogger{}.LogRaw(std::string_view{line, length});
}

/// 缓冲满了就丢弃这一行（Buffering 期间只会在 start() 迟迟没被调用时发生）
void bufferLine(std::string_view text) {
  if (PENDING_CAPACITY - s_pendingLength < text.size() + 1) return;
  std::memcpy(s_pending + s_pendingLength, text.data(), text.size());
  s_pendingLength += text.size();
  s_pending[s_pendingLength++] = '\n';
}

/// 每次写都开 → 写 → 关：文件不被游戏一直占着，游戏运行中也能远程读日志
bool writeBytes(const void* data, size_t size) {
  nn::fs::FileHandle file{};
  Result rc = nn::fs::OpenFile(&file, LOG_PATH, nn::fs::OpenMode_Write | nn::fs::OpenMode_Append);
  if (R_SUCCEEDED(rc)) {
    rc = nn::fs::WriteFile(file, s_offset, data, size,
                           nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
    nn::fs::CloseFile(file);
  }
  if (R_FAILED(rc)) {
    reportFailure("OpenFile/WriteFile", rc);
    return false;
  }
  s_offset += static_cast<int64_t>(size);
  return true;
}

/// 整个缓冲一次写出（每次 SD 写入都要几毫秒，攒批写避免逐行卡主线程）
void flushPending() {
  if (s_pendingLength == 0) return;
  if (writeBytes(s_pending, s_pendingLength)) s_pendingLength = 0;
}

/// 放不下就先把缓冲写出再追加
void appendLine(std::string_view text) {
  if (PENDING_CAPACITY - s_pendingLength < text.size() + 1) flushPending();
  if (s_state == State::Writing) bufferLine(text);
}

/// 每次启动一份新日志；目录已存在时 CreateDirectory 报错，忽略即可
Result openFreshLog() {
  nn::fs::CreateDirectory(CONFIG_DIR);
  nn::fs::CreateDirectory(LOG_DIR);
  nn::fs::DeleteFile(LOG_PATH);
  return nn::fs::CreateFile(LOG_PATH, 0);
}

}  // namespace

void SdLogger::LogRaw(std::string_view text) {
  if (s_state == State::Buffering) {
    bufferLine(text);
  } else if (s_state == State::Writing) {
    appendLine(text);
  }
}

namespace sdlog {

void start() {
  if (s_state != State::Buffering) return;
  Result rc = nn::fs::MountSdCardForDebug(MOUNT_NAME);
  if (R_FAILED(rc)) return reportFailure("MountSdCardForDebug", rc);
  rc = openFreshLog();
  if (R_FAILED(rc)) return reportFailure("CreateFile", rc);
  s_state = State::Writing;
  flushPending();
}

void flushIfDue(float now) {
  if (s_state != State::Writing || s_pendingLength == 0) return;
  if (now - s_lastFlushTime < FLUSH_INTERVAL_SECONDS) return;
  s_lastFlushTime = now;
  flushPending();
}

void flushNow() {
  if (s_state == State::Writing) flushPending();
}

}  // namespace sdlog

}  // namespace sshud
