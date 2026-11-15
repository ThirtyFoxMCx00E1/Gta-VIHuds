#pragma once
#include <dlfcn.h>
#include <stdint.h>
typedef void* HMODULE; typedef unsigned long DWORD;
static inline HMODULE GetModuleHandleA(const char*) { return 0; }
static inline void* GetProcAddress(HMODULE, const char*) { return 0; }
