#pragma once

#define ENTRYAPP(x) Win32::IApplication* EntryApplication() { return new x; }

namespace Win32
{
	class ENGINE_API IApplication
	{
	public:
		IApplication();
		virtual ~IApplication() {};

		virtual VOID SetupPerGameSettings() = 0;
		virtual VOID PreInitialize() = 0;
		virtual VOID Initialize() = 0;
		// Game Loop - Called on a loop while the Application is running
		virtual VOID Update() = 0;
	};

	IApplication* EntryApplication();
}
