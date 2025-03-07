#include "HUDHandler.h"

#include <string>
#include <windows.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <fnt.h>

extern bool dllReady;
extern bool dllEnd;
extern "C" {
	extern float xPos;
	extern float yPos;
	extern float zPos;
}

ImGuiIO* io = nullptr;

float DEFAULT_POSITION_POS_X = 50.0f;
float DEFAULT_POSITION_POS_Y = 760.0f;
float FIXED_FONT_SIZE = 38.0f;

void Initimgui(ID3D11Device* d3dDevice, ID3D11DeviceContext* d3dContext, HWND hwnd) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	io = &ImGui::GetIO(); (void)io;

	ImGui::StyleColorsDark();

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(d3dDevice, d3dContext);

	io->Fonts->AddFontFromMemoryCompressedTTF(robotomono_compressed_data, robotomono_compressed_size, FIXED_FONT_SIZE);
}

void Shutdownimgui() {
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
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

//must be called in render thread
void DrawHUD() {
	if (!dllReady || dllEnd)
		return;

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