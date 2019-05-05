/*
 *   stack.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gieﬂelink
 *
 */
#include "pch.h"
#include "resource.h"
#include "Emu71.h"
#include "io.h"
#include "ops.h"

#define BitsOK		(1 << (1 - 0))			// display status bits
#define UpdOff		(1 << (4 - 4))
#define CurOff		(1 << (6 - 4))

#define flCALC		64						// CALC mode?

#define DSPSTA		0x2F475					// display status
#define FIRSTC		0x2F47C					// first character
#define	CURSOR		0x2F47E					// cursor position
#define	DSPBFS		0x2F480					// display buffer start
#define	DSPBFE		0x2F540					// display buffer end
#define	SYSFLG		0x2F6D9					// start of system flags

#define BLDDSP		0x01898

BOOL GetSystemFlag(INT nFlag)
{
	DWORD dwAddr;
	BYTE byMask,byFlag;

	_ASSERT(nFlag > 0);						// first flag is 1

	// calculate memory address and bit mask
	dwAddr = SYSFLG + (nFlag - 1) / 4;
	byMask = 1 << ((nFlag - 1) & 0x3);

	Npeek(&byFlag,dwAddr,sizeof(byFlag));
	return (byFlag & byMask) != 0;
}

static VOID DspRedraw(VOID)					// variables must be in system RAM
{
	CHIPSET *pOrgChipset = (CHIPSET *) malloc(sizeof(CHIPSET));
	if (pOrgChipset != NULL)
	{
		*pOrgChipset = Chipset;				// save original chipset

		// entry for =BLDDSP
		Chipset.P = 0;						// P=0
		Chipset.mode_dec = FALSE;			// hex mode
		Chipset.dd[SLAVE1].Ram[0x78] &= ~BitsOK; // display bits not ok

		Chipset.pc = BLDDSP;				// =BLDDSP entry
		rstkpush(0xFFFFF);					// return address for stopping

		while (Chipset.pc != 0xFFFFF)		// wait for stop address
		{
			EvalOpcode(FASTPTR(Chipset.pc));// execute opcode
		}

		// update display area of original chipset
		CopyMemory(pOrgChipset->dd[0].IORam, Chipset.dd[0].IORam, 0x60);
		CopyMemory(pOrgChipset->dd[1].IORam, Chipset.dd[1].IORam, 0x60);
		CopyMemory(pOrgChipset->dd[2].IORam, Chipset.dd[2].IORam, 0x60);

		Chipset = *pOrgChipset;				// restore original chipset
		free(pOrgChipset);
	}
	return;
}

//################
//#
//#    Stack routines
//#
//################

//
// ID_STACK_COPY
//
LRESULT OnStackCopy(VOID)					// copy data from stack
{
	BYTE   byBuffer[DSPBFE-DSPBFS];
	HANDLE hClipObj;
	LPBYTE lpbyData;
	DWORD  dwStart,dwStop,dwSize;
	UINT   i;

	_ASSERT(nState == SM_RUN);				// emulator must be in RUN state
	if (WaitForSleepState())				// wait for cpu SHUTDN then sleep state
	{
		InfoMessage(_T("The emulator is busy."));
		return 0;
	}

	_ASSERT(nState == SM_SLEEP);

	// fetch content of display buffer
	Npeek(byBuffer,DSPBFS,ARRAYSIZEOF(byBuffer));

	// pack the display buffer
	for (i = 0; i < ARRAYSIZEOF(byBuffer) / 2; ++i)
	{
		byBuffer[i] = (byBuffer[i*2+1] << 4) | byBuffer[i*2];
	}

	// skip heading spaces
	for (dwStart = 0; dwStart < ARRAYSIZEOF(byBuffer) / 2 && byBuffer[dwStart] == ' '; ++dwStart) { }

	// skip tailing 0
	for (dwStop = ARRAYSIZEOF(byBuffer) / 2 - 1; dwStop > 0 && byBuffer[dwStop] == 0; --dwStop) { }

	// skip tailing spaces
	for (; dwStop > 0 && byBuffer[dwStop] == ' '; --dwStop) { }

	byBuffer[dwStop+1] = 0;					// write EOS

	if (dwStart > dwStop)					// only spaces
	{
		dwStart = dwStop = 0;
	}

	// get string length of buffer
	dwSize = dwStop + 1 - dwStart;

	// memory allocation for clipboard data
	if ((hClipObj = GlobalAlloc(GMEM_MOVEABLE,dwSize + 1)) == NULL)
		goto error;

	if ((lpbyData = (LPBYTE) GlobalLock(hClipObj))) // lock memory
	{
		// copy data into clipboard buffer
		CopyMemory(lpbyData,&byBuffer[dwStart],dwSize + 1);

		GlobalUnlock(hClipObj);				// unlock memory

		if (OpenClipboard(hWnd))
		{
			if (EmptyClipboard())
				SetClipboardData(CF_TEXT,hClipObj);
			else
				GlobalFree(hClipObj);
			CloseClipboard();
		}
		else								// clipboard open failed
		{
			GlobalFree(hClipObj);
		}
	}

error:
	SwitchToState(SM_RUN);
	return 0;
}

//
// ID_STACK_PASTE
//
LRESULT OnStackPaste(VOID)					// paste data to stack
{
	#if defined _UNICODE
		#define CF_TEXTFORMAT CF_UNICODETEXT
	#else
		#define CF_TEXTFORMAT CF_TEXT
	#endif

	HANDLE hClipObj;

	BOOL bSuccess = FALSE;

	// check if clipboard format is available
	if (!IsClipboardFormatAvailable(CF_TEXTFORMAT))
	{
		MessageBeep(MB_OK);					// error beep
		return 0;
	}

	SuspendDebugger();						// suspend debugger
	bDbgAutoStateCtrl = FALSE;				// disable automatic debugger state control

	// calculator off or cursor not blinking
	if (!(Chipset.dd[MASTER].IORam[DD1CTL & 0xFF]&WKE))
	{
		KeyboardEvent(TRUE,0,0x8000);
		Sleep(dwWakeupDelay);
		KeyboardEvent(FALSE,0,0x8000);

		// wait for sleep mode
		while(Chipset.Shutdn == FALSE) Sleep(0);
	}

	_ASSERT(nState == SM_RUN);				// emulator must be in RUN state
	if (WaitForSleepState())				// wait for cpu SHUTDN then sleep state
	{
		InfoMessage(_T("The emulator is busy."));
		goto cancel;
	}

	_ASSERT(nState == SM_SLEEP);

	if (OpenClipboard(hWnd))
	{
		if ((hClipObj = GetClipboardData(CF_TEXTFORMAT)))
		{
			LPCTSTR lpstrClipdata;
			LPBYTE  lpbyData;

			if ((lpstrClipdata = (LPCTSTR) GlobalLock(hClipObj)))
			{
				DWORD dwCursor,dwStart;

				DWORD dwSize = lstrlen(lpstrClipdata);
				if ((lpbyData = (LPBYTE) malloc(dwSize * 2)))
				{
					BYTE   byDspSta[2];
					LPBYTE lpbySrc,lpbyDest;
					DWORD  dwLoop;

					#if defined _UNICODE
						// copy data UNICODE -> ASCII
						WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK,
											lpstrClipdata, dwSize,
											(LPSTR) lpbyData+dwSize, dwSize, NULL, NULL);
					#else
						// copy data
						memcpy(lpbyData+dwSize,lpstrClipdata,dwSize);
					#endif

					// unpack data
					lpbySrc = lpbyData+dwSize;
					lpbyDest = lpbyData;
					dwLoop = dwSize;
					while (dwLoop-- > 0)
					{
						BYTE byTwoNibs = *lpbySrc++;
						*lpbyDest++ = (BYTE) (byTwoNibs & 0xF);
						*lpbyDest++ = (BYTE) (byTwoNibs >> 4);
					}

					dwSize *= 2;			// size in nibbles

					// cursor position in display buffer
					dwCursor = Read2(CURSOR);

					// insert position in CMD mode
					dwStart = DSPBFS + 2 * dwCursor;

					// size until buffer end
					dwSize = MIN(dwSize,DSPBFE - dwStart);

					// fill display buffer with data
					Nwrite(lpbyData,dwStart,dwSize);

					// cursor position
					dwCursor = MIN(dwCursor + dwSize / 2,(DSPBFE - DSPBFS) / 2 - 1);
					Write2(CURSOR,(BYTE) dwCursor);

					// update display status to redraw display
					Npeek(byDspSta,DSPSTA+3,sizeof(byDspSta));
					byDspSta[0] &= ~(BitsOK);			// clear BitsOK bit
					byDspSta[1] &= ~(CurOff | UpdOff);	// clear CurOff and UpdOff bit
					Nwrite(byDspSta,DSPSTA+3,sizeof(byDspSta));

					free(lpbyData);
				}

				GlobalUnlock(hClipObj);
			}
		}
		CloseClipboard();
	}

	SwitchToState(SM_RUN);					// run state
	while (nState!=nNextState) Sleep(0);
	_ASSERT(nState == SM_RUN);

	if (bSuccess == FALSE)					// data not copied
		goto cancel;

cancel:
	bDbgAutoStateCtrl = TRUE;				// enable automatic debugger state control
	ResumeDebugger();
	return 0;
	#undef CF_TEXTFORMAT
}
