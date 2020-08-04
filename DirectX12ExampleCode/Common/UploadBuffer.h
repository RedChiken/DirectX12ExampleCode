#pragma once
#include "Common.h"

template<typename T>
class UploadBuffer
{
public:
	UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) : mIsConstantBuffer(isConstantBuffer)
	{
		mElementByteSize = sizeof(T);
		
		if (isConstantBuffer)
		{
			mElementByteSize = calcConstantBufferByteSize(sizeof(T));
		}
		D3D12_HEAP_PROPERTIES HeapProperty;
		HeapProperty.Type = D3D12_HEAP_TYPE_UPLOAD;
		HeapProperty.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperty.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		HeapProperty.CreationNodeMask = 1;
		HeapProperty.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC Resource;
		Resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		Resource.Alignment = 0;
		Resource.Width = mElementByteSize * static_cast<UINT64>(elementCount);
		Resource.Height = 1;
		Resource.DepthOrArraySize = 1;
		Resource.MipLevels = 1;
		Resource.Format = DXGI_FORMAT_UNKNOWN;
		Resource.SampleDesc.Count = 1;
		Resource.SampleDesc.Quality = 0;
		Resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		Resource.Flags = D3D12_RESOURCE_FLAG_NONE;

		ThrowIfFailed(device->CreateCommittedResource(
			&HeapProperty,
			D3D12_HEAP_FLAG_NONE,
			&Resource,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mUploadBuffer)
		));

		ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));
	}
	UploadBuffer(const UploadBuffer& rhs) = delete;
	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
	~UploadBuffer()
	{
		if (mUploadBuffer != nullptr)
		{
			mUploadBuffer->Unmap(0, nullptr);
		}
		mMappedData = nullptr;
	}

	ID3D12Resource* Resource() const
	{
		return mUploadBuffer.Get();
	}

	void CopyData(int elementIndex, const T& data)
	{
		memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
	}

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
	BYTE* mMappedData = nullptr;

	UINT mElementByteSize = 0;
	bool mIsConstantBuffer = false;
};
