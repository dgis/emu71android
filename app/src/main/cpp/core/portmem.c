/*
 *   portmem.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2013 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "portmem.h"

////////////////////////////////////////////////////////////////////////////////
//
// Memory Interface
//
////////////////////////////////////////////////////////////////////////////////

//
// address is handled by module
//
static BOOL IsModuleMem(HANDLE h,DWORD d)
{
	return FALSE;							// address not handled
	UNREFERENCED_PARAMETER(h);
	UNREFERENCED_PARAMETER(d);
}

//
// map pages
//
static VOID MapMem(HANDLE h,DWORD a,DWORD b)
{
	return;									// do nothing
	UNREFERENCED_PARAMETER(h);
	UNREFERENCED_PARAMETER(a);
	UNREFERENCED_PARAMETER(b);
}

//
// configure module (CONFIG)
//
static BOOL ConfigMem(HANDLE h)
{
	return FALSE;							// module not configured
	UNREFERENCED_PARAMETER(h);
}

//
// unconfigure module (UNCNFG)
//
static VOID UncnfgMem(HANDLE h)
{
	return;									// do nothing
	UNREFERENCED_PARAMETER(h);
}

//
// reset module (RESET)
//
static VOID ResetMem(HANDLE h)
{
	return;									// do nothing
	UNREFERENCED_PARAMETER(h);
}

//
// fetch ID (C=ID)
//
static DWORD C_Eq_IdMem(HANDLE h)
{
	return 0;								// module is configured
	UNREFERENCED_PARAMETER(h);
}

//
// service request of module
//
static BYTE SReqMem(HANDLE h)
{
	return 0;								// no service request
	UNREFERENCED_PARAMETER(h);
}

//
// allocate port memory
//
PPORTACC AllocPortMem(VOID)
{
	PPORTACC psPort;

	// allocate memory for the port interface
	if ((psPort = (PPORTACC) malloc(sizeof(*psPort))) != NULL)
	{
		psPort->h            = NULL;		// module instance
		psPort->pfnIsModule  = IsModuleMem;	// check if address belongs to my module
		psPort->pfnMap       = MapMem;		// map pages
		psPort->pfnConfig    = ConfigMem;	// configure module (CONFIG)
		psPort->pfnUncnfg    = UncnfgMem;	// unconfigure module (UNCNFG)
		psPort->pfnReset     = ResetMem;	// reset module (RESET)
		psPort->pfnC_Eq_Id   = C_Eq_IdMem;	// fetch ID (C=ID)
		psPort->pfnSREQ      = SReqMem;		// service request of module
		psPort->pfnWriteIO   = NULL;		// write IO memory (no memory mapped IO module)
		psPort->pfnReadIO    = NULL;		// read IO memory (no memory mapped IO module)
		psPort->pfnDetachMem = NULL;		// detach module (pure virtual, must be overloaded)
		psPort->pNext        = NULL;		// next module in same queue
	}
	return psPort;
}
