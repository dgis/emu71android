/*
 *   tcpip.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "hpil.h"

// #define DEBUG_TCPIP

#if _MSC_VER >= 1400						// using VS2005 platform or later
	#define IPv6_DUAL						// use IPv4 / IPv6 dual stack
#endif

#if defined IPv6_DUAL
	#include <ws2tcpip.h>
	#if _MSC_VER <= 1500					// using VS2008 platform or earlier
		#include <wspiapi.h>				// use getaddrinfo() wrapper for Win2k compatibility
	#endif
	#if !defined IPV6_V6ONLY
		#define IPV6_V6ONLY		27			// Vista specific definition
	#endif
#endif

static BOOL TcpSendFrame(PTCPIP p, WORD wFrame)
{
	UINT uTry = 0;
	INT  nRet,nIndex,nActTransmitLength;
	BOOL bErr;

	wFrame = htons(wFrame);					// change frame to network (big-endian) byte order

	do
	{
		bErr = FALSE;

		if (p->sClient == SOCKET_ERROR)		// not connected so far
		{
			#if defined IPv6_DUAL
				// IPv4 / IPv6 implementation
				CHAR cPortOut[16];
				ADDRINFO sHints, *psAddrInfo, *psAI;

				// the port no. as ASCII string
				sprintf_s(cPortOut,sizeof(cPortOut),"%u",p->wPortOut);

				memset(&sHints, 0, sizeof(sHints));
				sHints.ai_family = PF_UNSPEC;
				sHints.ai_socktype = SOCK_STREAM;

				if (getaddrinfo(p->lpszAddrOut,cPortOut,&sHints,&psAddrInfo) != 0)
				{
					return TRUE;			// server not found
				}

				//
				// For each address getaddrinfo returned, we create a new socket,
				// bind that address to it, and create a queue to listen on.
				//
				for (psAI = psAddrInfo; psAI != NULL; psAI = psAI->ai_next)
				{
					p->sClient = socket(psAI->ai_family,psAI->ai_socktype,psAI->ai_protocol);
					if (p->sClient == SOCKET_ERROR)
					{
						continue;
					}
					// disable the Nagle buffering
					{
						int flag = 1;
						VERIFY(setsockopt(p->sClient,IPPROTO_TCP,TCP_NODELAY,(char *) &flag,sizeof(flag)) == 0);
					}
					if (connect(p->sClient,psAI->ai_addr,(int) psAI->ai_addrlen) == SOCKET_ERROR)
					{
						closesocket(p->sClient);
						p->sClient = SOCKET_ERROR;
						continue;
					}
					break;
				}
				freeaddrinfo(psAddrInfo);
			#else
				// IPv4 implementation
				SOCKADDR_IN sServer;

				LPCSTR lpszIpAddr = p->lpszAddrOut;

				// not a valid ip address -> try to get ip address from name server
				if (inet_addr(lpszIpAddr) == INADDR_NONE)
				{
					struct hostent *host = NULL;
					struct in_addr sin_addr;

					host = gethostbyname(p->lpszAddrOut);
					if (host == NULL)
					{
						return TRUE;		// server not found
					}

					CopyMemory(&sin_addr, host->h_addr_list[0], host->h_length);
					lpszIpAddr = inet_ntoa(sin_addr);
				}

				// create TCPIP socket
				p->sClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
				if (p->sClient == INVALID_SOCKET)
				{
					// socket() failed
					p->sClient = SOCKET_ERROR;
					return TRUE;
				}

				// disable the Nagle buffering
				{
					int flag = 1;
					VERIFY(setsockopt(p->sClient,IPPROTO_TCP,TCP_NODELAY,(char *) &flag,sizeof(flag)) == 0);
				}

				sServer.sin_family = AF_INET;
				sServer.sin_port = htons(p->wPortOut);
				sServer.sin_addr.s_addr = inet_addr(lpszIpAddr);

				// connect
				if (connect(p->sClient,(LPSOCKADDR) &sServer, sizeof(sServer)) == SOCKET_ERROR)
				{
					// connect() failed
					closesocket(p->sClient);
					p->sClient = SOCKET_ERROR;
				}
			#endif

			if (p->sClient == SOCKET_ERROR)
			{
				return TRUE;
			}
		}

		nActTransmitLength = sizeof(wFrame); // actual no. of bytes to send
		nIndex = 0;							// reset index of send buffer

		while (nActTransmitLength > 0)
		{
			// transmit
			nRet = send(p->sClient, &((const char *) &wFrame)[nIndex], nActTransmitLength, 0);
			if (nRet == SOCKET_ERROR)
			{
				bErr = TRUE;				// send() failed
				++uTry;						// incr. for retry
				closesocket(p->sClient);	// try to make a new connect()
				p->sClient = SOCKET_ERROR;
				break;
			}

			nIndex += nRet;					// new transmit buffer position
			nActTransmitLength -= nRet;		// remainder data to send
		}
	}
	while (bErr && uTry <= 1);
	return bErr;
}

static UINT __stdcall ThreadTcpIpServer(PTCPIP p)
{
	fd_set SockSet;
	int i;

	_ASSERT(p->nNumSocks > 0);				// established server

	//
	// We now put the server into an external loop,
	// serving requests as they arrive.
	//
	FD_ZERO(&SockSet);
	while (p->bRunning)
	{
		p->cfd = INVALID_SOCKET;

		//
		// Check to see if we have any sockets remaining to be served
		// from previous time through this loop.  If not, call select()
		// to wait for a connection request or a datagram to arrive.
		//
		for (i = 0; i < p->nNumSocks; ++i)
		{
			if (FD_ISSET(p->sockfds[i], &SockSet))
				break;
		}
		if (i == p->nNumSocks)				// no socket waiting
		{
			for (i = 0; i < p->nNumSocks; ++i)
				FD_SET(p->sockfds[i], &SockSet);

			// select() can be finished by closesocket()
			if (select(p->nNumSocks, &SockSet, NULL, NULL, NULL) == SOCKET_ERROR)
			{
				// Win9x break with WSAEINTR (a blocking socket call was canceled)
				if (WSAEINTR != WSAGetLastError())
				{
					return 0;
				}
			}

			if (p->bRunning == FALSE)		// exit thread
			{
				return 0;
			}
		}
		for (i = 0; i < p->nNumSocks; ++i)
		{
			if (FD_ISSET(p->sockfds[i], &SockSet))
			{
				FD_CLR(p->sockfds[i], &SockSet);
				break;
			}
		}
		_ASSERT(i < p->nNumSocks);			// at least one socket is waiting
		if (i == p->nNumSocks) continue;	// no socket waiting

		//
		// Since this socket was returned by the select(), we know we
		// have a connection waiting and that this accept() won't block.
		//
		p->cfd = accept(p->sockfds[i], NULL, NULL);

		while (TRUE)
		{
			WORD wFrame;

			INT nIndex = 0;					// reset index of receive buffer
			while (nIndex < sizeof(wFrame))
			{
				INT nAmountRead = recv(p->cfd, &((char *) &wFrame)[nIndex], sizeof(wFrame) - nIndex, 0);
				if (   nAmountRead == 0		// client closed connection
					|| nAmountRead == SOCKET_ERROR)
				{
					closesocket(p->cfd);
					p->cfd = INVALID_SOCKET;
					break;
				}
				nIndex += nAmountRead;
			}

			if (p->cfd == INVALID_SOCKET) break;
			_ASSERT(nIndex == sizeof(wFrame));

			// transmit virtual HP-IL loop data to HPIL interface
			p->wFrameRx = ntohs(wFrame);	// change frame to host byte order

			if (p->bOriginClient)			// frame initiated by client
			{
				#if defined DEBUG_TCPIP
				{
					TCHAR buffer[256];
					wsprintf(buffer,_T("Virtual HP-IL loop data to HPIL interface : %02X\n"),p->wFrameRx);
					OutputDebugString(buffer);
				}
				#endif
				ResetEvent(p->hSvrAckEvent); // make sure that event not already set
				SetEvent(p->hSvrEvent);		// inform the caller
				// wait for data read acknowledge (on timeout there's no ack)
				WaitForSingleObject(p->hSvrAckEvent,500);
			}
			else							// frame initiated by server
			{
				HpilDevice(p,p->wFrameRx);
			}
		}
	}
	return 0;
}

static BOOL StartServer(PTCPIP p)
{
	#if defined IPv6_DUAL
		// IPv4 / IPv6 implementation
		CHAR cPortIn[16];
		ADDRINFO sHints, *psAddrInfo, *psAI;

		SOCKET fd = 0;

		p->nNumSocks = 0;

		// the port no. as ASCII string
		sprintf_s(cPortIn,sizeof(cPortIn),"%u",p->wPortIn);

		memset(&sHints, 0, sizeof(sHints));
		sHints.ai_family = PF_UNSPEC;
		sHints.ai_socktype = SOCK_STREAM;
		sHints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;

		if (getaddrinfo(NULL,cPortIn,&sHints,&psAddrInfo) != 0)
		{
			return TRUE;
		}

		//
		// For each address getaddrinfo returned, we create a new socket,
		// bind that address to it, and create a queue to listen on.
		//
		for (p->nNumSocks = 0, psAI = psAddrInfo; psAI != NULL; psAI = psAI->ai_next)
		{
			if (p->nNumSocks == FD_SETSIZE)
			{
				break;
			}
			if ((psAI->ai_family != PF_INET) && (psAI->ai_family != PF_INET6))
			{
				// only PF_INET and PF_INET6 is supported
				continue;
			}
			fd = socket(psAI->ai_family,psAI->ai_socktype,psAI->ai_protocol);
			if (fd == INVALID_SOCKET)
			{
				// socket() failed
				continue;
			}
			if (psAI->ai_family == AF_INET6)
			{
				int ipv6only,optlen;

				if (   IN6_IS_ADDR_LINKLOCAL((PIN6_ADDR) &((PSOCKADDR_IN6) (psAI->ai_addr))->sin6_addr)
					&& (((PSOCKADDR_IN6) (psAI->ai_addr))->sin6_scope_id == 0)
				   )
				{
					// IPv6 link local addresses should specify a scope ID
					closesocket(fd);
					continue;
				}

				// this socket option is supported on Windows Vista or later
				optlen = sizeof(ipv6only);
				if (getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only, &optlen) == 0 && ipv6only == 0)
				{
					ipv6only = 1;			// set option

					// on Windows XP IPV6_V6ONLY is always set, on Vista and later set it manually
					if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only, sizeof(ipv6only)) == SOCKET_ERROR)
					{
						closesocket(fd);
						continue;
					}
				}
			}
			if (bind(fd,psAI->ai_addr,(int) psAI->ai_addrlen))
			{
				// bind() failed
				closesocket(fd);
				continue;
			}
			if (listen(fd,5) == SOCKET_ERROR)
			{
				// listen() failed
				closesocket(fd);
				continue;
			}
			p->sockfds[p->nNumSocks++] = fd;
			if (p->nNumSocks >= NFDS)
				break;
		}
		freeaddrinfo(psAddrInfo);

		if (p->nNumSocks == 0)				// socket not connected
		{
			return TRUE;
		}
	#else
		// IPv4 implementation
		struct sockaddr_in sServer;

		p->nNumSocks = 0;

		p->sockfds[p->nNumSocks] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (p->sockfds[p->nNumSocks] == INVALID_SOCKET)
		{
			return TRUE;					// socket() failed
		}

		sServer.sin_family = AF_INET;
		sServer.sin_addr.s_addr = INADDR_ANY;
		sServer.sin_port = htons(p->wPortIn);

		if (bind(p->sockfds[p->nNumSocks], (LPSOCKADDR) &sServer, sizeof(sServer)) == SOCKET_ERROR)
		{
			// bind() failed
			closesocket(p->sockfds[p->nNumSocks]);
			return TRUE;
		}
		if (listen(p->sockfds[p->nNumSocks], 5) == SOCKET_ERROR)
		{
			// listen() failed
			closesocket(p->sockfds[p->nNumSocks]);
			return TRUE;
		}

		++p->nNumSocks;						// only 1 socket
	#endif
	return FALSE;
}

static VOID StopServer(PTCPIP p)
{
	INT i;

	if (p->sClient != SOCKET_ERROR)			// client open
	{
		closesocket(p->sClient);			// close client to finisch server connction
		p->sClient = SOCKET_ERROR;
	}

	// delete server thread
	p->bRunning = FALSE;

	if (p->cfd != INVALID_SOCKET)
	{
		closesocket(p->cfd);
		p->cfd = INVALID_SOCKET;
	}

	for (i = 0; i < p->nNumSocks; ++i)
		closesocket(p->sockfds[i]);			// terminate select() for thread termination
	p->nNumSocks = 0;
	return;
}

VOID TcpInit(PTCPIP p)
{
	p->bLoopClosed = FALSE;					// interface loop isn't closed
	p->bOriginClient = FALSE;				// frame initiated by server

	p->bRealDevices = FALSE;				// no real IL hardware connected over Pilbox
	p->dwLoopTimeout = 500;					// standard timeout for virtual devices in ms

	p->hWorkerThread = NULL;

	p->nNumSocks = 0;
	p->cfd = INVALID_SOCKET;

	p->sClient = SOCKET_ERROR;				// client socket
	return;
}

BOOL TcpCreateSvr(PTCPIP p)
{
	WSADATA wsa;

	BOOL bErr = TRUE;

	VERIFY(WSAStartup(MAKEWORD(2,2),&wsa) == 0);

	// event for server data available
	VERIFY(p->hSvrEvent = CreateEvent(NULL,FALSE,FALSE,NULL));

	// event for data read acknowledge
	VERIFY(p->hSvrAckEvent = CreateEvent(NULL,FALSE,FALSE,NULL));

	if (StartServer(p) == FALSE)			// init server
	{
		// create server thread
		DWORD dwThreadID;

		p->bRunning = TRUE;
		p->hWorkerThread = CreateThread(NULL,
										0,
										(LPTHREAD_START_ROUTINE) ThreadTcpIpServer,
										(LPVOID) p,
										0,
										&dwThreadID);
		_ASSERT(p->hWorkerThread);
		bErr = (p->hWorkerThread == NULL);
	}
	return bErr;
}

VOID TcpCloseSvr(PTCPIP p)
{
	StopServer(p);							// delete server thread

	if (p->hWorkerThread)					// server running
	{
		WaitForSingleObject(p->hWorkerThread,INFINITE);
		CloseHandle(p->hWorkerThread);
		p->hWorkerThread = NULL;
	}
	if (p->hSvrEvent)
	{
		CloseHandle(p->hSvrEvent);			// close server data available event
		p->hSvrEvent = NULL;
	}
	if (p->hSvrAckEvent)
	{
		CloseHandle(p->hSvrAckEvent);		// close data read acknowledge event
		p->hSvrAckEvent = NULL;
	}

	WSACleanup();							// cleanup network stack
	return;
}

//
// send IL frame as controller over the loop
//
WORD HpilController(PTCPIP p, WORD wFrame)
{
	p->bLoopClosed = FALSE;					// interface loop isn't closed
	ResetEvent(p->hSvrEvent);				// make sure that event not already set

	p->bOriginClient = TRUE;				// frame initiated by client

	if (TcpSendFrame(p,wFrame) == FALSE)	// data send
	{
		DWORD dwTimeout = p->dwLoopTimeout;	// standard timeout for tcp/ip devices

		if (p->bRealDevices)				// real IL hardware connected over Pilbox
		{
			// wait for returned frame
			// timeouts: 10s for DOE/RDY, 3s for CMD, 1s for IDY
			do
			{
				if (wFrame < 0x400)			// DOE
				{
					dwTimeout = 10000;
					break;
				}
				if ((wFrame & 0x7F4) == 0x494)	// special PILBox frames 494-497 and 49c-49f
				{
					dwTimeout = 500;
					break;
				}
				if (wFrame < 0x500)			// CMD
				{
					dwTimeout = 3000;
					break;
				}
				if (wFrame < 0x600)			// RDY
				{
					dwTimeout = 10000;
					break;
				}
				dwTimeout = 1000;			// IDY
			}
			while (FALSE);
		}

		// wait for finishing the virtual IL
		if (WaitForSingleObject(p->hSvrEvent,dwTimeout) == WAIT_OBJECT_0)
		{
			wFrame = p->wFrameRx;			// the answer from the virtual IL
			#if defined DEBUG_TCPIP
			{
				TCHAR buffer[256];
				wsprintf(buffer,_T("Fetched Virtual HP-IL loop data to HPIL interface : %02X\n"),wFrame);
				OutputDebugString(buffer);
			}
			#endif
			SetEvent(p->hSvrAckEvent);		// acknowledge the tcp/ip server event
			p->bLoopClosed = TRUE;			// interface loop is closed
		}
		p->bOriginClient = FALSE;			// frame initiated by server
	}
	return wFrame;
}

//
// receive IL frame as device, eval it, and send it to the next device
//
VOID HpilDevice(PTCPIP p, WORD wFrame)
{
	// calculate the PHPILMEM pointer
	PHPILMEM pMem = (PHPILMEM) ((LPBYTE) (p) - offsetof(HPILMEM,sTcp));

	p->bLoopClosed = TRUE;					// assume interface loop is closed
	wFrame = DeviceFrame(pMem,wFrame);		// handle frame as 71B in device mode
	if (p->bLoopClosed)						// frame handled properly
	{
		TcpSendFrame(p,wFrame);				// send to next device
	}
	return;
}
