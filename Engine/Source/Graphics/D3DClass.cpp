#include "Engine.h"
#include "D3DClass.h"

#include <WindowsX.h>

namespace Graphics
{
	D3DClass::D3DClass()
	{
	}

	D3DClass::~D3DClass()
	{
		if (m_d3dDevice != nullptr)
			FlushCommandQueue();
	}

	HWND D3DClass::MainWnd() const
	{
		return m_hMainWnd;
	}

	float D3DClass::AspectRatio() const
	{
		return static_cast<float>(m_ClientWidth) / m_ClientHeight;
	}

	bool D3DClass::GetAppPaused()
	{
		return m_AppPaused;
	}

	void D3DClass::SetAppPaused(bool value)
	{
		m_AppPaused = value;
	}

	bool D3DClass::GetMinimized()
	{
		return m_Minimized;
	}

	void D3DClass::SetMinimized(bool value)
	{
		m_Minimized = value;
	}

	bool D3DClass::GetMaximized()
	{
		return m_Maximized;
	}

	void D3DClass::SetMaximized(bool value)
	{
		m_Maximized = value;
	}

	bool D3DClass::GetResizing()
	{
		return m_Resizing;
	}

	void D3DClass::SetResizing(bool value)
	{
		m_Resizing = value;
	}

	bool D3DClass::Get4xMsaaState() const
	{
		return m_4xMsaaState;
	}

	void D3DClass::Set4xMsaaState(bool value)
	{
		if (m_4xMsaaState != value)
		{
			m_4xMsaaState = value;

			// Recreate the swapchain and buffers with new multisample settings.
			CreateSwapChain();
			OnResize();
		}
	}

	bool D3DClass::Get4xMsaaQuality() const
	{
		return m_4xMsaaQuality;
	}

	void D3DClass::Set4xMsaaQuality(UINT value)
	{
		m_4xMsaaQuality = value;
	}

	void D3DClass::Run()
	{
		m_Timer.Tick();

		if (!m_AppPaused)
		{
			Update(m_Timer);
			Draw(m_Timer);
		}
		else
		{
			Sleep(100);
		}
	}

	void D3DClass::Initialize(HWND mainWnd, int width, int height)
	{
		m_hMainWnd = mainWnd;
		m_ClientWidth = width;
		m_ClientHeight = height;

		InitDirect3D();
		OnResize();
	}

	void D3DClass::CreateRtvAndDsvAndSrvDescriptorHeaps()
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = swapChainBufferCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		rtvHeapDesc.NodeMask = 0;
		ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(
			&rtvHeapDesc, IID_PPV_ARGS(m_RenderTargetViewHeap.GetAddressOf())));

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dsvHeapDesc.NodeMask = 0;
		ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(
			&dsvHeapDesc, IID_PPV_ARGS(m_DepthStencilViewHeap.GetAddressOf())));

		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.NumDescriptors = 1;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(
			&srvHeapDesc, IID_PPV_ARGS(&m_ShaderResourceViewHeap)));
	}

	void D3DClass::OnResize()
	{
		assert(m_d3dDevice);
		assert(m_SwapChain);
		assert(m_DirectCommandListAllocator);

		// Flush before changing any resources.
		FlushCommandQueue();

		ThrowIfFailed(m_CommandList->Reset(m_DirectCommandListAllocator.Get(), nullptr));

		// Release the previous resources we will be recreating.
		for (int i = 0; i < swapChainBufferCount; ++i)
			m_SwapChainBuffer[i].Reset();
		m_DepthStencilBuffer.Reset();

		// Resize the swap chain.
		ThrowIfFailed(m_SwapChain->ResizeBuffers(
			swapChainBufferCount,
			m_ClientWidth, m_ClientHeight,
			m_BackBufferFormat,
			DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

		m_CurrentBackBuffer = 0;

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(
			m_RenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart());
		for (UINT i = 0; i < swapChainBufferCount; i++)
		{
			ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapChainBuffer[i])));
			m_d3dDevice->CreateRenderTargetView(m_SwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
			rtvHeapHandle.Offset(1, m_RenderTargetViewDescriptorSize);
		}

		D3D12_RESOURCE_DESC depthStencilDesc = {};
		depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilDesc.Alignment = 0;
		depthStencilDesc.Width = m_ClientWidth;
		depthStencilDesc.Height = m_ClientHeight;
		depthStencilDesc.DepthOrArraySize = 1;
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depthStencilDesc.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
		depthStencilDesc.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
		depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE optClear = {};
		optClear.Format = m_DepthStencilFormat;
		optClear.DepthStencil.Depth = 1.0f;
		optClear.DepthStencil.Stencil = 0;

		D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

		ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_COMMON,
			&optClear,
			IID_PPV_ARGS(m_DepthStencilBuffer.GetAddressOf())));

		// Create descriptor to mip level 0 of entire resource using the format of the resource.
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Format = m_DepthStencilFormat;
		dsvDesc.Texture2D.MipSlice = 0;
		m_d3dDevice->CreateDepthStencilView(m_DepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

		D3D12_RESOURCE_BARRIER resourceBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_DepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		// Transition the resource from its initial state to be used as a depth buffer.
		m_CommandList->ResourceBarrier(1, &resourceBarrier);

		// Execute the resize commands.
		ThrowIfFailed(m_CommandList->Close());
		ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
		m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

		// Wait until resize is complete.
		FlushCommandQueue();

		// Update the viewport transform to cover the client area.
		m_ScreenViewport.TopLeftX = 0;
		m_ScreenViewport.TopLeftY = 0;
		m_ScreenViewport.Width = static_cast<float>(m_ClientWidth);
		m_ScreenViewport.Height = static_cast<float>(m_ClientHeight);
		m_ScreenViewport.MinDepth = 0.0f;
		m_ScreenViewport.MaxDepth = 1.0f;

		m_ScissorRect = { 0, 0, m_ClientWidth, m_ClientHeight };

		m_ImguiManager.Initialize(m_d3dDevice.Get(), m_BackBufferFormat, m_ShaderResourceViewHeap);
	}

	void D3DClass::InitDirect3D()
	{
		#if defined(DEBUG) || defined(_DEBUG) 
		// Enable the D3D12 debug layer.
		{
			ComPtr<ID3D12Debug> debugController;
			ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
			debugController->EnableDebugLayer();
		}
		#endif

		ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory)));

		// Try to create hardware device.
		HRESULT hardwareResult = D3D12CreateDevice(
			nullptr,             // default adapter
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_d3dDevice));

		// Fallback to WARP device.
		if (FAILED(hardwareResult))
		{
			ComPtr<IDXGIAdapter> pWarpAdapter;
			ThrowIfFailed(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

			ThrowIfFailed(D3D12CreateDevice(pWarpAdapter.Get(), 
				D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_d3dDevice)));
		}

		ThrowIfFailed(m_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)));

		m_RenderTargetViewDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_DepthStencilViewDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		m_CbvSrvUavDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// Check 4X MSAA quality support for our back buffer format.
		// All Direct3D 11 capable devices support 4X MSAA for all render 
		// target formats, so we only need to check quality support.
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels = {};
		msQualityLevels.Format = m_BackBufferFormat;
		msQualityLevels.SampleCount = 4;
		msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
		msQualityLevels.NumQualityLevels = 0;
		ThrowIfFailed(m_d3dDevice->CheckFeatureSupport(
			D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
			&msQualityLevels,
			sizeof(msQualityLevels)));

		m_4xMsaaQuality = msQualityLevels.NumQualityLevels;
		assert(m_4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

		#ifdef _DEBUG
			LogAdapters();
		#endif

		CreateCommandObjects();
		CreateSwapChain();
		CreateRtvAndDsvAndSrvDescriptorHeaps();
	}

	void D3DClass::CreateCommandObjects()
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		ThrowIfFailed(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

		ThrowIfFailed(m_d3dDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(m_DirectCommandListAllocator.GetAddressOf())));

		ThrowIfFailed(m_d3dDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_DirectCommandListAllocator.Get(), // Associated command allocator
			nullptr,							// Initial PipelineStateObject
			IID_PPV_ARGS(m_CommandList.GetAddressOf())));

		// Start off in a closed state.  This is because the first time we refer 
		// to the command list we will Reset it, and it needs to be closed before calling Reset.
		m_CommandList->Close();
	}

	void D3DClass::CreateSwapChain()
	{
		// Release the previous swapchain we will be recreating.
		m_SwapChain.Reset();

		DXGI_SWAP_CHAIN_DESC sd = {};
		sd.BufferDesc.Width = m_ClientWidth;
		sd.BufferDesc.Height = m_ClientHeight;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.BufferDesc.Format = m_BackBufferFormat;
		sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		sd.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
		sd.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = swapChainBufferCount;
		sd.OutputWindow = m_hMainWnd;
		sd.Windowed = true;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		// Note: Swap chain uses queue to perform flush.
		ThrowIfFailed(m_dxgiFactory->CreateSwapChain(
			m_CommandQueue.Get(),
			&sd,
			m_SwapChain.GetAddressOf()));
	}

	void D3DClass::FlushCommandQueue()
	{
		// Advance the fence value to mark commands up to this fence point.
		m_CurrentFence++;

		// Add an instruction to the command queue to set a new fence point.  Because we 
		// are on the GPU timeline, the new fence point won't be set until the GPU finishes
		// processing all the commands prior to this Signal().
		ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence));

		// Wait until the GPU has completed commands up to this fence point.
		if (m_Fence->GetCompletedValue() < m_CurrentFence)
		{
			HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);

			// Fire event when GPU hits current fence.  
			ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFence, eventHandle));

			// Wait until the GPU hits current fence event is fired.
			WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
		}
	}

	ID3D12Resource* D3DClass::CurrentBackBuffer() const
	{
		return m_SwapChainBuffer[m_CurrentBackBuffer].Get();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE D3DClass::CurrentBackBufferView() const
	{
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(
			m_RenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart(),
			m_CurrentBackBuffer,
			m_RenderTargetViewDescriptorSize);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE D3DClass::DepthStencilView() const
	{
		return m_DepthStencilViewHeap->GetCPUDescriptorHandleForHeapStart();
	}

	void D3DClass::LogAdapters()
	{
		UINT i = 0;
		IDXGIAdapter* adapter = nullptr;
		std::vector<IDXGIAdapter*> adapterList;
		while (m_dxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
		{
			DXGI_ADAPTER_DESC desc;
			adapter->GetDesc(&desc);

			std::wstring text = L"***Adapter: ";
			text += desc.Description;
			text += L"\n";

			OutputDebugString(text.c_str());
			adapterList.push_back(adapter);

			++i;
		}

		for (size_t i = 0; i < adapterList.size(); ++i)
		{
			LogAdapterOutputs(adapterList[i]);
			ReleaseCOM(adapterList[i]);
		}
	}

	void D3DClass::LogAdapterOutputs(IDXGIAdapter* adapter)
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

			LogOutputDisplayModes(output, m_BackBufferFormat);
			ReleaseCOM(output);

			++i;
		}
	}

	void D3DClass::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
	{
		UINT count = 0;
		UINT flags = 0;

		// Call with nullptr to get list count.
		output->GetDisplayModeList(format, flags, &count, nullptr);
		std::vector<DXGI_MODE_DESC> modeList(count);
		output->GetDisplayModeList(format, flags, &count, &modeList[0]);

		for (auto& x : modeList)
		{
			UINT n = x.RefreshRate.Numerator;
			UINT d = x.RefreshRate.Denominator;
			std::wstring text =
				L"Width = " + std::to_wstring(x.Width) + L" " +
				L"Height = " + std::to_wstring(x.Height) + L" " +
				L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
				L"\n";

			::OutputDebugString(text.c_str());
		}
	}

	std::wstring D3DClass::CalculateFrameStats()
	{
		// Code computes the average frames per second, and also the 
		// average time it takes to render one frame.  These stats 
		// are appended to the window caption bar.
		static std::wstring windowText;
		static int frameCount = 0;
		static float timeElapsed = 0.0f;

		frameCount++;
		// Compute averages over one second period.
		if ((m_Timer.TotalTime() - timeElapsed) >= 1.0f)
		{
			int fps = frameCount; // fps = frameCnt / 1
			float mspf = 1000.0f / fps;

			std::wstring fpsStr = std::to_wstring(fps);
			std::wstring mspfStr = std::to_wstring(mspf);

			windowText = 
				L"    fps: " + fpsStr +
				L"   mspf: " + mspfStr;

			// Reset for next average.
			frameCount = 0;
			timeElapsed += 1.0f;
		}
		return windowText;
	}
}