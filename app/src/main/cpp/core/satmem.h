/*
 *   satmem.h
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gieﬂelink
 *
 */

// satmem.c
extern HANDLE AllocSaturnMem(UINT nType,DWORD dwChipSize,DWORD dwChips,BOOL bHybrid,LPBYTE pbyMem,PSATCFG psCfg);
extern BOOL   AttachSaturnMem(PPORTACC *ppsPort,HANDLE hMemModule);
