/*
 *   satmem.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "ops.h"
#include "portmem.h"
#include "satmem.h"

// Saturn Memory Chip
typedef struct
{
	UINT	nType;							// module type
	DWORD	dwSize;							// size of hybrid chip in nibbles
	DWORD	dwChips;						// internal no. of chips
	BOOL	bHybrid;						// hybrid chip modul
	LPBYTE	pbyMem;							// pointer to the allocated memory
	PSATCFG	psCfg;							// configuration data
} SATMEM, *PSATMEM;

HANDLE AllocSaturnMem(UINT nType,DWORD dwChipSize,DWORD dwChips,BOOL bHybrid,LPBYTE pbyMem,PSATCFG psCfg)
{
	PSATMEM psSaturnMem;

	if ((psSaturnMem = (PSATMEM) malloc(sizeof(*psSaturnMem))) != NULL)
	{
		psSaturnMem->nType   = nType;
		psSaturnMem->dwSize  = dwChipSize;
		psSaturnMem->dwChips = dwChips;
		psSaturnMem->bHybrid = bHybrid;
		psSaturnMem->pbyMem  = pbyMem;
		psSaturnMem->psCfg   = psCfg;
	}
	return (HANDLE) psSaturnMem;
}

////////////////////////////////////////////////////////////////////////////////
//
// Memory Interface
//
////////////////////////////////////////////////////////////////////////////////

//
// address is handled by module
//
static BOOL IsModuleSaturnMem(HANDLE h,DWORD d)
{
	UINT  c;
	DWORD dwChipSize;

	// size of a single chip
	dwChipSize = ((PSATMEM) h)->dwSize / ((PSATMEM) h)->dwChips;

	for (c = 0; c < ((PSATMEM) h)->dwChips; ++c)
	{
		if ((((PSATMEM) h)->psCfg[c].bCfg))	// internal chip is configured
		{
			if ((((((PSATMEM) h)->psCfg[c].dwBase ^ d) >> ADDR_BITS)
				& MAPMASK(dwChipSize)) == 0)
				return TRUE;
		}
	}
	return FALSE;
}

//
// map pages
//
static VOID MapSaturnMem(HANDLE h,DWORD a,DWORD b)
{
	LPBYTE pBase;
	UINT   i,c;
	DWORD  dwChipSize,s,e,p,m;
	BOOL   bHrd;

	_ASSERT(h != NULL);

	// base of memory
	pBase = ((PSATMEM) h)->pbyMem;

	// size of a single chip
	dwChipSize = ((PSATMEM) h)->dwSize / ((PSATMEM) h)->dwChips;

	// hard configured chip
	bHrd = ((PSATMEM) h)->nType == TYPE_HRD;

	// scan each hybrid chip
	for (c = 0; c < ((PSATMEM) h)->dwChips; ++c)
	{
		if ((((PSATMEM) h)->psCfg[c].bCfg))	// internal chip is configured
		{
			s = MAX(a, (((PSATMEM) h)->psCfg[c].dwBase >> ADDR_BITS) & MAPMASK(dwChipSize));
			e = MIN(b,((((PSATMEM) h)->psCfg[c].dwBase >> ADDR_BITS) & MAPMASK(dwChipSize)) + MAPSIZE(dwChipSize));
			m = ((MAPSIZE(dwChipSize) + 1) << ADDR_BITS) - 1;
			p = (s << ADDR_BITS) & m;		// offset to begin in nibbles
			for (i=s; i<=e; ++i)
			{
				if (RMap[i] == NULL || (bHrd && HMap[i] == FALSE))
				{
					RMap[i] = pBase + p;
					WMap[i] = (((PSATMEM) h)->nType == TYPE_RAM) ? pBase + p : NULL;
					HMap[i] = bHrd;
				}
				p = (p + ADDR_SIZE) & m;
			}
		}

		pBase += dwChipSize;				// next internal chip
	}
	return;
}

//
// configure module (CONFIG)
//
static BOOL ConfigSaturnMem(HANDLE h)
{
	UINT  c;
	DWORD dwChipSize;

	DWORD p = Npack(Chipset.C,5) >> ADDR_BITS; // page address

	_ASSERT(h != NULL);

	// size of a single chip
	dwChipSize = ((PSATMEM) h)->dwSize / ((PSATMEM) h)->dwChips;

	// scan each hybrid chip
	for (c = 0; c < ((PSATMEM) h)->dwChips; ++c)
	{
		// internal chip is not configured
		if (((PSATMEM) h)->psCfg[c].bCfg == FALSE)
		{
			((PSATMEM) h)->psCfg[c].bCfg = TRUE;
			// adjust base to mapping boundary
			p &= MAPMASK(dwChipSize);
			((PSATMEM) h)->psCfg[c].dwBase = (p << ADDR_BITS);
			Map(p,p+MAPSIZE(dwChipSize));
			return TRUE;
		}
	}
	return FALSE;
}

//
// unconfigure module (UNCNFG)
//
static VOID UncnfgSaturnMem(HANDLE h)
{
	UINT  c;
	DWORD dwChipSize;

	DWORD p = Npack(Chipset.C,5) >> ADDR_BITS; // page address

	_ASSERT(h != NULL);

	// size of a single chip
	dwChipSize = ((PSATMEM) h)->dwSize / ((PSATMEM) h)->dwChips;

	// scan each hybrid chip
	for (c = 0; c < ((PSATMEM) h)->dwChips; ++c)
	{
		DWORD dwBasePage = ((PSATMEM) h)->psCfg[c].dwBase >> ADDR_BITS;

		if (   ((PSATMEM) h)->psCfg[c].bCfg != FALSE
			&& ((p & MAPMASK(dwChipSize)) == dwBasePage))
		{
			((PSATMEM) h)->psCfg[c].bCfg = FALSE;
			Map(dwBasePage,dwBasePage+MAPSIZE(dwChipSize));
		}
	}
	return;
}

//
// reset module (RESET)
//
static VOID ResetSaturnMem(HANDLE h)
{
	UINT c;

	_ASSERT(h != NULL);

	// scan each hybrid chip
	for (c = 0; c < ((PSATMEM) h)->dwChips; ++c)
	{
		((PSATMEM) h)->psCfg[c].bCfg   = FALSE;
		((PSATMEM) h)->psCfg[c].dwBase = 0x00000;
	}
	return;
}

//
// fetch ID (C=ID)
//
static DWORD C_Eq_IdSaturnMem(HANDLE h)
{
	UINT  c;
	DWORD dwID = 0;							// all modules configured

	// scan each hybrid chip
	for (c = 0; c < ((PSATMEM) h)->dwChips; ++c)
	{
		if (((PSATMEM) h)->psCfg[c].bCfg == FALSE)
		{
			// calculate ID for RAM from module size
			dwID = MemModuleId(((PSATMEM) h)->dwSize / ((PSATMEM) h)->dwChips);

			// is it a ROM module?
			if (((PSATMEM) h)->nType != TYPE_RAM)
			{
				dwID |= 0x00100;			// modify to ID for ROM
			}

			// not last chip in hybrid module
			if (((PSATMEM) h)->bHybrid && c + 1 < ((PSATMEM) h)->dwChips)
			{
				dwID &= ~0x80000;			// not last chip
			}
			break;
		}
	}
	return dwID;
}

//
// detach Saturn memory from address handling and delete instance
//
static VOID DetachSaturnMem(PPORTACC *ppsPort)
{
	PPORTACC pAct = *ppsPort;				// addr of allocated memory
	*ppsPort = (*ppsPort)->pNext;			// detach the memory
	free(pAct->h);							// free the module instance
	free(pAct);								// free the interface memory
	return;
}

//
// attach Saturn memory to address handling
//
BOOL AttachSaturnMem(PPORTACC *ppsPort,HANDLE hMemModule)
{
	// walk through modules to find a free slot
	while (*ppsPort != NULL) ppsPort = &(*ppsPort)->pNext;

	if (hMemModule != NULL)
	{
		// allocate memory for the port interface
		if ((*ppsPort = AllocPortMem()) != NULL)
		{
			(*ppsPort)->h           = hMemModule;			// module instance
			(*ppsPort)->pfnIsModule = IsModuleSaturnMem;	// check if address belongs to my module
			(*ppsPort)->pfnMap      = MapSaturnMem;			// map pages
			if (((PSATMEM) hMemModule)->nType != TYPE_HRD)	// hard wired modules don't respond to bus commands
			{
				(*ppsPort)->pfnConfig  = ConfigSaturnMem;	// configure module (CONFIG)
				(*ppsPort)->pfnUncnfg  = UncnfgSaturnMem;	// unconfigure module (UNCNFG)
				(*ppsPort)->pfnReset   = ResetSaturnMem;	// reset module (RESET)
				(*ppsPort)->pfnC_Eq_Id = C_Eq_IdSaturnMem;	// fetch ID (C=ID)
			}

			(*ppsPort)->pfnDetachMem = DetachSaturnMem;		// detach module
		}
		else
		{
			free(hMemModule);				// free the module instance
		}
	}
	return *ppsPort != NULL;
}
