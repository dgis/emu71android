/*
 *   external.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gieﬂelink
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "ops.h"
#include "io.h"

#define CPU_FREQ			625000			// CPU frequency (Hz)
#define flBEEP				0xFE			// beeper flag (-2)
#define FLGREG				0x2F6E9			// flag register address

static BOOL GetFlag(BYTE byFlag)			// =GTFLAG
{
	DWORD dwAddr;
	BYTE  byMask;

	dwAddr = FLGREG - 0x10;					// 2nd bank of flag register (system flags)
	byFlag = ~byFlag;						// 1'complement of flag (was negative for system flags)

	// calculate memory address and bit mask
	dwAddr += byFlag / 4;					// address of flag
	byMask = 1 << (byFlag & 0x3);			// mask of flag

	Npeek(&byFlag,dwAddr,sizeof(byFlag));
	return (byFlag & byMask) != 0;
}

VOID External0(CHIPSET* w)					// =RCKBp patch
{
	DWORD dwF;
	DWORD freq,dur;

	dwF = 60 + 33 * w->C[1];				// F

	freq = (CPU_FREQ / 2) / dwF;			// frequency in Hz
	dur = dwF * (256 - 16 * w->C[0]) * 1000	// duration in ms
		/ CPU_FREQ;

	SoundBeep(freq,dur);					// beeping

	// estimate cpu cycles for beeping time (765KHz)
	w->cycles += (T2CYCLES * 16384 * dur) / 1000;

	// original routine return with...
	w->P = 0;								// P=	0
	Nunpack(w->C,0x00F,3);					// LC(3) #00F
	w->out = 0x00F;							// OUT=C
	ScanKeyboard(FALSE,FALSE);
	w->carry = FALSE;						// RTNCC
	w->pc = rstkpop();
	return;
}

VOID External1(CHIPSET* w)					// =BP+C patch
{
	DWORD freq,dur;

	freq = Npack(w->D,5);					// frequency in Hz
	dur = Npack(w->C,5);					// duration in ms

	w->P = 0;								// P=	0

	if (   freq == 0						// 0 Hz
		|| GetFlag(flBEEP) == TRUE)			// beeper off
	{
		w->carry = TRUE;					// RTNC
	}
	else
	{
		SoundBeep(freq,dur);				// beeping

		// estimate cpu cycles for beeping time (765KHz)
		w->cycles += (T2CYCLES * 16384 * dur) / 1000;

		// original routine return with...
		Nunpack(w->C,0x00F,3);				// LC(3) #00F
		w->out = 0x00F;						// OUT=C
		ScanKeyboard(FALSE,FALSE);
		w->carry = FALSE;					// RTNCC
	}
	w->pc = rstkpop();
	return;
}
