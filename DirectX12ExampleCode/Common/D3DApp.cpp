#include "D3DApp.h"
#include <cassert>

using namespace std;
using namespace Microsoft::WRL;

LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

D3DApp* D3DApp::mApp = nullptr;
D3DApp* D3DApp::GetApp()
{
	return mApp;
}

D3DApp::D3DApp(HINSTANCE hInstance) : mhAppInst(hInstance)
{
	assert(mApp == nullptr);
	mApp = this;
}

D3DApp::~D3DApp()
{
	if (md3dDevice != nullptr)
	{
		FlushCommandQueue();
	}
}

HINSTANCE D3DApp::AppInst() const
{
	return mhAppInst;
}

HWND D3DApp::MainWnd() const
{
	return mhMainWnd;
}

float D3DApp::AspectRatio() const
{
	return static_cast<float>(mClientWidth) / mClientHeight;
}

bool D3DApp::Get4xMsaaState() const
{
	return m4xMsaaState;
}

void D3DApp::Set4xMsaaState(bool value)
{
	if (m4xMsaaState != value)
	{
		m4xMsaaState = value;
		CreateSwapChain();
		OnResize();
	}
}

int D3DApp::run()
{
	MSG msg{ 0 };
	mTimer.Reset();

	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			mTimer.Tick();
			if (!mAppPaused)
			{
				CalculateFrameStats();
				Update(mTimer);
				Draw(mTimer);
			}
			else
			{
				Sleep(100);
			}
		}
	}
	return static_cast<int>(msg.wParam);
}

bool D3DApp::Initialize()
{
	if (!InitMainWindow())
	{
		return false;
	}
	if (!InitDirect3D())
	{
		return false;
	}

	OnResize();
	return true;
}

void D3DApp::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvheapDesc;
	rtvheapDesc.NumDescriptors = SwapChainBufferCount;
	rtvheapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvheapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvheapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvheapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void D3DApp::OnResize()
{
	assert(md3dDevice);
	assert(mSwapChain);
	assert(mDirectCmdListAlloc);

	FlushCommandQueue();

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	
	for (int i = 0; i < SwapChainBufferCount; ++i)
	{
		mSwapChainbuffer[i].Reset();
		mDepthStencilBuffer.Reset();
	}

	ThrowIfFailed(mSwapChain->ResizeBuffers(
		SwapChainBufferCount, 
		mClientWidth, 
		mClientHeight, 
		mBackBufferFormat, 
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	mCurrentBackBuffer = 0;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SwapChainBufferCount; ++i)
	{
		ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainbuffer[i])));
		md3dDevice->CreateRenderTargetView(mSwapChainbuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.ptr += mRtvDescriptorSize;
	}

	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = mClientWidth;
	depthStencilDesc.Height = mClientHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	
	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = mDepthStencilFormat;
	optClear.DepthStencil.Depth = 1.f;
	optClear.DepthStencil.Stencil = 0;
	
	D3D12_HEAP_PROPERTIES HeapProperty;
	HeapProperty.Type = D3D12_HEAP_TYPE_DEFAULT;
	HeapProperty.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	HeapProperty.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	HeapProperty.CreationNodeMask = 1;
	HeapProperty.VisibleNodeMask = 1;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&HeapProperty, D3D12_HEAP_FLAG_NONE, 
		&depthStencilDesc, D3D12_RESOURCE_STATE_COMMON, 
		&optClear, IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

	D3D12_RESOURCE_BARRIER Barrier;
	ZeroMemory(&Barrier, sizeof(Barrier));
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	Barrier.Transition.pResource = mDepthStencilBuffer.Get();
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	mCommandList->ResourceBarrier(1, &Barrier);

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	mScreenViewport.TopLeftX = 0;
	mScreenViewport.TopLeftY = 0;
	mScreenViewport.Width = static_cast<float>(mClientWidth);
	mScreenViewport.Height = static_cast<float>(mClientHeight);
	mScreenViewport.MinDepth = 0.0f;
	mScreenViewport.MaxDepth = 1.0f;

	mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}

LRESULT D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	//WndProc
	switch (msg)
	{
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			mAppPaused = true;
			mTimer.Stop();
		}
		else
		{
			mAppPaused = false;
			mTimer.Start();
		}
		return 0;
	case WM_SIZE:
		mClientWidth = LOWORD(lParam);
		mClientHeight = HIWORD(lParam);
		if (md3dDevice)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				mAppPaused = true;
				mMinimized = true;
				mMaximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				mAppPaused = false;
				mMinimized = false;
				mMaximized = true;
				OnResize();
			}
			else if (wParam == SIZE_RESTORED)
			{
				if (mMinimized)
				{
					mAppPaused = false;
					mMinimized = false;
					OnResize();
				}
				else if (mMaximized)
				{
					mAppPaused = false;
					mMaximized = false;
					OnResize();
				}
				else if (mResizing)
				{

				}
				else
				{
					OnResize();
				}
			}
		}
		return 0;
	case WM_ENTERSIZEMOVE:
		mAppPaused = true;
		mResizing = true;
		mTimer.Stop();
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_MENUCHAR:
		return MAKELRESULT(0, MNC_CLOSE);
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
		return 0;
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_KEYUP:
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		else if (static_cast<int>(wParam) == VK_F2)
		{
			Set4xMsaaState(!m4xMsaaState);
		}
		return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool D3DApp::InitMainWindow()
{
	//--------------RegisterClass
	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = MainWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = mhAppInst;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
	wc.lpszMenuName = 0;
	wc.lpszClassName = L"MainWnd";

	if (!RegisterClass(&wc))
	{
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return false;
	}


	//--------------------------InitInstance
	RECT R = { 0, 0, mClientWidth, mClientHeight };
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width = R.right - R.left;
	int height = R.bottom - R.top;

	mhMainWnd = CreateWindow(L"MainWnd", mMainWndCaption.c_str(), 
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, mhAppInst, 0);
	if (!mhMainWnd)
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
	}

	ShowWindow(mhMainWnd, SW_SHOW);
	UpdateWindow(mhMainWnd);

	return true;

}

bool D3DApp::InitDirect3D()
{
#if defined(DEBUG) || defined(_DEBUG)
	{
		ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
	}
#endif

	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));

	HRESULT hardwareResult = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&md3dDevice));

	if (FAILED(hardwareResult))
	{
		ComPtr<IDXGIAdapter4> pWarpAdapter;
		ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(pWarpAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&md3dDevice)));
	}

	ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));

	mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = mBackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels)));

	m4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m4xMsaaQuality > 0 && "Unexpected Msaa quality level.");

#ifdef _DEBUG
	LogAdpaters();
#endif
	
	CreateCommandObjects();
	CreateSwapChain();
	CreateRtvAndDsvDescriptorHeaps();

	return true;
}

void D3DApp::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

	ThrowIfFailed(md3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));

	ThrowIfFailed(md3dDevice->CreateCommandList(
		0, 
		D3D12_COMMAND_LIST_TYPE_DIRECT, 
		mDirectCmdListAlloc.Get(), 
		nullptr, 
		IID_PPV_ARGS(mCommandList.GetAddressOf())));

	mCommandList->Close();
}

void D3DApp::CreateSwapChain()
{
	mSwapChain.Reset();
	IDXGIOutput* output = nullptr;

	DXGI_SWAP_CHAIN_DESC1 sd1;
	sd1.Width = mClientWidth;
	sd1.Height = mClientHeight;
	sd1.Format = mBackBufferFormat;
	sd1.Stereo = true;
	sd1.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	sd1.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	sd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd1.BufferCount = SwapChainBufferCount;
	sd1.Scaling = DXGI_SCALING_NONE;
	sd1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd1.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
	sd1.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainFSDesc;
	swapChainFSDesc.Windowed = true;
	
	/*
	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = mClientWidth;
	sd.BufferDesc.Height = mClientHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = mBackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	*/

	//ThrowIfFailed(mdxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, mSwapChain.GetAddressOf()));
	ThrowIfFailed(mdxgiFactory->CreateSwapChainForHwnd(mCommandQueue.Get(), mhMainWnd, &sd1, &swapChainFSDesc, nullptr, &mSwapChain));
	//not recommand to use CreateSwapChain
}

void D3DApp::FlushCommandQueue()
{
	++mCurrentFence;
	
	ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

	if (mFence->GetCompletedValue() < mCurrentFence)
	{
		HANDLE eventHandle = CreateEvent(nullptr, false, false, nullptr);
		//https://github.com/microsoft/DirectX-Graphics-Samples/issues/15

		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

ID3D12Resource* D3DApp::CurrentBackBuffer() const
{
	return mSwapChainbuffer[mCurrentBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::CurrentBackBufferView() const
{
	D3D12_CPU_DESCRIPTOR_HANDLE result = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	result.ptr += mCurrentBackBuffer * mRtvDescriptorSize;
	return result;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::DepthStencilView() const
{
	return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void D3DApp::CalculateFrameStats()
{
	static int frameCnt = 0;
	static float timeElapse = 0.f;

	++frameCnt;

	if ((mTimer.TotalTime() - timeElapse) >= 1.f)
	{
		float fps = static_cast<float>(frameCnt);
		float mspf = 1000.f / fps;
		
		wstring fpsStr = to_wstring(fps);
		wstring mspStr = to_wstring(mspf);

		wstring windowText = mMainWndCaption + L"	fps: " + fpsStr + L"	mspf: " + mspStr;

		SetWindowText(mhMainWnd, windowText.c_str());
		frameCnt = 0;
		timeElapse += 1.f;
	}
}

void D3DApp::LogAdpaters()
{
	UINT i = 0;
	IDXGIAdapter* adapter = nullptr;
	std::vector<IDXGIAdapter*> adapterList;
	while (mdxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);

		std::wstring text = L"***Adpater: ";
		text += desc.Description;
		text += L"\n";

		OutputDebugString(text.c_str());

		adapterList.push_back(adapter);

		++i;
	}

	for (size_t i = 0; i < adapterList.size(); ++i)
	{
		LogAdpaterOutputs(adapterList[i]);
		if (adapterList[i])
		{
			adapterList[i]->Release();
			adapterList[i] = 0;
		}
	}
}

void D3DApp::LogAdpaterOutputs(IDXGIAdapter* adapter)
{
	UINT i = 0;
	IDXGIOutput* output = nullptr;
	while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_OUTPUT_DESC desc;
		output->GetDesc(&desc);

		std::wstring text = L"***Output: ";
		text += desc.DeviceName;
		text += L"\n";
		OutputDebugString(text.c_str());

		LogOutputDisplayModes(output, mBackBufferFormat);

		if (output)
		{
			output->Release();
			output = 0;
		}

		++i;
	}
}

void D3DApp::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
{
	UINT count = 0;
	UINT flags = 0;

	output->GetDisplayModeList(format, flags, &count, nullptr);

	std::vector<DXGI_MODE_DESC> modeList(count);
	output->GetDisplayModeList(format, flags, &count, &modeList[0]);

	for (const auto& x : modeList)
	{
		UINT n = x.RefreshRate.Numerator;
		UINT d = x.RefreshRate.Denominator;
		std::wstring text =
			L"Width = " + to_wstring(x.Width) + L" " +
			L"Height = " + to_wstring(x.Height) + L" " +
			L"Refresh = " + to_wstring(n) + L"/" + to_wstring(d) + L"\n";

		::OutputDebugString(text.c_str());
	}
}
