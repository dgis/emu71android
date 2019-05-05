/*
 *   hpil.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "ops.h"
#include "io.h"
#include "portmem.h"
#include "hpil.h"

// #define DEBUG_ILIO						// switch for HPIL I/O debug purpose

#define MEMVERSION	0x0001					// HP-IL memory version

#define DSPSET		0x2F7B1					// Display device set up on HPIL

#define DispOK		0x08					// Display device is set up
#define Wallby		0x04					// Display device is a Wallaby
#define Printr		0x02					// Display device is a printer
#define LoopOK		0x01					// Loop has not died while in disp

BOOL  bEnableRFC = TRUE;					// send a RFC frame behind a CMD frame
BOOL  bHpilRealDevices  = TRUE;				// real IL hardware maybe connected over Pilbox
DWORD dwHpilLoopTimeout = 500;				// standard timeout for finishing the virtual IL

static DWORD WINAPI IoThread(LPVOID pParam);

////////////////////////////////////////////////////////////////////////////////
//
// Memory structure initialization
//
////////////////////////////////////////////////////////////////////////////////

static VOID InitIoMem(PHPILDATA p)
{
	p->wVersion = MEMVERSION;				// version of data structure

	// init I/O processor state data, STLC BPUI 0001 KRXV NNNN NNNN
	p->byIoStatus[0] = 0x1;					// STLC
	p->byIoStatus[1] = 0x2;					// BPUI
	p->byIoStatus[2] = 0x1;					// 0001
	p->byIoStatus[3] = 0x0;					// KRXV
	p->byIoStatus[4] = 0x0;					// NNNN
	p->byIoStatus[5] = 0x0;					// NNNN

	// init I/O processor internal RAM
	p->byRAM[0x3D] = 0;						// NRD-INTR-VALUE
	p->byRAM[0x3F] = 0;						// interrupt mask
	p->byRAM[0x40] = 0;						// interrupt cause
	p->byRAM[0x41] = 0;						// DDC content, lower 8 bit of DDL or DDT frame
	p->byRAM[0x64] = 255;					// IDY SRQ poll timeout
	p->byRAM[0x74] = 65;					// input  buffer size
	p->byRAM[0x75] = 66;					// output buffer size
	p->byRAM[0x76] = 0x7D;					// input buffer input  pointer
	p->byRAM[0x77] = p->byRAM[0x76];		// input buffer output pointer
	p->byRAM[0x78] = p->byRAM[0x74];		// input buffer space
	p->byRAM[0x79] = 0xBE;					// dividing address (start of output buffer)
	p->byRAM[0x7A] = p->byRAM[0x79];		// output buffer input  pointer
	p->byRAM[0x7B] = p->byRAM[0x7A];		// output buffer output pointer

	p->bySleep = 0x4;						// I/O CPU sleep

	// 1 = loop powered up
	p->byPowered = Chipset.dd[MASTER].IORam[DD1CTL & 0xFF] & DON;

	ResetHpilData(p);						// reset IO processor data
	return;
}

VOID ResetHpilData(PHPILDATA p)
{
	p->eState = MBIDLE;
	p->dwMaxCount = 0;
	p->dwCount = 0;
	p->dwFrameCount = 0;
	p->wLastframe = 0;
	p->byWrBufPos = 0;
	p->byRdBufPos = 0;
	p->byEndmode = 0;
	p->byCharmode = 0;
	p->byEndchar = 0x0A;					// LF at power on
	p->byManual = 0;						// auto
	p->byMsgCount = 0;						// mailbox command return one message
	p->byCstate = 0;						// HP-IL controller state machine flags
	p->wCurrentAddr = 0;
	p->dwFrameTimeout = 2000;				// frame timeout
	p->byAbort = 0;							// no abort operation because of fatal error
	p->byPassControl = 0;					// request to give up control
	DeviceInit(p);							// init device with default
	return;
}

////////////////////////////////////////////////////////////////////////////////
//
// I/O processor thread
//
////////////////////////////////////////////////////////////////////////////////

static VOID CreateHpilThread(PHPILMEM pMem)
{
	DWORD dwThreadId;

	InitializeCriticalSection(&pMem->csLock);
	InitializeCriticalSection(&pMem->csStatus);
	InitializeCriticalSection(&pMem->csBuffer);

	TcpInit(&pMem->sTcp);					// init tcp/ip stack
	pMem->sTcp.bRealDevices = bHpilRealDevices;	  // fetch real IL hardware setting from registry
	pMem->sTcp.dwLoopTimeout = dwHpilLoopTimeout; // fetch standard timeout setting from registry

	pMem->bEnableRFC = bEnableRFC;			// fetch RFC enable flag setting from registry

	// clear DispOK and set LoopOK bit in =DSPSET flag register to reinitialize DISPLAY IS setting
	*IRAM(DSPSET) = (*IRAM(DSPSET) & ~DispOK) | LoopOK;

	pMem->psData->byIoStatus[1] |= 0x2;		// set U: invalidate Address table to initalize the loop

	pMem->bIoRunning = TRUE;				// I/O thread started

	// event for mailbox data changed
	VERIFY(pMem->hIoEvent = CreateEvent(NULL,FALSE,FALSE,NULL));

	// event for data read acknowledge
	VERIFY(pMem->hAckEvent = CreateEvent(NULL,FALSE,FALSE,NULL));

	// event for device loop exchange
	VERIFY(pMem->hInSetEvent  = CreateEvent(NULL,FALSE,TRUE,NULL));
	VERIFY(pMem->hInGetEvent  = CreateEvent(NULL,FALSE,TRUE,NULL));
	VERIFY(pMem->hOutSetEvent = CreateEvent(NULL,FALSE,TRUE,NULL));
	VERIFY(pMem->hOutGetEvent = CreateEvent(NULL,FALSE,TRUE,NULL));

	// start the tcp/ip server for the virtual HPIL loop
	if (TcpCreateSvr(&pMem->sTcp))
	{
		TCHAR szInfo[64];
		wsprintf(szInfo,_T("Note: TCP/IP server port %u already in use."),pMem->sTcp.wPortIn);
		InfoMessage(szInfo);
	}

	// start I/O processor thread
	VERIFY(pMem->hIoThread = CreateThread(NULL,0,&IoThread,pMem,0,&dwThreadId));
	return;
}

static VOID DestroyHpilThread(PHPILMEM pMem)
{
	if (pMem->hIoThread)
	{
		pMem->bIoRunning = FALSE;			// exit I/O thread

		InClear(pMem);						// clear HP-IL buffers
		OutClear(pMem);
		pMem->psData->wLastframe = 0;

		SetEvent(pMem->hInGetEvent);		// exit pending buffers
		SetEvent(pMem->hOutSetEvent);

		SetEvent(pMem->hIoEvent);

		_ASSERT(pMem->hIoThread);			// wait for I/O thread down
		WaitForSingleObject(pMem->hIoThread,INFINITE);
		CloseHandle(pMem->hIoThread);
		pMem->hIoThread = NULL;
	}

	InClear(pMem);							// clear HP-IL buffers
	OutClear(pMem);
	SetEvent(pMem->hInSetEvent);			// exit pending buffers
	SetEvent(pMem->hOutGetEvent);
	TcpCloseSvr(&pMem->sTcp);				// close TCP/IP server
	if (pMem->hIoEvent)
	{
		CloseHandle(pMem->hIoEvent);		// close I/O event
		pMem->hIoEvent = NULL;
	}
	if (pMem->hAckEvent)
	{
		CloseHandle(pMem->hAckEvent);		// close data acknowledge event
		pMem->hAckEvent = NULL;
	}

	// close events for device loop exchange
	if (pMem->hInSetEvent)
	{
		CloseHandle(pMem->hInSetEvent);		// close event
		pMem->hInSetEvent = NULL;
	}
	if (pMem->hInGetEvent)
	{
		CloseHandle(pMem->hInGetEvent);		// close event
		pMem->hInGetEvent = NULL;
	}
	if (pMem->hOutSetEvent)
	{
		CloseHandle(pMem->hOutSetEvent);	// close event
		pMem->hOutSetEvent = NULL;
	}
	if (pMem->hOutGetEvent)
	{
		CloseHandle(pMem->hOutGetEvent);	// close event
		pMem->hOutGetEvent = NULL;
	}
	DeleteCriticalSection(&pMem->csLock);
	DeleteCriticalSection(&pMem->csStatus);
	DeleteCriticalSection(&pMem->csBuffer);
	return;
}

//
// I/O processor worker thread
//
static DWORD WINAPI IoThread(LPVOID pParam)
{
	PHPILMEM pMem = (PHPILMEM) pParam;

	PHPILDATA p = pMem->psData;				// pointer to the I/O RAM data

	_ASSERT(pMem != NULL);

	while (pMem->bIoRunning)				// main loop
	{
		DWORD dwResult,dwTimeout;

		// The timer for IDY frame polling is not implemented like in the
		// original HPIL module. The original module use a not interruptible
		// periodic timer generating the IDY frames. If the IDY frame event
		// is blocked by mailbox communication, the timer events are saved
		// and executed immediately after end of mailbox communication when
		// the I/O processor switch into idle state.

		// event signaled by mailbox access or timeout for IDY frame polling
		dwTimeout = (   (p->byIoStatus[1] & 0x4) != 0
					 && (*(WORD *) &p->byIOMem[8] & 0x0208) == 0x0000
					 && (p->byIoStatus[0] & 0x1) != 0
					 && pMem->sTcp.bLoopClosed
					 && p->byPowered != 0
					 && p->eState == MBIDLE
					)
				  ? p->byRAM[0x64]			// IDY SRQ poll timeout
				  : INFINITE;

		dwResult = WaitForSingleObject(pMem->hIoEvent,dwTimeout);
		if (!pMem->bIoRunning) break;

		switch (dwResult)
		{
		case WAIT_OBJECT_0: // mailbox data changed
			EnterCriticalSection(&pMem->csLock);
			{
				p->bySleep = 0x0;			// I/O CPU awake
				ilmailbox(pMem);			// eval command
				if (p->byMsgCount == 0)
				{
					p->bySleep = 0x4;		// I/O CPU sleep
				}
			}
			LeaveCriticalSection(&pMem->csLock);
			SetEvent(pMem->hAckEvent);
			break;
		case WAIT_TIMEOUT: // IDY frame polling
			EnterCriticalSection(&pMem->csLock);
			{
				// IDY poll is enabled
				if ((p->byIoStatus[1] & 0x4) != 0)
				{
					// send the IDY frame
					WORD wFrame = TransmitFrame(pMem,0x600);

					// SRQ detected
					if ((wFrame & 0x700) == 0x700)
					{
						// I/O CPU SRQ on HP-71 bus & SRQ received from loop
						*(WORD *) &p->byIOMem[8] |= 0x0208;

						if (Chipset.Shutdn)	// cpu sleeping -> Wake Up
						{
							Chipset.bShutdnWake = TRUE;
							SetEvent(hEventShutdn);
						}
					}
					else					// no SRQ from loop
					{
						// no SRQ received from loop
						p->byIOMem[9] &= ~0x2;
					}
				}
			}
			LeaveCriticalSection(&pMem->csLock);
			break;
		default:
			break;
		}
	}
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Type allocation for mounting
//
////////////////////////////////////////////////////////////////////////////////

HANDLE AllocHpilMem(UINT nType,LPDWORD pdwSize,LPBYTE *ppbyMem,PSATCFG psCfg,LPCSTR pszAddrOut,WORD wPortOut,WORD wPortIn)
{
	PHPILMEM psHpilMem = NULL;

	// data structure contain only mailbox data or version is incompatible
	if (   *pdwSize <= 16
		|| *pdwSize != sizeof(HPILDATA)
		|| (   *pdwSize >= offsetof(HPILDATA,wVersion) + sizeof(((PHPILDATA)0)->wVersion)
			&& *(LPWORD) (*ppbyMem + offsetof(HPILDATA,wVersion)) != MEMVERSION
		   )
	   )
	{
		free(*ppbyMem);						// create a new compatible dataset
		*pdwSize = sizeof(HPILDATA);
		if ((*ppbyMem = (LPBYTE) calloc(1,*pdwSize)) != NULL)
		{
			// init the I/O processor memory
			InitIoMem((PHPILDATA) *ppbyMem);
		}
	}

	// has configuration dataset then alloc HP-IL memory structure
	if (*ppbyMem != NULL && (psHpilMem = (PHPILMEM) calloc(1,sizeof(*psHpilMem))) != NULL)
	{
		// init HP-IL memory structure
		psHpilMem->nType            = nType;
		psHpilMem->dwSize           = *pdwSize;
		psHpilMem->psData           = (PHPILDATA) *ppbyMem;
		psHpilMem->psCfg            = psCfg;
		psHpilMem->sTcp.lpszAddrOut = pszAddrOut;
		psHpilMem->sTcp.wPortOut    = wPortOut;
		psHpilMem->sTcp.wPortIn     = wPortIn;
	}
	return (HANDLE) psHpilMem;
}

////////////////////////////////////////////////////////////////////////////////
//
// Memory Interface
//
////////////////////////////////////////////////////////////////////////////////

//
// address is handled by module
//
static BOOL IsModuleHpilMem(HANDLE h,DWORD d)
{
	_ASSERT(h != NULL);
	if ((((PHPILMEM) h)->psCfg[0].bCfg))	// chip is configured
	{
		if (((((PHPILMEM) h)->psCfg[0].dwBase ^ d) & ~(ARRAYSIZEOF(((PHPILDATA)0)->byIOMem) - 1)) == 0)
			return TRUE;
	}
	return FALSE;
}

//
// configure module (CONFIG)
//
static BOOL ConfigHpilMem(HANDLE h)
{
	DWORD p = Npack(Chipset.C,5);			// config address

	_ASSERT(h != NULL);

	// chip is not configured
	if (((PHPILMEM) h)->psCfg[0].bCfg == FALSE)
	{
		((PHPILMEM) h)->psCfg[0].bCfg = TRUE;
		// adjust base to mapping boundary
		p &= ~(ARRAYSIZEOF(((PHPILDATA)0)->byIOMem) - 1);
		((PHPILMEM) h)->psCfg[0].dwBase = p;

		// mailbox configured
		EnterCriticalSection(&((PHPILMEM) h)->csLock);
		{
			// set Mailbox Configured bit in HP-71 High Handshake Nibble
			((PHPILMEM) h)->psData->byIOMem[7] |= 0x4;
		}
		LeaveCriticalSection(&((PHPILMEM) h)->csLock);
		return TRUE;
	}
	return FALSE;
}

//
// unconfigure module (UNCNFG)
//
static VOID UncnfgHpilMem(HANDLE h)
{
	DWORD p = Npack(Chipset.C,5);			// unconfig address

	_ASSERT(h != NULL);
	if (   ((PHPILMEM) h)->psCfg[0].bCfg != FALSE
		&& (p & ~(ARRAYSIZEOF(((PHPILDATA)0)->byIOMem) - 1)) == ((PHPILMEM) h)->psCfg[0].dwBase)
	{
		((PHPILMEM) h)->psCfg[0].bCfg = FALSE;

		// mailbox unconfigured
		EnterCriticalSection(&((PHPILMEM) h)->csLock);
		{
			// clear Mailbox Configured bit in HP-71 High Handshake Nibble
			((PHPILMEM) h)->psData->byIOMem[7] &= ~0x4;
		}
		LeaveCriticalSection(&((PHPILMEM) h)->csLock);
	}
	return;
}

//
// reset module (RESET)
//
static VOID ResetHpilMem(HANDLE h)
{
	_ASSERT(h != NULL);
	((PHPILMEM) h)->psCfg[0].bCfg   = FALSE;
	((PHPILMEM) h)->psCfg[0].dwBase = 0x00000;

	// mailbox unconfigured
	EnterCriticalSection(&((PHPILMEM) h)->csLock);
	{
		// clear Mailbox Configured bit in HP-71 High Handshake Nibble
		((PHPILMEM) h)->psData->byIOMem[7] &= ~0x4;
	}
	LeaveCriticalSection(&((PHPILMEM) h)->csLock);
	return;
}

//
// fetch ID (C=ID)
//
static DWORD C_Eq_IdHpilMem(HANDLE h)
{
	DWORD dwID = 0;							// all modules configured

	_ASSERT(h != NULL);
	if (((PHPILMEM) h)->psCfg[0].bCfg == FALSE)
	{
		// calculate ID for MM I/O from module size
		dwID = IoModuleId(ARRAYSIZEOF(((PHPILDATA)0)->byIOMem));
	}
	return dwID;
}

//
// service request of module
//
static BYTE SReqMem(HANDLE h)
{
	WORD wSREQ;

	EnterCriticalSection(&((PHPILMEM) h)->csLock);
	{
		wSREQ = *(WORD *) &((PHPILMEM) h)->psData->byIOMem[8];
	}
	LeaveCriticalSection(&((PHPILMEM) h)->csLock);

	// I/O CPU SRQ on HP-71 bus, I/O CPU Message Available or SRQ received from loop
	return ((wSREQ >> 2) | (wSREQ << 1) | (wSREQ >> 8)) & 0x02;
}

//
// write memory mapped I/O
//
static VOID WriteIOHpilMem(HANDLE h,BYTE *a,DWORD d,LPDWORD ps)
{
	CONST BYTE byWrMask[] =					// mask bit set = write enabled
	{
		0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0x9,
		0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
	};

	UINT i;

	_ASSERT(ARRAYSIZEOF(byWrMask) == ARRAYSIZEOF(((PHPILDATA)0)->byIOMem));
	_ASSERT(h != NULL);

	// address inside module area
	_ASSERT((d & ~(ARRAYSIZEOF(((PHPILDATA)0)->byIOMem) - 1)) == ((PHPILMEM) h)->psCfg[0].dwBase);
	d &= ARRAYSIZEOF(((PHPILDATA)0)->byIOMem) - 1;

	// nibbles to write limited to IO buffer boundary
	*ps = MIN(*ps,ARRAYSIZEOF(((PHPILDATA)0)->byIOMem) - d);

	#if defined DEBUG_ILIO
	{
		TCHAR buffer[256];
		DWORD j;
		int   i;

		i = wsprintf(buffer,_T("%.5lx: ILIO write : %02x ,%u = "),Chipset.pc,d,*ps);
		for (j = 0; j < *ps; ++j,++i)
		{
			buffer[i] = a[j];
			if (buffer[i] > 9) buffer[i] += _T('a') - _T('9') - 1;
			buffer[i] += _T('0');
		}
		buffer[i++] = _T('\n');
		buffer[i] = 0;
		OutputDebugString(buffer);
	}
	#endif

	EnterCriticalSection(&((PHPILMEM) h)->csLock);
	{
		for (i = 0; i < *ps; ++i, ++d)
		{
			_ASSERT(d < ARRAYSIZEOF(byWrMask));

			// write data with write mask to protect the bits controlled by the I/O processor
			((PHPILMEM) h)->psData->byIOMem[d] = (((PHPILMEM) h)->psData->byIOMem[d] & ~byWrMask[d])
											   | (a[i]                               &  byWrMask[d]);
		}
	}
	LeaveCriticalSection(&((PHPILMEM) h)->csLock);

	_ASSERT(((PHPILMEM) h)->hIoEvent != NULL);
	SetEvent(((PHPILMEM) h)->hIoEvent);		// wake up I/O processor thread
	return;
}

//
// read memory mapped I/O
//
static VOID ReadIOHpilMem(HANDLE h,BYTE *a,DWORD d,LPDWORD ps,BOOL bUpdate)
{
	UINT i;

	_ASSERT(h != NULL);

	_ASSERT((d & ~(ARRAYSIZEOF(((PHPILDATA)0)->byIOMem) - 1)) == ((PHPILMEM) h)->psCfg[0].dwBase);
	d &= ARRAYSIZEOF(((PHPILDATA)0)->byIOMem) - 1;		// address inside module area

	// nibbles to read limited by the IO buffer boundary
	*ps = MIN(*ps,ARRAYSIZEOF(((PHPILDATA)0)->byIOMem) - d);

	if (((PHPILMEM) h)->hIoThread != NULL	// I/O processor running
		&& d <= 0x8 && (d + *ps) > 0x8)		// reading I/O Low handshake nibble
	{
		_ASSERT(((PHPILMEM) h)->hIoEvent  != NULL);
		_ASSERT(((PHPILMEM) h)->hAckEvent != NULL);

		// answer event may not be cleared from writing, so clear it manually
		ResetEvent(((PHPILMEM) h)->hAckEvent);

		// wake up I/O processor thread for update data
		SetEvent(((PHPILMEM) h)->hIoEvent);
		// wait for I/O processor answer
		VERIFY(WaitForSingleObject(((PHPILMEM) h)->hAckEvent,INFINITE) == WAIT_OBJECT_0);
	}

	EnterCriticalSection(&((PHPILMEM) h)->csLock);
	{
		for (i = 0; i < *ps; ++i, ++d)
		{
			switch (d)
			{
			case 0x8: // I/O CPU Low Handshake Nibble
				a[i] = (((PHPILMEM) h)->psData->byIOMem[d] & ~0x4) | ((PHPILMEM) h)->psData->bySleep;
				break;

			case 0xe: // I/O CPU High Byte of Message
			case 0xf:
				if (bUpdate)
				{
					// clear I/O CPU Message Available bit
					// in the I/O CPU Low Handshake Nibble
					((PHPILMEM) h)->psData->byIOMem[0x8] &= ~0x1;
				}
				// no break

			default:
				a[i] = ((PHPILMEM) h)->psData->byIOMem[d];
			}
		}

		#if defined DEBUG_ILIO
		{
			TCHAR buffer[256];
			DWORD j;
			int   i;

			i = wsprintf(buffer,_T("%.5lx: ILIO read : %02x ,%u = "),Chipset.pc,d-*ps,*ps);
			for (j = 0; j < *ps; ++j,++i)
			{
				buffer[i] = a[j];
				if (buffer[i] > 9) buffer[i] += _T('a') - _T('9') - 1;
				buffer[i] += _T('0');
			}
			buffer[i++] = _T('\n');
			buffer[i] = 0;
			OutputDebugString(buffer);
		}
		#endif
	}
	LeaveCriticalSection(&((PHPILMEM) h)->csLock);
	return;
}

//
// detach HPIL I/O memory from address handling
//
static VOID DetachHpilMem(PPORTACC *ppsPort)
{
	PPORTACC pAct = *ppsPort;				// addr of allocated memory
	*ppsPort = (*ppsPort)->pNext;			// detach the memory
	DestroyHpilThread((PHPILMEM) pAct->h);	// close I/O processor thread
	free(pAct->h);							// free the module instance
	free(pAct);								// free the interface memory
	return;
}

//
// attach HPIL I/O memory to address handling
//
BOOL AttachHpilMem(PPORTACC *ppsPort,HANDLE hMemModule)
{
	_ASSERT(ppsPort != NULL);

	// walk through modules to find a free slot
	while (*ppsPort != NULL) ppsPort = &(*ppsPort)->pNext;

	if (hMemModule != NULL)
	{
		// allocate memory for the port interface
		if ((*ppsPort = AllocPortMem()) != NULL)
		{
			(*ppsPort)->h           = hMemModule;		// module instance
			(*ppsPort)->pfnIsModule = IsModuleHpilMem;	// check if address belongs to my module
			(*ppsPort)->pfnConfig   = ConfigHpilMem;	// configure module (CONFIG)
			(*ppsPort)->pfnUncnfg   = UncnfgHpilMem;	// unconfigure module (UNCNFG)
			(*ppsPort)->pfnReset    = ResetHpilMem;		// reset module (RESET)
			(*ppsPort)->pfnC_Eq_Id  = C_Eq_IdHpilMem;	// fetch ID (C=ID)
			(*ppsPort)->pfnSREQ     = SReqMem;			// service request of module
			(*ppsPort)->pfnWriteIO  = WriteIOHpilMem;	// write IO memory
			(*ppsPort)->pfnReadIO   = ReadIOHpilMem;	// read IO memory

			(*ppsPort)->pfnDetachMem = DetachHpilMem;	// detach module

			// create I/O processor thread
			CreateHpilThread((PHPILMEM) hMemModule);
		}
		else
		{
			free(hMemModule);				// free the module instance
		}
	}
	return *ppsPort != NULL;
}
