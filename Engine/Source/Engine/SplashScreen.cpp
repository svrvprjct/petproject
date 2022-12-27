#include "Engine.h"
#include "SplashScreen.h"
#include "Platform/Win32/Win32Utils.h"

namespace SplashScreen
{
	#define WM_OUTPUTMESSAGE (WM_USER + 0x0001)

	SplashWindow* m_SplashWindow;

	VOID Open()
	{
		if (m_SplashWindow != nullptr)
			return;
		m_SplashWindow = new SplashWindow();
		SendMessage(m_SplashWindow->Handle(), WM_PAINT, 0, 0);
	}

	VOID Close()
	{
		SendMessage(m_SplashWindow->Handle(), WM_CLOSE, 0, 0);
	}

	VOID AddMessage(const WCHAR* message)
	{
		PostMessage(m_SplashWindow->Handle(), WM_OUTPUTMESSAGE, (WPARAM)message, 0);
	}
}

SplashWindow::SplashWindow()
	: Win32::Window(L"SplashScreen", NULL, Win32::WindowType::POPUP)
{
	wcscpy_s(m_OutputMessage, L"SplashScreen Starting...");
	Win32::Window::RegisterNewClass();
	Size(500, 600);
	Initialize();
}

SplashWindow::~SplashWindow()
{
}

LRESULT SplashWindow::MessageHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_PAINT:
		{
			HBITMAP hbitmap;
			HDC hdc, hmemdc;
			PAINTSTRUCT ps;

			hdc = BeginPaint(hWnd, &ps);

			Win32::Utils::AddBitmap(PerGameSettings::SplashURL(), hdc);

			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, RGB(255, 255, 255));

			if (Engine::GetMode() != Engine::EngineMode::RELEASE)
			{
				std::wstring engineModeText = Engine::EngineModeToString() + L" Mode";
				SetTextAlign(hdc, TA_RIGHT);
				TextOut(hdc, Size().cx - 15, 15, engineModeText.c_str(), wcslen(engineModeText.c_str()));
			}

			SetTextAlign(hdc, TA_CENTER);

			TextOut(hdc, Size().cx / 2, Size().cy - 30, m_OutputMessage, wcslen(m_OutputMessage));
			EndPaint(hWnd, &ps);
		}
		break;

		case WM_OUTPUTMESSAGE:
		{
			WCHAR* msg = (WCHAR*)wParam;
			wcscpy_s(m_OutputMessage, msg);
			RedrawWindow();
			return 0;
		}
		break;
	}

	return Window::MessageHandler(hWnd, message, wParam, lParam);
}

void SplashWindow::Initialize()
{
	RECT desktop;
	const HWND hDesctop = GetDesktopWindow();
	GetWindowRect(hDesctop, &desktop);

	RECT R = { 0, 0, Size().cx, Size().cy };
	AdjustWindowRect(&R, m_Type, false);
	int width = R.right - R.left;
	int height = R.bottom - R.top;

	m_Handle = CreateWindow
	(
		m_Class.c_str(),
		m_Title.c_str(),
		m_Type,
		((desktop.right / 2) - (Size().cx / 2)),
		((desktop.bottom / 2) - (Size().cy / 2)),
		Size().cx,
		Size().cy,
		NULL,
		NULL,
		HInstance(),
		(void*)this
	);

	if (!m_Handle)
	{
		MessageBox(0, L"Call to Create Window failed!", 0, 0);
		PostQuitMessage(0);
	}

	ShowWindow(Handle(), SW_SHOW);
}
