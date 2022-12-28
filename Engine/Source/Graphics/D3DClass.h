#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "D3DUtils.h"
#include "ImGui/ImguiManager.h"

// Link necessary d3d12 libraries.
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

namespace Graphics
{
	class ENGINE_API D3DClass
	{
	protected:
		D3DClass();
		D3DClass(const D3DClass& rhs) = delete;
		D3DClass& operator=(const D3DClass& rhs) = delete;
		virtual ~D3DClass();

	public:
		virtual void Initialize(HWND mainWnd, int width, int height);
		void Run();
		
		HWND MainWnd() const;
		float AspectRatio() const;
		bool GetAppPaused();
		void SetAppPaused(bool value);
		bool GetMinimized();
		void SetMinimized(bool value);
		bool GetMaximized();
		void SetMaximized(bool value);
		bool GetResizing();
		void SetResizing(bool value);
		bool Get4xMsaaState() const;
		void Set4xMsaaState(bool value);
		bool Get4xMsaaQuality() const;
		void Set4xMsaaQuality(UINT value);

	protected:
		virtual void Update(const Timer& gameTimer) = 0;
		virtual void Draw(const Timer& gameTimer) = 0;
		virtual void OnResize();
		virtual void CreateRtvAndDsvAndSrvDescriptorHeaps();

		virtual void OnRightMouseDown(WPARAM buttonState, int x, int y) = 0;
		virtual void OnRightMouseUp(WPARAM buttonState, int x, int y) = 0;
		virtual void OnMouseMove(WPARAM buttonState, int x, int y, int z) = 0;
		virtual void OnMouseWheel(WPARAM buttonState, int x, int y, int z) = 0;

	protected:
		void InitDirect3D();
		void CreateCommandObjects();
		void CreateSwapChain();
		void FlushCommandQueue();

		ID3D12Resource* CurrentBackBuffer() const;
		D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
		D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

		void LogAdapters();
		void LogAdapterOutputs(IDXGIAdapter* adapter);
		void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);

		std::wstring CalculateFrameStats();

	protected:
		ImguiManager m_ImguiManager;
		Timer m_Timer;

		Microsoft::WRL::ComPtr<ID3D12Device> m_d3dDevice;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_DirectCommandListAllocator;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;
		Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
		UINT64 m_CurrentFence = 0;

		D3D12_VIEWPORT m_ScreenViewport;
		D3D12_RECT m_ScissorRect;

		ID3D12DescriptorHeap* m_ShaderResourceViewHeap = NULL;

		static const int swapChainBufferCount = 2;
		int m_CurrentBackBuffer = 0;
		Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;

		int m_ClientWidth = 800;
		int m_ClientHeight = 600;

		// Derived class should set these in derived constructor to customize starting values.
		D3D_DRIVER_TYPE m_d3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
		DXGI_FORMAT m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	private:
		HWND m_hMainWnd = nullptr; // main window handle
		bool m_AppPaused = false; // is the application paused?
		bool m_Minimized = false; // is the application minimized?
		bool m_Maximized = false; // is the application maximized?
		bool m_Resizing = false; // are the resize bars being dragged?
		bool m_FullscreenState = false; // fullscreen enabled

		bool m_4xMsaaState = false; // 4X MSAA enabled
		UINT m_4xMsaaQuality = 0;

		Microsoft::WRL::ComPtr<IDXGIFactory4> m_dxgiFactory;

		Microsoft::WRL::ComPtr<ID3D12Resource> m_SwapChainBuffer[swapChainBufferCount];
		Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthStencilBuffer;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RenderTargetViewHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DepthStencilViewHeap;

		UINT m_RenderTargetViewDescriptorSize = 0;
		UINT m_DepthStencilViewDescriptorSize = 0;
		UINT m_CbvSrvUavDescriptorSize = 0;
	};
}
