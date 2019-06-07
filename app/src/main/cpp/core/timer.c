/*
 *   timer.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "ops.h"
#include "io.h"								// I/O definitions

// HP71B memory address for clock
#define NXTIRQ		0x2F70D					// clock (12 nibbles)

static TIMECAPS tc;							// timer information

static HANDLE hEvent = NULL;				// event for shut down timer

static BOOL bAccurateTimer = FALSE;			// flag if accurate timer is used

static BOOL bStarted = FALSE;

static WORD  wCnt = 0;						// cycle counter for timing
static DWORD dwRefTime = 0;					// timestamp of 0 reference

VOID SetHPTime(VOID)						// set date and time
{
	SYSTEMTIME ts;
	LONGLONG   ticks;
	DWORD      dw;
	WORD       i;

	_ASSERT(sizeof(LONGLONG) == 8);			// check size of datatype

	GetLocalTime(&ts);						// local time, _ftime() cause memory/resource leaks

	// calculate days until 01.01.0000 (Erlang BIF localtime/0)
	dw = (DWORD) ts.wMonth;
	if (dw > 2)
		dw -= 3L;
	else
	{
		dw += 9L;
		--ts.wYear;
	}
	dw = (DWORD) ts.wDay + (153L * dw + 2L) / 5L;
	dw += (146097L * (((DWORD) ts.wYear) / 100L)) / 4L;
	dw +=   (1461L * (((DWORD) ts.wYear) % 100L)) / 4L;
	dw += (-719469L + 719528L);				// days from year 0

	ticks = (ULONGLONG) dw;					// convert to 64 bit

	// convert into seconds and add time
	ticks = ticks * 24L + (ULONGLONG) ts.wHour;
	ticks = ticks * 60L + (ULONGLONG) ts.wMinute;
	ticks = ticks * 60L + (ULONGLONG) ts.wSecond;

	// create timerticks = (s + ms) * 512
	ticks = (ticks << 9) | (((LONGLONG) ts.wMilliseconds << 6) / 125);

	// add actual timer value
	ticks += (LONG) Npack(&Chipset.dd[SLAVE2].IORam[TIMER2 & 0xFF],6);

	if (Chipset.type == 'T')				// Titan, HP71B
	{
		LPBYTE pbyNxtIrq = IRAM(NXTIRQ);	// HP address for clock (=NEXTIRQ) in RAM

		for (i = 0; i < 12; ++i)			// write date and time
		{
			*pbyNxtIrq++ = (BYTE) ticks & 0xf;
			ticks >>= 4;
		}
	}
	return;
}

static void CALLBACK TimeProc(UINT uEventId, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
	UINT  i,uNib,uDelay;
	DWORD dwTime,dwAddr;
	BOOL  bSReq,bWke;

	struct
	{
		DWORD dwTimerVal;					// timer value after update
		BYTE  byDecr;						// borrow for subtraction
		BYTE  byMsb;						// state of timer MSB before decr.
	}
	sState[ARRAYSIZEOF(Chipset.dd)];

	if (bStarted == FALSE)					// finish timer
	{
		_ASSERT(hEvent != NULL);
		SetEvent(hEvent);					// signal that TimeProc() will finish
		return;
	}

	if (uEventId == 0) return;				// illegal EventId

	bSReq = FALSE;							// no service request

	// initialize decrement register belonging to the 1LF3 chips
	for (i = 0; i < ARRAYSIZEOF(Chipset.dd); ++i)
	{
		_ASSERT(i < ARRAYSIZEOF(sState));

		sState[i].dwTimerVal = 0;			// packed timer value
		sState[i].byDecr = 1;				// decrement with borrow

		// timer MSB state before decrement
		sState[i].byMsb = Chipset.dd[i].IORam[(DCONTR - 1) & 0xFF] >> 3;
	}

	// lower 8 bit of all timers must point to the same address
	_ASSERT((TIMER1 & 0xFF) == (TIMER2 & 0xFF));
	_ASSERT((TIMER1 & 0xFF) == (TIMER3 & 0xFF));

	// update the 6 timer nibbles
	for (uNib = 0, dwAddr = (TIMER1 & 0xFF); dwAddr < (DCONTR & 0xFF); ++dwAddr, uNib += 4)
	{
		// update all timer internally independent if they are mapped or not
		for (i = 0; i < ARRAYSIZEOF(Chipset.dd); ++i)
		{
			EnterCriticalSection(&csTLock);
			{
				_ASSERT(i < ARRAYSIZEOF(sState));
				Chipset.dd[i].IORam[dwAddr] -= sState[i].byDecr;

				// no underflow
				if ((Chipset.dd[i].IORam[dwAddr]) <= 0x0F)
				{
					sState[i].byDecr = 0;	// clear borrow
				}
				else
				{
					// adjust timer nibble
					Chipset.dd[i].IORam[dwAddr] &= 0x0F;
				}

				// packed timer value
				sState[i].dwTimerVal |= (Chipset.dd[i].IORam[dwAddr] << uNib);
			}
			LeaveCriticalSection(&csTLock);
		}
	}

	// check wakeup condition of each 1LF3 chip
	for (bWke = FALSE, i = 0; i < ARRAYSIZEOF(Chipset.dd); ++i)
	{
		// one timer needs service
		bSReq |= (   (sState[i].dwTimerVal & 0x800000) != 0
				  && (Chipset.dd[i].IORam[DD1CTL & 0xFF] & WKE) != 0
				 );

		// WKE = rising edge of MSB timer + rising edge of bit3 in display-timer control nibble
		if (   (sState[i].dwTimerVal & 0x800000) != 0
			&& (sState[i].byMsb == 0 || Chipset.dd[i].bWkeEn)
		   )
		{
			Chipset.dd[i].bWkeEn = FALSE;	// wait for new rising edge of bit3 in display-timer control
			bWke = TRUE;					// wakeup condition
		}
	}

	ChangeBit(&Chipset.SREQ,0x01,bSReq);	// update service request of timer

	// cpu sleeping and T -> Wake Up
	if (Chipset.Shutdn && bWke)
	{
		Chipset.bShutdnWake = TRUE;			// wake up from SHUTDN mode
		SetEvent(hEventShutdn);				// wake up emulation thread
	}

	// calculate the delay for the next loop (timer frequency is 512Hz)
	dwTime = timeGetTime();					// the actual time stamp
	if (wCnt++ == 0)						// new reference time
	{
		dwRefTime = dwTime;					// the reference time
		uDelay = 1;							// 1 ms
	}
	else
	{
		uDelay = (wCnt * 1000) / 512;		// nominal time for next trigger since dwCnt = 0
		dwTime -= dwRefTime;				// elapsed time since dwCnt = 0
		if (uDelay > dwTime)				// nominal time > elapsed time
		{
			uDelay -= dwTime;				// calculate remain time
		}
		else
		{
			uDelay = 1;						// 1 ms minimum delay
		}
	}
	VERIFY(timeSetEvent(uDelay,0,(LPTIMECALLBACK)&TimeProc,0,TIME_ONESHOT));
	return;
	UNREFERENCED_PARAMETER(uMsg);
	UNREFERENCED_PARAMETER(dwUser);
	UNREFERENCED_PARAMETER(dw1);
	UNREFERENCED_PARAMETER(dw2);
}

VOID StartTimers(VOID)
{
	if (bStarted)							// timer running
		return;								// -> quit

	// creating auto event for timer shut down
	VERIFY(hEvent = CreateEvent(NULL,FALSE,FALSE,NULL));

	wCnt = 0;								// create new reference time
	bStarted = TRUE;						// flag timer running
	timeGetDevCaps(&tc,sizeof(tc));			// get timer resolution

	// set timer resolution to greatest possible one
	bAccurateTimer = (timeBeginPeriod(tc.wPeriodMin) == TIMERR_NOERROR);

	// time for a periodic calling is too short, so CPU thread my not get enough
	// time to read a value between two timer update calls

	// set timer immediately
	VERIFY(timeSetEvent(1,0,(LPTIMECALLBACK)&TimeProc,0,TIME_ONESHOT));
	return;
}

VOID StopTimers(VOID)
{
	if (!bStarted)							// timer stopped
		return;								// -> quit

	ResetEvent(hEvent);
	bStarted = FALSE;						// quit timer function at next call

	WaitForSingleObject(hEvent,100);		// wait for timer function finished

	if (bAccurateTimer)						// "Accurate timer" running
	{
		timeEndPeriod(tc.wPeriodMin);		// finish service
	}
	CloseHandle(hEvent);
	hEvent = NULL;
	return;
}
