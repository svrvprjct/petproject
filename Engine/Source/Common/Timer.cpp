#include "Engine.h"

#include <ctime>
#include <sstream>
#include <iomanip>

namespace Time
{
	/* Get Time in format '00:00:00' */
	/* Stripped = 000000 */
	std::wstring GetTime(BOOL stripped)
	{
		time_t now = time(0);
		tm ltm;
		localtime_s(&ltm, &now);
		std::wstringstream wss;
		wss << std::put_time(&ltm, L"%T");

		std::wstring timeString = wss.str();

		if (stripped)
		{
			std::wstring chars = L":";
			for (WCHAR c : chars)
			{
				timeString.erase(std::remove(timeString.begin(), timeString.end(), c), timeString.end());
			}
		}
		return timeString;
	}

	/* Get date in format '00/00/00' */
	/* Stripped = 000000 */
	std::wstring GetDate(BOOL stripped)
	{
		time_t now = time(0);
		tm ltm;
		localtime_s(&ltm, &now);
		std::wstringstream wss;
		wss << std::put_time(&ltm, L"%d/%m/%y");
		std::wstring timeString = wss.str();

		if (stripped)
		{
			std::wstring chars = L"/";
			for (WCHAR c : chars)
			{
				timeString.erase(std::remove(timeString.begin(), timeString.end(), c), timeString.end());
			}
		}

		return timeString;
	}

	/* Get date time in format '00/00/00 00:00:00' */
	/* Stripped = 000000000000 */
	std::wstring GetDateTimeString(BOOL stripped)
	{
		std::wstring timeString = GetDate(stripped) + L" " + GetTime(stripped);

		if (stripped)
		{
			std::wstring chars = L" ";
			for (WCHAR c : chars)
			{
				timeString.erase(std::remove(timeString.begin(), timeString.end(), c), timeString.end());
			}
		}

		return timeString;
	}
}

Timer::Timer() : m_SecondsPerCount(0.0), m_DeltaTime(-1.0), m_BaseTime(0), m_PausedTime(0),
	m_PreviousTime(0), m_CurrentTime(0), m_IsStopped(FALSE)
{
	INT64 countsPerSec;
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	m_SecondsPerCount = 1.0 / (DOUBLE)countsPerSec;
}

Timer::Timer(CONST Timer& other)
{
}


Timer::~Timer()
{
}

VOID Timer::Tick()
{
	if (m_IsStopped)
	{
		m_DeltaTime = 0.0;
		return;
	}
	// Get the time this frame.
	INT64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	m_CurrentTime = currTime;
	// Time difference between this frame and the previous.
	m_DeltaTime = (m_CurrentTime - m_PreviousTime) * m_SecondsPerCount;
	// Prepare for next frame.
	m_PreviousTime = m_CurrentTime;
	// Force nonnegative. The DXSDK’s CDXUTTimer mentions that if the 
	// processor goes into a power save mode or we get shuffled to
	// another processor, then mDeltaTime can be negative.
	if (m_DeltaTime < 0.0)
	{
		m_DeltaTime = 0.0;
	}
}

FLOAT Timer::DeltaTime() CONST
{
	return (FLOAT)m_DeltaTime;
}

VOID Timer::Reset()
{
	INT64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	m_BaseTime = currTime;
	m_PreviousTime = currTime;
	m_StopTime = 0;
	m_IsStopped = FALSE;
}

VOID Timer::Stop()
{
	// If we are already stopped, then don’t do anything.
	if (!m_IsStopped)
	{
		INT64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
		// Otherwise, save the time we stopped at, and set 
		// the Boolean flag indicating the timer is stopped.
		m_StopTime = currTime;
		m_IsStopped = TRUE;
	}
}

VOID Timer::Start()
{
	INT64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);
	// Accumulate the time elapsed between stop and start pairs.
	//
	//                |<-------d------->|
	// ---------------*-----------------*------------> time
	//    mStopTime                        startTime 
	// If we are resuming the timer from a stopped state...
	if (m_IsStopped)
	{
		// then accumulate the paused time.
		m_PausedTime += (startTime - m_StopTime);
		// since we are starting the timer back up, the current 
		// previous time is not valid, as it occurred while paused.
		// So reset it to the current time.
		m_PreviousTime = startTime;
		// no longer stopped...
		m_StopTime = 0;
		m_IsStopped = FALSE;
	}
}

FLOAT Timer::TotalTime() CONST
{
	// If we are stopped, do not count the time that has passed
	// since we stopped. Moreover, if we previously already had
	// a pause, the distance mStopTime - mBaseTime includes paused 
	// time,which we do not want to count. To correct this, we can
	// subtract the paused time from mStopTime: 
	//
	// previous paused time
	//                 |<----------->|
	// ---*------------*-------------*-------*-----------*------> time
	//      mBaseTime     mStopTime  mCurrTime
	if (m_IsStopped)
	{
		return (FLOAT)(((m_StopTime - m_PausedTime) - m_BaseTime) * m_SecondsPerCount);
	}
	// The distance mCurrTime - mBaseTime includes paused time,
	// which we do not want to count. To correct this, we can subtract 
	// the paused time from mCurrTime: 
	//
	// (mCurrTime - mPausedTime) - mBaseTime 
	//
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------> time
	//     mBaseTime       mStopTime         startTime    mCurrTime
	else
	{
		return (FLOAT)(((m_CurrentTime - m_PausedTime) - m_BaseTime) * m_SecondsPerCount);
	}
}

