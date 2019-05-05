/*
 *   tcpip.h
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gieﬂelink
 *
 */

// TCP/IP
typedef struct
{
	LPCSTR lpszAddrOut;						// tcp/ip address of target server
	WORD   wPortOut;						// tcp/ip port of target server
	WORD   wPortIn;							// tcp/ip port of my receive server

	WORD   wFrameRx;						// received frame from server
	HANDLE hSvrEvent;						// inform an tcp/ip server event
	HANDLE hSvrAckEvent;					// acknowledge the tcp/ip server event

	BOOL   bLoopClosed;						// interface loop is closed

	BOOL   bRealDevices;					// real devices connected with Pilbox
	DWORD  dwLoopTimeout;					// standard timeout for virtual devices connected over tpc/ip

	BOOL   bOriginClient;					// frame initiated by client

	BOOL   bRunning;
	HANDLE hWorkerThread;

	#define NFDS 2
	SOCKET sockfds[NFDS];
	INT    nNumSocks;
	SOCKET cfd;
	SOCKET sClient;
} TCPIP, *PTCPIP;

// tcpip.c
extern VOID TcpInit(PTCPIP p);
extern BOOL TcpCreateSvr(PTCPIP p);
extern VOID TcpCloseSvr(PTCPIP p);
extern WORD HpilController(PTCPIP p, WORD wFrame);
extern VOID HpilDevice(PTCPIP p, WORD wFrame);
