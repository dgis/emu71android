/*
 *   keyboard.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "io.h"								// I/O definitions

DWORD dwKeyMinDelay = 50;					// minimum time for key hold in ms

static WORD Keyboard_GetIR(VOID)
{
	WORD r = 0;

	// OR[0:11] are wired on Saturn CPU
	if (Chipset.out==0) return 0;
	if (Chipset.out&0x001) r|=Chipset.Keyboard_Row[0];
	if (Chipset.out&0x002) r|=Chipset.Keyboard_Row[1];
	if (Chipset.out&0x004) r|=Chipset.Keyboard_Row[2];
	if (Chipset.out&0x008) r|=Chipset.Keyboard_Row[3];
//	if (Chipset.out&0x010) r|=Chipset.Keyboard_Row[4];
//	if (Chipset.out&0x020) r|=Chipset.Keyboard_Row[5];
//	if (Chipset.out&0x040) r|=Chipset.Keyboard_Row[6];
//	if (Chipset.out&0x080) r|=Chipset.Keyboard_Row[7];
//	if (Chipset.out&0x100) r|=Chipset.Keyboard_Row[8];
//	if (Chipset.out&0x200) r|=Chipset.Keyboard_Row[9];
	_ASSERT(4 == ARRAYSIZEOF(Chipset.Keyboard_Row));

	// out = 0x400 and out = 0x800 are reserved for beeper
	return r;
}

VOID ScanKeyboard(BOOL bActive, BOOL bReset)
{
	// bActive = TRUE  -> function called by direct read (A=IN, C=IN, RSI)
	//           FALSE -> function called by 1ms keyboard poll simulation
	// bReset  = TRUE  -> Reset Chipset.in interrupt state register
	//           FALSE -> generate interrupt only for new pressed keys

	// Chipset.IR15X interrupts are alway handled because OR0 is always driven
	// high at executing an opcode independent from the out register state

	// keyboard read not active?
	if (!(bActive || Chipset.Shutdn || Chipset.IR15X || Chipset.intk))
	{
		EnterCriticalSection(&csKeyLock);
		{
			Chipset.in &= ~0x8000;			// remove ON key
		}
		LeaveCriticalSection(&csKeyLock);
		return;
	}

	EnterCriticalSection(&csKeyLock);		// synchronize
	{
		BOOL bKbdInt;

		WORD wOldIn = Chipset.in;			// save old Chipset.in state

		Chipset.in = Keyboard_GetIR();		// update Chipset.in register
		if (Chipset.out != 0)				// ON key is wired to the OR0 output pin
		{
			Chipset.in |= Chipset.IR15X;	// add ON key to in register
		}

		// interrupt for any new pressed keys?
		bKbdInt = (Chipset.in && wOldIn == 0) || bReset; // 1LK7, Chipset.IR15X edge triggered

		// update keyboard interrupt pending flag when 1ms keyboard scan is disabled
		Chipset.intd = Chipset.intd || (bKbdInt && !Chipset.intk);

		// keyboard interrupt enabled?
		bKbdInt = bKbdInt && Chipset.intk;

		// 1LK7, interrupt at rising edge of ON key
		bKbdInt = bKbdInt || (wOldIn == 0 && Chipset.IR15X != 0);

		// no interrupt if still inside interrupt service routine
		bKbdInt = bKbdInt && Chipset.inte;

		if (Chipset.in != 0)				// any key pressed
		{
			if (bKbdInt)					// interrupt enabled
			{
				Chipset.SoftInt = TRUE;		// interrupt request
				bInterrupt = TRUE;			// exit emulation loop
			}

			if (Chipset.Shutdn)				// cpu sleeping
			{
				Chipset.bShutdnWake = TRUE;	// wake up from SHUTDN mode
				SetEvent(hEventShutdn);		// wake up emulation thread
			}
		}
		else
		{
			Chipset.intd = FALSE;			// no keyboard interrupt pending
		}
	}
	LeaveCriticalSection(&csKeyLock);

	return;
}

VOID KeyboardEvent(BOOL bPress, UINT out, UINT in)
{
	if (nState != SM_RUN)					// not in running state
		return;								// ignore key

	KeyMacroRecord(bPress,out,in);			// save all keyboard events

	if ((in & 0x8000) != 0)					// ON key ?
	{
		Chipset.IR15X = bPress?0x8000:0x0000; // refresh special ON key flag
	}

	// "out" is outside Keyboard_Row
	if (out >= ARRAYSIZEOF(Chipset.Keyboard_Row)) return;

	_ASSERT(out < ARRAYSIZEOF(Chipset.Keyboard_Row));
	if (bPress)								// key pressed
		Chipset.Keyboard_Row[out] |= in;	// set key marker in keyboard row
	else
		Chipset.Keyboard_Row[out] &= (~in);	// clear key marker in keyboard row

	AdjKeySpeed();							// adjust key repeat speed
	ScanKeyboard(FALSE,FALSE);				// update Chipset.in register by 1ms keyboard poll
	Sleep(dwKeyMinDelay);					// hold key state for a definite time
	return;
}
