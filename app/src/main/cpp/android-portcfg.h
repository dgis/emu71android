// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

#define IRAMSIG		0xEDDDDD3B				// independent RAM signature (byte reverse order)

typedef struct _PORTCFG
{
    UINT	nIndex;							// logical no. in port queue
    BOOL	bApply;							// module setting applied
    UINT	nType;							// MODULETYPE
    DWORD	dwBase;							// base address of module
    DWORD	dwSize;							// size of hybrid chip in nibbles
    DWORD	dwChips;						// internal no. of chips
    LPBYTE	pbyData;						// pointer to original data
    TCHAR	szFileName[MAX_PATH];			// filename for ROM
    LPSTR	lpszAddrOut;					// tcp/ip address of HPIL target server
    WORD	wPortOut;						// tcp/ip port of HPIL target server
    WORD	wPortIn;						// tcp/ip port of my HPIL receive server
    PPORTTCPIP psTcp;						// tcp/ip settings of original HPIL module
    struct _PORTCFG *pNext;					// next module in same queue
} PORTCFG, *PPORTCFG;

BOOL bChanged[];

VOID LoadCurrPortConfig(VOID);
