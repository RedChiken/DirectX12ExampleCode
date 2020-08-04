#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")

#pragma once
#include <Windows.h>
#include <wrl.h>
#include <windowsx.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <DirectXMath.h>
#include <pix.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include "GameTimer.h"
#include "DXException.h"

#include <iostream>

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

inline std::wstring AnsiToWString(const std::string& str)
{
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)							\
{													\
	HRESULT hr__ = (x);								\
	std::wstring wfn = AnsiToWString(__FILE__);		\
	if(FAILED(hr__))								\
	{												\
		throw DXException(hr__, L#x, wfn, __LINE__);\
	}												\
}
#endif

template<typename T>
static T Clamp(const T& x, const T& low, const T& high)
{
	return x < low ? low : (x > high ? high : x);
}

static UINT calcConstantBufferByteSize(UINT byteSize)
{
	return (byteSize + 255) & ~255;
}

static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	UINT compileFlags = 0;

#if defined(DEBUG) || defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	Microsoft::WRL::ComPtr<ID3DBlob> byteCode = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errors = nullptr;

	hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, 
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

	if (errors != nullptr)
	{
		OutputDebugStringA(static_cast<char*>(errors->GetBufferPointer()));
	}

	ThrowIfFailed(hr);

	return byteCode;
}

static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultbuffer(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* initData,
	UINT64 byteSize,
	Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
	Microsoft::WRL::ComPtr<ID3D12Resource> defaultBuffer;

	D3D12_HEAP_PROPERTIES DefaultHeapProperty{ D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
	D3D12_RESOURCE_DESC DefaultResource{
		D3D12_RESOURCE_DIMENSION_BUFFER, 
		0, byteSize, 1, 1, 1, 
		DXGI_FORMAT_UNKNOWN, 1, 0, 
		D3D12_TEXTURE_LAYOUT_ROW_MAJOR, 
		D3D12_RESOURCE_FLAG_NONE };
	ThrowIfFailed(device->CreateCommittedResource(
		&DefaultHeapProperty,
		D3D12_HEAP_FLAG_NONE,
		&DefaultResource,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())
	));

	D3D12_HEAP_PROPERTIES UploadHeapProperty{ D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
	D3D12_RESOURCE_DESC UploadResource{
		D3D12_RESOURCE_DIMENSION_BUFFER,
		0, byteSize, 1, 1, 1,
		DXGI_FORMAT_UNKNOWN, 1, 0,
		D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		D3D12_RESOURCE_FLAG_NONE };
	ThrowIfFailed(device->CreateCommittedResource(
		&UploadHeapProperty,
		D3D12_HEAP_FLAG_NONE,
		&UploadResource,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())
	));

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = static_cast<LONG_PTR>(byteSize);
	subResourceData.SlicePitch = subResourceData.RowPitch;

	D3D12_RESOURCE_BARRIER Barrier;
	ZeroMemory(&Barrier, sizeof(Barrier));
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	Barrier.Transition.pResource = defaultBuffer.Get();
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	cmdList->ResourceBarrier(1, &Barrier);

	return defaultBuffer;
}