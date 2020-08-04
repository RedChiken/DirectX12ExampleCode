#include "BoxApp.h"

BoxApp::BoxApp(HINSTANCE hInstance) : D3DApp(hInstance), mLastMousePos(POINT{ 0, 0 })
{
}

BoxApp::~BoxApp()
{
}

bool BoxApp::Initialize()
{
	if (!D3DApp::Initialize())
	{
		return false;
	}

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildBoxGeometry();
	BuildPSO();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	FlushCommandQueue();
	return true;
}

void BoxApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(XM_PIDIV4, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void BoxApp::Update(const GameTimer& gt)
{
	float x = mRadius * sinf(mPhi) * cosf(mTheta);
	float z = mRadius * sinf(mPhi) * sinf(mTheta);
	float y = mRadius * cosf(mPhi);

	XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);

	XMMATRIX world = XMLoadFloat4x4(&mWorld);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX worldViewProj = world * view * proj;

	ObjectConstants objConstants;
	XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
	mObjectCB->CopyData(0, objConstants);
}

void BoxApp::Draw(const GameTimer& gt)
{
	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	D3D12_RESOURCE_BARRIER Barrier;
	ZeroMemory(&Barrier, sizeof(Barrier));
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	Barrier.Transition.pResource = CurrentBackBuffer();
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	mCommandList->ResourceBarrier(1, &Barrier);

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	mCommandList->IASetVertexBuffers(0, 1, &mBoxGeo->VertexBufferView());
	mCommandList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());
	mCommandList->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

	mCommandList->DrawIndexedInstanced(mBoxGeo->DrawArgs["box"].IndexCount, 1, 0, 0, 0);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrentBackBuffer = (mCurrentBackBuffer + 1) % SwapChainBufferCount;

	FlushCommandQueue();
}

void BoxApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;
	SetCapture(mhMainWnd);
}

void BoxApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mTheta += dx;
		mPhi += dy;

		mPhi = Clamp(mPhi, 0.1f, XM_PI - 0.1f);
	}
	else if ((btnState && MK_RBUTTON) != 0)
	{
		float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

		mRadius += dx - dy;

		mRadius = Clamp(mRadius, 3.f, 15.f);
	}
	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void BoxApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));
}

void BoxApp::BuildConstantBuffers()
{
	mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);

	UINT objCBByteSize = calcConstantBufferByteSize(sizeof(ObjectConstants));

	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
	
	int boxCBufIndex = 0;
	cbAddress += static_cast<UINT64>(boxCBufIndex) * static_cast<UINT64>(objCBByteSize);
	
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = cbAddress;
	cbvDesc.SizeInBytes = calcConstantBufferByteSize(sizeof(ObjectConstants));

	md3dDevice->CreateConstantBufferView(&cbvDesc, mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void BoxApp::BuildRootSignature()
{
	D3D12_ROOT_PARAMETER slotRootParameter[1];

	D3D12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	cbvTable.NumDescriptors = 1;
	cbvTable.BaseShaderRegister = 0;
	cbvTable.RegisterSpace = 0;
	cbvTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	
	slotRootParameter[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	slotRootParameter[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	slotRootParameter[0].DescriptorTable.NumDescriptorRanges = 1;
	slotRootParameter[0].DescriptorTable.pDescriptorRanges = &cbvTable;

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc{ 1, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0, 
		serializedRootSig->GetBufferPointer(), 
		serializedRootSig->GetBufferSize(), 
		IID_PPV_ARGS(&mRootSignature)));

}

void BoxApp::BuildShadersAndInputLayout()
{
	HRESULT hr = S_OK;
	
	mvsByteCode = CompileShader(L"Shaders/Ch6/colors.hlsl", nullptr, "VS", "vs_5_0");
	mpsByteCode = CompileShader(L"Shaders/Ch6/Colors.hlsl", nullptr, "PS", "ps_5_0");

	mInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void BoxApp::BuildBoxGeometry()
{
	std::array<Vertex, 8> vertices
	{
		Vertex({XMFLOAT3(-1.f, -1.f, -1.f), XMFLOAT4(Colors::White)}), 
		Vertex({XMFLOAT3(-1.f, 1.f, -1.f), XMFLOAT4(Colors::Black)}),
		Vertex({XMFLOAT3(1.f, 1.f, -1.f), XMFLOAT4(Colors::Red)}),
		Vertex({XMFLOAT3(1.f, -1.f, -1.f), XMFLOAT4(Colors::Green)}),
		Vertex({XMFLOAT3(-1.f, -1.f, 1.f), XMFLOAT4(Colors::Blue)}),
		Vertex({XMFLOAT3(-1.f, 1.f, 1.f), XMFLOAT4(Colors::Yellow)}),
		Vertex({XMFLOAT3(1.f, 1.f, 1.f), XMFLOAT4(Colors::Cyan)}),
		Vertex({XMFLOAT3(1.f, -1.f, 1.f), XMFLOAT4(Colors::Magenta)}),
	};

	std::array<std::uint16_t, 36> indices =
	{
		0, 1, 2, 
		0, 2, 3, 
		
		4, 6, 5, 
		4, 7, 6, 
		
		4, 5, 1, 
		4, 1, 0, 
		
		3, 2, 6, 
		3, 6, 7, 
		
		1, 5, 6, 
		1, 6, 2,

		4, 0, 3, 
		4, 3, 7
	};

	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

	mBoxGeo = std::make_unique<MeshGeometry>();
	mBoxGeo->Name = "boxGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
	CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU));
	CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	mBoxGeo->VertexBufferGPU = CreateDefaultbuffer(md3dDevice.Get(), mCommandList.Get(), vertices.data(), vbByteSize, mBoxGeo->VertexBufferUploader);
	mBoxGeo->IndexBufferGPU = CreateDefaultbuffer(md3dDevice.Get(), mCommandList.Get(), indices.data(), ibByteSize, mBoxGeo->IndexBufferUploader);

	mBoxGeo->VertexByteStride = sizeof(Vertex);
	mBoxGeo->VertexBufferByteSize = vbByteSize;
	mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
	mBoxGeo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	mBoxGeo->DrawArgs["box"] = submesh;
}

void BoxApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { mInputLayout.data(), static_cast<UINT>(mInputLayout.size()) };
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
		mvsByteCode->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
		mpsByteCode->GetBufferSize()
	};
	
	D3D12_RASTERIZER_DESC Rasterizer;
	Rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	Rasterizer.CullMode = D3D12_CULL_MODE_BACK;
	Rasterizer.FrontCounterClockwise = FALSE;
	Rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	Rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	Rasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	Rasterizer.DepthClipEnable = TRUE;
	Rasterizer.MultisampleEnable = FALSE;
	Rasterizer.AntialiasedLineEnable = FALSE;
	Rasterizer.ForcedSampleCount = 0;
	Rasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	
	D3D12_BLEND_DESC Blend;
	Blend.AlphaToCoverageEnable = FALSE;
	Blend.IndependentBlendEnable = FALSE;
	const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc
	{
		FALSE, FALSE, 
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, 
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, 
		D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL,
	};
	for (auto& iter : Blend.RenderTarget)
	{
		iter = defaultRenderTargetBlendDesc;
	}

	D3D12_DEPTH_STENCIL_DESC DepthStencil;
	DepthStencil.DepthEnable = TRUE;
	DepthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	DepthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	DepthStencil.StencilEnable = FALSE;
	DepthStencil.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	DepthStencil.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = 
	{
		D3D12_STENCIL_OP_KEEP, 
		D3D12_STENCIL_OP_KEEP,
		D3D12_STENCIL_OP_KEEP,
		D3D12_COMPARISON_FUNC_ALWAYS
	};
	DepthStencil.FrontFace = defaultStencilOp;
	DepthStencil.BackFace = defaultStencilOp;

	psoDesc.RasterizerState = Rasterizer;
	psoDesc.BlendState = Blend;
	psoDesc.DepthStencilState = DepthStencil;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = mBackBufferFormat;
	psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	psoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}
