#pragma once

#include <string>

class ENGINE_API Logger
{
private:
	static Logger* instance;
public:
	static Logger* Instance() { return instance; }

public:
	Logger();
	~Logger();

	static VOID PrintLog(const WCHAR* fmt, ...);

	static std::wstring LogDirectory();
	static std::wstring LogFile();

	static VOID PrintDebugSeperator();
	static BOOL IsMTailRunning();
	static VOID StartMTail();
};