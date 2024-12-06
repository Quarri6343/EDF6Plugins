
#include "ParameterHUD.h"
#include "MemoryUtils.h"
#include "FunctionHooks.h"

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

//Libraries
#include <MinHook.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <fnt.h>

extern uintptr_t hedfDLL;
extern ID3D11Device* d3dDevice;
extern ID3D11DeviceContext* d3dContext;

HWND hwnd = nullptr;
HHOOK messageHook = NULL;
bool init = false;
bool done = false;
ImGuiIO* io = nullptr;

float DEFAULT_POSITION_POS_X = 50.0f;
float DEFAULT_POSITION_POS_Y = 900.0f;
float FIXED_FONT_SIZE = 38.0f;

extern "C" {
	void RecordPos();
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

	ImVec2 textSize = ImGui::CalcTextSize(buffer);
	ImVec2 pos = ImGui::GetCursorScreenPos();

	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddRectFilled(pos, ImVec2(pos.x + textSize.x, pos.y + textSize.y), ImColor(bgColor));
	ImGui::TextColored(textColor, "%s", buffer);
}

void MainLoop() {
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

void MainProcess() {
	if (MH_Initialize() != MH_OK) {
		return;
	}
	InitDeviceHook();
	InitRadarHook();
	while (d3dDevice == nullptr || d3dContext == nullptr) {
		Sleep(1000);
	}

	//EDF.dll + 0x21360E8
	HWND* hwndPTR = reinterpret_cast<HWND*>(hedfDLL + 0x21360E8);
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
	loggingAddress = (uintptr_t)MainLoop;
}

void EndProcess() {
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	UnInitDeviceHook();
	MH_DisableHook(MH_ALL_HOOKS);
	MH_Uninitialize();
}

LRESULT CALLBACK MessageProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		MSG* msg = (MSG*)lParam;
		if (msg->message == WM_QUIT && done == false) {
			done = true;
			EndProcess();
		}
	}
	return CallNextHookEx(messageHook, nCode, wParam, lParam);
}


extern "C" void __fastcall CreateLogFile() {
	FILE* logFile = nullptr;
	errno_t err = _wfopen_s(&logFile, L"HUDLog.txt", L"w");

	if (err == 0 && logFile) {
		fclose(logFile);
	}
}

extern "C" void __fastcall LogPosition(float x, float y, float z) {
	FILE* logFile = nullptr;
	errno_t err = _wfopen_s(&logFile, L"HUDLog.txt", L"a+");

	if (err == 0 && logFile) {
		fwprintf(logFile, L"%f, %f, %f\n", x, y, z);

		fclose(logFile);
	}
}

void HookGetPosFunction() {
	void* originalFunctionAddr = (void*)(SigScan(
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
	uint64_t addrToJumpTo64 = (uint64_t)RecordPos; //Hook Function

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
	HookGetPosFunction();
	//createLogFile();
	MainProcess();
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
