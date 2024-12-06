
#include "FunctionHooks.h"
#include "ParameterHUD.h"

#include <iostream>
#include <MinHook.h>
#include <d3d11.h>

extern uintptr_t hedfDLL;

ID3D11Device* d3dDevice = nullptr;
ID3D11DeviceContext* d3dContext = nullptr;

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

	MainLoop();
}

void InitDeviceHook() {
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

void UnInitDeviceHook() {
	if (d3dDevice) {
		d3dDevice->Release();
		d3dDevice = nullptr;
	}
	if (d3dContext) {
		d3dContext->Release();
		d3dContext = nullptr;
	}
}

void InitRadarHook() {
	void* pUpdateRadar = reinterpret_cast<LPVOID>(hedfDLL + 0x82B1F0);

	if (MH_CreateHook(pUpdateRadar, &hkUpdateRadar, reinterpret_cast<LPVOID*>(&oUpdateRadar)) != MH_OK) {
		std::cerr << "Failed to create hook for UpdateRadar!" << std::endl;
		return;
	}

	if (MH_EnableHook(pUpdateRadar) != MH_OK) {
		std::cerr << "Failed to enable hook for UpdateRadar!" << std::endl;
		return;
	}
}