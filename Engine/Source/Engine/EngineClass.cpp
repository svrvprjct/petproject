#include "Engine.h"

namespace Engine
{
	EngineClass g_Engine;

	VOID ENGINE_API SetMode(EngineMode mode)
	{
		g_Engine.SetEngineMode(mode);
	}

	EngineMode ENGINE_API GetMode()
	{
		return g_Engine.GetEngineMode();
	}

	std::wstring ENGINE_API EngineModeToString()
	{
		switch (Engine::GetMode())
		{
			case EngineMode::DEBUG:		return L"Debug";
			case EngineMode::EDITOR:	return L"Editor";
			case EngineMode::RELEASE:	return L"Release";
			default:			return L"None";
		}
	}
}

EngineClass::EngineClass()
{
	#ifdef _DEBUG
		m_EngineMode = EngineMode::DEBUG;
	#else
		m_EngineMode = EngineMode::RELEASE;
	#endif
}

EngineClass::~EngineClass()
{
}

EngineMode EngineClass::GetEngineMode()
{
	return m_EngineMode;
}

VOID EngineClass::SetEngineMode(EngineMode mode)
{
	m_EngineMode = mode;
}
