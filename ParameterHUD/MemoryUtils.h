#pragma once

#include <windows.h>
#include <psapi.h>
#include <stdexcept>
#include <string>

bool GetModuleBounds(const std::wstring name, uintptr_t* start, uintptr_t* end);

uintptr_t SigScan(const std::wstring name, const char* sig, const char* mask);

void* AllocatePageNearAddress(void* targetAddr);