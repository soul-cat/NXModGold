#ifndef SALTYSD_DYNAMIC_H
#define SALTYSD_DYNAMIC_H

#include <stdint.h>

#include "useful/useful.h"

extern "C" {
	uint64_t SaltySDCore_GetSymbolAddr(void* base, char* name);
	uint64_t SaltySDCore_FindSymbol(char* name);
	uint64_t SaltySDCore_FindSymbolBuiltin(char* name);
	void SaltySDCore_RegisterModule(void* base);
	void SaltySDCore_RegisterBuiltinModule(void* base);
	void SaltySDCore_DynamicLinkModule(void* base);
	void SaltySDCore_ReplaceModuleImport(void* base, char* name, void* new_replace);
	void SaltySDCore_ReplaceImport(char* name, void* new_replace);
}

#endif // SALTYSD_DYNAMIC_H
