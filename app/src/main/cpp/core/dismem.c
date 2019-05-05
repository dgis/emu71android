/*
 *   dismem.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2012 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu71.h"

static enum MEM_MAPPING eMapType = MEM_MMU;	// MMU memory mapping

static LPBYTE pbyMapData = NULL;
static DWORD  dwMapDataSize = 0;
static DWORD  dwMapDataMask = 0;

BOOL SetMemMapType(enum MEM_MAPPING eType)
{
	BOOL bSucc = TRUE;

	eMapType = eType;

	switch (eMapType)
	{
	case MEM_MMU:	
		pbyMapData = NULL;					// data
		dwMapDataSize = 512 * 1024 * 2;		// CPU address range
		dwMapDataMask = dwMapDataSize - 1;	// size mask
		break;
	case MEM_ROM:
		pbyMapData = pbyRom;
		dwMapDataSize = dwRomSize;			// ROM size is always in nibbles
		dwMapDataMask = dwMapDataSize - 1;	// size mask
		break;
	default: _ASSERT(FALSE);
		pbyMapData = NULL;
		dwMapDataSize = 0;
		dwMapDataMask = 0;
		bSucc = FALSE;
	}
	return bSucc;
}

enum MEM_MAPPING GetMemMapType(VOID)
{
	return eMapType;
}

DWORD GetMemDataSize(VOID)
{
	return dwMapDataSize;
}

DWORD GetMemDataMask(VOID)
{
	return dwMapDataMask;
}

BYTE GetMemNib(DWORD *p)
{
	BYTE byVal;

	if (pbyMapData == NULL)
	{
		Npeek(&byVal, *p, 1);
	}
	else
	{
		byVal = pbyMapData[*p];
	}
	*p = (*p + 1) & dwMapDataMask;
	return byVal;
}

VOID GetMemPeek(BYTE *a, DWORD d, UINT s)
{
	if (pbyMapData == NULL)
	{
		Npeek(a, d, s);
	}
	else
	{
		for (; s > 0; --s, ++d)
		{
			*a++ = pbyMapData[d & dwMapDataMask];
		}
	}
	return;
}
