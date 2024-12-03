
// Standard imports
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <format>
#include <stdexcept>
#include <list>
#include <map>

//Mod loader
#include <PluginAPI.h>

extern "C" {
	BOOL __declspec(dllexport) EML4_Load(PluginInfo* pluginInfo) {
		return false;
	}

	BOOL __declspec(dllexport) EML5_Load(PluginInfo* pluginInfo) {
		return false;
	}

	BOOL __declspec(dllexport) EML6_Load(PluginInfo* pluginInfo) {
		pluginInfo->infoVersion = PluginInfo::MaxInfoVer;
		pluginInfo->name = "EDF6 Game Log Output";
		pluginInfo->version = PLUG_VER(1, 0, 0, 0);
		return true;
	}
}

bool get_module_bounds(const std::wstring name, uintptr_t* start, uintptr_t* end)
{
	const auto module = GetModuleHandle(name.c_str());
	if (module == nullptr)
		return false;

	MODULEINFO info;
	GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info));
	*start = (uintptr_t)(info.lpBaseOfDll);
	*end = *start + info.SizeOfImage;
	return true;
}

// Scan for a byte pattern with a mask in the form of "xxx???xxx".
uintptr_t sigscan(const std::wstring name, const char* sig, const char* mask)
{
	uintptr_t start, end;
	if (!get_module_bounds(name, &start, &end))
		throw std::runtime_error("Module not loaded");

	const auto last_scan = end - strlen(mask) + 1;

	for (auto addr = start; addr < last_scan; addr++) {
		for (size_t i = 0;; i++) {
			if (mask[i] == '\0')
				return addr;
			if (mask[i] != '?' && sig[i] != *(char*)(addr + i))
				break;
		}
	}

	return NULL;
}

//did not work on "start event", sad
static void logSpecialCase(FILE* logFile, wchar_t* format, va_list args) {
	wchar_t buffer[1024];
	va_list args_copy;
	va_copy(args_copy, args);

	while (*format != L'\0' && *format != L'\n') {
		if (*format == L'%') {
			++format;
			if (*format == L's') {
				char* utf8_string = va_arg(args_copy, char*);
				if (utf8_string) {
					wchar_t wide_string[1024];
					int wide_length = MultiByteToWideChar(
						CP_UTF8,
						0,
						utf8_string,
						-1,
						wide_string,
						sizeof(wide_string) / sizeof(wide_string[0])
					);

					if (wide_length > 0) {
						fwprintf(logFile, L"%ls", wide_string);
					}
					else {
						fwprintf(logFile, L"(Invalid UTF-8)");
					}
				}
			}
			else {
				vswprintf(buffer, sizeof(buffer) / sizeof(wchar_t), format - 1, args);
				fwprintf(logFile, L"%ls", buffer);
			}
		}
		else {
			fputwc(*format, logFile);
		}
		++format;
	}
}

extern "C" void __fastcall createLogFile() {
	FILE* logFile = nullptr;
	errno_t err = _wfopen_s(&logFile, L"GameLog.txt", L"w");

	if (err == 0 && logFile) {
		fclose(logFile);
	}
}

extern "C" void __fastcall logFunction(wchar_t* format, ...) {
	FILE* logFile = nullptr;
	errno_t err = _wfopen_s(&logFile, L"GameLog.txt", L"a+");

	if (err == 0 && logFile) {
		va_list args;
		va_start(args, format);

		// "LoadComplete" uses special character
		if (wcsncmp(format, L"LoadComplete", 12) == 0) {
			logSpecialCase(logFile, format, args);
		}
		else {
			vfwprintf(logFile, format, args);
		}

		va_end(args);
		fclose(logFile);
	}
}

void hookLogfunction() {
	void* originalFunctionAddr = (void*)(sigscan(
		L"EDF.dll",
		"\x48\x89\x54\x24\x10\x4C\x89\x44\x24\x18\x4C\x89\x4C\x24\x20\xC3\xF3\x0F\x10\x41\x04\xF3\x0F\x58\x01\xF3\x0F\x58\x41\x08\x0F",
		"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"));

	DWORD oldProtect;
	VirtualProtect(originalFunctionAddr, 64, PAGE_EXECUTE_READWRITE, &oldProtect);
	uint8_t jmpInstruction[] = {
		0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xFF, 0xE0,   // call rax
	};

	uint64_t logFunctionAddr = (uint64_t)logFunction;
	memcpy(&jmpInstruction[2], &logFunctionAddr, sizeof(logFunctionAddr));

	memcpy(originalFunctionAddr, jmpInstruction, sizeof(jmpInstruction));

	VirtualProtect(originalFunctionAddr, 64, oldProtect, &oldProtect);
}

int WINAPI main()
{
	createLogFile();
	hookLogfunction();
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		DisableThreadLibraryCalls(hModule);
		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)main, NULL, NULL, NULL);
	}
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
