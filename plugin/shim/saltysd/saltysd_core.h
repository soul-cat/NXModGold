#ifndef SALTYSD_CORE_H
#define SALTYSD_CORE_H

#include <switch_min.h>

#include "useful/useful.h"

extern "C" {
	u64 SaltySDCore_getCodeStart();
	u64 SaltySDCore_getCodeSize();
	u64 SaltySDCore_findCode(u8* code, size_t size);
	FILE* SaltySDCore_fopen(const char* filename, const char* mode);
	size_t SaltySDCore_fread(void* ptr, size_t size, size_t count, FILE* stream);
	int SaltySDCore_fclose(FILE* stream);
	int SaltySDCore_mkdir(const char* dirname, mode_t mode);
	size_t SaltySDCore_fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
	extern int SaltySDCore_remove(const char* filename);
	Result SaltySDCore_printf(const char* format, ...);
	int SaltySDCore_fseek(FILE* stream, int64_t offset, int origin);
	int64_t SaltySDCore_ftell(FILE* stream);
}

#endif // SALTYSD_CORE_H
