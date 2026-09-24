#include "fs_util.hpp"

#include <algorithm>

namespace sshud::fsutil {

namespace {

constexpr size_t COPY_CHUNK_SIZE = 16 * 1024;

char s_chunk[COPY_CHUNK_SIZE];

bool hasEntryType(const char* path, nn::fs::DirectoryEntryType expected) {
  nn::fs::DirectoryEntryType type{};
  return R_SUCCEEDED(nn::fs::GetEntryType(&type, path)) && type == expected;
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

}  // namespace

bool isFile(const char* path) { return hasEntryType(path, nn::fs::DirectoryEntryType_File); }

bool isDirectory(const char* path) { return hasEntryType(path, nn::fs::DirectoryEntryType_Directory); }

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

}  // namespace sshud::fsutil
