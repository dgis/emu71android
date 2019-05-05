/*
 *   hpilbuf.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2017 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "hpil.h"

//
// set data into input buffer
//
BOOL InSetData(PHPILMEM pMem,BYTE byData)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	BOOL bSucc;

	// room in buffer?
	WaitForSingleObject(pMem->hInSetEvent,p->dwFrameTimeout);

	EnterCriticalSection(&pMem->csBuffer);
	{
		BYTE byIn  = p->byRAM[0x76] - 0x7D;	// zero based in pointer
		BYTE byOut = p->byRAM[0x77] - 0x7D;	// zero based out pointer

		// next zero based in pointer
		BYTE byNxtIn = (byIn + 1) % p->byRAM[0x74];

		if ((bSucc = (byNxtIn != byOut)))	// still room for data
		{
			p->byRAM[0x7D + byIn] = byData;
			p->byRAM[0x76] = 0x7D + byNxtIn;

			SetEvent(pMem->hInGetEvent);	// in data available
		}

		if (!InFullData(pMem))				// still room in input buffer
		{
			SetEvent(pMem->hInSetEvent);	// buffer not full
		}
	}
	LeaveCriticalSection(&pMem->csBuffer);
	return bSucc;
}

//
// get data from input buffer
//
BOOL InGetData(PHPILMEM pMem,LPBYTE pbyData)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	BOOL bSucc;

	// data in buffer?
	WaitForSingleObject(pMem->hInGetEvent,p->dwFrameTimeout);

	EnterCriticalSection(&pMem->csBuffer);
	{
		BYTE byIn  = p->byRAM[0x76] - 0x7D;	// zero based in pointer
		BYTE byOut = p->byRAM[0x77] - 0x7D;	// zero based out pointer

		if ((bSucc = (byIn != byOut)))		// data in buffer
		{
			*pbyData = p->byRAM[0x7D + byOut];
			p->byRAM[0x77] = 0x7D + ((byOut + 1) % p->byRAM[0x74]);
		}

		if (InCountData(pMem) > 0)				
		{
			SetEvent(pMem->hInGetEvent);	// buffer contain data
		}
		if (!InFullData(pMem))				// still room in input buffer
		{
			SetEvent(pMem->hInSetEvent);	// buffer not full
		}
	}
	LeaveCriticalSection(&pMem->csBuffer);
	return bSucc;
}

//
// clear input buffer
//
VOID InClear(PHPILMEM pMem)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	EnterCriticalSection(&pMem->csBuffer);
	{
		p->byRAM[0x77] = p->byRAM[0x76];
		ResetEvent(pMem->hInGetEvent);		// buffer empty
		SetEvent(pMem->hInSetEvent);		// buffer not full
	}
	LeaveCriticalSection(&pMem->csBuffer);
	return;
}

//
// get number of data bytes in input buffer
//
BYTE InCountData(PHPILMEM pMem)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	BYTE byCnt;

	EnterCriticalSection(&pMem->csBuffer);
	{
		DWORD dwIn  = p->byRAM[0x76];		// in pointer
		DWORD dwOut = p->byRAM[0x77];		// out pointer 
		byCnt = (BYTE) ((dwIn + p->byRAM[0x74] - dwOut) % p->byRAM[0x74]);
	}
	LeaveCriticalSection(&pMem->csBuffer);
	return byCnt;
}

//
// check if output buffer is full
//
BOOL InFullData(PHPILMEM pMem)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	BOOL bFull;

	EnterCriticalSection(&pMem->csBuffer);
	{
		// cannot add 1 data byte
		bFull = p->byRAM[0x74] <= InCountData(pMem) + 1;
	}
	LeaveCriticalSection(&pMem->csBuffer);
	return bFull;
}

//
// set data into output buffer
//
BOOL OutSetData(PHPILMEM pMem,BYTE byData)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	BOOL bSucc;

	// room in buffer?
	WaitForSingleObject(pMem->hOutSetEvent,p->dwFrameTimeout);

	EnterCriticalSection(&pMem->csBuffer);
	{
		BYTE byBuf = p->byRAM[0x79];		// output buffer start
		BYTE byIn  = p->byRAM[0x7A] - byBuf; // zero based in pointer
		BYTE byOut = p->byRAM[0x7B] - byBuf; // zero based out pointer

		// next zero based in pointer
		BYTE byNxtIn = (byIn + 1) % p->byRAM[0x75];

		if ((bSucc = (byNxtIn != byOut)))	// still room for data
		{
			p->byRAM[byBuf + byIn] = byData;
			p->byRAM[0x7A] = byBuf + byNxtIn;

			SetEvent(pMem->hOutGetEvent);	// in data available
		}

		if (!OutFullData(pMem))				// still room in output buffer
		{
			SetEvent(pMem->hOutSetEvent);	// buffer not full
		}
	}
	LeaveCriticalSection(&pMem->csBuffer);
	return bSucc;
}

//
// send frame after output buffer is empty
//
BOOL OutSetFrame(PHPILMEM pMem,WORD wFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	DWORD dwRefTime;
	BYTE  byRefCnt;
	BOOL  bSucc;

	// check is already a control frame in buffer
	if ((bSucc = (p->wLastframe & 0x8000) == 0))
	{
		// no, add it
		p->wLastframe = 0x8000 | wFrame;
	}

	SetEvent(pMem->hOutGetEvent);			// in data available

	dwRefTime = timeGetTime();				// the actual time stamp
	byRefCnt = OutCountData(pMem);			// data frames in out buffer

	while ((p->wLastframe & 0x8000) != 0)	// wait for last frame send
	{
		DWORD dwTime = timeGetTime();		// the actual time stamp
		BYTE byCnt = OutCountData(pMem);	// data frames in out buffer

		if (byCnt != byRefCnt)				// sending data
		{
			dwRefTime = dwTime;				// update references
			byRefCnt = byCnt;
		}
		else								// no change on buffer size
		{
			// check for frame timeout
			if (dwTime > dwRefTime + p->dwFrameTimeout)
			{
				break;						// timeout
			}
		}

		Sleep(10);
	}
	return bSucc;
}

//
// get data from output buffer
//
BOOL OutGetData(PHPILMEM pMem,LPWORD pwFrame)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	BOOL bSucc;

	// data in buffer?
	WaitForSingleObject(pMem->hOutGetEvent,p->dwFrameTimeout);

	EnterCriticalSection(&pMem->csBuffer);
	{
		BYTE byBuf = p->byRAM[0x79];		// output buffer start
		BYTE byIn  = p->byRAM[0x7A] - byBuf; // zero based in pointer
		BYTE byOut = p->byRAM[0x7B] - byBuf; // zero based out pointer

		if ((bSucc = (byIn != byOut)))		// data in buffer
		{
			*pwFrame = p->byRAM[byBuf + byOut];
			p->byRAM[0x7B] = byBuf + ((byOut + 1) % p->byRAM[0x75]);
		}
		else
		{
			// have a control frame
			if ((p->wLastframe & 0x8000) != 0)
			{
				// use it
				*pwFrame = p->wLastframe & 0x7FF;
				p->wLastframe = 0;
				bSucc = TRUE;
			}
		}

		if (OutCountData(pMem) > 0 || (p->wLastframe & 0x8000) != 0)				
		{
			SetEvent(pMem->hOutGetEvent);	// buffer contain data
		}
		if (!OutFullData(pMem))				// still room in output buffer
		{
			SetEvent(pMem->hOutSetEvent);	// buffer not full
		}
	}
	LeaveCriticalSection(&pMem->csBuffer);
	return bSucc;
}

//
// clear output buffer
//
VOID OutClear(PHPILMEM pMem)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	EnterCriticalSection(&pMem->csBuffer);
	{
		p->byRAM[0x7B] = p->byRAM[0x7A];
		ResetEvent(pMem->hOutGetEvent);		// buffer empty
		SetEvent(pMem->hOutSetEvent);		// buffer not full
	}
	LeaveCriticalSection(&pMem->csBuffer);
	return;
}

//
// get number of data bytes in output buffer
//
BYTE OutCountData(PHPILMEM pMem)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	BYTE byCnt;

	EnterCriticalSection(&pMem->csBuffer);
	{
		DWORD dwIn  = p->byRAM[0x7A];		// in pointer
		DWORD dwOut = p->byRAM[0x7B];		// out pointer 
		byCnt = (BYTE) ((dwIn + p->byRAM[0x75] - dwOut) % p->byRAM[0x75]);
	}
	LeaveCriticalSection(&pMem->csBuffer);
	return byCnt;
}

//
// check if output buffer is full
//
BOOL OutFullData(PHPILMEM pMem)
{
	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	BOOL bFull;

	EnterCriticalSection(&pMem->csBuffer);
	{
		// cannot add 3 data bytes
		bFull = p->byRAM[0x75] <= OutCountData(pMem) + 3;
	}
	LeaveCriticalSection(&pMem->csBuffer);
	return bFull;
}
