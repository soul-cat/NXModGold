// minizip stub：dkp-libs 仓库没有 switch-minizip 包。
// 本 overlay 不使用下载/解压功能（libultrahand README 明确该依赖可省），
// 此头仅满足 download_funcs.hpp 的无条件 include。链接时不带 -lminizip，
// 若未来真的调用 unzip API，链接器会报未定义符号提醒你补依赖。
#ifndef MINIZIP_UNZIP_STUB_H
#define MINIZIP_UNZIP_STUB_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void* unzFile;
typedef struct { uint32_t compressed_size, uncompressed_size; size_t offset; } unz_file_info_simplify_stub;
typedef unz_file_info_simplify_stub unz_file_info;
#define UNZ_OK (0)
#define UNZ_END_OF_LIST_OF_FILE (-100)
unzFile unzOpen(const char* path);
int unzClose(unzFile file);
int unzGoToNextFile(unzFile file);
int unzGetCurrentFileInfo(unzFile file, unz_file_info* info, char* name, uint16_t name_buf_size, void* e1, void* e2, void* e3, void* e4);
int unzOpenCurrentFile(unzFile file);
int unzReadCurrentFile(unzFile file, void* buf, uint32_t len);
int unzCloseCurrentFile(unzFile file);
#ifdef __cplusplus
}
#endif
#endif
