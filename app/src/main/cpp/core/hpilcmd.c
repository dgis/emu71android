/*
 *   hpilcmd.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gießelink
 *   Copyright (C) HPIL module emulation, J-F Garnier 1997-2010
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "hpil.h"

// #define DEBUG_HPILCMD

#if defined DEBUG_HPILCMD
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
#else  /* DEBUG_HPILCMD */
	static __inline void __cdecl Trace(LPCTSTR lpFormat, ...) { }
	#define TRACE 1 ? (void)0 : Trace
#endif  /* DEBUG_HPILCMD */

/*********************************************/
/* IntrFrame()                               */
/*                                           */
/* add interrupt cause of frames             */
/*   p: I/O processor data                   */
/*   frame: frame to check                   */
/*********************************************/
static __inline VOID IntrFrame(PHPILDATA p,WORD wFrame)
{
	BYTE byIntrCause = 0;

	BYTE byArg = (BYTE) (wFrame & 0xFF);

	switch (wFrame >> 8)
	{
	case 0: // DOE
	case 2: // END
	case 6: // IDY
		// no interrupt cause
		break;
	case 1: // DSR
	case 3: // ESR
	case 7: // ISR
		byIntrCause = (1 << 3);				// interrupt cause = service request
		break;
	case 4: // CMD
		if (byArg == 0x04)					// SDC (100 0000 0100)
		{
			if ((p->byIoStatus[0] & 0x02))	// L
			{
				byIntrCause = (1 << 2);		// interrupt cause = device clear
			}
		}
		else if (byArg == 0x08)				// GET (100 0000 1000)
		{
			byIntrCause = (1 << 1);			// interrupt cause = trigger
		}
		else if (byArg == 0x14)				// DCL (100 0001 0100)
		{
			byIntrCause = (1 << 2);			// interrupt cause = device clear
		}
		else if (byArg == 0x20)				// LAD 00 (100 001 AAAAA)
		{
			byIntrCause = (1 << 6);			// interrupt cause = listener
		}
		else if ((byArg & 0xE0) == 0xA0)	// DDL xx
		{
			if ((p->byIoStatus[0] & 0x02))	// L
			{
				p->byRAM[0x41] = byArg;		// save lower 8 bit of DDL frame
				byIntrCause = (1 << 0);		// interrupt cause = device dependent
			}
		}
		else if ((byArg & 0xE0) == 0xC0)	// DDT xx
		{
			if ((p->byIoStatus[0] & 0x04))	// T
			{
				p->byRAM[0x41] = byArg;		// save lower 8 bit of DDT frame
				byIntrCause = (1 << 0);		// interrupt cause = device dependent
			}
		}
		break;
	case 5: // RDY
		if (byArg == 0x60)	// SDA (101 0110 0000)
		{
			if ((p->byIoStatus[0] & 0x04))	// T
			{
				byIntrCause = (1 << 4);		// interrupt cause = talker
			}
		}
		break;
	}
	p->byRAM[0x40] |= byIntrCause;			// add interrupt cause
	return;
}

/*********************************************/
/* TransmitFrame()                           */
/*                                           */
/* send a frame with LAD 00, TAD 00, UNL and */
/* UNT handling over the loop                */
/*   pMem: module data                       */
/*   frame: frame to send                    */
/*********************************************/
WORD TransmitFrame(PHPILMEM pMem,WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	switch (wFrame)
	{
	case 0x405: // PPD
	case 0x415: // PPU
		// addressed listen or PPU frame
		if ((p->byCstate & 0x02) != 0 || wFrame == 0x415)
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
	case 0x420: // LAD 00
		EnterCriticalSection(&pMem->csStatus);
		{
			p->byCstate = 0x02;				// set addressed listen
			p->byIoStatus[0] &= ~0x04;		// clear T
			p->byIoStatus[0] |= 0x02;		// set L

			// remote enabled (bit 5 set)
			if ((p->byRAM[0x35] & (1 << 5)) != 0)
			{
				// set bit 6 -> remote mode
				p->byRAM[0x35] |= (1 << 6);

				p->byIoStatus[3] |= 0x04;	// set R
			}
		}
		LeaveCriticalSection(&pMem->csStatus);
		break;
	case 0x440: // TAD 00
		// TAD 00 works only in connection with SDA
		EnterCriticalSection(&pMem->csStatus);
		{
			p->byCstate = 0x04;				// set addressed talker
			p->byIoStatus[0] &= ~0x02;		// clear L
		}
		LeaveCriticalSection(&pMem->csStatus);
		break;
	case 0x43F: // UNL
		EnterCriticalSection(&pMem->csStatus);
		{
			p->byCstate = ~0x02;			// clear addressed listen
			p->byIoStatus[0] &= ~0x02;		// clear L
		}
		LeaveCriticalSection(&pMem->csStatus);
		break;
	case 0x492: // REN
		EnterCriticalSection(&pMem->csStatus);
		{
			// set bit 5 -> remote enabled
			p->byRAM[0x35] |= (1 << 5);
		}
		LeaveCriticalSection(&pMem->csStatus);
		break;
	case 0x493: // NRE
		EnterCriticalSection(&pMem->csStatus);
		{
			// clear bit 6, 5 -> local mode
			p->byRAM[0x35] &= ~((1 << 6) | (1 << 5));

			p->byIoStatus[3] &= ~0x0C;		// clear KR
		}
		LeaveCriticalSection(&pMem->csStatus);
		break;
	case 0x560: // SDA
		EnterCriticalSection(&pMem->csStatus);
		{
			if ((p->byCstate & 0x04) != 0)	// addressed talker?
			{
				p->byIoStatus[0] |= 0x04;	// set T
			}
		}
		LeaveCriticalSection(&pMem->csStatus);
		break;
	default: // all other frames
		// TAD 01 .. TAD 1E, UNT
		if (wFrame > 0x440 && wFrame <= 0x45F)
		{
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byCstate &= ~0x04;		// clear addressed talker
				p->byIoStatus[0] &= ~0x04;	// clear T
			}
			LeaveCriticalSection(&pMem->csStatus);
		}
		// addressed listen PPE 00 .. PPE 15
		else if ((p->byCstate & 0x02) != 0 && wFrame >= 0x480 && wFrame <= 0x48F)
		{
			EnterCriticalSection(&pMem->csStatus);
			{
				// set opcode with no SRQ Pending
				p->byRAM[0x12] = (BYTE) (wFrame & 0xFF);
				p->byRAM[0x51] = p->byRAM[0x12];
			}
			LeaveCriticalSection(&pMem->csStatus);
		}
		break;
	}

	// execute frame
	wFrame = HpilController(&pMem->sTcp,wFrame);

	if (!pMem->sTcp.bLoopClosed)			// HPIL loop not closed
	{
		EnterCriticalSection(&pMem->csStatus);
		{
			p->byIoStatus[0] &= ~0x06;		// clear TL
			p->byIoStatus[1] |= 0x02;		// set U
		}
		LeaveCriticalSection(&pMem->csStatus);
	}
	else									// loop closed
	{
		IntrFrame(p,wFrame);				// add interrupt cause

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
	return wFrame;
}

/*********************************************/
/* SendFrame()                               */
/*                                           */
/* send a frame over the loop                */
/*   pMem: module data                       */
/*   frame: frame to send                    */
/*********************************************/
static WORD SendFrame(PHPILMEM pMem,WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	WORD wRecFrame = TransmitFrame(pMem,wFrame);

	if (pMem->sTcp.bLoopClosed)				// HPIL loop closed
	{
		p->byIOMem[9] &= ~0x2;				// clear "SRQ received from loop" bit

		if ((wRecFrame & 0x600) != 0x400)	// received DOE or IDY frame
		{
			// update "SRQ received from loop" bit
			p->byIOMem[9] |= ((wRecFrame >> 7) & 0x2);
		}

		if ((wFrame & 0x700) == 0x400)		// CMD frame
		{
			// abort operation because of fatal error
			p->byAbort = (wRecFrame != wFrame);

			if (p->byAbort == 0)			// returned original CMD frame
			{
				if (pMem->bEnableRFC)		// adding RFC frame enabled
				{
					// send the RFC frame
					TransmitFrame(pMem,0x500);
				}
			}
			else							// fatal error
			{
				EnterCriticalSection(&pMem->csStatus);
				{
					// Frame sent out is not the same as frame received
					p->byIoStatus[4] = 0x06;
					p->byIoStatus[5] = 0x00;
				}
				LeaveCriticalSection(&pMem->csStatus);
				p->byIOMem[9] |= 0x01;		// error occured
				p->eState = MBIDLE;
			}
		}
	}
	return wRecFrame;
}

/*********************************************/
/* autoaddrloop()                            */
/*                                           */
/* auto-addressing the loop and set up the   */
/* address table                             */
/*   pMem: module data                       */
/*   n: 1 = simple addressing                */
/*      0 = extended + simple addressing     */
/*********************************************/
static WORD autoaddrloop(PHPILMEM pMem, UINT n)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	WORD wFrame,wAddr;

	p->byAAD = 0;							// no last AAD address
	p->byAEP = 0;							// no last AEP address
	p->byAES = 0;							// no last AES address
	wAddr = 0;								// no last device address

	EnterCriticalSection(&pMem->csStatus);
	{
		p->byIoStatus[1] |= 0x2;			// invalidate address table
	}
	LeaveCriticalSection(&pMem->csStatus);

	SendFrame(pMem,0x49a);					// AAU

	// loop broken or error
	if (!pMem->sTcp.bLoopClosed || p->byAbort != 0)
		return 0;

	if (n == 0)								// extended addressing
	{
		do
		{
			// init loop with AES 0
			if ((wFrame = SendFrame(pMem,0x5c0)) > 0x5c0)
			{
				p->byAES = wFrame - 0x5c0;	// remember last AES address
				++p->byAEP;					// next AEP

				// send AEP address to devices
				SendFrame(pMem,(WORD) (0x5a0 + p->byAEP));
			}
		}
		while (wFrame != 0x5c0 && p->byAES == 31 && p->byAEP < 31);

		// last auto-extended address
		wAddr = p->byAEP | ((WORD) p->byAES << 5);
	}

	// call simple addressing with AAD
	if (p->byAEP < 31)						// remaining AAD addresses
	{
		wFrame = SendFrame(pMem,(WORD) (0x581 + p->byAEP));

		// found additional devices
		if (wFrame > (WORD) (0x581 + p->byAEP))
		{
			p->byAAD = wFrame - 0x581;		// last device address
			wAddr = p->byAAD;				// last auto-address
		}
	}
	EnterCriticalSection(&pMem->csStatus);
	{
		p->byIoStatus[1] &= ~0x2;			// validate address table
	}
	LeaveCriticalSection(&pMem->csStatus);
	return wAddr;
}

/*********************************************/
/* incr_addr()                               */
/*                                           */
/* increment HP-IL address to next device    */
/*   p: I/O processor data                   */
/*   pwAddr: HP-IL address                   */
/*********************************************/
static BOOL incr_addr(PHPILDATA p, WORD *pwAddr)
{
	BYTE i,s;
	BOOL bSucc;

	if (*pwAddr == 0)						// look for 1st device
	{
		if (p->byAEP > 0 && p->byAES > 0)
			*pwAddr = 1 | (1 << 5);			// 1.01
		else if (p->byAAD > 0)
			*pwAddr = 1;					// 1

		return *pwAddr != 0;
	}

	i = *pwAddr & 0x1f;						// primary address
	s = (*pwAddr >> 5) & 0x1f;				// secondary address

	if (i <= p->byAEP)						// primary address with secondary address
	{
		// increment secondary address
		if (++s > ((i < p->byAEP) ? 31 : p->byAES))
		{
			++i;							// next primary address
			s = (i <= p->byAEP) ? 1 : 0;	// has primary address a secondary address
		}
	}
	else									// primary address without secondary address
	{
		++i;								// next primary address
		s = 0;								// no secondary address
	}

	// not last address
	bSucc = (i <= p->byAAD) || (i <= p->byAEP);
	if (bSucc)								// update address
	{
		*pwAddr = i | ((WORD) s << 5);
	}
	return bSucc;
}

/*********************************************/
/* sendbytes()                               */
/*                                           */
/* transmit n data bytes to the loop         */
/*   pMem: module data                       */
/*   pbyMsg: mailbox data                    */
/*   n: no of bytes to send                  */
/*********************************************/
static VOID sendbytes(PHPILMEM pMem, LPBYTE pbyMsg, UINT n)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	WORD wFrame;

	for (; n > 0; --n)
	{
		wFrame = (pbyMsg[1] << 4) | pbyMsg[0];
		// TRACE(_T("HPIL: sendbyte : %02X\n"),wFrame);
		if ((p->byIoStatus[0] & 0x01) != 0)	// controller
		{
			SendFrame(pMem,wFrame);			// send frame
		}
		else								// device
		{
			// put data into output buffer
			VERIFY(OutSetData(pMem,(BYTE) wFrame));
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byIoStatus[3] |= 0x02;	// set X bit
			}
			LeaveCriticalSection(&pMem->csStatus);
			if (OutFullData(pMem))
			{
				pbyMsg[7] |= 0x02;			// I/O CPU NRD (Not Ready for Data)
			}
		}
		pbyMsg += 2;
	}
	return;
}

/*********************************************/
/* enter_bytes()                             */
/*                                           */
/* enter 1 to 3 data bytes from the loop     */
/* manage the end of transfer condition      */
/*   pMem: module data                       */
/*   startframe: SOT frame to use            */
/*********************************************/
static INT enter_bytes(PHPILMEM pMem, WORD wStartframe)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	WORD wFrame;
	INT  f;

	p->byWrBufPos = 0;
	wFrame = wStartframe;
	while (TRUE)
	{
		wFrame = SendFrame(pMem,wFrame);

		if ((wStartframe & 0x500) != 0 && (wFrame == wStartframe))
			return -1;						// error or no response

		p->nBuffer[p->byWrBufPos++] = wFrame;

		if ((wFrame & 0x7FE) == 0x540)		// ETO ETE
			return 0;

		_ASSERT((wFrame & 0x600) != 0x400);	// must be a DOE or IDY frame here
		wFrame &= 0x6FF;					// clear SRQ bit
		p->wLastframe = wFrame;				// save last received frame

		p->dwCount++;
		f = 0;
		if (p->dwMaxCount != 0xFFFFF && p->dwCount >= p->dwMaxCount) f = 1;
		if (p->byEndmode  && (wFrame & 0x600) == 0x200) f = 2;
		if (p->byCharmode && (wFrame & 0xff) == p->byEndchar) f = 2;
		if (f)
		{
			SendFrame(pMem,0x542);			// NRD

			// send last received frame
			wFrame = SendFrame(pMem,p->wLastframe);

			if (f==2)						// end or char mode
			{
				// term cond met
				p->nBuffer[p->byWrBufPos++] = -1;
			}
			else							// count overflow
			{
				if (wFrame == 0x541)		// ETE
				{
					// replace last data frame with ETE frame
					_ASSERT(p->byWrBufPos > 0);
					p->nBuffer[p->byWrBufPos-1] = wFrame;
				}
			}
			return 0;
		}
		if (p->byWrBufPos == 1 && p->dwCount == 1)
			return 1;						// returns immediately the 1st byte (needed by INITIALIZE)
		if (p->byWrBufPos == 3)
			return 3;
	}
}

/*********************************************/
/* end_listen()                              */
/*                                           */
/* process the loop when in                  */
/* end_listen state                          */
/*   pMem: module data                       */
/*   pbyMsg: mailbox data                    */
/*********************************************/
static VOID end_listen(PHPILMEM pMem, LPBYTE pbyMsg)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	INT n;

	pbyMsg[8] &= 0x0a;						// clear bit sleep and msg available
	pbyMsg[9] &= 0x06;						// clear bit three data bytes and error occurred

	n = p->nBuffer[p->byRdBufPos++];
	if (n < 0)
	{
		// term cond met
		pbyMsg[10] = 0x05;
		pbyMsg[11] = 0x00;
		pbyMsg[12] = 0x00;
		pbyMsg[13] = 0x00;
		p->eState = MBIDLE;
	}
	else if ((n & 0x7FE) == 0x540)			// EOT received
	{
		// EOT frame: ETO / ETE
		pbyMsg[10] = 0x02 | (BYTE) (n & 0x01);
		pbyMsg[11] = 0x00;
		pbyMsg[12] = 0x00;
		pbyMsg[13] = 0x00;
		p->eState = MBIDLE;
	}
	else
	{
		pbyMsg[10] = ((n >> 0) & 0x0f);
		pbyMsg[11] = ((n >> 4) & 0x0f);
		pbyMsg[12] = 0x08;					// data byte
		pbyMsg[13] = 0x00;
		if (p->byRdBufPos == p->byWrBufPos)	// read last byte
			p->eState = MBIDLE;
	}
	pbyMsg[14] = 0x00;
	pbyMsg[15] = 0x00;
	// set bit msg available
	pbyMsg[8] |= 0x01;
	return;
}

/*********************************************/
/* do_listen()                               */
/*                                           */
/* process the loop when in                  */
/* active listen state                       */
/*   pMem: module data                       */
/*   pbyMsg: mailbox data                    */
/*   frame: SOT frame or last received frame */
/*********************************************/
static VOID do_listen(PHPILMEM pMem, LPBYTE pbyMsg, WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	INT n;

	// enter bytes until EOT or count
	n = enter_bytes(pMem, wFrame);			// return: -1:error, 0: bytes+EOT, 1: 1byte, 3: 3bytes
	// TRACE(_T("HPIL: enter_bytes : %d\n"),n);
	switch (n)
	{
	case -1:
		// error
		// no response --> device not ready error
		EnterCriticalSection(&pMem->csStatus);
		{
			p->byIoStatus[4] = 0x02;
			p->byIoStatus[5] = 0x00;
		}
		LeaveCriticalSection(&pMem->csStatus);
		// set error bit
		pbyMsg[8] &= 0x0a;					// clear bit sleep and msg available
		pbyMsg[9] &= 0x07;					// clear bit three data bytes
		pbyMsg[9] |= 0x01;
		// send status
		_ASSERT(p->byIoStatus[2] == 0x01);
		pbyMsg[10] = p->byIoStatus[0];
		pbyMsg[11] = p->byIoStatus[1];
		pbyMsg[12] = p->byIoStatus[2];
		pbyMsg[13] = p->byIoStatus[3];
		pbyMsg[14] = p->byIoStatus[4];
		pbyMsg[15] = p->byIoStatus[5];
		// set bit msg available
		p->eState = MBIDLE;
		pbyMsg[8] |= 0x01;
		break;
	case 0:
		// conversation is over (by count, EOT or cond)
		p->byRdBufPos = 0;
		p->eState = END_LISTEN;
		end_listen(pMem,pbyMsg);
		break;
	case 1:
		// received 1 bytes, transmit to 71 and go on
		pbyMsg[8] &= 0x0a;					// clear bit sleep and msg available
		pbyMsg[9] &= 0x06;					// clear bit three data bytes and error occurred
		pbyMsg[10] = ((p->nBuffer[0] >> 0) & 0x0f);
		pbyMsg[11] = ((p->nBuffer[0] >> 4) & 0x0f);
		pbyMsg[12] = 0x08;					// data byte
		pbyMsg[13] = 0x00;
		pbyMsg[14] = 0x00;
		pbyMsg[15] = 0x00;
		// set bit msg available
		p->eState = ACT_LISTEN;
		pbyMsg[8] |= 0x01;
		break;
	case 3:
		// received 3 bytes, transmit to 71 and go on
		pbyMsg[8] &= 0x0a;					// clear bit sleep and msg available
		pbyMsg[9] &= 0x0e;					// clear bit error occurred
		pbyMsg[9] |= 0x08;					// set three data bytes
		pbyMsg[10] = ((p->nBuffer[0] >> 0) & 0x0f);
		pbyMsg[11] = ((p->nBuffer[0] >> 4) & 0x0f);
		pbyMsg[12] = ((p->nBuffer[1] >> 0) & 0x0f);
		pbyMsg[13] = ((p->nBuffer[1] >> 4) & 0x0f);
		pbyMsg[14] = ((p->nBuffer[2] >> 0) & 0x0f);
		pbyMsg[15] = ((p->nBuffer[2] >> 4) & 0x0f);
		// set bit msg available
		p->eState = ACT_LISTEN;
		pbyMsg[8] |= 0x01;
		break;
	}
	return;
}

/*********************************************/
/* findndevice()                             */
/*                                           */
/* find the Nth device of given type         */
/*   pMem: module data                       */
/*   type: seaching type M                   */
/*   nb: Nth device                          */
/*********************************************/
static INT findndevice(PHPILMEM pMem, int type, int nb)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	WORD wFrame,wAddr,n;
	INT  i,cpt;

	SendFrame(pMem,0x43f);					// UNL

	if (!pMem->sTcp.bLoopClosed || p->byAbort != 0) // loop broken or error
		return 0;

	cpt = 0;

	wAddr = 0;

	while (incr_addr(p, &wAddr))
	{
		n = wAddr >> 5;						// secondary address
		_ASSERT(n <= 31);

		SendFrame(pMem,(WORD)(0x440+(wAddr & 0x1f))); // TAD current addr
		if (p->byAbort != 0) return -1;		// error

		if (n > 0)							// device with auto extended addressing
		{
			SendFrame(pMem,(WORD)(0x460-1+n));	// SAD n-1
			if (p->byAbort != 0) return -1;	// error
		}

		wFrame = SendFrame(pMem,0x563);		// SAI
		if (wFrame != 0x563)				// got accessory ID
		{
			// return the accessory ID to get the ETO frame from the device
			SendFrame(pMem,wFrame);

			// check for response
			if ((type & 0x0f) == 0x0f)
			{
				// test general type
				if ((type & 0xf0) == (wFrame & 0xf0)) cpt++;
			}
			else
			{
				if (type == wFrame) cpt++;
			}

			if (cpt == nb)					// found device
			{
				i = wAddr;
				break;
			}
		}
	}

	if (cpt != nb)
	{
		SendFrame(pMem,0x45f);				// UNT
		i = -1;
	}
	return i;
}

/*********************************************/
/* execmd0()                                 */
/*                                           */
/* execution of no parameter class opcodes   */
/* note: several opcodes are not implemented */
/* in Emu71, they are indicated with '!' at  */
/* the end of the trace messages             */
/*   pMem: module data                       */
/*   pbyMsg: mailbox data                    */
/*********************************************/
static VOID execmd0(PHPILMEM pMem, LPBYTE pbyMsg)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	switch (pbyMsg[4])
	{
	case 0: // nop
		TRACE(_T("HPIL: nop\n"));
		break;
	case 1: // read address table
		TRACE(_T("HPIL: read address table\n"));
		pbyMsg[8] &= 0x0a;					// clear bit sleep and msg available
		pbyMsg[9] &= 0x0e;					// clear bit error occurred
		pbyMsg[9] |= 0x08;					// set three data bytes
		pbyMsg[10] = (p->byAAD & 0x0f);		// last AAD address
		pbyMsg[11] = (p->byAAD >> 4);
		pbyMsg[12] = (p->byAEP & 0x0f);		// last AEP address
		pbyMsg[13] = (p->byAEP >> 4);
		pbyMsg[14] = (p->byAES & 0x0f);		// last AES address
		pbyMsg[15] = (p->byAES >> 4);
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	case 2: // request hpil status
		TRACE(_T("HPIL: request hpil status\n"));
		if ((pbyMsg[2] & 0x1) != 0)			// C bit set
		{
			pbyMsg[8] &= ~0x08;				// clear I/O CPU requires service
		}
		// device mode & data in input buffer
		if ((p->byIoStatus[0] & 0x1) == 0 && InCountData(pMem) > 0)
		{
			pbyMsg[8] |= 0x08;				// I/O CPU requires service
		}
		// send status
		pbyMsg[8] &= 0x08;					// clear bit sleep, NRD and msg available
		pbyMsg[9] &= 0x06;					// clear bit three data bytes and error occurred
		_ASSERT(p->byIoStatus[2] == 0x01);
		pbyMsg[10] = p->byIoStatus[0];
		pbyMsg[11] = p->byIoStatus[1];
		pbyMsg[12] = p->byIoStatus[2];
		pbyMsg[13] = p->byIoStatus[3];
		pbyMsg[14] = p->byIoStatus[4];
		pbyMsg[15] = p->byIoStatus[5];
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	case 3: // end of message
		TRACE(_T("HPIL: end of message\n"));
		// addressed as talker
		if ((p->byIoStatus[0] & 0x04) != 0)
		{
			// controller
			if ((p->byIoStatus[0] & 0x01) != 0)
			{
				SendFrame(pMem,0x540);		// ETO
			}
			else							// device
			{
				OutSetFrame(pMem,0x540);	// ETO
			}
		}
		break;
	case 4:	// clear SRQ
		TRACE(_T("HPIL: clear SRQ !\n"));
		break;
	case 5: // set SRQ
		TRACE(_T("HPIL: set SRQ !\n"));
		break;
	case 6: // send error msg
		TRACE(_T("HPIL: send error msg\n"));
		// clear error bit
		pbyMsg[8] &= 0x08;					// clear bit sleep, NRD and msg available
		pbyMsg[9] &= 0x06;					// clear bit three data bytes and error occurred
		// send status
		_ASSERT(p->byIoStatus[2] == 0x01);
		pbyMsg[10] = p->byIoStatus[0];
		pbyMsg[11] = p->byIoStatus[1];
		pbyMsg[12] = p->byIoStatus[2];
		pbyMsg[13] = p->byIoStatus[3];
		pbyMsg[14] = p->byIoStatus[4];
		pbyMsg[15] = p->byIoStatus[5];
		// clear error
		p->byIoStatus[4] = 0;
		p->byIoStatus[5] = 0;
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	case 7: // enter auto end mode
		TRACE(_T("HPIL: enter auto end mode !\n"));
		break;
	case 8: // go into manual mode
		TRACE(_T("HPIL: go into manual mode !\n"));
		// 1 = manual, 2 = scope
		p->byManual = (pbyMsg[2] & 0x01) + 1;
		pbyMsg[9] |= 0x04;					// set manual mode flag
		break;
	case 9: // go into auto mode
		TRACE(_T("HPIL: go into auto mode !\n"));
		p->byManual = 0;					// auto
		pbyMsg[9] &= ~0x04;					// clear manual mode flag
		break;
	case 10: // update system controller bit
		TRACE(_T("HPIL: system ctrl bit\n"));
		p->byIoStatus[0] = (p->byIoStatus[0] & ~0x08) | (pbyMsg[2] & 0x08);
		break;
	case 11: // reset current address
		TRACE(_T("HPIL: reset current address\n"));
		p->wCurrentAddr = 0;
		incr_addr(p, &p->wCurrentAddr);
		break;
	case 12: // read current address
		TRACE(_T("HPIL: read current address\n"));
		pbyMsg[8] &= 0x0a;					// clear bit sleep and msg available
		pbyMsg[9] &= 0x06;					// clear bit three data bytes and error occurred
		pbyMsg[10] = ((p->wCurrentAddr >> 0) & 0x0f);
		pbyMsg[11] = ((p->wCurrentAddr >> 4) & 0x0f);
		pbyMsg[12] = ((p->wCurrentAddr >> 8) & 0x03) | 0x04;
		pbyMsg[13] = ((p->wCurrentAddr >> 4) & 0x0e);
		// pbyMsg[14] and [15] aren't updated by the I/O CPU
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	case 13: // increment current address
		TRACE(_T("HPIL: increment current address\n"));
		if (incr_addr(p, &p->wCurrentAddr))
		{
			pbyMsg[8] &= 0x0a;				// clear bit sleep and msg available
			pbyMsg[9] &= 0x06;				// clear bit three data bytes and error occurred
			pbyMsg[10] = ((p->wCurrentAddr >> 0) & 0x0f);
			pbyMsg[11] = ((p->wCurrentAddr >> 4) & 0x0f);
			pbyMsg[12] = ((p->wCurrentAddr >> 8) & 0x03) | 0x04;
			pbyMsg[13] = ((p->wCurrentAddr >> 4) & 0x0e);
			// pbyMsg[14] and [15] aren't updated by the I/O CPU
		}
		else
		{
			p->wCurrentAddr = 0;			// address of first device in loop
			incr_addr(p, &p->wCurrentAddr);
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byIoStatus[4] = 0x0C;	// illegal current address
				p->byIoStatus[5] = 0x00;
			}
			LeaveCriticalSection(&pMem->csStatus);
			// send status
			pbyMsg[8] &= 0x0a;				// clear bit sleep and msg available
			pbyMsg[9] &= 0x06;				// clear bit three data bytes and error occurred
			_ASSERT(p->byIoStatus[2] == 0x01);
			pbyMsg[10] = p->byIoStatus[0];
			pbyMsg[11] = p->byIoStatus[1];
			pbyMsg[12] = p->byIoStatus[2];
			pbyMsg[13] = p->byIoStatus[3];
			pbyMsg[14] = p->byIoStatus[4];
			pbyMsg[15] = p->byIoStatus[5];
		}
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	case 14: // read my loop address
		TRACE(_T("HPIL: read my loop address\n"));
		pbyMsg[8] &= 0x0a;					// clear bit sleep and msg available
		pbyMsg[9] &= 0x06;					// clear bit three data bytes and error occurred
		{
			WORD wAddr = p->byAddr & 0x1f;	// primary address
			if ((p->byAddr2nd & 0x80) != 0)	// has secondary address?
			{
				wAddr |= (WORD) ((p->byAddr2nd & 0x1f) + 1) << 5;
			}

			pbyMsg[10] = ((wAddr >> 0) & 0x0f);
			pbyMsg[11] = ((wAddr >> 4) & 0x0f);
			pbyMsg[12] = ((wAddr >> 8) & 0x03) | 0x04;
			pbyMsg[13] = ((wAddr >> 4) & 0x0e);
		}
		// pbyMsg[14] and [15] aren't updated by the I/O CPU
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	case 15: // take/give loop control
		TRACE(_T("HPIL: take/give loop control\n"));
		if ((pbyMsg[2] & 0x01) != 0)
		{
			// control on
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byIoStatus[0] = (p->byIoStatus[0] & 0x08) | 0x01;
			}
			LeaveCriticalSection(&pMem->csStatus);

			p->byEndmode = 0;				// clear terminator modes
			p->byCharmode = 0;

			if ((pbyMsg[2] & 0x02) != 0)	// L bit set
			{
				// send given CMD frame
				WORD wFrame = 0x400 | (pbyMsg[1] << 4) | pbyMsg[0];
				SendFrame(pMem,wFrame);
				if (pMem->sTcp.bLoopClosed)
				{
					p->byPowered = 1;		// loop powered up
				}
				else
				{
					// Frame Timed Out on the Loop
					EnterCriticalSection(&pMem->csStatus);
					{
						p->byIoStatus[4] = 0x0B;
						p->byIoStatus[5] = 0x00;
					}
					LeaveCriticalSection(&pMem->csStatus);
				}
			}
		}
		else
		{
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byCstate &= ~0x06;		// clear addressed talker / listen
				p->byIoStatus[0] &= ~0x07;	// clear TLC
			}
			LeaveCriticalSection(&pMem->csStatus);

			p->byEndmode = 1;				// enable terminator modes
			p->byCharmode = 1;
		}
		p->byAddr = 0;						// controller address
		p->byAddr2nd = 0;
		break;
	default:
		_ASSERT(FALSE);
	}
	return;
}

/*********************************************/
/* execmd15()                                */
/*                                           */
/* execution of multibyte parameter class    */
/* opcodes                                   */
/*   pMem: module data                       */
/*   pbyMsg: mailbox data                    */
/*********************************************/
static VOID execmd15(PHPILMEM pMem, LPBYTE pbyMsg)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	INT  n;

	switch (pbyMsg[4])
	{
	case 0: // read memory
		TRACE(_T("HPIL: read memory !\n"));
		n = p->byRAM[(pbyMsg[1] << 4) | pbyMsg[0]];

		// send address of last device
		pbyMsg[8] &= 0x0a;					// clear bit sleep and msg available
		pbyMsg[9] &= 0x06;					// clear bit three data bytes and error occurred
		pbyMsg[10] = ((n >> 0) & 0x0f);
		pbyMsg[11] = ((n >> 4) & 0x0f);
		pbyMsg[12] = 3;
		pbyMsg[13] = 0;
		pbyMsg[14] = 1;
		pbyMsg[15] = 0;
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	case 1: // write memory
		TRACE(_T("HPIL: write memory !\n"));
		break;
	case 2: // self test
		TRACE(_T("HPIL: self test\n"));
		if ((pbyMsg[8] & 0x01) == 0)		// prior message fetched
		{
			switch (++p->byMsgCount)
			{
			case 1: // 1st message
				pbyMsg[7] &= 0x0d;			// clear bit I/O CPU NRD
				pbyMsg[8] &= 0x0a;			// clear bit sleep and msg available
				pbyMsg[9]  = 0x0a;
				pbyMsg[10] = 0x05;
				pbyMsg[11] = 0x05;
				pbyMsg[12] = 0x0a;
				pbyMsg[13] = 0x0a;
				pbyMsg[14] = 0x05;
				pbyMsg[15] = 0x05;
				// set bit msg available & I/O CPU SRQ on HP-71 bus
				pbyMsg[8] |= 0x09;
				break;
			case 2: // 2nd message
				pbyMsg[7] |= 0x02;			// set bit I/O CPU NRD
				pbyMsg[8] &= 0x0a;			// clear bit sleep and msg available
				pbyMsg[9]  = 0x05;
				pbyMsg[10] = 0x0a;
				pbyMsg[11] = 0x0a;
				pbyMsg[12] = 0x05;
				pbyMsg[13] = 0x05;
				pbyMsg[14] = 0x0a;
				pbyMsg[15] = 0x0a;
				// set bit msg available & I/O CPU SRQ on HP-71 bus
				pbyMsg[8] |= 0x09;
				break;
			case 3: // 3rd message
				pbyMsg[8] &= 0x0a;			// clear bit sleep and msg available
				pbyMsg[9] &= 0x06;			// clear bit three data bytes and error occurred
				pbyMsg[10] = 0x06;			// ROM and RAM self test was successful
				pbyMsg[11] = 0x00;
				pbyMsg[12] = 0x02;
				pbyMsg[13] = 0x00;
				// set bit msg available
				pbyMsg[8] |= 0x01;

				p->byMsgCount = 0;			// last message
				break;
			}
		}
		break;
	case 3: // set SOT response
		TRACE(_T("HPIL: set SOT response\n"));
		{
			BYTE byValue = (pbyMsg[3] << 4) | pbyMsg[2];
			BYTE byType = pbyMsg[1] & 0x7;
			BYTE byByte = pbyMsg[0];

			TRACE(_T("NNNN = %d SAI = %d RRRRRRRR = %d\n"),byByte,byType,byValue);

			switch (byType)					// SAI
			{
			case 4: // Status
				if (byByte == 1)			// status byte
				{
					// setting status bit 6 enables SRQ request
					p->bySrqEn = byValue & (BYTE) (1 << 6);

					// update SRQ pending in parallel poll enable
					EnterCriticalSection(&pMem->csStatus);
					{
						// set SRQ pending only in device mode
						if (p->bySrqEn && (p->byIoStatus[0] & 0x01) == 0)
						{
							p->byRAM[0x12] |= 0x30;
						}
						else
						{
							p->byRAM[0x12] &= ~0x30;
						}
						p->byRAM[0x51] = p->byRAM[0x12];
					}
					LeaveCriticalSection(&pMem->csStatus);
				}
				p->byRAM[0x42 + byByte] = byValue;
				break;
			case 2: // Accessory ID
				p->byRAM[0x4E + byByte] = byValue;
				break;
			case 1: // Device ID
				p->byRAM[0x45 + byByte] = byValue;
				break;
			}
		}
		break;
	case 4: // set terminator mode
		TRACE(_T("HPIL: set terminator mode\n"));
		if ((pbyMsg[2] & 0x08) != 0)
		{
			p->byEndmode = (pbyMsg[2] >> 2) & 1;
		}
		else
		{
			p->byCharmode = (pbyMsg[2] >> 0) & 1;
		}
		break;
	case 5: // set terminator character
		TRACE(_T("HPIL: set terminator character\n"));
		p->byEndchar = (pbyMsg[3] << 4) | pbyMsg[2];
		break;
	case 6: // set number of IDY timeout
		TRACE(_T("HPIL: set number of IDY timeout !\n"));
		break;
	case 7: // set IDY timeout value
		TRACE(_T("HPIL: set IDY timeout value !\n"));
		break;
	case 8: // clear data buffers
		TRACE(_T("HPIL: clear data buffers !\n"));
		break;
	case 9: // set IDY SRQ poll timeout
		TRACE(_T("HPIL: set IDY SRQ poll timeout\n"));
		p->byRAM[0x64] = (pbyMsg[3] << 4) | pbyMsg[2];
		break;
	case 10: // set up interrupt mask
		TRACE(_T("HPIL: set up interrupt mask\n"));
		EnterCriticalSection(&pMem->csStatus);
		{
			// I Interrupt Pending (set when an enabled interrupt has
			// occurred, cleared everytime interrupt mask byte
			// is set)
			p->byIoStatus[1] &= ~0x01;		// clear I
		}
		LeaveCriticalSection(&pMem->csStatus);
		p->byRAM[0x3F] = (pbyMsg[3] << 4) | pbyMsg[2];
		break;
	case 11: // read interrupt cause
		TRACE(_T("HPIL: read interrupt cause\n"));
		pbyMsg[8] &= 0x0a;					// clear bit sleep and msg available
		pbyMsg[9] &= 0x06;					// clear bit three data bytes and error occurred
		{
			BYTE byData = p->byRAM[0x40];	// load interrupt cause
			p->byRAM[0x40] = 0;				// content read
			pbyMsg[10] = byData & 0x0f;
			pbyMsg[11] = byData >> 4;
		}
		pbyMsg[12] = 0x03;
		pbyMsg[13] = 0x00;
		pbyMsg[14] = 0x00;
		pbyMsg[15] = 0x00;
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	case 12: // read DDC frame
		TRACE(_T("HPIL: read DDC frame\n"));
		pbyMsg[8] &= 0x0a;					// clear bit sleep and msg available
		pbyMsg[9] &= 0x06;					// clear bit three data bytes and error occurred
		{
			BYTE byData = p->byRAM[0x41];	// load DDC content
			p->byRAM[0x41] = 0;				// content read
			pbyMsg[10] = byData & 0x0f;
			pbyMsg[11] = byData >> 4;
		}
		pbyMsg[12] = 0x03;
		pbyMsg[13] = 0x00;
		pbyMsg[14] = 0x00;
		pbyMsg[15] = 0x00;
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	case 13: // terminate on loop SRQ
		TRACE(_T("HPIL: terminate on loop SRQ !\n"));
		break;
	case 14: // power up the loop
		TRACE(_T("HPIL: power up the loop\n"));
		if (p->byPowered == 0)				// loop powered down
		{
			SendFrame(pMem,0x400);			// NOP
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byIoStatus[1] &= ~0x4;	// clear P, disable IDY polling
			}
			LeaveCriticalSection(&pMem->csStatus);
		}

		// send status
		pbyMsg[8] &= 0x02;					// clear bit bus SRQ, sleep and msg available
		pbyMsg[9] &= 0x06;					// clear bit three data bytes and error occurred

		// already powered up or powered up successfully
		if (p->byPowered != 0 || pMem->sTcp.bLoopClosed)
		{
			p->byPowered = 1;				// loop powered up

			EnterCriticalSection(&pMem->csStatus);
			{
				p->byIoStatus[4] = 0;		// clear error
				p->byIoStatus[5] = 0;
			}
			LeaveCriticalSection(&pMem->csStatus);

			pbyMsg[8] |= 0x01;				// set bit msg available
		}

		_ASSERT(p->byIoStatus[2] == 0x01);
		pbyMsg[10] = p->byIoStatus[0];
		pbyMsg[11] = p->byIoStatus[1];
		pbyMsg[12] = p->byIoStatus[2];
		pbyMsg[13] = p->byIoStatus[3];
		pbyMsg[14] = p->byIoStatus[4];
		pbyMsg[15] = p->byIoStatus[5];
		break;
	case 15: // enable/disable IDY poll
		TRACE(_T("HPIL: enable/disable IDY poll\n"));
		EnterCriticalSection(&pMem->csStatus);
		{
			p->byIoStatus[1] &= ~0x4;		// clear P bit
		}
		LeaveCriticalSection(&pMem->csStatus);
		if (p->byPowered != 0)				// loop powered up
		{
			EnterCriticalSection(&pMem->csStatus);
			{
				// update P bit
				p->byIoStatus[1] |= (pbyMsg[2] << 2) & 0x4;
			}
			LeaveCriticalSection(&pMem->csStatus);
		}
		break;
	default:
		_ASSERT(FALSE);
	}
	return;
}

/*********************************************/
/* execmd()                                  */
/*                                           */
/* execution of mailbox opcodes              */
/*   pMem: module data                       */
/*   pbyMsg: mailbox data                    */
/*********************************************/
static VOID execmd(PHPILMEM pMem, LPBYTE pbyMsg)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	WORD wFrame;
	INT  n;

	switch (pbyMsg[5])
	{
	case 0: // no parameter class
		execmd0(pMem,pbyMsg);
		break;
	case 1: // frame class
		TRACE(_T("HPIL: send a frame\n"));

		// send a frame
		wFrame = (((WORD) pbyMsg[4] << 8) & 0x700)
				| (WORD) ((pbyMsg[3] << 4) | pbyMsg[2]);

		if ((p->byIoStatus[0] & 0x01) != 0)	// controller
		{
			// R bit set or manual mode
			if ((pbyMsg[4] & 0x08) != 0 || p->byManual == 1)
			{
				// no automatic RFC on CMD frames
				wFrame = TransmitFrame(pMem,wFrame);

				pbyMsg[8] &= 0x0a;			// clear bit sleep and msg available
				pbyMsg[9] &= 0x04;			// clear bit three data bytes, SRQ received from loop
											// and error occurred

				// received DOE or IDY frame
				if ((wFrame & 0x600) != 0x400)
				{
					// update "SRQ received from loop" bit
					pbyMsg[9] |= ((wFrame >> 7) & 0x2);
				}

				pbyMsg[10] = ((wFrame >> 0)  & 0x0f);
				pbyMsg[11] = ((wFrame >> 4)  & 0x0f);
				pbyMsg[12] = ((wFrame >> 8)  & 0x07) | 0x08; // data byte
				pbyMsg[13] = 0x08;
				// pbyMsg[14] and [15] aren't updated by the I/O CPU
				// set bit msg available
				pbyMsg[8] |= 0x01;
			}
			else
			{
				SendFrame(pMem,wFrame);
			}
		}
		else								// not controller
		{
			// check for frame
			BOOL bFrameValid = FALSE;

			// active talker
			if ((p->byFstate & 0xC0) == 0x40 && (p->byFstate & 0x03) != 0)
			{
				// DOE or EOT frame
				bFrameValid =  ((wFrame & 0x400) == 0)
							|| ((wFrame & 0xFFE) == 0x540);	
			}

			// listener
			if ((p->byFstate & 0xC0) == 0x80)
			{
				// NRD frame
				bFrameValid = (wFrame == 0x542);
			}

			// valid also IDY if asynchronous requests are enabled,
			// but asynchronous requests are not supported so far

			if (bFrameValid)
			{
				pbyMsg[7] |= 0x02;			// I/O CPU NRD (Not Ready for Data)
				pbyMsg[8] &= 0x0a;			// clear bit sleep and msg available
				pbyMsg[9] &= 0x04;			// clear bit three data bytes, SRQ received from loop
											// and error occurred
				OutSetFrame(pMem,wFrame);	// send frame on loop request
			}
			else
			{
				EnterCriticalSection(&pMem->csStatus);
				{
					p->byIoStatus[4] = 0x09; // illegal status for command
					p->byIoStatus[5] = 0x00;
				}
				LeaveCriticalSection(&pMem->csStatus);
				// set error bit
				pbyMsg[8] &= 0x0a;			// clear bit sleep and msg available
				pbyMsg[9] &= 0x07;			// clear bit three data bytes
				pbyMsg[9] |= 0x01;
				// send status
				_ASSERT(p->byIoStatus[2] == 0x01);
				pbyMsg[10] = p->byIoStatus[0];
				pbyMsg[11] = p->byIoStatus[1];
				pbyMsg[12] = p->byIoStatus[2];
				pbyMsg[13] = p->byIoStatus[3];
				pbyMsg[14] = p->byIoStatus[4];
				pbyMsg[15] = p->byIoStatus[5];
				// set bit msg available
				pbyMsg[8] |= 0x01;
			}
		}
		break;
	// single nibble parameter class
	case 2: // (un)address me as TL
		TRACE(_T("HPIL: (un)address me as TL\n"));
		if ((pbyMsg[3] & 0x1) != 0)			// unaddress
		{
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byCstate &= ~(pbyMsg[2] & 0x06);
				p->byIoStatus[0] &= ~(pbyMsg[2] & 0x06);
			}
			LeaveCriticalSection(&pMem->csStatus);
		}
		else								// address
		{
			BYTE byFlags = pbyMsg[2] & 0x06;

			if (byFlags != 0)				// any bit set
			{
				if ((byFlags & 0x04) != 0)	// MTA
				{
					SendFrame(pMem,0x45f);	// UNT
					byFlags &= ~0x02;		// reset L bit
				}

				// MLA + MTA
				EnterCriticalSection(&pMem->csStatus);
				{
					p->byCstate = byFlags;
					p->byIoStatus[0] &= ~0x06;
					p->byIoStatus[0] |= byFlags;
				}
				LeaveCriticalSection(&pMem->csStatus);
			}
		}
		break;
	case 3: // power down the loop
		TRACE(_T("HPIL: power down the loop\n"));
		if (p->byPowered != 0)				// loop powered up
		{
			if (pMem->sTcp.bLoopClosed) SendFrame(pMem,0x400); // NOP
			if (pMem->sTcp.bLoopClosed) SendFrame(pMem,0x49b); // LPD
			*(WORD *)&pbyMsg[8] &= ~0x0208;	// delete I/O SRQ on HP-71 bus & SRQ received from loop
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byIoStatus[1] |= 0x2;	// invalidate address table
				p->byIoStatus[1] &= ~0x4;	// clear P, disable IDY polling
			}
			LeaveCriticalSection(&pMem->csStatus);
			p->byPowered = 0;				// loop powered down
		}
		break;
	case 4: // address P,S as talker
		TRACE(_T("HPIL: address P,S as talker\n"));
		wFrame = (WORD) ((pbyMsg[3] << 4) | pbyMsg[2]) & 0x1f;
		n = ((pbyMsg[4] << 3) | (pbyMsg[3] >> 1)) & 0x1f;
		if (wFrame == 0 && n == 0)
		{
			wFrame = p->wCurrentAddr & 0x1f;
			n = (p->wCurrentAddr >> 5) & 0x1f;
		}
		EnterCriticalSection(&pMem->csStatus);
		{
			p->byIoStatus[0] &= ~0x04;		// device is talker, unaddress me
		}
		LeaveCriticalSection(&pMem->csStatus);
		SendFrame(pMem,(WORD)(0x440+wFrame)); // TAD n
		if (n > 0) // send SAD n-1
		{
			if (pMem->sTcp.bLoopClosed) SendFrame(pMem,(WORD)(0x460-1+n)); // SAD n-1
		}
		break;
	case 5: // address P,S as listener
		TRACE(_T("HPIL: address P,S as listener\n"));
		wFrame = (WORD) ((pbyMsg[3] << 4) | pbyMsg[2]) & 0x1f;
		n = ((pbyMsg[4] << 3) | (pbyMsg[3] >> 1)) & 0x1f;
		if (wFrame == 0 && n == 0)
		{
			wFrame = p->wCurrentAddr & 0x1f;
			n = (p->wCurrentAddr >> 5) & 0xfF;
		}
		EnterCriticalSection(&pMem->csStatus);
		{
			if (wFrame == 31) p->byIoStatus[0] &= ~0x02;
		}
		LeaveCriticalSection(&pMem->csStatus);
		SendFrame(pMem,(WORD)(0x420+wFrame)); // LAD n
		if (n > 0) // send SAD n-1
		{
			if (pMem->sTcp.bLoopClosed) SendFrame(pMem,(WORD)(0x460-1+n)); // SAD n-1
		}
		break;
	case 6: // find Nth device, type M
		TRACE(_T("HPIL: find Nth device, type M\n"));
		n = findndevice(pMem,(pbyMsg[3] << 4) | pbyMsg[2],pbyMsg[4] + 1);
		if (n > 0)
		{
			p->wCurrentAddr = n;
			pbyMsg[8] &= 0x0a;				// clear bit sleep and msg available
			pbyMsg[9] &= 0x06;				// clear bit three data bytes and error occurred
			pbyMsg[10] = ((p->wCurrentAddr >> 0) & 0x0f);
			pbyMsg[11] = ((p->wCurrentAddr >> 4) & 0x0f);
			pbyMsg[12] = ((p->wCurrentAddr >> 8) & 0x03) | 0x04;
			pbyMsg[13] = ((p->wCurrentAddr >> 4) & 0x0e);
			// pbyMsg[14] and [15] aren't updated by the I/O CPU
		}
		else
		{
			p->wCurrentAddr = 0;			// address of first device in loop
			incr_addr(p, &p->wCurrentAddr);

			// send status
			pbyMsg[8] &= 0x0a;				// clear bit sleep and msg available
			pbyMsg[9] &= 0x06;				// clear bit three data bytes and error occurred
			if (p->byAbort != 0)			// restore bit error occurred
			{
				pbyMsg[9] |= 0x01;
			}
			_ASSERT(p->byIoStatus[2] == 0x01);
			pbyMsg[10] = p->byIoStatus[0];
			pbyMsg[11] = p->byIoStatus[1];
			pbyMsg[12] = p->byIoStatus[2];
			pbyMsg[13] = p->byIoStatus[3];
			pbyMsg[14] = 0x01;				// error code not in the status register
			pbyMsg[15] = 0x00;
		}
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	case 7: // auto address the loop
		TRACE(_T("HPIL: auto address the loop\n"));
		n = (pbyMsg[4] & 0x01);				// n = 0 -> extended + simple adressing
											// n = 1 -> simple adressing
		n = autoaddrloop(pMem,n);			// address the loop
		p->wCurrentAddr = 0;				// no address
		if (n != 0)							// found a device
		{
			// set current address to address of 1st device in the loop
			incr_addr(p, &p->wCurrentAddr);
		}
		// send address of last device
		pbyMsg[8] &= 0x0a;					// clear bit sleep and msg available
		pbyMsg[9] &= 0x06;					// clear bit three data bytes and error occurred
		pbyMsg[10] = ((n >> 0) & 0x0f);
		pbyMsg[11] = ((n >> 4) & 0x0f);
		pbyMsg[12] = ((n >> 8) & 0x03) | 0x04;
		pbyMsg[13] = ((n >> 4) & 0x0e);
		// pbyMsg[14] and [15] aren't updated by the I/O CPU
		// set bit msg available
		pbyMsg[8] |= 0x01;
		break;
	// conversation class
	case 8:  // start data
	case 9:  // start status poll
	case 10: // start device id
	case 11: // start accessory id
		TRACE(_T("HPIL: start conversation\n"));
		p->dwMaxCount = pbyMsg[4];
		p->dwMaxCount <<= 4;
		p->dwMaxCount |= pbyMsg[3];
		p->dwMaxCount <<= 4;
		p->dwMaxCount |= pbyMsg[2];
		p->dwMaxCount <<= 4;
		p->dwMaxCount |= pbyMsg[1];
		p->dwMaxCount <<= 4;
		p->dwMaxCount |= pbyMsg[0];
		p->dwCount = 0;
		wFrame = 0x560 + (pbyMsg[5] - 8);	// SDA to SAI
		do_listen(pMem,pbyMsg,wFrame);
		break;
	case 12: // pass control
		TRACE(_T("HPIL: pass control\n"));
		wFrame = SendFrame(pMem,0x564);		// TCT
		if (pMem->sTcp.bLoopClosed && wFrame == 0x564)
		{
			EnterCriticalSection(&pMem->csStatus);
			{
				p->byIoStatus[4] = 0x02;	// device not ready error
				p->byIoStatus[5] = 0x00;
			}
			LeaveCriticalSection(&pMem->csStatus);
			// set error bit
			pbyMsg[8] &= 0x0a;				// clear bit sleep and msg available
			pbyMsg[9] &= 0x07;				// clear bit three data bytes
			pbyMsg[9] |= 0x01;
			// send status
			_ASSERT(p->byIoStatus[2] == 0x01);
			pbyMsg[10] = p->byIoStatus[0];
			pbyMsg[11] = p->byIoStatus[1];
			pbyMsg[12] = p->byIoStatus[2];
			pbyMsg[13] = p->byIoStatus[3];
			pbyMsg[14] = p->byIoStatus[4];
			pbyMsg[15] = p->byIoStatus[5];
			// set bit msg available
			pbyMsg[8] |= 0x01;
		}
		else
		{
			p->byPassControl = 1;			// request to give up control

			if (pMem->sTcp.bLoopClosed)		// got a frame
			{
				// handle frame as device
				HpilDevice(&pMem->sTcp,wFrame);
			}
		}
		break;
	case 13: // set frame timeout value
		TRACE(_T("HPIL: set timeout value\n"));
		p->dwFrameTimeout = pbyMsg[4];
		p->dwFrameTimeout <<= 4;
		p->dwFrameTimeout |= pbyMsg[3];
		p->dwFrameTimeout <<= 4;
		p->dwFrameTimeout |= pbyMsg[2];
		p->dwFrameTimeout <<= 4;
		p->dwFrameTimeout |= pbyMsg[1];
		p->dwFrameTimeout <<= 4;
		p->dwFrameTimeout |= pbyMsg[0];
		break;
	case 14: // set frame count
		TRACE(_T("HPIL: set frame count\n"));
		p->dwFrameCount = pbyMsg[4];
		p->dwFrameCount <<= 4;
		p->dwFrameCount |= pbyMsg[3];
		p->dwFrameCount <<= 4;
		p->dwFrameCount |= pbyMsg[2];
		p->dwFrameCount <<= 4;
		p->dwFrameCount |= pbyMsg[1];
		p->dwFrameCount <<= 4;
		p->dwFrameCount |= pbyMsg[0];
		TRACE(_T("HPIL: set frame count = %d\n"),p->dwFrameCount);
		p->dwCount = 0;
		break;
	case 15: // multibyte parameter class
		execmd15(pMem,pbyMsg);
		break;
	}
	return;
}

/*********************************************/
/* reset_mb()                                */
/*                                           */
/* reset a HPIL mailbox                      */
/*   pMem: module data                       */
/*********************************************/
static VOID reset_mb(PHPILMEM pMem)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	TRACE(_T("HPIL: mailbox reset\n"));

	InClear(pMem);							// clear input buffer
	OutClear(pMem);							// clear output buffer

	p->byIOMem[7] &= ~0x03;					// clear I/O CPU NRD (Not Ready for Data)
	p->byIOMem[8] &= ~0x03;					// clear I/O CPU NRD (Not Ready for Data)

	// init I/O processor status data, STLC BPUI 0001 KRXV NNNN NNNN
	EnterCriticalSection(&pMem->csStatus);
	{
		p->byIoStatus[0] = 0x1;				// STLC
		p->byIoStatus[1] = 0x0;				// BPUI
		p->byIoStatus[2] = 0x1;				// 0001
		p->byIoStatus[3] = 0x0;				// KRXV
		p->byIoStatus[4] = 0x0;				// NNNN
		p->byIoStatus[5] = 0x0;				// NNNN
	}
	LeaveCriticalSection(&pMem->csStatus);
	p->byRAM[0x64] = 255;					// IDY SRQ poll timeout

	ResetHpilData(p);						// reset IO processor data
	return;
}

/*********************************************/
/* device_listen()                           */
/*                                           */
/* listen transfer when configured as device */
/*   pMem: module data                       */
/*********************************************/
static VOID device_listen(PHPILMEM pMem)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	BYTE byInCnt = InCountData(pMem);		// no. of data bytes in input buffer

	if (byInCnt > 0)						// has data
	{
		TRACE(_T("HPIL: device_listen() = %d (%d)\n"),p->dwCount,p->dwFrameCount);

		if (p->dwCount < p->dwFrameCount || p->dwFrameCount == 0xFFFFF)
		{
			BYTE byRecData[3];

			TRACE(_T("HPIL: device_listen() fetch\n"));

			p->byIOMem[8] &= 0x0a;			// clear bit sleep and msg available
			p->byIOMem[9] &= 0x06;			// clear bit three data bytes and error occurred

			// 1st data byte
			VERIFY(InGetData(pMem,&byRecData[0]));

			// 1 byte to receive
			if (byInCnt < 3 || p->dwFrameCount < 3 + p->dwCount)
			{
				byRecData[1] = 0x08;		// data byte
				byRecData[2] = 0x00;

				++p->dwCount;
				--byInCnt;					// remaining data bytes in input buffer
			}
			else // 3 bytes to receive
			{
				// 2nd and 3rd data byte
				VERIFY(InGetData(pMem,&byRecData[1]));
				VERIFY(InGetData(pMem,&byRecData[2]));

				p->byIOMem[9] |= 0x08;		// set three data bytes
				p->dwCount += 3;
				byInCnt -= 3;				// remaining data bytes in input buffer
			}

			p->byIOMem[10] = ((byRecData[0] >> 0) & 0x0f);
			p->byIOMem[11] = ((byRecData[0] >> 4) & 0x0f);
			p->byIOMem[12] = ((byRecData[1] >> 0) & 0x0f);
			p->byIOMem[13] = ((byRecData[1] >> 4) & 0x0f);
			p->byIOMem[14] = ((byRecData[2] >> 0) & 0x0f);
			p->byIOMem[15] = ((byRecData[2] >> 4) & 0x0f);
			// set bit service request & msg available
			p->byIOMem[8] |= 0x09;
		}
	}
	else
	{
		TRACE(_T("HPIL: device_listen() no data\n"));

		if (p->dwFrameCount == 0xFFFFF)		// unlimited transfer
		{
			// last received data frame was END frame or character matched
			if (   (p->byEndmode  && (p->wLastDataFrame & 0x600) == 0x200)
				|| (p->byCharmode && (p->wLastDataFrame & 0xff) == p->byEndchar)
			   )
			{
				TRACE(_T("HPIL: device_listen() term match\n"));

				p->byIOMem[9] &= ~0x08;		// clear bit three data bytes

				// term cond met
				p->byIOMem[10] = 5;			// terminator match
				p->byIOMem[11] = 0;			// status message
				p->byIOMem[12] = 0;
				p->byIOMem[13] = 0;
				// set bit msg available
				p->byIOMem[8] |= 0x01;
			}
		}
	}

	if (byInCnt == 0)						// no data bytes in input buffer
	{
		EnterCriticalSection(&pMem->csStatus);
		{
			// clear Data Available in input buffer
			p->byIoStatus[3] &= ~0x1;
		}
		LeaveCriticalSection(&pMem->csStatus);
	}
	return;
}

/*********************************************/
/* ilmailbox()                               */
/*                                           */
/* process the hpil mailbox data after a     */
/* read of write access to a mailbox         */
/*   pMem: module data                       */
/*********************************************/
VOID ilmailbox(PHPILMEM pMem)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	LPBYTE pbyMsg = p->byIOMem;				// message buffer

	// get operating mode
	BOOL bController = ((p->byIoStatus[0] & 0x01) != 0);

	_ASSERT(!IsDataPacked(pbyMsg,0x10));	// mailbox data is unpacked

	// test if msg received
	if ((pbyMsg[7] & 0x08) != 0)			// I/O CPU reset bit
	{
		if (p->eState != MBRESET)
		{
			p->eState = MBRESET;
			reset_mb(pMem);
		}
	}
	else
	{
		if (p->eState == MBRESET)
		{
			p->eState = MBIDLE;
		}
	}
	// 71 message available or mailbox command still has a message
	if ((pbyMsg[7] & 0x01) != 0 || p->byMsgCount != 0)
	{
		if ((pbyMsg[6] & 0x08) != 0)		// three data bytes
		{
			sendbytes(pMem,pbyMsg,3);
		}
		else
		{
			if ((pbyMsg[6] & 0x04) != 0)	// one data byte
			{
				sendbytes(pMem,pbyMsg,1);
			}
			else
			{
				execmd(pMem,pbyMsg);
			}
		}
		pbyMsg[7] &= ~0x01;					// clear msg available bit
		bController = TRUE;					// control transfer
	}
	if (bController && p->eState == ACT_LISTEN)
	{
		if ((pbyMsg[8] & 0x01) == 0)		// I/O CPU message not available
		{
			do_listen(pMem,pbyMsg,p->wLastframe);
		}
	}
	if (bController && p->eState == END_LISTEN)
	{
		if ((pbyMsg[8] & 0x01) == 0)		// I/O CPU message not available
		{
			end_listen(pMem,pbyMsg);
		}
	}
	if (!bController)						// device mode and no cmd handling
	{
		if ((pbyMsg[8] & 0x01) == 0)		// I/O CPU message not available
		{
			device_listen(pMem);			// device listen when device mode
		}
	}
	_ASSERT(!IsDataPacked(pbyMsg,0x10));	// mailbox data is unpacked
	return;
}
