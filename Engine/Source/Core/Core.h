#pragma once

#include "CoreDefinitions.h"

#include "Engine/EngineClass.h"

#include "Common/Logger.h"
#include "Common/Timer.h"
#include "Core/PerGameSettings.h"

#ifdef WIN32
#include "Platform/Win32/Win32Utils.h"
#include "Platform/Win32/SubObject.h"
#include "Platform/Win32/w32Caption.h"
#include "Platform/Win32/Window.h"
#include "Platform/Win32/IApplication.h"
#endif

#include "ImGui/ImguiManager.h"
#include "Graphics/Graphics.h"