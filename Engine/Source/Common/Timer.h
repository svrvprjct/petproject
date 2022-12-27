#pragma once
#include <string>

namespace Time
{
	std::wstring ENGINE_API GetTime(BOOL stripped = FALSE);

	std::wstring ENGINE_API GetDate(BOOL stripped = FALSE);

	std::wstring ENGINE_API GetDateTimeString(BOOL stripped = FALSE);
}

class ENGINE_API Timer
{
public:
	Timer();
	Timer(CONST Timer&);
	~Timer();

	FLOAT TotalTime() CONST; // in seconds
	FLOAT DeltaTime() CONST; // in seconds

	VOID Reset(); // Call before message loop.
	VOID Start(); // Call when unpaused.
	VOID Stop();  // Call when paused.
	VOID Tick();  // Call every frame.

private:
	DOUBLE m_SecondsPerCount;
	DOUBLE m_DeltaTime;

	INT64 m_BaseTime; 
	INT64 m_PausedTime;
	INT64 m_StopTime;
	INT64 m_PreviousTime;
	INT64 m_CurrentTime;

	BOOL m_IsStopped;
};