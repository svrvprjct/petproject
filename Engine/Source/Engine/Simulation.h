#pragma once

namespace Engine
{
	class ENGINE_API Simulation : public Win32::IApplication, public Win32::Window, public Graphics::GraphicsClass
	{
	public:
		Simulation();
		~Simulation();

		virtual VOID SetupPerGameSettings() override;
		virtual VOID PreInitialize() override;
		virtual VOID Initialize() override;
		virtual VOID Update() override;
		virtual LRESULT MessageHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;

	protected:
		VOID OnActivate(WPARAM wParam);
		VOID OnSize(WPARAM wParam, LPARAM lParam);
		VOID OnEnterSizeMove();
		VOID OnExitSizeMove();
		VOID OnKeyUp(WPARAM wParam);
		VOID OnKeyDown(UINT message, WPARAM wParam);
		VOID OnRButtonDown(WPARAM wParam, LPARAM lParam);
		VOID OnRButtonUp(WPARAM wParam, LPARAM lParam);
		VOID OnMouseMove(WPARAM wParam, LPARAM lParam);
		VOID OnMouseWheel(WPARAM wParam, LPARAM lParam);
	};
}