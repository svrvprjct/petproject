#pragma once

#define CB_CLOSE 0
#define CB_MINIMIZE 1
#define CB_MAXIMIZE 2 

namespace Win32
{
	class ENGINE_API Caption
	{
	public:
		struct CaptionButton
		{
			std::wstring Text = L"X";
			INT Command = -1;
			INT Width = 50;
			RECT Rect;

			CaptionButton(std::wstring text, INT command, INT width = 50)
			{
				Text = text;
				Command = command;
				Width = width;
			}
		};

		BOOL ShowTitle() { return m_ShowTitle; }
		VOID ShowTitle(BOOL show) { m_ShowTitle = show; }
		VOID AddCaptionButton(CaptionButton* button);
		std::list<CaptionButton*> CaptionButtons() { return m_CaptionButtons; }
		VOID FpsMspfText(std::wstring text) { fpsMspfText = text; }
		std::wstring FpsMspfText() { return fpsMspfText; }

	private:
		BOOL m_ShowTitle = TRUE;
		std::list<CaptionButton*> m_CaptionButtons;
		std::wstring fpsMspfText = L"0";
	};
}