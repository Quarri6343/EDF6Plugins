
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

#include "MinHook.h"

#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <fnt.h>

uintptr_t baseAddress;
HWND hwnd = nullptr;
ID3D11Device* d3dDevice = nullptr;
ID3D11DeviceContext* d3dContext = nullptr;
bool swapChainOccluded = false;
HHOOK messageHook = NULL;
bool init = false;
bool done = false;
ImGuiIO* io = nullptr;

float DEFAULT_POSITION_POS_X = 50.0f;
float DEFAULT_POSITION_POS_Y = 900.0f;
float FIXED_FONT_SIZE = 38.0f;

void mainLoop();

typedef HRESULT(WINAPI* D3D11CreateDeviceFn)(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext);

D3D11CreateDeviceFn oD3D11CreateDevice = nullptr;

HRESULT WINAPI hkD3D11CreateDevice(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	std::cout << "D3D11CreateDevice called!" << std::endl;

	HRESULT hr = oD3D11CreateDevice(
		pAdapter,
		DriverType,
		Software,
		Flags,
		pFeatureLevels,
		FeatureLevels,
		SDKVersion,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	if (SUCCEEDED(hr)) {
		if (ppDevice && *ppDevice) {
			d3dDevice = *ppDevice;
			d3dDevice->AddRef();
		}
		if (ppImmediateContext && *ppImmediateContext) {
			d3dContext = *ppImmediateContext;
			d3dContext->AddRef();
		}
	}

	return hr;
}

using UpdateRadarFuncType = void(__fastcall*)(__int64, __int64);
UpdateRadarFuncType oUpdateRadar = nullptr;

void __fastcall hkUpdateRadar(__int64 a1, __int64 a2)
{
	oUpdateRadar(a1, a2);

	mainLoop();
}

void initDeviceHook() {
	HMODULE hD3D11 = GetModuleHandle(L"d3d11.dll");
	if (!hD3D11) {
		std::cerr << "Failed to get d3d11.dll module handle!" << std::endl;
		return;
	}

	void* pD3D11CreateDevice = GetProcAddress(hD3D11, "D3D11CreateDevice");
	if (!pD3D11CreateDevice) {
		std::cerr << "Failed to get D3D11CreateDevice address!" << std::endl;
		return;
	}

	if (MH_CreateHook(pD3D11CreateDevice, &hkD3D11CreateDevice, reinterpret_cast<LPVOID*>(&oD3D11CreateDevice)) != MH_OK) {
		std::cerr << "Failed to create hook for D3D11CreateDevice!" << std::endl;
		return;
	}

	if (MH_EnableHook(pD3D11CreateDevice) != MH_OK) {
		std::cerr << "Failed to enable hook for D3D11CreateDevice!" << std::endl;
		return;
	}
}

void unInitDeviceHook() {
	if (d3dDevice) {
		d3dDevice->Release();
		d3dDevice = nullptr;
	}
	if (d3dContext) {
		d3dContext->Release();
		d3dContext = nullptr;
	}
}

void initRadarHook() {
	void* pUpdateRadar = reinterpret_cast<LPVOID>(baseAddress + 0x82B1F0);

	if (MH_CreateHook(pUpdateRadar, &hkUpdateRadar, reinterpret_cast<LPVOID*>(&oUpdateRadar)) != MH_OK) {
		std::cerr << "Failed to create hook for UpdateRadar!" << std::endl;
		return;
	}

	if (MH_EnableHook(pUpdateRadar) != MH_OK) {
		std::cerr << "Failed to enable hook for UpdateRadar!" << std::endl;
		return;
	}
}

uintptr_t GetPointerAddress(const uintptr_t base, std::initializer_list<int> offsets) {
	uintptr_t out = base;
	const int* it = offsets.begin();
	for (int i = 0; i < offsets.size(); i++) {
		out = *(uintptr_t*)(out + *(it + i));
		if (out == 0) {
			return 0;
		}
	}
	return out;
}

extern "C" {
	void recordPos();
	float xPos;
	float yPos;
	float zPos;
	uintptr_t hookRetAddress;
	uintptr_t loggingAddress;

	BOOL __declspec(dllexport) EML4_Load(PluginInfo* pluginInfo) {
		return false;
	}

	BOOL __declspec(dllexport) EML5_Load(PluginInfo* pluginInfo) {
		return false;
	}

	BOOL __declspec(dllexport) EML6_Load(PluginInfo* pluginInfo) {
		pluginInfo->infoVersion = PluginInfo::MaxInfoVer;
		pluginInfo->name = "Parameter HUD";
		pluginInfo->version = PLUG_VER(1, 0, 0, 0);
		return true;
	}
}

void DrawTextWithBackground(ImVec4 textColor, ImVec4 bgColor, const char* fmt...) {
	va_list args;
	va_start(args, fmt);
	char buffer[256];
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	ImVec2 text_size = ImGui::CalcTextSize(buffer);
	ImVec2 pos = ImGui::GetCursorScreenPos();

	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddRectFilled(pos, ImVec2(pos.x + text_size.x, pos.y + text_size.y), ImColor(bgColor));
	ImGui::TextColored(textColor, "%s", buffer);
}

void mainLoop() {
	if (!init || done)
		return;

	// Handle window resize (we don't resize directly in the WM_SIZE handler)
	//if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
	//{
	//	CleanupRenderTarget();
	//	g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
	//	g_ResizeWidth = g_ResizeHeight = 0;
	//	CreateRenderTarget();
	//}

	// Start the Dear ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
	//ImGui::ShowDemoWindow();

	//Our main window
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImVec2 defaultPos = ImVec2(DEFAULT_POSITION_POS_X, DEFAULT_POSITION_POS_Y);
	ImGui::SetNextWindowPos(defaultPos, ImGuiCond_Once);
	ImGui::Begin("EDF hook", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus);
	ImDrawList* dl = ImGui::GetWindowDrawList();

	ImVec4 yellow = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	ImVec4 black = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);

	DrawTextWithBackground(yellow, black, "Location = %.1f, %.1f, %.1f", xPos, yPos, zPos);
	DrawTextWithBackground(yellow, black, "Ctrl+C to copy...");
	DrawTextWithBackground(yellow, black, "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io->Framerate, io->Framerate);
	ImGui::End();

	// Rendering
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void mainProcess() {
	if (MH_Initialize() != MH_OK) {
		return;
	}
	initDeviceHook();
	initRadarHook();
	while (d3dDevice == nullptr || d3dContext == nullptr) {
		Sleep(1000);
	}

	//EDF.dll + 0x21360E8
	HWND* hwndPTR = reinterpret_cast<HWND*>(baseAddress + 0x21360E8);
	while (*hwndPTR == nullptr) {
		Sleep(1000);
	}
	hwnd = *hwndPTR;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	io = &ImGui::GetIO(); (void)io;

	ImGui::StyleColorsDark();

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(d3dDevice, d3dContext);

	io->Fonts->AddFontFromMemoryCompressedTTF(robotomono_compressed_data, robotomono_compressed_size, FIXED_FONT_SIZE);

	// enable Main loop
	init = true;
	loggingAddress = (uintptr_t)mainLoop;
}

void endProcess() {
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	unInitDeviceHook();
	MH_DisableHook(MH_ALL_HOOKS);
	MH_Uninitialize();
}

LRESULT CALLBACK MessageProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		MSG* msg = (MSG*)lParam;
		if (msg->message == WM_QUIT && done == false) {
			done = true;
			endProcess();
		}
	}
	return CallNextHookEx(messageHook, nCode, wParam, lParam);
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

void* AllocatePageNearAddress(void* targetAddr)
{
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	const uint64_t PAGE_SIZE = sysInfo.dwPageSize;

	uint64_t startAddr = (uint64_t(targetAddr) & ~(PAGE_SIZE - 1)); //round down to nearest page boundary
	uint64_t minAddr = min(startAddr - 0x7FFFFF00, (uint64_t)sysInfo.lpMinimumApplicationAddress);
	uint64_t maxAddr = max(startAddr + 0x7FFFFF00, (uint64_t)sysInfo.lpMaximumApplicationAddress);

	uint64_t startPage = (startAddr - (startAddr % PAGE_SIZE));

	uint64_t pageOffset = 1;
	while (1)
	{
		uint64_t byteOffset = pageOffset * PAGE_SIZE;
		uint64_t highAddr = startPage + byteOffset;
		uint64_t lowAddr = (startPage > byteOffset) ? startPage - byteOffset : 0;

		bool needsExit = highAddr > maxAddr && lowAddr < minAddr;

		if (highAddr < maxAddr)
		{
			void* outAddr = VirtualAlloc((void*)highAddr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (outAddr)
				return outAddr;
		}

		if (lowAddr > minAddr)
		{
			void* outAddr = VirtualAlloc((void*)lowAddr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (outAddr != nullptr)
				return outAddr;
		}

		pageOffset++;

		if (needsExit)
		{
			break;
		}
	}

	return nullptr;
}


extern "C" void __fastcall createLogFile() {
	FILE* logFile = nullptr;
	errno_t err = _wfopen_s(&logFile, L"HUDLog.txt", L"w");

	if (err == 0 && logFile) {
		fclose(logFile);
	}
}

extern "C" void __fastcall logPosition(float x, float y, float z) {
	FILE* logFile = nullptr;
	errno_t err = _wfopen_s(&logFile, L"HUDLog.txt", L"a+");

	if (err == 0 && logFile) {
		fwprintf(logFile, L"%f, %f, %f\n", x, y, z);

		fclose(logFile);
	}
}

void hookGetPosFunction() {
	void* originalFunctionAddr = (void*)(sigscan(
		L"EDF.dll",
		"\x0F\x10\x86\x90\x00\x00\x00\x66\x0F\x7F\x44\x24\x50", //hooks Radar HUD update
		"xxxxxxxxxxxxx"));
	hookRetAddress = (uint64_t)originalFunctionAddr + 0x7;

	void* memoryBlock = AllocatePageNearAddress(originalFunctionAddr);


	uint8_t hookFunction[] =
	{
		0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //mov rax, addr
		0xFF, 0xE0 //jmp rax
	};
	uint64_t addrToJumpTo64 = (uint64_t)recordPos; //Hook Function

	memcpy(&hookFunction[2], &addrToJumpTo64, sizeof(addrToJumpTo64));
	memcpy(memoryBlock, hookFunction, sizeof(hookFunction));


	DWORD oldProtect;
	VirtualProtect(originalFunctionAddr, 1024, PAGE_EXECUTE_READWRITE, &oldProtect);
	uint8_t jmpInstruction[7] = { 0xE9, 0x0, 0x0, 0x0, 0x0, 0x90, 0x90 };


	const uint64_t relAddr = (uint64_t)memoryBlock - ((uint64_t)originalFunctionAddr + 5); //sizeof(jmpInstruction)
	memcpy(jmpInstruction + 1, &relAddr, 4);

	memcpy(originalFunctionAddr, jmpInstruction, sizeof(jmpInstruction));

	//loggingAddress = (uintptr_t)logPosition;
}

bool IsCtrlCPressed() {
	bool ctrlPressed = GetAsyncKeyState(VK_CONTROL) & 0x8000;
	bool cPressed = GetAsyncKeyState('C') & 0x8000;
	return ctrlPressed && cPressed;
}

void CopyToClipboard(const std::wstring& text) {
	if (OpenClipboard(nullptr)) {
		EmptyClipboard();

		size_t size = (text.size() + 1) * sizeof(wchar_t);
		HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
		
		if (hGlobal != 0) {
			wchar_t* clipboardText = (wchar_t*)GlobalLock(hGlobal);
			if (clipboardText != 0) {
				wcscpy_s(clipboardText, size / sizeof(wchar_t), text.c_str());
				SetClipboardData(CF_UNICODETEXT, hGlobal);
			}
			GlobalUnlock(hGlobal);
		}
		CloseClipboard();
	}
}

void MonitorKeys() {
	while (!done) {
		if (IsCtrlCPressed() && xPos != 0 && yPos != 0 && zPos != 0) {
			char buffer[100];
			sprintf_s(buffer, sizeof(buffer), "[ %.3f, %.3f, %.3f ]\n", xPos, yPos, zPos);

			std::wstring text(buffer, buffer + strlen(buffer));
			CopyToClipboard(text);
		}

		Sleep(10);
	}
}

int WINAPI main()
{
	baseAddress = (uintptr_t)GetModuleHandle(L"EDF.dll");
	hookGetPosFunction();
	//createLogFile();
	mainProcess();
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
		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)MonitorKeys, NULL, NULL, NULL);
		messageHook = SetWindowsHookEx(WH_MSGFILTER, MessageProc, hModule, 0);
	}
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH: 
	{
		UnhookWindowsHookEx(messageHook);
		messageHook = NULL;
	}
		break;
	}
	return TRUE;
}
