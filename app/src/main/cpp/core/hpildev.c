/*
 *   hpildev.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2017 Christoph Gießelink
 *   Copyright (C) HPIL device emulation, J-F Garnier 2008
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "hpil.h"

// #define DEBUG_HPILDEV

#if defined DEBUG_HPILDEV
	static void __cdecl Trace(LPCTSTR lpFormat, ...)
	{
		TCHAR cOutput[1024];
		va_list arglist;

		va_start(arglist,lpFormat);
		wvsprintf(cOutput,lpFormat,arglist);
		OutputDebugString(cOutput);
		va_end(arglist);
		return;
	}
	#define TRACE Trace
#else  /* DEBUG_HPILDEV */
	static __inline void __cdecl Trace(LPCTSTR lpFormat, ...) { }
	#define TRACE 1 ? (void)0 : Trace
#endif  /* DEBUG_HPILDEV */

/*********************************************/
/* DeviceInit()                              */
/*                                           */
/* initialize device variables               */
/*   p: I/O processor data                   */
/*********************************************/
VOID DeviceInit(PHPILDATA p)
{
	// HP-IL data and variables
	p->byDEFADDR = 21;						// default address after AAU

	p->byRAM[0x12] = 0;						// parallel poll
	p->byRAM[0x35] = 0;						// local mode
	p->byRAM[0x3F] = 0;						// interrupt mask
	p->byRAM[0x40] = 0;						// interrupt cause
	p->byRAM[0x41] = 0;						// DDC content, lower 8 bit of DDL or DDT frame

	// Status
	p->byRAM[0x42] = 1;						// length Status
	p->byRAM[0x43] = 0;						// Status
	p->byRAM[0x44] = 0;

	// Device ID
	p->byRAM[0x45] = 6;						// length Device ID
	p->byRAM[0x46] = 'H';
	p->byRAM[0x47] = 'P';
	p->byRAM[0x48] = '7';
	p->byRAM[0x49] = '1';
	p->byRAM[0x4A] = '\r';
	p->byRAM[0x4B] = '\n';
	p->byRAM[0x4C] = 0;
	p->byRAM[0x4D] = 0;

	// Accessory ID
	p->byRAM[0x4E] = 1;						// length Accessory ID
	p->byRAM[0x4F] = 3;						// Accessory ID

	p->byRAM[0x51] = p->byRAM[0x12];		// copy of parallel poll

	p->byAddr = 0;							// HP-IL primary address (addressed by TAD or LAD)
											// bits 0-5 = AAD or AEP, bit 7 = 1 means auto address taken
	p->byAddr2nd = 0;						// HP-IL secondary address (addressed by SAD)
											// bits 0-5 = AES, bit 7 = 1 means auto address taken
	p->byFstate = 0;						// HP-IL state machine flags
	p->byPtOff = 0;							// RAM data offset for multibyte handling
	p->byPtSxx = 0;							// pointer for multibyte handling
	p->wLastDataFrame = 0;					// last input data frame
	p->wTalkerFrame = 0;					// last talker frame
	p->bySrqEn = 0;							// SRQ bit disabled
	return;
}

// implementation for device clear
static VOID DeviceClear(PHPILMEM pMem)
{
	TRACE(_T("DeviceClear\n"));
	EnterCriticalSection(&pMem->csStatus);
	{
		// interrupt cause = device clear
		pMem->psData->byRAM[0x40] |= (1 << 2);
	}
	LeaveCriticalSection(&pMem->csStatus);
	InClear(pMem);
	OutClear(pMem);
	return;
}

// implementation for interface clear
static VOID InterfaceClear(PHPILMEM pMem)
{
	TRACE(_T("InterfaceClear\n"));
	InClear(pMem);
	OutClear(pMem);
	EnterCriticalSection(&pMem->csStatus);
	{
		// interrupt cause = interface clear
		pMem->psData->byRAM[0x40] |= (1 << 7);

		// clear TLC
		pMem->psData->byIoStatus[0] &= ~0x07;
	}
	LeaveCriticalSection(&pMem->csStatus);
	pMem->psData->byEndmode = 1;			// enable terminator modes
	pMem->psData->byCharmode = 1;
	return;
}

// implementation for remote device
static VOID RemoteDevice(PHPILMEM pMem,BOOL bRemote)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	TRACE(_T("RemoteDevice = %d\n"),bRemote);
	EnterCriticalSection(&pMem->csStatus);
	{
		if (bRemote)
		{
			p->byIoStatus[3] |= 0x04;		// set R
		}
		else
		{
			p->byIoStatus[3] &= ~0x0C;		// clear KR
		}
	}
	LeaveCriticalSection(&pMem->csStatus);
	return;
}

// implementation for listener transfer
static VOID InData(PHPILMEM pMem,WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	TRACE(_T("InData = %04X\n"),wFrame);

	if (InSetData(pMem,(BYTE) (wFrame & 0xFF)))
	{
		EnterCriticalSection(&pMem->csStatus);
		{
			p->byIoStatus[3] |= 0x1;		// Data Available in input buffer
		}
		LeaveCriticalSection(&pMem->csStatus);

		p->byIOMem[8] |= 0x08;				// service request

		if (Chipset.Shutdn)					// cpu sleeping -> Wake Up
		{
			Chipset.bShutdnWake = TRUE;		// wake up from SHUTDN mode
			SetEvent(hEventShutdn);			// wake up emulation thread
		}
	}
	else									// buffer full
	{
		pMem->sTcp.bLoopClosed = FALSE;		// no frame return
	}
	return;
}

// implementation for talker transfer, for no response pass frame
static WORD OutData(PHPILMEM pMem,WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	TRACE(_T("OutData(In)  = %04X\n"),wFrame);

	if (OutGetData(pMem,&wFrame))
	{
		if (OutFullData(pMem))
		{
			p->byIOMem[7] |= 0x02;			// set I/O CPU NRD (Not Ready for Data)
		}
		else
		{
			if (OutCountData(pMem) == 0)	// no data in buffer any more
			{
				EnterCriticalSection(&pMem->csStatus);
				{
					// clear X bit
					p->byIoStatus[3] &= ~0x02;
				}
				LeaveCriticalSection(&pMem->csStatus);
			}
			p->byIOMem[7] &= ~0x02;			// clear I/O CPU NRD (Not Ready for Data)
		}
	}

	TRACE(_T("OutData(Out) = %04X\n"),wFrame);
	return wFrame;
}

// check if controller want to give up control with PASS CONTROL
static __inline VOID CheckPassControl(PHPILMEM pMem,WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	// request to give up control & answer is not TCT frame
	if (p->byPassControl != 0 && wFrame != 0x564)
	{
		p->byPassControl = 0;				// PASS CONTROL accepted

		EnterCriticalSection(&pMem->csStatus);
		{
			p->byIoStatus[0] = 0x00;		// control off
		}
		LeaveCriticalSection(&pMem->csStatus);

		DeviceInit(p);						// after PASS CONTROL
		p->byEndmode = 1;					// enable terminator modes
		p->byCharmode = 1;

		p->byIOMem[8] = 0;					// Nop message
		p->byIOMem[9] = 0;
		p->byIOMem[10] = 0;
		p->byIOMem[11] = 0;
		p->byIOMem[12] = 0;
		p->byIOMem[13] = 0;
		p->byIOMem[14] = 0;
		p->byIOMem[15] = 0;
		// set bit msg available
		p->byIOMem[8] |= 0x01;
	}
	return;
}

/*********************************************/
/* class_doe()                               */
/*                                           */
/* manage the HPIL data frames               */
/*   pMem: module data                       */
/*   wFrame: input frame data                */
/*********************************************/
static __inline WORD class_doe(PHPILMEM pMem,WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	WORD wLastInFrame = wFrame;				// remember last in data frame

	if ((p->byFstate & 0xC0) == 0x40)		// addressed talker
	{
		WORD wTalkerError = 0;				// no talker error

		if ((p->byFstate & 0x03) != 0)		// active talker
		{
			// compare last talker frame with actual frame without SRQ bit
			wTalkerError = (WORD) ((wFrame & 0x6FF) != (p->wTalkerFrame & 0x6FF));

			// data (SDA), status (SST), accessory ID (SAI) or device ID (SDI)
			if (!wTalkerError && (p->byFstate & 0x02) != 0)
			{
				WORD wSrqBit = wFrame & 0x100; // actual SRQ state

				// status (SST), accessory ID (SAI) or device ID (SDI)
				if ((p->byFstate & 0x01) != 0)
				{
					// 0x43: active talker (multiple byte status)
					if (p->byPtSxx > 0)		// SST, SAI, SDI
					{
						// base of RAM location with length information
						LPBYTE pbyData = &p->byRAM[p->byPtOff];

						if (p->byPtSxx < pbyData[0])
						{
							wFrame = pbyData[++p->byPtSxx];
						}
						else				// end of multiple byte status
						{
							p->byPtSxx = 0;
							wFrame = 0x540;	// EOT
						}
					}
				}
				else // 0x42: active talker (data)
				{
					// SDA
					wFrame = OutData(pMem,wFrame);
				}

				// a set SRQ bit doesn't matter on ready class frames
				_ASSERT((wFrame & 0x700) != 0x400);
				wFrame |= wSrqBit;			// restore SRQ bit
			}
			else // 0x41: active talker (single byte status)
			{
				// end of SAI, NRD or talker error
				wFrame = 0x540;				// EOT
			}
		}

		if (wFrame == 0x540)				// EOT type
		{
			wFrame += wTalkerError;			// check for error and set ETO/ETE frame
			p->byFstate &= ~0x03;			// delete active talker
		}

		p->wTalkerFrame = wFrame;			// save last talker frame
	}

	if ((p->byFstate & 0xC0) == 0x80)		// listener
	{
		InData(pMem,wFrame);
		p->wLastDataFrame = wLastInFrame;	// last received data frame
	}
	return wFrame;
}

/*********************************************/
/* class_cmd()                               */
/*                                           */
/* manage the HPIL command frames            */
/*   pMem: module data                       */
/*   wFrame: input frame data                */
/*********************************************/
static __inline WORD class_cmd(PHPILMEM pMem,WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	WORD n = wFrame & 0xFF;

	switch (n >> 5)
	{
	case 0:
		switch (n)
		{
		case 1: // GTL (Go To Local)
			if ((p->byFstate & 0x80) != 0)	// listener
			{
				EnterCriticalSection(&pMem->csStatus);
				{
					// clear bit 6 -> remote enabled
					p->byRAM[0x35] &= ~(1 << 6);
				}
				LeaveCriticalSection(&pMem->csStatus);
			}
			break;
		case 4: // SDC (Selected Device Clear)
			if ((p->byFstate & 0x80) != 0)	// listener
				DeviceClear(pMem);
			break;
		case 5: // PPD (Parallel Poll Disable)
			if ((p->byFstate & 0x80) != 0)	// listener
			{
				EnterCriticalSection(&pMem->csStatus);
				{
					// clear enable and SRQ pending bits
					p->byRAM[0x12] &= 0x0F;
					p->byRAM[0x51] = p->byRAM[0x12];
				}
				LeaveCriticalSection(&pMem->csStatus);
			}
			break;
		case 8: // GET (Group Execute Trigger)
			if ((p->byFstate & 0x80) != 0)	// listener
			{
				EnterCriticalSection(&pMem->csStatus);
				{
					// interrupt cause = trigger
					p->byRAM[0x40] |= (1 << 1);
				}
				LeaveCriticalSection(&pMem->csStatus);
			}
			break;
		case 17: // LLO (Local Lockout)
			EnterCriticalSection(&pMem->csStatus);
			{
				// Remote (R) enabled?
				if ((p->byIoStatus[3] & 0x04) != 0)
				{
					// set Locked Out Mode (K)
					p->byIoStatus[3] |= 0x08;
				}
			}
			LeaveCriticalSection(&pMem->csStatus);
			break;
		case 20: // DCL (Device Clear)
			DeviceClear(pMem);
			break;
		case 21: // PPU (Parallel Poll Unconfigure)
			EnterCriticalSection(&pMem->csStatus);
			{
				// clear enable and SRQ pending bits
				p->byRAM[0x12] &= 0x0F;
				p->byRAM[0x51] = p->byRAM[0x12];
			}
			LeaveCriticalSection(&pMem->csStatus);
			break;
		}
		break;
	case 1: // LAD
		n = n & 31;
		if (n == 31) // UNL
		{
			if ((p->byFstate & 0xA0) != 0)	// listener
				p->byFstate &= 0x50;
		}
		else
		{
			// else, if MLA go to listen state
			if ((p->byFstate & 0x80) == 0 && n == (p->byAddr & 31))
			{
				p->byFstate = ((p->byAddr2nd & 0x80) == 0)
							? 0x80			// addressed listen
							: 0x20;			// addressed listen in secondary address mode

				if (p->byFstate == 0x80)	// got listener
				{
					EnterCriticalSection(&pMem->csStatus);
					{
						// interrupt cause = listener
						p->byRAM[0x40] |= (1 << 6);

						// remote enabled (bit 5 set)
						if ((p->byRAM[0x35] & (1 << 5)) != 0)
						{
							// set bit 6 -> remote mode
							p->byRAM[0x35] |= (1 << 6);
							RemoteDevice(pMem,TRUE);
						}
					}
					LeaveCriticalSection(&pMem->csStatus);
				}
			}
		}
		break;
	case 2: // TAD
		n = n & 31;
		if (n == (p->byAddr & 31))
		{
			// if MTA go to talker state
			p->byFstate = ((p->byAddr2nd & 0x80) == 0)
						? 0x40				// addressed talker
						: 0x10;				// addressed talker in secondary address mode
		}
		else // UNT
		{
			// else if talker, go to idle state
			if ((p->byFstate & 0x50) != 0)
				p->byFstate &= 0xA0;
		}
		break;
	case 3: // SAD
		if ((p->byFstate & 0x30) != 0)		// LAD or TAD address matched
		{
			n = n & 31;
			if (n == (p->byAddr2nd & 31))	// secondary address match
			{
				p->byFstate <<= 2;			// switch to addressed listen/talker

				if (p->byFstate == 0x80)	// got listener
				{
					EnterCriticalSection(&pMem->csStatus);
					{
						// interrupt cause = listener
						p->byRAM[0x40] |= (1 << 6);
					}
					LeaveCriticalSection(&pMem->csStatus);
				}
			}
			else
			{
				p->byFstate = 0;			// secondary address don't match, go to idle state
			}
		}
		break;
	case 4:
		if ((n & 0x10) == 0) // PPE (Parallel Poll Enable)
		{
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byRAM[0x12] = (BYTE) n;	// set opcode (enable + arguments)
				if (p->bySrqEn)				// SRQ request enabled
				{
					p->byRAM[0x12] |= 0x30;	// set SRQ Pending
				}
				p->byRAM[0x51] = p->byRAM[0x12];
			}
			LeaveCriticalSection(&pMem->csStatus);
			break;
		}

		switch (n & 31)
		{
		case 16: // IFC (Interface Clear)
			InterfaceClear(pMem);
			p->byFstate = 0;
			break;
		case 18: // REN
			EnterCriticalSection(&pMem->csStatus);
			{
				// set bit 5 -> remote enabled
				p->byRAM[0x35] |= (1 << 5);
			}
			LeaveCriticalSection(&pMem->csStatus);
			break;
		case 19: // NRE
			EnterCriticalSection(&pMem->csStatus);
			{
				// clear bit 6, 5 -> local mode
				p->byRAM[0x35] &= ~((1 << 6) | (1 << 5));
			}
			LeaveCriticalSection(&pMem->csStatus);
			RemoteDevice(pMem,FALSE);
			break;
		case 26: // AAU
			p->byAddr = p->byDEFADDR;
			p->byAddr2nd = 0;
			break;
		}
		break;
	case 5: // DDL
		if ((p->byFstate & 0xC0) == 0x80)	// active listener
		{
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byRAM[0x40] |= (1 << 0);	// interrupt cause = device dependent
			}
			LeaveCriticalSection(&pMem->csStatus);

			p->byRAM[0x41] = (BYTE) n;		// save lower 8 bit of DDL frame
		}
		break;
	case 6: // DDT
		if ((p->byFstate & 0xC0) == 0x40)	// addressed talker
		{
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byRAM[0x40] |= (1 << 0);	// interrupt cause = device dependent
			}
			LeaveCriticalSection(&pMem->csStatus);

			p->byRAM[0x41] = (BYTE) n;		// save lower 8 bit of DDT frame
		}
		break;
	}

	EnterCriticalSection(&pMem->csStatus);
	{
		// listen
		if ((p->byFstate & 0xC0) == 0x80)
		{
			p->byIoStatus[0] |= 0x02;		// set L
		}
		else
		{
			p->byIoStatus[0] &= ~0x02;		// clear L
		}
	}
	LeaveCriticalSection(&pMem->csStatus);
	return wFrame;
}

/*********************************************/
/* class_rdy()                               */
/*                                           */
/* manage the HPIL ready frames              */
/*   pMem: module data                       */
/*   wFrame: input frame data                */
/*********************************************/
static __inline WORD class_rdy(PHPILMEM pMem,WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	WORD n = wFrame & 0xFF;

	if (n <= 127)							// not AAD, AEP, AES or AMP
	{
		// SOT
		if ((p->byFstate & 0xC0) == 0x40)	// addressed as talker
		{
			switch (n)
			{
			case 66: // NRD
				EnterCriticalSection(&pMem->csStatus);
				{
					// clear T (active talker)
					p->byIoStatus[0] &= ~0x04;
				}
				LeaveCriticalSection(&pMem->csStatus);
				p->byPtSxx = 0;				// abort transfer
				p->byFstate = 0x41;			// (single byte status) for EOT
				break;
			case 96: // SDA
				EnterCriticalSection(&pMem->csStatus);
				{
					// interrupt cause = talker
					p->byRAM[0x40] |= (1 << 4);
				}
				LeaveCriticalSection(&pMem->csStatus);
				wFrame = OutData(pMem,wFrame);
				if (wFrame != 0x560)		// not SDA, received data
				{
					EnterCriticalSection(&pMem->csStatus);
					{
						// set T (active talker)
						p->byIoStatus[0] |= 0x04;
					}
					LeaveCriticalSection(&pMem->csStatus);
					p->dwCount = 0;			// reset frame counter
					p->byFstate = 0x42;		// active talker (data)
					p->wTalkerFrame = wFrame; // save last talker frame
				}
				break;
			case 97: // SST
			case 98: // SDI
			case 99: // SAI
				{
					LPBYTE pbyData;

					// RAM offsets SST, SDI, SAI
					CONST BYTE byPtOff[] = { 0x42, 0x45, 0x4E };

					_ASSERT(n >= 97 && n < 97 + ARRAYSIZEOF(byPtOff));
					p->byPtOff = byPtOff[n - 97];

					if (n == 97)			// SST
					{
						p->bySrqEn = 0;		// reading HP-IL status disables SRQ request

						EnterCriticalSection(&pMem->csStatus);
						{
							// clear SRQ pending in parallel poll enable
							p->byRAM[0x12] &= ~0x30;
							p->byRAM[0x51] = p->byRAM[0x12];
						}
						LeaveCriticalSection(&pMem->csStatus);
					}

					// base of RAM location with length information
					pbyData = &p->byRAM[p->byPtOff];

					if (pbyData[0] != 0)		// have data
					{
						wFrame = pbyData[1];
						p->byPtSxx = 1;
						p->dwCount = 0;			// reset frame counter
						p->byFstate = 0x43;		// active talker (multiple byte status)
						p->wTalkerFrame = wFrame; // save last talker frame
					}
				}
				// else no response
				break;
			case 100: // TCT
				// control on
				EnterCriticalSection(&pMem->csStatus);
				{
					p->byRAM[0x40] |= (1 << 5);	// interrupt cause = controller

					p->byIoStatus[0] = (p->byIoStatus[0] & 0x08) | 0x01;
				}
				LeaveCriticalSection(&pMem->csStatus);

				// no frame return
				pMem->sTcp.bLoopClosed = FALSE;
				break;
			}
		}
	}
	else
	{
		_ASSERT(n >= 0x80);

		if (n < 0x80 + 31) // AAD
		{
			// AAD, if not already an assigned address, take it
			if ((p->byAddr & 0x80) == 0 && p->byAddr2nd == 0)
			{
				p->byAddr = (BYTE) n;
				wFrame++;
			}
		}
		else if (n >= 0xA0 && n < 0xA0 + 31) // AEP
		{
			// AEP, if not already an assigned address and got an AES frame, take it
			if ((p->byAddr & 0x80) == 0 && (p->byAddr2nd & 0x80) != 0)
			{
				p->byAddr = (BYTE) (n & 0x9F);
			}
		}
		else if (n >= 0xC0 && n < 0xC0 + 31) // AES
		{
			// AES, if not already an assigned address, take it
			if ((p->byAddr & 0x80) == 0)
			{
				p->byAddr2nd = (BYTE) (n & 0x9F);
				wFrame++;
			}
		}
		// AMP (0xE0 .. 0xE0 + 31)
	}
	return wFrame;
}

/*********************************************/
/* DeviceFrame()                             */
/*                                           */
/* process the frame data when configured as */
/* device                                    */
/*   pMem: module data                       */
/*   wFrame: input frame data                */
/*********************************************/
WORD DeviceFrame(PHPILMEM pMem, WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	CheckPassControl(pMem,wFrame);			// give up control on PASS CONTROL request

	// device mode or IFC frame
	if ((pMem->psData->byIoStatus[0] & 0x01) == 0 || wFrame == 0x490)
	{
		if ((wFrame & 0x400) == 0)			// DOE
		{
			// data frame
			wFrame = class_doe(pMem,wFrame);
		}
		else if ((wFrame & 0x700) == 0x400)	// CMD
		{
			// command frame
			wFrame = class_cmd(pMem,wFrame);
		}
		else if ((wFrame & 0x700) == 0x500)	// RDY
		{
			// ready frame
			wFrame = class_rdy(pMem,wFrame);
		}

		// if Status bit 6 is set and enabled and DOE or IDY frame
		if ((p->byRAM[0x43] & p->bySrqEn) != 0 && ((wFrame & 0x600) != 0x400))
		{
			_ASSERT((wFrame & 0x400) == 0 || (wFrame & 0x600) == 0x600);
			wFrame |= 0x100;				// set SRQ bit
		}

		// Parallel Poll Enabled and IDY frame
		if ((p->byRAM[0x12] & 0x80) != 0 && (wFrame & 0x600) == 0x600)
		{
			//	wBitset  SRQ  Sense
			//  1        0    0
			//  0        0    1
			//  0        1    0
			//  1        1    1
			WORD wBitset = ((wFrame >> 8) ^ (p->byRAM[0x12] >> 3) ^ 1) & 1;

			// set according bit
			_ASSERT((wBitset << (p->byRAM[0x12] & 0x7)) <= 255);
			wFrame |= (wBitset << (p->byRAM[0x12] & 0x7));
		}

		// compare interrupt mask with interrupt cause
		if ((p->byRAM[0x3F] & p->byRAM[0x40]) != 0)
		{
			TRACE(_T("Interrupt detected: Mask = %02X  Cause = %02X\n"),
				p->byRAM[0x3F],p->byRAM[0x40]);

			p->byRAM[0x3F] = 0;				// clear interrupt mask

			EnterCriticalSection(&pMem->csStatus);
			{
				p->byIoStatus[1] |= 0x01;	// set I
			}
			LeaveCriticalSection(&pMem->csStatus);

			p->byIOMem[8] |= 0x08;			// service request

			if (Chipset.Shutdn)				// cpu sleeping -> Wake Up
			{
				Chipset.bShutdnWake = TRUE;	// wake up from SHUTDN mode
				SetEvent(hEventShutdn);		// wake up emulation thread
			}
		}
	}
	else									// controller mode
	{
		pMem->sTcp.bLoopClosed = FALSE;		// no frame return
	}
	return wFrame;
}
