/*
 *   mops.c
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
#include "satmem.h"
#include "hpil.h"

// #define DEBUG_IO							// switch for I/O debug purpose
// #define DEBUG_MEMACC						// switch for MEMORY access debug purpose

// defines for reading an open data bus
#define READEVEN	0x0A
#define READODD		0x03

// size in nibbles of the 4 internal 1LG8 memory chips inside the hybrid chip
#define ISIZE_1LG8	(ARRAYSIZEOF(Chipset.ir[0].Ram) / ARRAYSIZEOF(Chipset.ir[0].sCfg))

// values for memory mapping area
enum MMUMAP
{
	M_NONE = 0,
	M_ROM,
	M_RAM,
	M_IO_SLAVE1,
	M_IO_SLAVE2,
	M_IO_MASTER,
	M_RAM_SLAVE1,
	M_RAM_SLAVE2,
	M_RAM_MASTER,
	M_PORT
};

BOOL bLowBatDisable = FALSE;

static PPORTACC psExtPorts[NOEXTPORTS];		// the external port interface

// function prototypes
static VOID ReadIO(enum CHIP eChip, BYTE *a, DWORD b, DWORD s, BOOL bUpdate);
static VOID WriteIO(enum CHIP eChip, BYTE *a, DWORD b, DWORD s);

static BOOL NextUncfg(UINT *pnChip4k, UINT *pnChip)
{
	// 4KB hybrid chip
	for (*pnChip4k = 0; *pnChip4k < ARRAYSIZEOF(Chipset.ir); ++(*pnChip4k))
	{
		// 1LG8 1KB chip
		for (*pnChip = 0; *pnChip < ARRAYSIZEOF(Chipset.ir[0].sCfg); ++(*pnChip))
		{
			// check if module unconfigured
			if (Chipset.ir[*pnChip4k].sCfg[*pnChip].bCfg == FALSE)
			{
				return TRUE;				// unconfigured module found
			}
		}
	}
	return FALSE;							// all modules configured
}


// port mapping
LPBYTE RMap[1<<PAGE_BITS] = {NULL,};		// read page
LPBYTE WMap[1<<PAGE_BITS] = {NULL,};		// write page
BOOL   HMap[1<<PAGE_BITS] = {FALSE,};		// hard configured chips

static __inline VOID MapDDC(UINT c, DWORD a, DWORD b) // display driver chip 1LF3
{
	UINT  i;
	DWORD s,e,p,m;

	// base address of hard configured chips
	CONST DWORD dwIOBase[]  = { 0x2E100, 0x2E200, 0x2E300 };
	CONST DWORD dwRamBase[] = { 0x2F400, 0x2F800, 0x2FC00 };

	_ASSERT(0 == SLAVE1);
	_ASSERT(1 == SLAVE2);
	_ASSERT(2 == MASTER);					// check if last chip is master

	_ASSERT(c >= 0);
	_ASSERT(c < ARRAYSIZEOF(Chipset.dd));
	_ASSERT(c < ARRAYSIZEOF(dwIOBase));
	_ASSERT(c < ARRAYSIZEOF(dwRamBase));

	// IORAM
	s = MAX(a, (dwIOBase[c] >> ADDR_BITS) & MAPMASK(ARRAYSIZEOF(Chipset.dd[0].IORam)));
	e = MIN(b,((dwIOBase[c] >> ADDR_BITS) & MAPMASK(ARRAYSIZEOF(Chipset.dd[0].IORam))) + MAPSIZE(ARRAYSIZEOF(Chipset.dd[0].IORam)));
	m = ((MAPSIZE(ARRAYSIZEOF(Chipset.dd[0].IORam)) + 1) << ADDR_BITS) - 1;
	p = (s << ADDR_BITS) & m;				// offset to begin in nibbles
	for (i=s; i<=e; ++i)
	{
		RMap[i] = Chipset.dd[c].IORam + p;
		WMap[i] = Chipset.dd[c].IORam + p;
		HMap[i] = TRUE;						// hard configured chip
		p = (p + ADDR_SIZE) & m;
	}

	// user RAM
	s = MAX(a, (dwRamBase[c] >> ADDR_BITS) & MAPMASK(ARRAYSIZEOF(Chipset.dd[0].Ram)));
	e = MIN(b,((dwRamBase[c] >> ADDR_BITS) & MAPMASK(ARRAYSIZEOF(Chipset.dd[0].Ram))) + MAPSIZE(ARRAYSIZEOF(Chipset.dd[0].Ram)));
	m = ((MAPSIZE(ARRAYSIZEOF(Chipset.dd[0].Ram)) + 1) << ADDR_BITS) - 1;
	p = (s << ADDR_BITS) & m;				// offset to begin in nibbles
	for (i=s; i<=e; ++i)
	{
		RMap[i] = Chipset.dd[c].Ram + p;
		WMap[i] = Chipset.dd[c].Ram + p;
		HMap[i] = TRUE;						// hard configured chip
		p = (p + ADDR_SIZE) & m;
	}
	return;
}

static __inline VOID MapIRam(UINT c, DWORD a, DWORD b) // internal RAM 1LG8
{
	LPBYTE pBase;
	UINT   i,h;
	DWORD  s,e,p,m;

	_ASSERT(c >= 0 && c < ARRAYSIZEOF(Chipset.ir));

	// base of memory
	pBase = Chipset.ir[c].Ram;

	// scan each internal chip in the hybrid chip
	for (h = 0; h < ARRAYSIZEOF(Chipset.ir[0].sCfg); ++h)
	{
		if ((Chipset.ir[c].sCfg[h].bCfg))	// internal chip is configured
		{
			s = MAX(a, (Chipset.ir[c].sCfg[h].dwBase >> ADDR_BITS) & MAPMASK(ISIZE_1LG8));
			e = MIN(b,((Chipset.ir[c].sCfg[h].dwBase >> ADDR_BITS) & MAPMASK(ISIZE_1LG8)) + MAPSIZE(ISIZE_1LG8));
			m = ((MAPSIZE(ISIZE_1LG8) + 1) << ADDR_BITS) - 1;
			p = (s << ADDR_BITS) & m;		// offset to begin in nibbles
			for (i=s; i<=e; ++i)
			{
				if (RMap[i] == NULL)
				{
					RMap[i] = pBase + p;
					WMap[i] = pBase + p;
				}
				p = (p + ADDR_SIZE) & m;
			}
		}

		pBase += ISIZE_1LG8;				// next internal RAM chip
	}
	return;
}

static __inline VOID MapROM(DWORD a, DWORD b)		// ROM 1LG7 (4 chips)
{
	UINT i;
	DWORD p, m;

	if (dwRomSize && Chipset.bOD == FALSE)	// ROM loaded and enabled
	{
		m = dwRomSize - 1;					// ROM address mask for mirroring
		b = MIN(b,m >> ADDR_BITS);
		p = (a << ADDR_BITS) & m;
		for (i=a; i<=b; ++i)				// scan each 1K nibble page
		{
			RMap[i] = pbyRom + p;			// save page address for read
			WMap[i] = NULL;					// no writing
			HMap[i] = TRUE;					// hard configured chip
			p = (p + ADDR_SIZE) & m;
		}
	}
	return;
}

VOID Map(DWORD a, DWORD b)					// map pages
{
	UINT i;

	for (i=a; i<=b; ++i)					// clear area
	{
		RMap[i] = NULL;
		WMap[i] = NULL;
		HMap[i] = FALSE;
	}

	MapROM(a,b);							// ROM (hard configured)

	for (i = 0; i < ARRAYSIZEOF(Chipset.dd); ++i)
	{
		MapDDC(i,a,b);						// display driver chips (hard configured)
	}

	for (i = 0; i < ARRAYSIZEOF(Chipset.ir); ++i)
	{
		MapIRam(i,a,b);						// internal RAM chips (soft configured)
	}

	// scan through attached ports
	for (i = 0; i < ARRAYSIZEOF(psExtPorts); ++i)
	{
		// the root of the port
		PPORTACC psPort = psExtPorts[i];

		// walk through all chips attached to the port
		for (; psPort != NULL; psPort = psPort->pNext)
		{
			psPort->pfnMap(psPort->h,a,b);	// memory modules (soft configured)
		}
	}
	return;
}

VOID MountPorts(VOID)
{
	HANDLE h;
	UINT   i;

	BOOL bAttached = FALSE;					// nothing attached

	Chipset.bOD = FALSE;					// ROM not disabled

	// mount port interface
	for (i = 0; i < ARRAYSIZEOF(psExtPortData); ++i)
	{
		PPORTDATA psData = psExtPortData[i];

		// plugged modules in port
		while (psData != NULL)				// walk through all modules
		{
			switch (psData->sInfo.nType)
			{
			case TYPE_HRD:
				if (i == 1)					// Port1
				{
					// hard wired module in port 1 with base address 0 disables ROM
					Chipset.bOD = (psData->psCfg[0].dwBase == 0x00000);
				}
				// no break
			case TYPE_RAM:
			case TYPE_ROM: // generic RAM/ROM driver
				_ASSERT(i < ARRAYSIZEOF(psExtPorts));

				// create an instance of this RAM/ROM chip
				h = AllocSaturnMem(psData->sInfo.nType,
								   psData->sInfo.dwSize,
								   psData->sInfo.dwChips,
								   psData->sInfo.bHybrid,
								   psData->pbyData,
								   psData->psCfg);
				_ASSERT(h);
				AttachSaturnMem(&psExtPorts[i],h);
				bAttached = TRUE;			// something attached
				break;
			case TYPE_HPIL:
				_ASSERT(i < ARRAYSIZEOF(psExtPorts));
				_ASSERT(psData->sInfo.dwChips == 1);

				// create an instance of this HPIL IO memory
				h = AllocHpilMem(psData->sInfo.nType,
								 &psData->sInfo.dwSize,
								 &psData->pbyData,
								 psData->psCfg,
								 psData->psTcp->lpszAddrOut,
								 psData->psTcp->wPortOut,
								 psData->psTcp->wPortIn);
				_ASSERT(h);
				AttachHpilMem(&psExtPorts[i],h);
				bAttached = TRUE;			// something attached
				break;
			default: _ASSERT(FALSE);
			}

			psData = psData->pNext;
		}
	}

	if (bAttached)							// attached a port
	{
		Map(0x00,ARRAYSIZEOF(RMap)-1);		// update memory mapping
	}
	return;
}

VOID DismountPorts(VOID)
{
	UINT i;

	BOOL bDetached = FALSE;					// nothing detached

	// dismount port interface
	for (i = 0; i < ARRAYSIZEOF(psExtPorts); ++i)
	{
		// plugged modules in port
		while (psExtPorts[i] != NULL)		// walk through all modules
		{
			// detach and free memory of module
			psExtPorts[i]->pfnDetachMem(&psExtPorts[i]);
			bDetached = TRUE;				// something detached
		}
	}

	if (bDetached)							// detached a port
	{
		Map(0x00,ARRAYSIZEOF(RMap)-1);		// update memory mapping
	}
	return;
}

////////////////////////////////////////////////////////////////////////////////
//
// Bus Commands
//
////////////////////////////////////////////////////////////////////////////////

/*

Module ID of HPIL module

8 0 F 0 F
| | | | |
| | | | +- 16 nibbles
| | | +--- reserved
| | +----- Memory-mapped I/O
| +------- HP-IL mailbox
+--------- last chip in module

*/

DWORD IoModuleId(DWORD dwSize)				// module I/O size value (Nibble) must be a power of 2
{
	DWORD dwID = 0x80F0F;					// ID for Memory-mapped I/O with 16 nibbles

	// nibble 0: F = 16 nibbles
	//           E = 32
	//           D = 64
	//           C = 128
	//           B = 256
	//           A = 512
	//           9 = 1K
	//           8 = 2K
	//           7 = 4K
	//           6 = 8K
	//           5 = 16K

	// size must be a power of 2
	_ASSERT(dwSize > 0 && (dwSize & (dwSize - 1)) == 0);

	dwSize >>= (4 + 1);						// no. of 16 nibbles pages - 1
	while (dwSize)							// generate nibble 0
	{
		dwSize >>= 1;						// next bit
		--dwID;								// next ID
	}

	_ASSERT(dwID >= 0x80F05);				// ID for <= 8KB
	return dwID;
}

DWORD MemModuleId(DWORD dwSize)				// module size value (Nibble) must be a power of 2
{
	DWORD dwID = 0x8000F;					// ID for hybrid RAM with 1K nibble

	// nibble 0: F = 1K nibble
	//           E = 2
	//           D = 4
	//           C = 8
	//           B = 16
	//           A = 32
	//           9 = 64  (max RAM)
	//           8 = 128
	//           7 = 256 (max)

	// size must be a power of 2
	_ASSERT(dwSize > 0 && (dwSize & (dwSize - 1)) == 0);

	dwSize >>= (10 + 1);					// no. of 1K nibble pages - 1
	while (dwSize)							// generate nibble 0
	{
		dwSize >>= 1;						// next bit
		--dwID;								// next ID
	}

	_ASSERT(dwID >= 0x80007);				// ID for <= 128KB
	return dwID;
}

VOID Config(VOID)							// configure modules in fixed order
{
	UINT nChip4k;							// 4KB hybrid chip
	UINT nChip;								// 1LG8 1KB chip
	UINT i;

	if (NextUncfg(&nChip4k,&nChip))			// scan for an unconfigured module in main RAM
	{
		DWORD p = Npack(Chipset.C,5);		// config address

		Chipset.ir[nChip4k].sCfg[nChip].bCfg = TRUE;
		// adjust base to mapping boundary
		p &= (MAPMASK(ISIZE_1LG8) << ADDR_BITS);
		Chipset.ir[nChip4k].sCfg[nChip].dwBase = p;

		p >>= ADDR_BITS;					// page address
		Map(p,p+MAPSIZE(ISIZE_1LG8));
		return;
	}

	// scan through attached ports
	for (i = 0; i < ARRAYSIZEOF(psExtPorts); ++i)
	{
		// Daisy Chain In = high?
		// Port0, Port1 = OR0, Port2 = OR1, Port3 = OR2, Port4 = OR3, Port5 = OR4
		if (i == 0 || (Chipset.out & (1 << (i - 1))) != 0)
		{
			// the root of the port daisy chain
			PPORTACC psPort = psExtPorts[i];

			// walk through all chips in the daisy chain
			for (; psPort != NULL; psPort = psPort->pNext)
			{
				if (psPort->pfnConfig(psPort->h))
					return;					// configured a chip
			}
		}
	}
	return;
}

VOID Uncnfg(VOID)
{
	UINT nChip4k;							// 4KB hybrid chip
	UINT nChip;								// 1LG8 1KB chip
	UINT i;

	WORD p=(WORD)(Npack(Chipset.C,5)>>ADDR_BITS); // page address

	// search for 1LG8 chip to unconfigure
	for (nChip4k = 0; nChip4k < ARRAYSIZEOF(Chipset.ir); ++nChip4k)
	{
		for (nChip = 0; nChip < ARRAYSIZEOF(Chipset.ir[0].sCfg); ++nChip)
		{
			if (   Chipset.ir[nChip4k].sCfg[nChip].bCfg
				&& ((p & MAPMASK(ISIZE_1LG8)) == (Chipset.ir[nChip4k].sCfg[nChip].dwBase >> ADDR_BITS)))
			{
				Chipset.ir[nChip4k].sCfg[nChip].bCfg = FALSE;
				Map( (Chipset.ir[nChip4k].sCfg[nChip].dwBase >> ADDR_BITS),
					((Chipset.ir[nChip4k].sCfg[nChip].dwBase >> ADDR_BITS)+MAPSIZE(ISIZE_1LG8)));
			}
		}
	}

	// scan through attached ports
	for (i = 0; i < ARRAYSIZEOF(psExtPorts); ++i)
	{
		// the root of the port daisy chain
		PPORTACC psPort = psExtPorts[i];

		// walk through all chips in the daisy chain
		for (; psPort != NULL; psPort = psPort->pNext)
		{
			psPort->pfnUncnfg(psPort->h);
		}
	}
	return;
}

VOID Reset(VOID)
{
	UINT nChip4k;							// 4KB hybrid chip
	UINT nChip;								// 1LG8 1KB chip
	UINT i;

	// unconfigure all 1LG8 chips
	for (nChip4k = 0; nChip4k < ARRAYSIZEOF(Chipset.ir); ++nChip4k)
	{
		for (nChip = 0; nChip < ARRAYSIZEOF(Chipset.ir[0].sCfg); ++nChip)
		{
			Chipset.ir[nChip4k].sCfg[nChip].bCfg   = FALSE;
			Chipset.ir[nChip4k].sCfg[nChip].dwBase = 0x00000;
		}
	}

	// scan through attached ports
	for (i = 0; i < ARRAYSIZEOF(psExtPorts); ++i)
	{
		// the root of the port daisy chain
		PPORTACC psPort = psExtPorts[i];

		// walk through all chips in the daisy chain
		for (; psPort != NULL; psPort = psPort->pNext)
		{
			psPort->pfnReset(psPort->h);
		}
	}

	Map(0,ARRAYSIZEOF(RMap)-1);				// refresh mapping
	return;
}

VOID C_Eq_Id(VOID)
{
	UINT  nChip4k;							// 4KB hybrid chip
	UINT  nChip;							// 1LG8 1KB chip
	UINT  i;

	DWORD dwID = 0;							// all modules configured

	if (NextUncfg(&nChip4k,&nChip))			// scan for an unconfigured module in main RAM
	{
		// found one
		dwID = MemModuleId(ISIZE_1LG8);		// calculate ID from module size

		// not last RAM chip in 1LG8 hybrid module
		if (nChip < ARRAYSIZEOF(Chipset.ir[0].sCfg) - 1)
			dwID &= ~0x80000;				// not last chip
	}

	// scan through attached ports
	for (i = 0; dwID == 0 && i < ARRAYSIZEOF(psExtPorts); ++i)
	{
		// Daisy Chain In = high?
		// Port0, Port1 = OR0, Port2 = OR1, Port3 = OR2, Port4 = OR3, Port5 = OR4
		if (i == 0 || (Chipset.out & (1 << (i - 1))) != 0)
		{
			// the root of the port daisy chain
			PPORTACC psPort = psExtPorts[i];

			// walk through all chips in the daisy chain
			for (; dwID == 0 && psPort != NULL; psPort = psPort->pNext)
			{
				dwID = psPort->pfnC_Eq_Id(psPort->h);
			}
		}
	}

	Nunpack(Chipset.C,dwID,5);
	return;
}

//
// service request
//
BYTE SREQ(VOID)
{
	UINT i;

	BYTE bySREQ = Chipset.SREQ;

	// scan through attached ports
	for (i = 0; i < ARRAYSIZEOF(psExtPorts); ++i)
	{
		// the root of the port daisy chain
		PPORTACC psPort = psExtPorts[i];

		// walk through all chips in the daisy chain
		for (; psPort != NULL; psPort = psPort->pNext)
		{
			bySREQ |= psPort->pfnSREQ(psPort->h);
		}
	}
	return bySREQ;
}

VOID CpuReset(VOID)							// register setting after Cpu Reset
{
	StopTimers();							// stop timer

	Chipset.pc = 0;
	Chipset.rstkp = 0;
	ZeroMemory(Chipset.rstk,sizeof(Chipset.rstk));
	Chipset.HST = 0;
	Chipset.SoftInt = FALSE;
	Chipset.Shutdn = TRUE;
	Chipset.inte = TRUE;					// enable interrupts
	Chipset.intk = TRUE;					// INTON
	Chipset.intd = FALSE;					// no keyboard interrupts pending
	Reset();								// reset MMU
	ZeroMemory(Chipset.dd[0].IORam,sizeof(Chipset.dd[0].IORam));
	ZeroMemory(Chipset.dd[1].IORam,sizeof(Chipset.dd[1].IORam));
	ZeroMemory(Chipset.dd[2].IORam,sizeof(Chipset.dd[2].IORam));
	UpdateContrast();						// update contrast
	return;
}

static PPORTACC GetPortModule(DWORD d)
{
	PPORTACC psPort;
	UINT i;

	// scan through attached ports
	for (i = 0; i < ARRAYSIZEOF(psExtPorts); ++i)
	{
		psPort = psExtPorts[i];				// the root of the port

		// walk through all chips attached to the port
		for (; psPort != NULL; psPort = psPort->pNext)
		{
			if (psPort->pfnIsModule(psPort->h,d))
				return psPort;
		}
	}
	return NULL;							// not handled by port memory
}

static enum MMUMAP MapData(DWORD d,PPORTACC *ppsPort) // check MMU area
{
	UINT  nChip4k;							// 4KB hybrid chip
	UINT  nChip;							// 1LG8 1KB chip
	DWORD p;

	_ASSERT(ppsPort != NULL);
	*ppsPort = NULL;						// no port descriptor

	p = d >> ADDR_BITS;						// page address
	if (!Chipset.bOD && (p & ((~(dwRomSize-1)&0xFFFFF)>>ADDR_BITS)) == 0)          return M_ROM;
	if ((p & MAPMASK(ARRAYSIZEOF(Chipset.dd[0].IORam))) == (0x2E100 >> ADDR_BITS)) return M_IO_SLAVE1;
	if ((p & MAPMASK(ARRAYSIZEOF(Chipset.dd[1].IORam))) == (0x2E200 >> ADDR_BITS)) return M_IO_SLAVE2;
	if ((p & MAPMASK(ARRAYSIZEOF(Chipset.dd[2].IORam))) == (0x2E300 >> ADDR_BITS)) return M_IO_MASTER;
	if ((p & MAPMASK(ARRAYSIZEOF(Chipset.dd[0].Ram)))   == (0x2F400 >> ADDR_BITS)) return M_RAM_SLAVE1;
	if ((p & MAPMASK(ARRAYSIZEOF(Chipset.dd[1].Ram)))   == (0x2F800 >> ADDR_BITS)) return M_RAM_SLAVE2;
	if ((p & MAPMASK(ARRAYSIZEOF(Chipset.dd[2].Ram)))   == (0x2FC00 >> ADDR_BITS)) return M_RAM_MASTER;

	// search for a 1LG8 chip mapped to the given address
	for (nChip4k = 0; nChip4k < ARRAYSIZEOF(Chipset.ir); ++nChip4k)
	{
		for (nChip = 0; nChip < ARRAYSIZEOF(Chipset.ir[0].sCfg); ++nChip)
		{
			if (   Chipset.ir[nChip4k].sCfg[nChip].bCfg
				&& ((p & MAPMASK(ISIZE_1LG8)) == (Chipset.ir[nChip4k].sCfg[nChip].dwBase >> ADDR_BITS)))
			{
				return M_RAM;
			}
		}
	}

	if ((*ppsPort = GetPortModule(d)) != NULL)                                     return M_PORT;
	return M_NONE;
}

static VOID NreadEx(BYTE *a, DWORD d, UINT s, BOOL bUpdate)
{
	enum MMUMAP eMap;
	PPORTACC psPort;
	DWORD u, v, c;
	BYTE *p;

	#if defined DEBUG_MEMACC
	{
		TCHAR buffer[256];
		wsprintf(buffer,_T("%.5lx: Mem %s : %02x,%u\n"),
				 Chipset.pc,(bUpdate) ? _T("read") : _T("peek"),d,s);
		OutputDebugString(buffer);
	}
	#endif

	do
	{
		eMap = MapData(d,&psPort);			// get active memory controller

		do
		{
			enum CHIP eChip;

			switch (eMap)
			{
			case M_IO_SLAVE1: eChip = SLAVE1; break;
			case M_IO_SLAVE2: eChip = SLAVE2; break;
			case M_IO_MASTER: eChip = MASTER; break;
			default: eChip = NONE;
			}

			if (eChip != NONE)				// IO access
			{
				v = d & (sizeof(Chipset.dd[0].IORam)-1);
				c = MIN(s,sizeof(Chipset.dd[0].IORam)-v);
				ReadIO(eChip,a,v,c,bUpdate);
				break;
			}

			// module has memory mapped IO
			if (psPort && psPort->pfnReadIO != NULL)
			{
				c = s;						// no. of nibbles to read
				psPort->pfnReadIO(psPort->h,a,d,&c,bUpdate);
				break;
			}

			u = d >> ADDR_BITS;
			v = d & (ADDR_SIZE-1);
			c = MIN(s,ADDR_SIZE-v);
			if ((p=RMap[u]) != NULL)		// module mapped
			{
				memcpy(a, p+v, c);
			}
			// simulate open data bus
			else							// open data bus
			{
				if (M_NONE != eMap)			// open data bus
				{
					for (u=0; u<c; ++u)		// fill all nibbles
					{
						if ((v+u) & 1)		// odd address
							a[u] = READODD;
						else				// even address
							a[u] = READEVEN;
					}
				}
				else
				{
					memset(a, 0x00, c);		// fill with 0
				}
			}
		}
		while (FALSE);

		a += c;
		d = (d + c) & 0xFFFFF;
	} while (s -= c);
	return;
}

__forceinline VOID Npeek(BYTE *a, DWORD d, UINT s)
{
	NreadEx(a, d, s, FALSE);
	return;
}

__forceinline VOID Nread(BYTE *a, DWORD d, UINT s)
{
	NreadEx(a, d, s, TRUE);
	return;
}

VOID Nwrite(BYTE *a, DWORD d, UINT s)
{
	enum MMUMAP eMap;
	PPORTACC psPort;
	DWORD u, v, c;
	BYTE *p;

	#if defined DEBUG_MEMACC
	{
		TCHAR buffer[256];
		wsprintf(buffer,_T("%.5lx: Mem write: %02x,%u\n"),Chipset.pc,d,s);
		OutputDebugString(buffer);
	}
	#endif

	do
	{
		eMap = MapData(d,&psPort);			// get active memory controller

		do
		{
			enum CHIP eChip;

			switch (eMap)
			{
			case M_IO_SLAVE1: eChip = SLAVE1; break;
			case M_IO_SLAVE2: eChip = SLAVE2; break;
			case M_IO_MASTER: eChip = MASTER; break;
			default: eChip = NONE;
			}

			if (eChip != NONE)				// IO access
			{
				v = d & (sizeof(Chipset.dd[0].IORam)-1);
				c = MIN(s,sizeof(Chipset.dd[0].IORam)-v);
				WriteIO(eChip, a, v, c);
				break;
			}

			// module has memory mapped IO
			if (psPort && psPort->pfnWriteIO != NULL)
			{
				c = s;						// no. of nibbles to write
				psPort->pfnWriteIO(psPort->h,a,d,&c);
				break;
			}

			u = d >> ADDR_BITS;
			v = d & (ADDR_SIZE-1);
			c = MIN(s,ADDR_SIZE-v);
			if ((p=WMap[u]) != NULL) memcpy(p+v, a, c);
		}
		while(FALSE);

		a += c;
		d = (d + c) & 0xFFFFF;
	} while (s -= c);
	return;
}

DWORD Read5(DWORD d)
{
	BYTE p[5];

	Npeek(p,d,5);
	return Npack(p,5);
}

BYTE Read2(DWORD d)
{
	BYTE p[2];

	Npeek(p,d,2);
	return (BYTE)(p[0]|(p[1]<<4));
}

VOID Write5(DWORD d, DWORD n)
{
	BYTE p[5];

	Nunpack(p,n,5);
	Nwrite(p,d,5);
	return;
}

VOID Write2(DWORD d, BYTE n)
{
	BYTE p[2];

	Nunpack(p,n,2);
	Nwrite(p,d,2);
	return;
}

VOID ChangeBit(LPBYTE pbyV, BYTE b, BOOL s)	// thread safe set/clear bit in a byte
{
	EnterCriticalSection(&csBitLock);
	{
		if (s)
			*pbyV |= b;						// set bit
		else
			*pbyV &= ~b;					// clear bit
	}
	LeaveCriticalSection(&csBitLock);
}

static VOID UpdateBatState(BYTE *r, BOOL bUpdate)
{
	if (bUpdate)							// update register content
	{
		SYSTEM_POWER_STATUS sSps;

		*r &= ~(LBI | VLBI);				// clear LBI and VLBI bits in Display-Timer Control Nibble

		VERIFY(GetSystemPowerStatus(&sSps));

		// low bat emulation enabled and battery powered
		if (!bLowBatDisable && sSps.ACLineStatus == AC_LINE_OFFLINE)
		{
			// on critical battery state make sure that lowbat flag is also set
			if ((sSps.BatteryFlag & BATTERY_FLAG_CRITICAL) != 0)
				sSps.BatteryFlag |= BATTERY_FLAG_LOW;

			// low bat detection
			if ((sSps.BatteryFlag & BATTERY_FLAG_LOW) != 0)
			{
				*r |= LBI;
			}

			// very low bat detection
			if ((sSps.BatteryFlag & BATTERY_FLAG_CRITICAL) != 0)
			{
				*r |= VLBI;
			}
		}
	}
	return;
}

static VOID UpdateSreq(VOID)
{
	UINT i;

	BOOL bSReq = FALSE;

	// check wakeup condition of each 1LF3 chip
	for (i = 0; i < ARRAYSIZEOF(Chipset.dd); ++i)
	{
		// one timer needs service
		bSReq =  bSReq
			  || (   (Chipset.dd[i].IORam[(TIMER1 + 5) & 0xFF] & 0x8) != 0
				  && (Chipset.dd[i].IORam[DD1CTL       & 0xFF] & WKE) != 0
				 );
	}

	ChangeBit(&Chipset.SREQ,0x01,bSReq);	// update service request of timer
}

static VOID TAcc(enum CHIP eChip)
{
	static struct
	{
		DWORD dwCyc;						// CPU cycle counter at last timer read access
		BOOL  bEnAcc;						// access enabled (because timer is always read twice)
	}
	sState[ARRAYSIZEOF(Chipset.dd)] =
	{
		{ 0, FALSE },
		{ 0, FALSE },
		{ 0, FALSE }
	};

	DWORD dwCycDif;
	BOOL  bEnAcc;

	_ASSERT(eChip < ARRAYSIZEOF(sState));

	// CPU cycles since last call
	dwCycDif = (DWORD) (Chipset.cycles & 0xFFFFFFFF) - sState[eChip].dwCyc;
	sState[eChip].dwCyc = (DWORD) (Chipset.cycles & 0xFFFFFFFF);

	// maybe CPU speed measurement, slow down the next 10 CPU opcodes
	if ((bEnAcc = dwCycDif < 150))			// access inside time frame
	{
		// it's suggested to read a timer always twice to
		// detect a nibble carry at the timer increment
		if (sState[eChip].bEnAcc == TRUE)	// 2nd access inside time frame
		{
			EnterCriticalSection(&csSlowLock);
			{
				InitAdjustSpeed();			// init variables if necessary
				nOpcSlow = 10;				// slow down next 10 opcodes
			}
			LeaveCriticalSection(&csSlowLock);
		}
	}
	sState[eChip].bEnAcc = bEnAcc;			// set last access mode
	return;
}

static VOID ReadIO(enum CHIP eChip, BYTE *a, DWORD d, DWORD s, BOOL bUpdate)
{
	LPBYTE pbyIORam = Chipset.dd[eChip].IORam;

	#if defined DEBUG_IO
	{
		TCHAR buffer[256];
		wsprintf(buffer,_T("%.5lx: IO read : %d,%02x,%u\n"),Chipset.pc,eChip,d,s);
		OutputDebugString(buffer);
	}
	#endif

	do
	{
		_ASSERT(d >= 0x00 && d <= 0xFF);

		switch (d)
		{
		default:
			// display RAM (96 nibbles) or reserved (152 nibbles)
			_ASSERT(d >= 0x00 && d <= 0xF7);
			*a = (d <= 0x5F) ? pbyIORam[d] : 0;
			break;
		case 0xF8:	// LSB Timer
			if (bUpdate && eChip == SLAVE2)	// =CLKSPEED use this timer for speed measurement
			{
				TAcc(eChip);				// remember this access
			}
		case 0xF9:	// Timer
		case 0xFA:	// Timer
		case 0xFB:	// Timer
		case 0xFC:	// Timer
		case 0xFD:	// MSB Timer
			EnterCriticalSection(&csTLock);
			{
				*a = pbyIORam[d];			// TIMER content
			}
			LeaveCriticalSection(&csTLock);
			break;
		case 0xFE:	// DCONTR (only MASTER)
			*a = pbyIORam[d];
			break;
		case 0xFF:	// DD1CTL
			*a = pbyIORam[d];
			if (bUpdate)
			{
				if (eChip == MASTER)
				{
					UpdateBatState(a,bUpdate);
				}
				else
				{
					*a &= ~(LBI | VLBI);	// clear LBI and VLBI bits
				}
			}
			break;
		}

		d++; a++;
	} while (--s);
	return;
}

static VOID WriteIO(enum CHIP eChip, BYTE *a, DWORD d, DWORD s)
{
	BYTE  c;
	DWORD dwAnnunciator = 0;				// no annunciator write

	LPBYTE pbyIORam = Chipset.dd[eChip].IORam;

	#if defined DEBUG_IO
	{
		TCHAR buffer[256];
		DWORD j;
		int   i;

		i = wsprintf(buffer,_T("%.5lx: IO write: %d,%02x,%u = "),Chipset.pc,eChip,d,s);
		for (j = 0;j < s;++j,++i)
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

	do
	{
		_ASSERT(d >= 0x00 && d <= 0xFF);

		c = *a;
		switch (d)
		{
		default:
			_ASSERT(d >= 0x00 && d <= 0xF7);
			if (d <= 0x5F)					// display RAM (96 nibbles)
			{
				// update annunciator area
				// ANNAD1, ANN1_5, ANNAD2, ANN2_5
				if (eChip == SLAVE1 && d >= 0x00 && d <= 0x03)
				{
					dwAnnunciator |= (pbyIORam[d] ^ c) << (d * 4);
				}
				if (eChip == MASTER)
				{
					// ANNAD3, ANN3_5, ANNAD4, ANN4_5
					if (d >= 0x4C && d <= 0x4F)
					{
						dwAnnunciator |= (pbyIORam[d] ^ c) << ((d - 0x4C + 4) * 4);
					}
					// ROWDVR (16 nibbles)
					if (d >= 0x50 && d <= 0x5F && (pbyIORam[d] ^ c) != 0)
					{
						dwAnnunciator = 0xFFFFFFFF;
					}
				}

				pbyIORam[d] = c;			// display RAM content
			}
			// reserved (152 nibbles) no write
			break;
// 000F8 =  TIMER
// 000F8 @  hardware timer (F8-FD), decremented 512 times/s
		case 0xF8:	// LSB Timer
		case 0xF9:	// Timer
		case 0xFA:	// Timer
		case 0xFB:	// Timer
		case 0xFC:	// Timer
		case 0xFD:	// MSB Timer
			EnterCriticalSection(&csTLock);
			{
				pbyIORam[d] = c;			// write TIMER data
			}
			LeaveCriticalSection(&csTLock);
			if (d == 0xFD)
			{
				UpdateSreq();				// update timer service request
			}
			break;
// 000FE =  DCONTR
// 000FE @  Contrast Control Nibble [CONT3 CONT2 CONT1 CONT0]
// 000FE @  Higher value = darker screen
		case 0xFE:	// DCONTR (only MASTER)
			pbyIORam[d] = c;				// write contrast data
			if (eChip == MASTER)			// only master chip
			{
				UpdateContrast();
			}
			break;
// 000FF =  DDCTL
// 000FF @  Display-Timer Control Nibble [WKE DTEST DBLINK DON]
		case 0xFF:	// DD1CTL
			if (eChip == MASTER)
			{
				if ((c^pbyIORam[d])&DON)	// DON bit changed
				{
					if ((c & DON) != 0)		// set display on
					{
						pbyIORam[d] |= DON;
						StartDisplay();		// start display update
					}
					else					// display is off
					{
						pbyIORam[d] &= ~DON;
						StopDisplay();		// stop display update
					}
					UpdateContrast();		// with annunciator redraw
				}

				// redraw annunciators if blink bit switched to off
				dwAnnunciator |= -(INT)(((c^pbyIORam[d]) & pbyIORam[d] & DBLINK) != 0);
			}

			// WkeEn OldReg NewReg -> WkeEn
			//   0      0      0        0
			//   0      0      1        1
			//   0      1      0        0
			//   0      1      1        0
			//   1      0      0        0
			//   1      0      1        1
			//   1      1      0        0
			//   1      1      1        1

			Chipset.dd[eChip].bWkeEn =  (!Chipset.dd[eChip].bWkeEn && (pbyIORam[d] & WKE) == 0 && (c & WKE) != 0)
									 || ( Chipset.dd[eChip].bWkeEn && (pbyIORam[d] & WKE) == 0 && (c & WKE) != 0)
									 || ( Chipset.dd[eChip].bWkeEn && (pbyIORam[d] & WKE) != 0 && (c & WKE) != 0);

			pbyIORam[d] = c;				// write data
			UpdateSreq();					// update timer service request
			break;
		}
		a++; d++;
	} while (--s);

	// when display is blinking, the annunciators are controlled by the UpdateDisplay() function
	if (dwAnnunciator && (Chipset.dd[MASTER].IORam[DD1CTL & 0xFF] & DBLINK) == 0) UpdateAnnunciators(dwAnnunciator);
	return;
}
