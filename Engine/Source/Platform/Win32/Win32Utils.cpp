#include "Engine.h"
#include "Win32Utils.h"

namespace Win32
{
	namespace Utils
	{
		BOOL AddBitmap(const WCHAR* szFileName, HDC hWinDC, INT x, INT y)
		{
			BITMAP qBitMap;
			HDC hLocalDC = CreateCompatibleDC(hWinDC);

			HBITMAP hBitMap = (HBITMAP)LoadImage(NULL, szFileName, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
			if (hBitMap == NULL)
			{
				MessageBox(NULL, L"Load Image Failed", L"Error", MB_OK);
				return false;
			}
			GetObject(reinterpret_cast<HGDIOBJ>(hBitMap), sizeof(BITMAP), reinterpret_cast<LPVOID>(&qBitMap));

			SelectObject(hLocalDC, hBitMap);

			if (!BitBlt(hWinDC, x, y, qBitMap.bmWidth, qBitMap.bmHeight, hLocalDC, 0, 0, SRCCOPY))
			{
				MessageBox(NULL, L"Blit Failed", L"Error", MB_OK);
				return false;
			}

			::DeleteDC(hLocalDC);
			::DeleteObject(hBitMap);
			return true;
		}
	}
}
