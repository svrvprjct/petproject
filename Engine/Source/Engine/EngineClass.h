#pragma once

class ENGINE_API EngineClass;

namespace Engine
{
	enum EngineMode : INT
	{
		NONE,
		DEBUG,
		RELEASE,
		EDITOR
	};

	extern EngineClass g_Engine;

	VOID ENGINE_API SetMode(EngineMode mode);
	EngineMode ENGINE_API GetMode();
	std::wstring ENGINE_API EngineModeToString();
}

using namespace Engine;

class ENGINE_API EngineClass
{
public:
	EngineClass();
	~EngineClass();

public:
	EngineMode GetEngineMode();
	VOID SetEngineMode(EngineMode mode);

private:
	EngineMode m_EngineMode;
};