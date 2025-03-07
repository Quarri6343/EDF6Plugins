#pragma once
#include <d3d11.h>

void Initimgui(ID3D11Device* d3dDevice, ID3D11DeviceContext* d3dContext, HWND hwnd);
void Shutdownimgui();
void DrawHUD();