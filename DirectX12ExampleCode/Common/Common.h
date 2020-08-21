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


//------------------------------------------------------------------------------------------------
// Row-by-row memcpy
inline void MemcpySubresource(
	_In_ const D3D12_MEMCPY_DEST* pDest,
	_In_ const D3D12_SUBRESOURCE_DATA* pSrc,
	SIZE_T RowSizeInBytes,
	UINT NumRows,
	UINT NumSlices)
{
	for (UINT z = 0; z < NumSlices; ++z)
	{
		BYTE* pDestSlice = reinterpret_cast<BYTE*>(pDest->pData) + pDest->SlicePitch * z;
		const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(pSrc->pData) + pSrc->SlicePitch * z;
		for (UINT y = 0; y < NumRows; ++y)
		{
			memcpy(pDestSlice + pDest->RowPitch * y,
				pSrcSlice + pSrc->RowPitch * y,
				RowSizeInBytes);
		}
	}
}


//------------------------------------------------------------------------------------------------
// All arrays must be populated (e.g. by calling GetCopyableFootprints)
inline UINT64 UpdateSubresources(
	_In_ ID3D12GraphicsCommandList* pCmdList,
	_In_ ID3D12Resource* pDestinationResource,
	_In_ ID3D12Resource* pIntermediate,
	_In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
	_In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
	UINT64 RequiredSize,
	_In_reads_(NumSubresources) const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts,
	_In_reads_(NumSubresources) const UINT* pNumRows,
	_In_reads_(NumSubresources) const UINT64* pRowSizesInBytes,
	_In_reads_(NumSubresources) const D3D12_SUBRESOURCE_DATA* pSrcData)
{
	// Minor validation
	D3D12_RESOURCE_DESC IntermediateDesc = pIntermediate->GetDesc();
	D3D12_RESOURCE_DESC DestinationDesc = pDestinationResource->GetDesc();
	if (IntermediateDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
		IntermediateDesc.Width < RequiredSize + pLayouts[0].Offset ||
		RequiredSize >(SIZE_T) - 1 ||
		(DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
			(FirstSubresource != 0 || NumSubresources != 1)))
	{
		return 0;
	}

	BYTE* pData;
	HRESULT hr = pIntermediate->Map(0, NULL, reinterpret_cast<void**>(&pData));
	if (FAILED(hr))
	{
		return 0;
	}

	for (UINT i = 0; i < NumSubresources; ++i)
	{
		if (pRowSizesInBytes[i] > (SIZE_T)-1) return 0;
		D3D12_MEMCPY_DEST DestData = { pData + pLayouts[i].Offset, pLayouts[i].Footprint.RowPitch, pLayouts[i].Footprint.RowPitch * pNumRows[i] };
		MemcpySubresource(&DestData, &pSrcData[i], (SIZE_T)pRowSizesInBytes[i], pNumRows[i], pLayouts[i].Footprint.Depth);
	}
	pIntermediate->Unmap(0, NULL);

	if (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		D3D12_BOX SrcBox{ static_cast<UINT>(pLayouts[0].Offset), 0, 0, static_cast<UINT>(pLayouts[0].Offset + pLayouts[0].Footprint.Width), 1, 1 };
		//CD3DX12_BOX SrcBox(UINT(pLayouts[0].Offset), UINT(pLayouts[0].Offset + pLayouts[0].Footprint.Width));
		pCmdList->CopyBufferRegion(
			pDestinationResource, 0, pIntermediate, pLayouts[0].Offset, pLayouts[0].Footprint.Width);
	}
	else
	{
		for (UINT i = 0; i < NumSubresources; ++i)
		{
			D3D12_TEXTURE_COPY_LOCATION Dst{ pDestinationResource, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, i + FirstSubresource };
			D3D12_TEXTURE_COPY_LOCATION Src{ pIntermediate, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, pLayouts[i] };
			//CD3DX12_TEXTURE_COPY_LOCATION Dst(pDestinationResource, i + FirstSubresource);
			//CD3DX12_TEXTURE_COPY_LOCATION Src(pIntermediate, pLayouts[i]);
			pCmdList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
		}
	}
	return RequiredSize;
}

//------------------------------------------------------------------------------------------------
// Heap-allocating UpdateSubresources implementation
inline UINT64 UpdateSubresources(
	_In_ ID3D12GraphicsCommandList* pCmdList,
	_In_ ID3D12Resource* pDestinationResource,
	_In_ ID3D12Resource* pIntermediate,
	UINT64 IntermediateOffset,
	_In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
	_In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
	_In_reads_(NumSubresources) D3D12_SUBRESOURCE_DATA* pSrcData)
{
	UINT64 RequiredSize = 0;
	UINT64 MemToAlloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * NumSubresources;
	if (MemToAlloc > SIZE_MAX)
	{
		return 0;
	}
	void* pMem = HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(MemToAlloc));
	if (pMem == NULL)
	{
		return 0;
	}
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
	UINT64* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + NumSubresources);
	UINT* pNumRows = reinterpret_cast<UINT*>(pRowSizesInBytes + NumSubresources);

	D3D12_RESOURCE_DESC Desc = pDestinationResource->GetDesc();
	ID3D12Device* pDevice;
	pDestinationResource->GetDevice(__uuidof(*pDevice), reinterpret_cast<void**>(&pDevice));
	pDevice->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);
	pDevice->Release();

	UINT64 Result = UpdateSubresources(pCmdList, pDestinationResource, pIntermediate, FirstSubresource, NumSubresources, RequiredSize, pLayouts, pNumRows, pRowSizesInBytes, pSrcData);
	HeapFree(GetProcessHeap(), 0, pMem);
	return Result;
}

static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultbuffer(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* initData,
	UINT64 byteSize,
	Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
	Microsoft::WRL::ComPtr<ID3D12Resource> defaultBuffer;

	D3D12_HEAP_PROPERTIES BeforeDefaultHeapProperty{ D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
	D3D12_RESOURCE_DESC BeforeDefaultResource{
		D3D12_RESOURCE_DIMENSION_BUFFER, 
		0, byteSize, 1, 1, 1, 
		DXGI_FORMAT_UNKNOWN, 1, 0, 
		D3D12_TEXTURE_LAYOUT_ROW_MAJOR, 
		D3D12_RESOURCE_FLAG_NONE };
	ThrowIfFailed(device->CreateCommittedResource(
		&BeforeDefaultHeapProperty,
		D3D12_HEAP_FLAG_NONE,
		&BeforeDefaultResource,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())
	));

	D3D12_HEAP_PROPERTIES AfterUploadHeapProperty{ D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
	D3D12_RESOURCE_DESC AfterUploadResource{
		D3D12_RESOURCE_DIMENSION_BUFFER,
		0, byteSize, 1, 1, 1,
		DXGI_FORMAT_UNKNOWN, 1, 0,
		D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		D3D12_RESOURCE_FLAG_NONE };
	ThrowIfFailed(device->CreateCommittedResource(
		&AfterUploadHeapProperty,
		D3D12_HEAP_FLAG_NONE,
		&AfterUploadResource,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())
	));

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = static_cast<LONG_PTR>(byteSize);
	subResourceData.SlicePitch = subResourceData.RowPitch;



	D3D12_RESOURCE_BARRIER BeforeBarrier;
	ZeroMemory(&BeforeBarrier, sizeof(BeforeBarrier));
	BeforeBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	BeforeBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	BeforeBarrier.Transition.pResource = defaultBuffer.Get();
	BeforeBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	BeforeBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	BeforeBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmdList->ResourceBarrier(1, &BeforeBarrier);

	UpdateSubresources(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

	D3D12_RESOURCE_BARRIER AfterBarrier;
	ZeroMemory(&AfterBarrier, sizeof(AfterBarrier));
	AfterBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	AfterBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	AfterBarrier.Transition.pResource = defaultBuffer.Get();
	AfterBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	AfterBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	AfterBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmdList->ResourceBarrier(1, &AfterBarrier);

	return defaultBuffer;
}