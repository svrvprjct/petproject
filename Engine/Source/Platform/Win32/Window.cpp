#include "Engine.h"
#include "Window.h"
#include "ImGui/imgui_impl_win32.h"

#define DCX_USESTYLE 0x00010000 

namespace Win32
{
	Window::Window(std::wstring title, HICON icon, WindowType type)
		: Win32::SubObject(title, title, icon), m_Type(type)
	{
		Size(DEFAULTWIDTH, DEFAULTHEIGHT);
	}

	Window::~Window()
	{
	}


	VOID Window::Initialize()
	{
		RECT desktop;
		const HWND hDesctop = GetDesktopWindow();
		GetWindowRect(hDesctop, &desktop);

		RECT R = { 0, 0, Size().cx, Size().cy};
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
		ShowWindow(Handle(), SW_SHOWMINIMIZED);
		// Init ImGui Win32
		ImGui_ImplWin32_Init(Handle());
	}

	LRESULT Window::MessageHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
			case WM_NCCREATE:	 { OnNonClientCreate(); }				 return TRUE;
			case WM_NCACTIVATE:	 { OnNonClientActivate(LOWORD(wParam) != WA_INACTIVE); } return TRUE;
			case WM_NCPAINT:	 { OnNonClientPaint((HRGN)wParam); }			 return FALSE;
			case WM_NCLBUTTONDOWN:	 { OnNonClientLeftMouseButtonDown(); }			 break;
			case WM_NCLBUTTONDBLCLK: { Utils::MaximizeWindow(Handle()); }			 return 0;
			case WM_GETMINMAXINFO:	 { OnGetMinMaxInfo((MINMAXINFO*)lParam); }		 return 0;
			case WM_EXITSIZEMOVE:	 { OnExitSizeMove(); }					 break;
			case WM_PAINT:		 { OnPaint(); }						 break;
			case WM_TIMER:		 { RedrawWindow(); }					 break;	
		}

		return SubObject::MessageHandler(hWnd, message, wParam, lParam);
	}

	VOID Window::RedrawWindow()
	{
		SetWindowPos(Handle(), 0, 0, 0, 0, 0, 
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_DRAWFRAME | SWP_FRAMECHANGED); // reset window
		SendMessage(Handle(), WM_PAINT, 0, 0);
	}

	VOID Window::OnNonClientCreate()
	{
		SetTimer(Handle(), 1, 100, NULL);
		SetWindowTheme(Handle(), L"", L"");
		Utils::ModifyClassStyle(Handle(), 0, CS_DROPSHADOW);
		Caption::AddCaptionButton(new CaptionButton(L"x", CB_CLOSE));
		Caption::AddCaptionButton(new CaptionButton(L"🗖", CB_MAXIMIZE));
		Caption::AddCaptionButton(new CaptionButton(L"🗕", CB_MINIMIZE));
	}

	VOID Window::OnNonClientActivate(BOOL active)
	{
		Active(active);
	}

	VOID Window::OnNonClientPaint(HRGN region)
	{
		// Start Draw
		HDC hdc = GetDCEx(Handle(), region, DCX_WINDOW | DCX_INTERSECTRGN | DCX_USESTYLE);

		RECT rect;
		GetWindowRect(Handle(), &rect);

		SIZE size = SIZE{ rect.right - rect.left, rect.bottom - rect.top };

		HBITMAP hbmMem = CreateCompatibleBitmap(hdc, size.cx, size.cy);
		HANDLE hOld = SelectObject(hdc, hbmMem);

		// Draw
		HBRUSH brush = CreateSolidBrush(RGB(100, 100, 100));

		RECT newRect = RECT{ 0, 0, size.cx, size.cy };

		FillRect(hdc, &newRect, brush);

		if (Active() && !Utils::IsWindowFullscreen(Handle()))
		{
			brush = CreateSolidBrush(RGB(0, 100, 150));
			FrameRect(hdc, &newRect, brush);
		}

		PaintCaption(hdc);

		DeleteObject(brush);

		// End Draw
		BitBlt(hdc, 0, 0, size.cx, size.cy, hdc, 0, 0, SRCCOPY);
		DeleteObject(hbmMem);

		ReleaseDC(Handle(), hdc);
	}

	VOID Window::PaintCaption(HDC hdc)
	{
		RECT rect;
		GetWindowRect(Handle(), &rect);

		SIZE size = SIZE{ rect.right - rect.left, rect.bottom - rect.top };

		if (ShowTitle())
		{
			rect = RECT{ 0, 0, size.cx, 30 };

			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, Active() ? RGB(255, 255, 255) : RGB(90, 90, 90));
			DrawText(hdc, m_Title.c_str(), wcslen(m_Title.c_str()), &rect, 
				DT_SINGLELINE | DT_VCENTER | DT_CENTER);

			rect = RECT{ 10, 0, size.cx, 30 };
			DrawText(hdc, FpsMspfText().c_str(), wcslen(FpsMspfText().c_str()), &rect, 
				DT_SINGLELINE | DT_VCENTER | DT_LEFT);
		}

		int spacing = 0;

		POINT point;
		GetCursorPos(&point);

		GetWindowRect(Handle(), &rect);

		point.x -= rect.left;
		point.y -= rect.top;

		for (auto& button : Caption::CaptionButtons())
		{
			spacing += button->Width;

			button->Rect = RECT{ size.cx - spacing, 0, size.cx - spacing + button->Width, 30};

			if (button->Rect.left < point.x && button->Rect.right > point.x &&
					button->Rect.top < point.y && button->Rect.bottom > point.y)
			{
				HBRUSH brush = CreateSolidBrush(RGB(90, 90, 90));
				FillRect(hdc, &button->Rect, brush);
				DeleteObject(brush);
			}

			if (button->Text.compare(L"🗖") == 0 && Utils::IsWindowFullscreen(Handle()))
			{
				button->Text = L"🗗";
			}
			else if (button->Text.compare(L"🗗") == 0 && !Utils::IsWindowFullscreen(Handle())) 
			{
				button->Text = L"🗖";
			}

			DrawText(hdc, button->Text.c_str(), wcslen(button->Text.c_str()), &button->Rect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
		}
	}

	VOID Window::OnNonClientLeftMouseButtonDown()
	{
		int spacing = 0;
		RECT rect;

		POINT point;
		GetCursorPos(&point);

		GetWindowRect(Handle(), &rect);

		point.x -= rect.left;
		point.y -= rect.top;

		for (auto& button : Caption::CaptionButtons())
		{
			if (button->Rect.left < point.x && button->Rect.right > point.x &&
				button->Rect.top < point.y && button->Rect.bottom > point.y)
			{
				switch (button->Command)
				{
				case CB_CLOSE:	  { SendMessage(Handle(), WM_CLOSE, 0, 0); }  break;
				case CB_MAXIMIZE: { Win32::Utils::MaximizeWindow(Handle()); } break;
				case CB_MINIMIZE: { ShowWindow(Handle(), SW_MINIMIZE); }	  break;
				}
			}
		}
	}

	VOID Window::OnGetMinMaxInfo(MINMAXINFO* minmax)
	{
		RECT WorkArea;
		SystemParametersInfo(SPI_GETWORKAREA, 0, &WorkArea, 0);
		minmax->ptMaxSize.x = (WorkArea.right - WorkArea.left);
		minmax->ptMaxSize.y = (WorkArea.bottom - WorkArea.top);
		minmax->ptMaxPosition.x = WorkArea.left;
		minmax->ptMaxPosition.y = WorkArea.top;
		minmax->ptMinTrackSize.x = 400;
		minmax->ptMinTrackSize.x = 300;
	}

	VOID Window::OnExitSizeMove()
	{
		RECT rect;
		GetWindowRect(Handle(), &rect);
		RECT WorkArea;
		SystemParametersInfo(SPI_GETWORKAREA, 0, &WorkArea, 0);
		if (rect.top < WorkArea.top + 10 && !Utils::IsWindowFullscreen(Handle()))
		{
			Utils::MaximizeWindow(Handle());
		}
	}

	VOID Window::OnPaint()
	{
		PAINTSTRUCT paintstruct;
		HDC hdc = BeginPaint(Handle(), &paintstruct);

		RECT rect;
		GetClientRect(Handle(), &rect);

		HBRUSH brush = CreateSolidBrush(RGB(40, 40, 40));

		FillRect(hdc, &rect, brush);

		DeleteObject(brush);

		EndPaint(Handle(), &paintstruct);
	}
}
