#include "Engine.h"
#include "Simulation.h"
#include "SplashScreen.h"

namespace Engine
{
	Simulation::Simulation()
		: Win32::Window(L"MainApplication", NULL)
	{
	}

	Simulation::~Simulation()
	{
	}

	VOID Simulation::SetupPerGameSettings()
	{
		return;
	}

	VOID Simulation::PreInitialize()
	{
		Logger::PrintDebugSeperator();
		Logger::PrintLog(L"Application starting...");
		Logger::PrintLog(L"Game Name: %s\n", PerGameSettings::GameName());
		Logger::PrintLog(L"Boot Time: %s\n", Time::GetDateTimeString().c_str());
		Logger::PrintLog(L"Engine Mode: %s\n", Engine::EngineModeToString().c_str());
		Logger::PrintDebugSeperator();
		
		SplashScreen::Open();
		Win32::Window::RegisterNewClass();
		Win32::Window::Initialize();
	}

	VOID Simulation::Initialize()
	{
		Logger::PrintLog(L"Initialize");
		Logger::PrintDebugSeperator();
		Graphics::GraphicsClass::Initialize(Win32::Window::Handle(), Win32::Window::Size().cx, Win32::Window::Size().cy);
		Graphics::GraphicsClass::m_Timer.Reset();
		SplashScreen::Close();
		ShowWindow(Handle(), SW_NORMAL);
	}

	VOID Simulation::Update()
	{
		FpsMspfText(Graphics::GraphicsClass::CalculateFrameStats());
		Graphics::GraphicsClass::Run();
	}

	LRESULT Simulation::MessageHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
			return true;

		switch (message)
		{
			// WM_ACTIVATE is sent when the window is activated or deactivated.  
			// We pause the game when the window is deactivated and unpause it 
			// when it becomes active.  
			case WM_ACTIVATE:	{ OnActivate(wParam); }			return 0;
			// WM_SIZE is sent when the user resizes the window.  
			case WM_SIZE:		{ OnSize(wParam, lParam); }		return 0;
			// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
			case WM_ENTERSIZEMOVE:	{ OnEnterSizeMove(); }			return 0;
			// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
			// Here we reset everything based on the new window dimensions.
			case WM_EXITSIZEMOVE:	{ OnExitSizeMove(); }			return Window::MessageHandler(hWnd, message, wParam, lParam);
			case WM_KEYUP:		{ OnKeyUp(wParam); }			return 0;
			case WM_KEYDOWN:	{ OnKeyDown(message, wParam); }		return 0;
			// WM_DESTROY is sent when the window is being destroyed.
			case WM_NCPAINT:	{ }					return Window::MessageHandler(hWnd, message, wParam, lParam);
			case WM_DESTROY:	{ PostQuitMessage(0); }			return 0;
			case WM_RBUTTONDOWN:	{ OnRButtonDown(wParam, lParam); }	return 0;
			case WM_RBUTTONUP:	{ OnRButtonUp(wParam, lParam); }	return 0;
			case WM_MOUSEMOVE:	{ OnMouseMove(wParam, lParam); }	return 0;
			case WM_MOUSEWHEEL:	{ OnMouseWheel(wParam, lParam); }	return 0;
		}

		return Window::MessageHandler(hWnd, message, wParam, lParam);
	}


	VOID Simulation::OnActivate(WPARAM wParam)
	{
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			Graphics::D3DClass::SetAppPaused(true);
			Graphics::D3DClass::m_Timer.Stop();
		}
		else
		{
			Graphics::D3DClass::SetAppPaused(false);
			Graphics::D3DClass::m_Timer.Start();
		}
	}

	VOID Simulation::OnSize(WPARAM wParam, LPARAM lParam)
	{
		// Save the new client area dimensions.
		Graphics::GraphicsClass::m_ClientWidth = LOWORD(lParam);
		Graphics::GraphicsClass::m_ClientHeight = HIWORD(lParam);
		if (Graphics::GraphicsClass::m_d3dDevice)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				Graphics::GraphicsClass::SetAppPaused(true);
				Graphics::GraphicsClass::SetMinimized(true);
				Graphics::GraphicsClass::SetMaximized(false);
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				Graphics::GraphicsClass::SetAppPaused(false);
				Graphics::GraphicsClass::SetMinimized(false);
				Graphics::GraphicsClass::SetMaximized(true);
				Graphics::GraphicsClass::OnResize();
			}
			else if (wParam == SIZE_RESTORED)
			{

				// Restoring from minimized state?
				if (Graphics::GraphicsClass::GetMinimized())
				{
					Graphics::GraphicsClass::SetAppPaused(false);
					Graphics::GraphicsClass::SetMinimized(false);
					Graphics::GraphicsClass::OnResize();
				}

				// Restoring from maximized state?
				else if (Graphics::GraphicsClass::GetMinimized())
				{
					Graphics::GraphicsClass::SetAppPaused(false);
					Graphics::GraphicsClass::SetMaximized(false);
					Graphics::GraphicsClass::OnResize();
				}
				else if (Graphics::GraphicsClass::GetResizing())
				{
					// If user is dragging the resize bars, we do not resize 
					// the buffers here because as the user continuously 
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is 
					// done resizing the window and releases the resize bars, which 
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					Graphics::GraphicsClass::OnResize();
				}
			}
		}
	}

	VOID Simulation::OnEnterSizeMove()
	{
		Graphics::GraphicsClass::SetAppPaused(true);
		Graphics::GraphicsClass::SetResizing(true);
		Graphics::GraphicsClass::m_Timer.Stop();
	}

	VOID Simulation::OnExitSizeMove()
	{
		Graphics::GraphicsClass::SetAppPaused(false);
		Graphics::GraphicsClass::SetResizing(false);
		Graphics::GraphicsClass::m_Timer.Start();
		Graphics::GraphicsClass::OnResize();
	}

	VOID Simulation::OnKeyUp(WPARAM wParam)
	{
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
	}

	VOID Simulation::OnKeyDown(UINT message, WPARAM wParam)
	{
	}

	VOID Simulation::OnRButtonDown(WPARAM wParam, LPARAM lParam)
	{
		Graphics::GraphicsClass::OnRightMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	}

	VOID Simulation::OnRButtonUp(WPARAM wParam, LPARAM lParam)
	{
		Graphics::GraphicsClass::OnRightMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	}

	VOID Simulation::OnMouseMove(WPARAM wParam, LPARAM lParam)
	{
		Graphics::GraphicsClass::OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), GET_WHEEL_DELTA_WPARAM(wParam));
	}

	VOID Simulation::OnMouseWheel(WPARAM wParam, LPARAM lParam)
	{
		Graphics::GraphicsClass::OnMouseWheel(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), GET_WHEEL_DELTA_WPARAM(wParam));
	}
}

