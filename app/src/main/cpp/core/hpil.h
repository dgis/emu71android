/*
 *   hpil.h
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gieﬂelink
 *
 */
#include "tcpip.h"

// state conditions of the IL
enum IL_STATE { MBIDLE = 0, MBRESET, ACT_LISTEN, END_LISTEN };

// HPIL I/O processor data
typedef struct
{
	BYTE	byIOMem[16];					// mailbox I/O memory

	WORD	wVersion;						// version of the HP-IL data structure

	// I/O processor state data
	BYTE	byIoStatus[6];					// STLC BPUI 0001 KRXV NNNN NNNN

	// RAM (272 byte = 256 byte page 0 + 6 byte status)
	BYTE	byRAM[256];						// I/O processor RAM page 0

											// 12       state of parallel poll enable bits: x0yysbbb
											//           x:     1 = enable
											//           yy:    00 = no SRQ      (SST bit 6 clear or ack by reading SST)
											//                  11 = SRQ pending (SST bit 6 set)
											//           sbbb:  PPE argument s = sense, bbb = bit

											// 35       remote status bit 6, 5
											//                            0  0 = local mode
											//                            0  1 = remote enabled
											//                            1  1 = remote mode

											// 3D       NRD-INTR-VALUE (init 00)

											// 3F       interrupt mask
											// 40       interrupt cause

											// 41       DDC content, lower 8 bit of DDL or DDT frame

											// 42       length status
											// 43 - 44  status

											// 45       length device ID
											// 46 - 4D  device ID

											// 4E       length accessory ID
											// 4F       accessory ID

											// 51       state of parallel poll enable (copy of addr 12)

											// 64       IDY SRQ poll timeout (init FF)

											// 66       terminator mode 00 = off
											//                          42 = terminate on END frame
											//                          44 = terminate on character mode
											//                          46 = terminate on END frame or character mode

											// 74       input  buffer size (init 65)
											// 75       output buffer size (init 66)

											// 76       input buffer input  pointer
											// 77       input buffer output pointer

											// 78       input buffer space (init 65)
											// 79       dividing address (init 190)

											// 7A       output buffer input  pointer
											// 7B       output buffer output pointer

											// 7D - BD  input buffer  (BD - 7D + 1 = 65)
											// BE - FF  output buffer (FF - BE + 1 = 66)
											// input buffer count = buffer size - buffer space

	BYTE	bySleep;						// 0 = I/O CPU awake, 4 = I/O CPU sleep
	BYTE	byPowered;						// 0 = loop powered down, 1 = loop powered up
	BYTE	byAAD;							// last AAD address
	BYTE	byAEP;							// last AEP address
	BYTE	byAES;							// last AES address
	enum IL_STATE eState;					// state of the loop
	DWORD	dwMaxCount;						// max count during transfer
	DWORD	dwCount;						// actual count during transfer
	DWORD	dwFrameCount;					// no. of frames to send from input buffer to the HP-71
	WORD	wLastframe;						// last frame
	INT		nBuffer[4];						// last 3 received frames + possible termination frame
	BYTE	byWrBufPos;						// write position to last received frame buffer
	BYTE	byRdBufPos;						// read  position to last received frame buffer
	BYTE	byEndmode;						// transfer end mode
	BYTE	byCharmode;						// terminator character mode
	BYTE	byEndchar;						// transfer terminator character
	BYTE	byManual;						// 0 = auto, 1 = manual, 2 = scope
	BYTE	byMsgCount;						// no. of messages, 0 = one message
	BYTE	byCstate;						// HP-IL controller state machine flags:
	// bit 2, bit 1:
	// 0      0    idle
	// 0      1    addressed listen
	// 1      0    addressed talker
	WORD	wCurrentAddr;					// current HPIL address for transfer (SSSSSPPPPP)
											// S = secondary address + 1, P = primary address
	DWORD	dwFrameTimeout;					// frame timeout
	BYTE	byAbort;						// abort operation because of fatal error
	BYTE	byPassControl;					// request to give up control

	// device data
	BYTE	byDEFADDR;						// default address after AAU
	BYTE	byAddr;							// HP-IL primary address (addressed by TAD or LAD)
											// bits 0-5 = AAD or AEP, bit 7 = 1 means auto address taken
	BYTE	byAddr2nd;						// HP-IL secondary address (addressed by SAD)
											// bits 0-5 = AES, bit 7 = 1 means auto address taken
	BYTE	byFstate;						// HP-IL state machine flags:
	// bit 7, bit 6, bit 5, bit 4:
	// 0      0      0      0    idle
	// 0      0      1      0    addressed listen in secondary address mode
	// 0      0      0      1    addressed talker in secondary address mode
	// 1      0      0      0    addressed listen
	// 0      1      0      0    addressed talker
	// bit 0 or bit 1 set        active talker
	// bit 1: SDA, SDI
	// bit 0: SST, SDI, SAI, SDA, NRD
	BYTE	byPtOff;						// RAM data offset for multibyte handling
	BYTE	byPtSxx;						// pointer for multibyte handling
	WORD	wLastDataFrame;					// last input data frame
	WORD	wTalkerFrame;					// last talker frame
	BYTE    bySrqEn;						// SRQ bit enabled
} HPILDATA, *PHPILDATA;

// HPIL I/O data for module handling
typedef struct
{
	UINT	nType;							// module type
	DWORD	dwSize;							// size I/O area in nibbles
	PSATCFG	psCfg;							// configuration data

	PHPILDATA psData;						// pointer to the I/O RAM data

	// internal global variables
	CRITICAL_SECTION csLock;				// critical section for mailbox access
	CRITICAL_SECTION csBuffer;				// critical section for buffer access
	CRITICAL_SECTION csStatus;				// critical section for IO status access

	BOOL	bIoRunning;						// I/O thread running
	HANDLE	hIoThread;						// I/O thread ID

	HANDLE	hAckEvent;						// waiting for an data event
	HANDLE	hIoEvent;						// waiting for an I/O event

	HANDLE	hInSetEvent;					// buffer handling with timeout
	HANDLE	hInGetEvent;
	HANDLE	hOutSetEvent;
	HANDLE	hOutGetEvent;

	TCPIP	sTcp;							// the TCP/IP communication data

	BOOL	bEnableRFC;						// enable the RFC behind a CMD frame
} HPILMEM, *PHPILMEM;

// hpilbuf.c
extern BOOL   InSetData(PHPILMEM pMem,BYTE byData);
extern BOOL   InGetData(PHPILMEM pMem,LPBYTE pbyData);
extern VOID   InClear(PHPILMEM pMem);
extern BYTE   InCountData(PHPILMEM pMem);
extern BOOL   InFullData(PHPILMEM pMem);
extern BOOL   OutSetData(PHPILMEM pMem,BYTE byData);
extern BOOL   OutSetFrame(PHPILMEM pMem,WORD wFrame);
extern BOOL   OutGetData(PHPILMEM pMem,LPWORD pwFrame);
extern VOID   OutClear(PHPILMEM pMem);
extern BYTE   OutCountData(PHPILMEM pMem);
extern BOOL   OutFullData(PHPILMEM pMem);

// hpilcmd.c
extern WORD   TransmitFrame(PHPILMEM pMem,WORD wFrame);
extern VOID   ilmailbox(PHPILMEM pMem);

// hpildev.c
extern VOID   DeviceInit(PHPILDATA p);
extern WORD   DeviceFrame(PHPILMEM pMem, WORD wFrame);

// hpil.c
extern BOOL   bEnableRFC;
extern BOOL   bHpilRealDevices;
extern DWORD  dwHpilLoopTimeout;
extern VOID   ResetHpilData(PHPILDATA p);
extern HANDLE AllocHpilMem(UINT nType,LPDWORD pdwSize,LPBYTE *ppbyMem,PSATCFG psCfg,LPCSTR pszAddrOut,WORD wPortOut,WORD wPortIn);
extern BOOL   AttachHpilMem(PPORTACC *ppsPort,HANDLE hMemModule);
