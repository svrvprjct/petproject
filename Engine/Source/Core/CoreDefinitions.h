#pragma once

#ifdef BUILD_DLL
  #define ENGINE_API __declspec(dllexport)
#else
  #define ENGINE_API __declspec(dllimport)
#endif BUILD_DLL

#define MAX_NAME_STRING 256
#define HInstance() GetModuleHandle(NULL)
#define SCREEN_DEPTH 1000.0f
#define SCREEN_NEAR = 0.1f

const int gNumFrameResources = 3;
