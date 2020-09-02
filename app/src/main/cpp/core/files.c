/*
 *   files.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu71.h"
#include "ops.h"
#include "io.h"								// I/O register definitions
#include "kml.h"
#include "debugger.h"
#include "lodepng.h"

#pragma intrinsic(abs,labs)

#define CKSUM1			0x0000D				// =CKSUM1
#define CKSUM2			0x0AA81				// =CKSUM2
#define CKSUM3			0x153A9				// =CKSUM3
#define CKSUM4			0x1DBA6				// =CKSUM4

#define WARM1X			0x010A6				// =WARM1X (INIT: 1)

typedef struct
{
	DWORD	dwStructSize;
	DWORD	dwPortRev;						// port implementation revision
} PORTPLUG, *PPORTPLUG;

TCHAR  szEmuDirectory[MAX_PATH];
TCHAR  szCurrentDirectory[MAX_PATH];
TCHAR  szCurrentKml[MAX_PATH];
TCHAR  szBackupKml[MAX_PATH];
TCHAR  szCurrentFilename[MAX_PATH];
TCHAR  szBackupFilename[MAX_PATH];
TCHAR  szBufferFilename[MAX_PATH];

PPORTDATA psExtPortData[NOEXTPORTS];		// port data

BOOL   bDocumentAvail = FALSE;				// document not available

LPBYTE pbyRom = NULL;
DWORD  dwRomSize = 0;
WORD   wRomCrc = 0;							// fingerprint of patched ROM
BYTE   cCurrentRomType = 0;					// Model -> hardware
UINT   nCurrentClass = 0;					// Class -> derivate

BOOL   bBackup = FALSE;

// document signatures
static BYTE pbySignature[] = "Emu71 Document\xFE";
static HANDLE hCurrentFile = NULL;

static CHIPSET   BackupChipset;
static PPORTDATA psExtPortDataBackup[NOEXTPORTS]; // port data

//################
//#
//#    Window Position Tools
//#
//################

VOID SetWindowLocation(HWND hWnd,INT nPosX,INT nPosY)
{
	WINDOWPLACEMENT wndpl;
	RECT *pRc = &wndpl.rcNormalPosition;

	wndpl.length = sizeof(wndpl);
	GetWindowPlacement(hWnd,&wndpl);
	pRc->right = pRc->right - pRc->left + nPosX;
	pRc->bottom = pRc->bottom - pRc->top + nPosY;
	pRc->left = nPosX;
	pRc->top = nPosY;
	SetWindowPlacement(hWnd,&wndpl);
	return;
}



//################
//#
//#    Filename Title Helper Tool
//#
//################

DWORD GetCutPathName(LPCTSTR szFileName, LPTSTR szBuffer, DWORD dwBufferLength, INT nCutLength)
{
	TCHAR  cPath[_MAX_PATH];				// full filename
	TCHAR  cDrive[_MAX_DRIVE];
	TCHAR  cDir[_MAX_DIR];
	TCHAR  cFname[_MAX_FNAME];
	TCHAR  cExt[_MAX_EXT];

	_ASSERT(nCutLength >= 0);				// 0 = only drive and name

	// split original filename into parts
	_tsplitpath(szFileName,cDrive,cDir,cFname,cExt);

	if (*cDir != 0)							// contain directory part
	{
		LPTSTR lpFilePart;					// address of file name in path
		INT    nNameLen,nPathLen,nMaxPathLen;

		GetFullPathName(szFileName,ARRAYSIZEOF(cPath),cPath,&lpFilePart);
		_tsplitpath(cPath,cDrive,cDir,cFname,cExt);

		// calculate size of drive/name and path
		nNameLen = lstrlen(cDrive) + lstrlen(cFname) + lstrlen(cExt);
		nPathLen = lstrlen(cDir);

		// maximum length for path
		nMaxPathLen = nCutLength - nNameLen;

		if (nPathLen > nMaxPathLen)			// have to cut path
		{
			TCHAR cDirTemp[_MAX_DIR] = _T("");
			LPTSTR szPtr;

			// UNC name
			if (cDir[0] == _T('\\') && cDir[1] == _T('\\'))
			{
				// skip server
				if ((szPtr = _tcschr(cDir + 2,_T('\\'))) != NULL)
				{
					// skip share
					if ((szPtr = _tcschr(szPtr + 1,_T('\\'))) != NULL)
					{
						INT nLength = (INT) (szPtr - cDir);

						*szPtr = 0;			// set EOS behind share

						// enough room for \\server\\share and "\...\"
						if (nLength + 5 <= nMaxPathLen)
						{
							lstrcpyn(cDirTemp,cDir,ARRAYSIZEOF(cDirTemp));
							nMaxPathLen -= nLength;
						}
					}
				}
			}

			lstrcat(cDirTemp,_T("\\..."));
			nMaxPathLen -= 5;				// need 6 chars for additional "\..." + "\"
			if (nMaxPathLen < 0) nMaxPathLen = 0;

			// get earliest possible '\' character
			szPtr = &cDir[nPathLen - nMaxPathLen];
			szPtr = _tcschr(szPtr,_T('\\'));
			// not found
			if (szPtr == NULL) szPtr = _T("");

			lstrcat(cDirTemp,szPtr);		// copy path with preample to dir buffer
			lstrcpyn(cDir,cDirTemp,ARRAYSIZEOF(cDir));
		}
	}

	_tmakepath(cPath,cDrive,cDir,cFname,cExt);
	lstrcpyn(szBuffer,cPath,dwBufferLength);
	return lstrlen(szBuffer);
}

VOID SetWindowPathTitle(LPCTSTR szFileName)
{
	TCHAR cPath[MAX_PATH];
	RECT  rectClient;

	if (*szFileName != 0)					// set new title
	{
		_ASSERT(hWnd != NULL);
		VERIFY(GetClientRect(hWnd,&rectClient));
		GetCutPathName(szFileName,cPath,ARRAYSIZEOF(cPath),rectClient.right/11);
		SetWindowTitle(cPath);
	}
	return;
}



//################
//#
//#    generic file mapping
//#
//################

BOOL MapFile(LPCTSTR szFilename,LPBYTE *ppbyData,LPDWORD pdwFileSize)
{
	HANDLE hFile;
	DWORD  dwFileSize,dwRead;
	QWORD  qwData;

	SetCurrentDirectory(szEmuDirectory);
	hFile = CreateFile(szFilename,
					   GENERIC_READ,
					   FILE_SHARE_READ,
					   NULL,
					   OPEN_EXISTING,
					   FILE_FLAG_SEQUENTIAL_SCAN,
					   NULL);
	SetCurrentDirectory(szCurrentDirectory);

	if (hFile == INVALID_HANDLE_VALUE) return FALSE;

	// get file size
	dwFileSize = GetFileSize(hFile, NULL);
	if (dwFileSize == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
	{
		CloseHandle(hFile);
		return FALSE;
	}

	// read the first 8 bytes
	ReadFile(hFile,&qwData,sizeof(qwData),&dwRead,NULL);
	if (dwRead < sizeof(qwData))
	{ // file is too small.
		CloseHandle(hFile);
		return FALSE;
	}

	*pdwFileSize = dwFileSize;				// calculate image buffer size
	if ((qwData & CLL(0xF0F0F0F0F0F0F0F0)) != 0) // packed image ->
		*pdwFileSize *= 2;					// unpacked image has double size

	if (ppbyData == NULL)					// no mapping data
	{
		CloseHandle(hFile);					// return only size
		return TRUE;
	}

	*ppbyData = (LPBYTE) malloc(*pdwFileSize);
	if (*ppbyData == NULL)
	{
		CloseHandle(hFile);
		*pdwFileSize = 0;
		return FALSE;
	}

	*((QWORD *)(*ppbyData)) = qwData;		// save first 8 bytes

	// load rest of file content
	ReadFile(hFile,&(*ppbyData)[sizeof(qwData)],dwFileSize - sizeof(qwData),&dwRead,NULL);
	_ASSERT(dwFileSize - sizeof(qwData) == dwRead);
	CloseHandle(hFile);

	if (*pdwFileSize != dwFileSize)			// packed ROM image
	{
		DWORD dwSize = *pdwFileSize;		// destination start address
		while (dwFileSize > 0)				// unpack source
		{
			BYTE byValue = (*ppbyData)[--dwFileSize];
			_ASSERT(dwSize >= 2);
			(*ppbyData)[--dwSize] = byValue >> 4;
			(*ppbyData)[--dwSize] = byValue & 0xF;
		}
	}
	return TRUE;
}



//################
//#
//#    BEEP Patch check
//#
//################

BOOL CheckForBeepPatch(VOID)
{
	typedef struct beeppatch
	{
		const DWORD dwAddress;				// patch address
		const BYTE  byPattern[4];			// patch pattern
	} BEEPPATCH, *PBEEPPATCH;

	// known beep patches
	const BEEPPATCH s71[] = { { 0x0254A, { 0x8, 0x1, 0xB, 0x0 } },		// =RCKBp
							  { 0x0EB40, { 0x8, 0x1, 0xB, 0x1 } } };	// =BP+C

	const BEEPPATCH *psData;
	UINT nDataItems;
	BOOL bMatch;

	switch (cCurrentRomType)
	{
	case 'T':								// HP71B
		psData = s71;
		nDataItems = ARRAYSIZEOF(s71);
		break;
	default:
		psData = NULL;
		nDataItems = 0;
	}

	// check if one data set match
	for (bMatch = FALSE; !bMatch && nDataItems > 0; --nDataItems)
	{
		_ASSERT(pbyRom != NULL && psData != NULL);

		// pattern matching?
		bMatch =  (psData->dwAddress + ARRAYSIZEOF(psData->byPattern) < dwRomSize)
			   && (memcmp(&pbyRom[psData->dwAddress],psData->byPattern,ARRAYSIZEOF(psData->byPattern))) == 0;
		++psData;							// next data set
	}
	return bMatch;
}



//################
//#
//#    Patch
//#
//################

// checksum calculation
static BYTE Checksum(LPBYTE pbyROM, DWORD dwStart)
{
	INT  i;
	BYTE byDat,byChk;

	byChk = 0;								// reset checksum
	pbyROM += dwStart;						// start address

	for (i = 0; i < 0x4000; ++i)			// address 0-7FFFF
	{
		// data byte
		byDat = (pbyROM[1] << 4) | pbyROM[0];

		byChk += byDat;						// new checksum
		if (byChk < byDat) ++byChk;			// add carry

		pbyROM += 2;						// next byte
	}
	return byChk;
}

static VOID CorrectChecksum(DWORD dwAddress)
{
	BYTE byChk;

	pbyRom[dwAddress+0] = 0;				// clear old checksum
	pbyRom[dwAddress+1] = 0;

	// recalculate checksum
	byChk = Checksum(pbyRom,dwAddress & ~0x7FFF);

	if (byChk > 0)							// adjust checksum
		byChk = -byChk;						// last addition with carry set
	else
		byChk = 1;							// last addition with carry clear

	pbyRom[dwAddress+(0^(dwAddress & 1))] = byChk & 0xF;
	pbyRom[dwAddress+(1^(dwAddress & 1))] = byChk >> 4;
	return;
}

static VOID RebuildRomChecksum(VOID)
{
	CorrectChecksum(CKSUM1);				// patch ROM chip 1st section
	CorrectChecksum(CKSUM2);				// patch ROM chip 2nd section
	CorrectChecksum(CKSUM3);				// patch ROM chip 3rd section
	CorrectChecksum(CKSUM4);				// patch ROM chip 4th section
	return;
}

static __inline BYTE Asc2Nib(BYTE c)
{
	if (c<'0') return 0;
	if (c<='9') return c-'0';
	if (c<'A') return 0;
	if (c<='F') return c-'A'+10;
	if (c<'a') return 0;
	if (c<='f') return c-'a'+10;
	return 0;
}

BOOL PatchRom(LPCTSTR szFilename)
{
	HANDLE hFile = NULL;
	DWORD  dwFileSizeLow = 0;
	DWORD  dwFileSizeHigh = 0;
	DWORD  lBytesRead = 0;
	PSZ    lpStop,lpBuf = NULL;
	DWORD  dwAddress = 0;
	UINT   nPos = 0;
	BOOL   bPatched = FALSE;
	BOOL   bSucc = TRUE;

	if (pbyRom == NULL) return FALSE;
	SetCurrentDirectory(szEmuDirectory);
	hFile = CreateFile(szFilename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	SetCurrentDirectory(szCurrentDirectory);
	if (hFile == INVALID_HANDLE_VALUE) return FALSE;
	dwFileSizeLow = GetFileSize(hFile, &dwFileSizeHigh);
	if (dwFileSizeHigh != 0 || dwFileSizeLow == 0)
	{ // file is too large or empty
		CloseHandle(hFile);
		return FALSE;
	}
	lpBuf = (PSZ) malloc(dwFileSizeLow+1);
	if (lpBuf == NULL)
	{
		CloseHandle(hFile);
		return FALSE;
	}
	ReadFile(hFile, lpBuf, dwFileSizeLow, &lBytesRead, NULL);
	CloseHandle(hFile);
	lpBuf[dwFileSizeLow] = 0;
	nPos = 0;
	while (lpBuf[nPos])
	{
		// skip whitespace characters
		nPos += (UINT) strspn(&lpBuf[nPos]," \t\n\r");

		if (lpBuf[nPos] == ';')				// comment?
		{
			do
			{
				nPos++;
				if (lpBuf[nPos] == '\n')
				{
					nPos++;
					break;
				}
			} while (lpBuf[nPos]);
			continue;
		}
		dwAddress = strtoul(&lpBuf[nPos], &lpStop, 16);
		nPos = (UINT) (lpStop - lpBuf);		// position of lpStop

		if (*lpStop != 0)					// data behind address
		{
			if (*lpStop != ':')				// invalid syntax
			{
				// skip to end of line
				while (lpBuf[nPos] != '\n' && lpBuf[nPos] != 0)
				{
					++nPos;
				}
				bSucc = FALSE;
				continue;
			}

			while (lpBuf[++nPos])
			{
				if (isxdigit(lpBuf[nPos]) == FALSE) break;
				if (dwAddress < dwRomSize)	// patch ROM
				{
					pbyRom[dwAddress] = Asc2Nib(lpBuf[nPos]);
					bPatched = TRUE;
				}
				++dwAddress;
			}
		}
	}
	_ASSERT(nPos <= dwFileSizeLow);			// buffer overflow?
	free(lpBuf);
	if (bPatched)							// ROM has been patched
	{
		RebuildRomChecksum();				// rebuild the ROM checksums
	}
	return bSucc;
}



//################
//#
//#    ROM
//#
//################

BOOL CrcRom(WORD *pwChk)					// calculate fingerprint of ROM
{
	DWORD *pdwData,dwSize;
	DWORD dwChk = 0;

	if (pbyRom == NULL) return TRUE;		// ROM CRC isn't available

	_ASSERT(pbyRom);						// view on ROM
	pdwData = (DWORD *) pbyRom;

	_ASSERT((dwRomSize % sizeof(*pdwData)) == 0);
	dwSize = dwRomSize / sizeof(*pdwData);	// file size in DWORD's

	// use checksum, because it's faster
	while (dwSize-- > 0)
	{
		DWORD dwData = *pdwData++;
		if ((dwData & 0xF0F0F0F0) != 0)		// data packed?
			return FALSE;
		dwChk += dwData;
	}

	*pwChk = (WORD) ((dwChk >> 16) + (dwChk & 0xFFFF));
	return TRUE;
}

BOOL MapRom(LPCTSTR szFilename)
{
	if (pbyRom != NULL) return FALSE;
	return MapFile(szFilename,&pbyRom,&dwRomSize);
}

VOID UnmapRom(VOID)
{
	if (pbyRom == NULL) return;
	free(pbyRom);
	pbyRom = NULL;
	dwRomSize = 0;
	wRomCrc = 0;
	return;
}



//################
//#
//#    Documents
//#
//################

BOOL IsDataPacked(VOID *pMem, DWORD dwSize)
{
	DWORD *pdwMem = (DWORD *) pMem;

	_ASSERT((dwSize % sizeof(DWORD)) == 0);
	if ((dwSize % sizeof(DWORD)) != 0) return TRUE;

	for (dwSize /= sizeof(DWORD); dwSize-- > 0;)
	{
		if ((*pdwMem++ & 0xF0F0F0F0) != 0)
			return TRUE;
	}
	return FALSE;
}

VOID CalcWarmstart(VOID)					// perform a =WARM1X if calculator ROM enabled
{
	if (Chipset.bOD == FALSE)				// ROM enabled
	{
		Chipset.pc = WARM1X;				// execute INIT: 1 (warmstart)
	}
	else
	{
		CpuReset();							// execute INIT: 3 (coldstart)
		Chipset.Shutdn = FALSE;				// automatic restart
	}
	return;
}

VOID ResetPortModule(PPORTDATA psData)
{
	_ASSERT(psData != NULL);

	// delete the data of my instance
	if (psData->psCfg)   free(psData->psCfg);
	if (psData->pszName) free(psData->pszName);
	if (psData->pbyData) free(psData->pbyData);
	if (psData->psTcp)
	{
		if (psData->psTcp->lpszAddrOut)
			free(psData->psTcp->lpszAddrOut);
		free(psData->psTcp);
	}
	free(psData);
	return;
}

VOID ResetPortData(PPORTDATA *ppsData)
{
	_ASSERT(ppsData != NULL);

	if (*ppsData != NULL)					// still data in the queue
	{
		ResetPortData(&(*ppsData)->pNext);	// delete successor
		ResetPortModule(*ppsData);			// delete the data of my instance
		*ppsData = NULL;
	}
	return;
}

VOID ResetDocument(VOID)
{
	UINT i;

	DisableDebugger();
	if (szCurrentKml[0])
	{
		KillKML();
	}
	if (hCurrentFile)
	{
		CloseHandle(hCurrentFile);
		hCurrentFile = NULL;
	}

	szCurrentKml[0] = 0;
	szCurrentFilename[0] = 0;
	ZeroMemory(&Chipset,sizeof(Chipset));

	DismountPorts();						// dismount the ports

	// init external port data
	for (i = 0; i < ARRAYSIZEOF(psExtPortData); ++i)
	{
		ResetPortData(&psExtPortData[i]);
	}

	// delete MMU mappings
	ZeroMemory(&RMap,sizeof(RMap));
	ZeroMemory(&WMap,sizeof(WMap));
	bDocumentAvail = FALSE;					// document not available
	return;
}

BOOL NewDocument(VOID)
{
	SaveBackup();
	ResetDocument();

	if (!DisplayChooseKml(0)) goto restore;
	if (!InitKML(szCurrentKml,FALSE)) goto restore;
	Chipset.type = cCurrentRomType;
	CrcRom(&Chipset.wRomCrc);				// save fingerprint of loaded ROM

	LoadBreakpointList(NULL);				// clear debugger breakpoint list
	bDocumentAvail = TRUE;					// document available
	return TRUE;
restore:
	RestoreBackup();
	ResetBackup();
	return FALSE;
}

BOOL OpenDocument(LPCTSTR szFilename)
{
	#define CHECKAREA(s,e) (offsetof(CHIPSET,e)-offsetof(CHIPSET,s)+sizeof(((CHIPSET *)NULL)->e))

	HANDLE hFile = INVALID_HANDLE_VALUE;
	DWORD  lBytesRead,lSizeofChipset,dwLength;
	BYTE   pbyFileSignature[sizeof(pbySignature)];
	UINT   i,ctBytesCompared;

	// Open file
	if (lstrcmpi(szCurrentFilename,szFilename) == 0)
	{
		if (YesNoMessage(_T("Do you want to reload this document?")) == IDNO)
			return TRUE;
	}

	SaveBackup();
	ResetDocument();

	hFile = CreateFile(szFilename, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		AbortMessage(_T("This file is missing or already loaded in another instance of Emu71."));
		goto restore;
	}

	// Read and Compare Emu71 1.0 format signature
	ReadFile(hFile, pbyFileSignature, sizeof(pbyFileSignature), &lBytesRead, NULL);
	for (ctBytesCompared=0; ctBytesCompared<sizeof(pbyFileSignature); ctBytesCompared++)
	{
		if (pbyFileSignature[ctBytesCompared]!=pbySignature[ctBytesCompared])
		{
			AbortMessage(_T("This file is not a valid Emu71 document."));
			goto restore;
		}
	}

	// read length of KML script name, no script name characters to skip
	ReadFile(hFile,&dwLength,sizeof(dwLength),&lBytesRead,NULL);

	// KML script name too long for file buffer
	if (dwLength >= ARRAYSIZEOF(szCurrentKml))
	{
		// skip heading KML script name characters until remainder fits into file buffer
		UINT nSkip = dwLength - (ARRAYSIZEOF(szCurrentKml) - 1);
		SetFilePointer(hFile, nSkip, NULL, FILE_CURRENT);

		dwLength = ARRAYSIZEOF(szCurrentKml) - 1;
	}
	#if defined _UNICODE
	{
		LPSTR szTmp = (LPSTR) malloc(dwLength);
		if (szTmp == NULL)
		{
			AbortMessage(_T("Memory Allocation Failure."));
			goto restore;
		}
		ReadFile(hFile, szTmp, dwLength, &lBytesRead, NULL);
		MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, szTmp, lBytesRead,
							szCurrentKml, ARRAYSIZEOF(szCurrentKml));
		free(szTmp);
	}
	#else
	{
		ReadFile(hFile, szCurrentKml, dwLength, &lBytesRead, NULL);
	}
	#endif
	if (dwLength != lBytesRead) goto read_err;
	szCurrentKml[dwLength] = 0;

	// read chipset size inside file
	ReadFile(hFile, &lSizeofChipset, sizeof(lSizeofChipset), &lBytesRead, NULL);
	if (lBytesRead != sizeof(lSizeofChipset)) goto read_err;
	if (lSizeofChipset <= sizeof(Chipset))	// actual or older chipset version
	{
		// read chipset content
		ZeroMemory(&Chipset,sizeof(Chipset)); // init chipset
		ReadFile(hFile, &Chipset, lSizeofChipset, &lBytesRead, NULL);
	}
	else									// newer chipset version
	{
		// read my used chipset content
		ReadFile(hFile, &Chipset, sizeof(Chipset), &lBytesRead, NULL);

		// skip rest of chipset
		SetFilePointer(hFile, lSizeofChipset-sizeof(Chipset), NULL, FILE_CURRENT);
		lSizeofChipset = sizeof(Chipset);
	}
	if (lBytesRead != lSizeofChipset) goto read_err;

	if (!isModelValid(Chipset.type))		// check for valid model in emulator state file
	{
		AbortMessage(_T("Emulator state file with invalid calculator model."));
		goto restore;
	}

	SetWindowLocation(hWnd,Chipset.nPosX,Chipset.nPosY);

	while (TRUE)
	{
		if (szCurrentKml[0])				// KML file name
		{
			BOOL bOK;

			bOK = InitKML(szCurrentKml,FALSE);
			bOK = bOK && (cCurrentRomType == Chipset.type);
			if (bOK) break;

			KillKML();
		}
		if (!DisplayChooseKml(Chipset.type))
			goto restore;
	}
	// reload old button state
	ReloadButtons(Chipset.Keyboard_Row,ARRAYSIZEOF(Chipset.Keyboard_Row));

	if (Chipset.bExtModulesPlugged)			// ports plugged
	{
		PORTPLUG sExtPlugInfo;				// port plugged info
		LPBYTE   pbyPlugData;
		DWORD    dwModules,dwIndex;
		BYTE     byType;
		BOOL     bSucc;

		// port plug info structure
		ReadFile(hFile,&sExtPlugInfo,sizeof(PORTPLUG),&lBytesRead,NULL);

		switch (sExtPlugInfo.dwPortRev)
		{
		case 0: // 2nd generation since v0.10
			// no. of module types
			dwModules = sExtPlugInfo.dwStructSize - sizeof(sExtPlugInfo);

			// read the module types
			pbyPlugData = (LPBYTE) malloc(dwModules);
			ReadFile(hFile,pbyPlugData,dwModules,&lBytesRead,NULL);
			break;
		case 6:	// 1st generation until v0.09
			{
				UINT nType[NOEXTPORTS];		// enum MODULETYPE

				_ASSERT(sExtPlugInfo.dwStructSize == 32);

				// read the module type area
				ReadFile(hFile,&nType,sizeof(nType),&lBytesRead,NULL);
				pbyPlugData = (LPBYTE) malloc(sizeof(nType)*2);

				// read the module data
				for (dwModules = 0, i = 0; i < ARRAYSIZEOF(nType); ++i)
				{
					for (; nType[i] != 0; nType[i] >>= 4)
					{
						pbyPlugData[dwModules++] = nType[i] & 0xF;
					}
					pbyPlugData[dwModules++] = TYPE_EMPTY;
				}
			}
			break;
		default:
			_ASSERT(FALSE);
			goto restore;					// error exit
		}

		// read the module data
		for (dwIndex = 0, i = 0; i < ARRAYSIZEOF(psExtPortData); ++i)
		{
			PPORTDATA *ppsData = &psExtPortData[i];

			while (dwIndex < dwModules && (byType = pbyPlugData[dwIndex++]) != TYPE_EMPTY)
			{
				switch (byType)
				{
				case TYPE_HRD:
				case TYPE_ROM:
					*ppsData = (PPORTDATA) calloc(1,sizeof(*psExtPortData[0]));

					// read the port data info block
					ReadFile(hFile,&(*ppsData)->sInfo,sizeof(psExtPortData[0]->sInfo),&lBytesRead,NULL);

					// read chip configuration data
					dwLength = sizeof(*psExtPortData[0]->psCfg) * (*ppsData)->sInfo.dwChips;
					(*ppsData)->psCfg = (PSATCFG) malloc(dwLength);
					ReadFile(hFile,(*ppsData)->psCfg,dwLength,&lBytesRead,NULL);

					// length of filename
					ReadFile(hFile,&(*ppsData)->dwNameSize,sizeof((*ppsData)->dwNameSize),&lBytesRead,NULL);

					// filename target buffer length
					dwLength = sizeof(TCHAR) * ((*ppsData)->dwNameSize + 1);

					// read the filename
					(*ppsData)->pszName = (LPTSTR) calloc(dwLength,1);

					#if defined _UNICODE
					{
						LPSTR szTmp = (LPSTR) malloc((*ppsData)->dwNameSize);
						if (szTmp != NULL)
						{
							ReadFile(hFile, szTmp, (*ppsData)->dwNameSize, &lBytesRead, NULL);
							MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, szTmp, lBytesRead,
												(*ppsData)->pszName, dwLength);
							free(szTmp);
						}
					}
					#else
					{
						ReadFile(hFile,(*ppsData)->pszName,(*ppsData)->dwNameSize,&lBytesRead,NULL);
					}
					#endif

					do
					{
						// load the file into memory
						bSucc = MapFile((*ppsData)->pszName,&(*ppsData)->pbyData,&dwLength);

						if (!bSucc || (*ppsData)->sInfo.dwSize != dwLength)
						{
							TCHAR szBuffer[MAX_PATH+128];

							wsprintf(szBuffer,
									 _T("The external file \"%s\" is corrupt or missing.\n\n")
									 _T("Do you want to load the file from a new location?"),
									 (*ppsData)->pszName);
							if (YesNoMessage(szBuffer) == IDYES)
							{
								if (GetOpenImageFile(hWnd,szBuffer,ARRAYSIZEOF(szBuffer)) == TRUE)
								{
									// data loaded -> free data
									if ((*ppsData)->pbyData != NULL)
									{
										free((*ppsData)->pbyData);
										(*ppsData)->pbyData = NULL;
									}

									// retry with new file name
									free((*ppsData)->pszName);
									(*ppsData)->dwNameSize = lstrlen(szBuffer);
									(*ppsData)->pszName = DuplicateString(szBuffer);
									bSucc = FALSE;
									continue;
								}
							}

							goto restore;	// error exit
						}
					}
					while (!bSucc);
					break;
				case TYPE_RAM:
					*ppsData = (PPORTDATA) calloc(1,sizeof(*psExtPortData[0]));

					// read the port data info block
					ReadFile(hFile,&(*ppsData)->sInfo,sizeof(psExtPortData[0]->sInfo),&lBytesRead,NULL);

					// read chip configuration data
					dwLength = sizeof(*psExtPortData[0]->psCfg) * (*ppsData)->sInfo.dwChips;
					(*ppsData)->psCfg = (PSATCFG) malloc(dwLength);
					ReadFile(hFile,(*ppsData)->psCfg,dwLength,&lBytesRead,NULL);

					(*ppsData)->pszName = NULL;

					// alloc the module memory
					(*ppsData)->pbyData = (LPBYTE) malloc((*ppsData)->sInfo.dwSize);

					// read the port data block
					ReadFile(hFile,(*ppsData)->pbyData,(*ppsData)->sInfo.dwSize,&lBytesRead,NULL);
					break;
				case TYPE_HPIL:
					// MM I/O buffer
					*ppsData = (PPORTDATA) calloc(1,sizeof(*psExtPortData[0]));

					// read the port data info block
					ReadFile(hFile,&(*ppsData)->sInfo,sizeof(psExtPortData[0]->sInfo),&lBytesRead,NULL);

					// read chip configuration data
					dwLength = sizeof(*psExtPortData[0]->psCfg) * (*ppsData)->sInfo.dwChips;
					(*ppsData)->psCfg = (PSATCFG) malloc(dwLength);
					ReadFile(hFile,(*ppsData)->psCfg,dwLength,&lBytesRead,NULL);

					// read length of TCP/IP server name
					VERIFY((*ppsData)->psTcp = (PPORTTCPIP) malloc(sizeof(*(*ppsData)->psTcp)));
					ReadFile(hFile,&(*ppsData)->psTcp->dwAddrSize,sizeof((*ppsData)->psTcp->dwAddrSize),&lBytesRead,NULL);

					// read the TCP/IP server name (always ANSI string)
					(*ppsData)->psTcp->lpszAddrOut = (LPSTR) malloc((*ppsData)->psTcp->dwAddrSize+1);
					ReadFile(hFile,(*ppsData)->psTcp->lpszAddrOut,(*ppsData)->psTcp->dwAddrSize,&lBytesRead,NULL);
					(*ppsData)->psTcp->lpszAddrOut[(*ppsData)->psTcp->dwAddrSize] = 0; // EOS

					// read the TCP/IP server out port
					ReadFile(hFile,&(*ppsData)->psTcp->wPortOut,sizeof((*ppsData)->psTcp->wPortOut),&lBytesRead,NULL);

					// read the TCP/IP server in port
					ReadFile(hFile,&(*ppsData)->psTcp->wPortIn,sizeof((*ppsData)->psTcp->wPortIn),&lBytesRead,NULL);

					// alloc the MM I/O buffer data
					(*ppsData)->pbyData = (LPBYTE) malloc((*ppsData)->sInfo.dwSize);

					// read the MM I/O buffer data
					ReadFile(hFile,(*ppsData)->pbyData,(*ppsData)->sInfo.dwSize,&lBytesRead,NULL);
					break;
				default: _ASSERT(FALSE);
				}

				ppsData = &(*ppsData)->pNext;
			}
			_ASSERT(*ppsData == NULL);
		}
		free(pbyPlugData);
		_ASSERT(dwIndex == dwModules);
	}

	if (Chipset.wRomCrc != wRomCrc)			// ROM changed
	{
		CalcWarmstart();					// if calculator ROM enabled do a warmstart else a coldstart
	}

	// check CPU main registers
	if (IsDataPacked(Chipset.A,CHECKAREA(A,R4))) goto read_err;

	// 1LF3 RAM data area
	for (i = 0; i < ARRAYSIZEOF(Chipset.dd); ++i)
	{
		if (IsDataPacked(Chipset.dd[i].Ram,  ARRAYSIZEOF(Chipset.dd[0].Ram)))   goto read_err;
		if (IsDataPacked(Chipset.dd[i].IORam,ARRAYSIZEOF(Chipset.dd[0].IORam))) goto read_err;
	}

	// 1LG8 RAM data area
	for (i = 0; i < ARRAYSIZEOF(Chipset.ir); ++i)
	{
		if (IsDataPacked(Chipset.ir[i].Ram,ARRAYSIZEOF(Chipset.ir[0].Ram))) goto read_err;
	}

	if (Chipset.bExtModulesPlugged)			// ports plugged
	{
		// external port data area
		for (i = 0; i < ARRAYSIZEOF(psExtPortData); ++i)
		{
			PPORTDATA psData = psExtPortData[i];

			while (psData != NULL)			// walk through all modules
			{
				if (psData->sInfo.nType != TYPE_HPIL)
				{
					_ASSERT(psData->pbyData != NULL);
					if (IsDataPacked(psData->pbyData,psData->sInfo.dwSize)) goto read_err;
				}
				psData = psData->pNext;
			}
		}

		MountPorts();						// mount the ports
	}

	LoadBreakpointList(hFile);				// load debugger breakpoint list

	lstrcpy(szCurrentFilename, szFilename);
	_ASSERT(hCurrentFile == NULL);
	hCurrentFile = hFile;
	#if defined _USRDLL						// DLL version
		// notify main proc about current document file
		if (pEmuDocumentNotify) pEmuDocumentNotify(szCurrentFilename);
	#endif
	SetWindowPathTitle(szCurrentFilename);	// update window title line
	bDocumentAvail = TRUE;					// document available
	return TRUE;

read_err:
	AbortMessage(_T("This file must be truncated, and cannot be loaded."));
restore:
	if (INVALID_HANDLE_VALUE != hFile)		// close if valid handle
		CloseHandle(hFile);

	// reset external port data
	for (i = 0; i < ARRAYSIZEOF(psExtPortData); ++i)
	{
		ResetPortData(&psExtPortData[i]);
	}

	RestoreBackup();
	ResetBackup();
	return FALSE;
	#undef CHECKAREA
}

BOOL SaveDocument(VOID)
{
	PORTPLUG		sExtPlugInfo;			// port plugged info
	PPORTDATA       psData;
	DWORD           lBytesWritten;
	DWORD           lSizeofChipset;
	DWORD           dwModules;
	UINT            i,nLength;
	WINDOWPLACEMENT wndpl;

	if (hCurrentFile == NULL) return FALSE;

	_ASSERT(hWnd);							// window open
	wndpl.length = sizeof(wndpl);			// update saved window position
	GetWindowPlacement(hWnd, &wndpl);
	Chipset.nPosX = (SWORD) wndpl.rcNormalPosition.left;
	Chipset.nPosY = (SWORD) wndpl.rcNormalPosition.top;

	SetFilePointer(hCurrentFile,0,NULL,FILE_BEGIN);

	if (!WriteFile(hCurrentFile, pbySignature, sizeof(pbySignature), &lBytesWritten, NULL))
	{
		AbortMessage(_T("Could not write into file !"));
		return FALSE;
	}

	CrcRom(&Chipset.wRomCrc);				// save fingerprint of ROM

	// count the number of plugged modules
	for (dwModules = 0, i = 0; i < ARRAYSIZEOF(psExtPortData); ++i)
	{
		// walk through all modules in a slot
		for (psData = psExtPortData[i]; psData != NULL; psData = psData->pNext)
		{
			++dwModules;
		}
	}

	// any external module plugged
	Chipset.bExtModulesPlugged = (dwModules != 0);

	nLength = lstrlen(szCurrentKml);
	WriteFile(hCurrentFile, &nLength, sizeof(nLength), &lBytesWritten, NULL);
	#if defined _UNICODE
	{
		LPSTR szTmp = (LPSTR) malloc(nLength);
		if (szTmp != NULL)
		{
			WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK,
								szCurrentKml, nLength,
								szTmp, nLength, NULL, NULL);
			WriteFile(hCurrentFile, szTmp, nLength, &lBytesWritten, NULL);
			free(szTmp);
		}
	}
	#else
	{
		WriteFile(hCurrentFile, szCurrentKml, nLength, &lBytesWritten, NULL);
	}
	#endif
	lSizeofChipset = sizeof(Chipset);
	WriteFile(hCurrentFile,&lSizeofChipset,sizeof(lSizeofChipset),&lBytesWritten,NULL);
	WriteFile(hCurrentFile,&Chipset,lSizeofChipset,&lBytesWritten,NULL);

	if (Chipset.bExtModulesPlugged)			// ports plugged
	{
		LPBYTE	pbyPlugData;

		// add the TYPE_EMPTY delimiter for each port slot
		dwModules += ARRAYSIZEOF(psExtPortData);

		// write port plug info structure head
		sExtPlugInfo.dwStructSize = sizeof(sExtPlugInfo) + dwModules;
		sExtPlugInfo.dwPortRev = 0;
		WriteFile(hCurrentFile,&sExtPlugInfo,sizeof(sExtPlugInfo),&lBytesWritten,NULL);

		// allocate the module type buffer
		pbyPlugData = (LPBYTE) malloc(dwModules);

		// build port plug info structure body
		for (nLength = 0, i = 0; i < ARRAYSIZEOF(psExtPortData); ++i)
		{
			// walk through all modules
			for (psData = psExtPortData[i]; psData != NULL; psData = psData->pNext)
			{
				pbyPlugData[nLength++] = psData->sInfo.nType;
			}
			pbyPlugData[nLength++] = TYPE_EMPTY;
		}
		_ASSERT(nLength == dwModules);

		// write port plug info structure body
		WriteFile(hCurrentFile,pbyPlugData,dwModules,&lBytesWritten,NULL);
		free(pbyPlugData);

		// save the module data
		for (i = 0; i < ARRAYSIZEOF(psExtPortData); ++i)
		{
			psData = psExtPortData[i];

			while (psData != NULL)			// walk through all modules
			{
				switch (psData->sInfo.nType)
				{
				case TYPE_HRD:
				case TYPE_ROM:
					// write the port data info block
					WriteFile(hCurrentFile,&psData->sInfo,sizeof(psExtPortData[0]->sInfo),&lBytesWritten,NULL);

					// write chip configuration data
					WriteFile(hCurrentFile,
							  psData->psCfg,
							  sizeof(*psExtPortData[0]->psCfg) * psData->sInfo.dwChips,
							  &lBytesWritten,
							  NULL);

					// length of filename
					WriteFile(hCurrentFile,&psData->dwNameSize,sizeof(psData->dwNameSize),&lBytesWritten,NULL);

					// write the filename
					#if defined _UNICODE
					{
						LPSTR szTmp = (LPSTR) malloc(psData->dwNameSize);
						if (szTmp != NULL)
						{
							WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK,
												psData->pszName, psData->dwNameSize,
												szTmp, psData->dwNameSize, NULL, NULL);
							WriteFile(hCurrentFile,szTmp,psData->dwNameSize, &lBytesWritten,NULL);
							free(szTmp);
						}
					}
					#else
					{
						WriteFile(hCurrentFile,psData->pszName,psData->dwNameSize,&lBytesWritten,NULL);
					}
					#endif
					break;
				case TYPE_RAM:
					// write the port data info block
					WriteFile(hCurrentFile,&psData->sInfo,sizeof(psExtPortData[0]->sInfo),&lBytesWritten,NULL);

					// write chip configuration data
					WriteFile(hCurrentFile,
							  psData->psCfg,
							  sizeof(*psExtPortData[0]->psCfg) * psData->sInfo.dwChips,
							  &lBytesWritten,
							  NULL);

					// write the port data block
					WriteFile(hCurrentFile,psData->pbyData,psData->sInfo.dwSize,&lBytesWritten,NULL);
					break;
				case TYPE_HPIL:
					// write the port data info block
					WriteFile(hCurrentFile,&psData->sInfo,sizeof(psExtPortData[0]->sInfo),&lBytesWritten,NULL);

					// write chip configuration data
					WriteFile(hCurrentFile,
							  psData->psCfg,
							  sizeof(*psExtPortData[0]->psCfg) * psData->sInfo.dwChips,
							  &lBytesWritten,
							  NULL);

					// write length of TCP/IP server name
					_ASSERT(psData->psTcp != NULL);
					WriteFile(hCurrentFile,&psData->psTcp->dwAddrSize,sizeof(psData->psTcp->dwAddrSize),&lBytesWritten,NULL);

					// write the TCP/IP server name (always ANSI string)
					WriteFile(hCurrentFile,psData->psTcp->lpszAddrOut,psData->psTcp->dwAddrSize,&lBytesWritten,NULL);

					// write the TCP/IP server out port
					WriteFile(hCurrentFile,&psData->psTcp->wPortOut,sizeof(psData->psTcp->wPortOut),&lBytesWritten,NULL);

					// write the TCP/IP server in port
					WriteFile(hCurrentFile,&psData->psTcp->wPortIn,sizeof(psData->psTcp->wPortIn),&lBytesWritten,NULL);

					// write the MM I/O buffer data
					WriteFile(hCurrentFile,psData->pbyData,psData->sInfo.dwSize,&lBytesWritten,NULL);
					break;
				default: _ASSERT(FALSE);
				}

				psData = psData->pNext;
			}
			_ASSERT(psData == NULL);
		}
	}

	SaveBreakpointList(hCurrentFile);		// save debugger breakpoint list
	SetEndOfFile(hCurrentFile);				// cut the rest
	return TRUE;
}

BOOL SaveDocumentAs(LPCTSTR szFilename)
{
	HANDLE hFile;

	if (hCurrentFile)						// already file in use
	{
		CloseHandle(hCurrentFile);			// close it, even it's same, so data always will be saved
		hCurrentFile = NULL;
	}
	hFile = CreateFile(szFilename, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)		// error, couldn't create a new file
	{
		AbortMessage(_T("This file must be currently used by another instance of Emu71."));
		return FALSE;
	}
	lstrcpy(szCurrentFilename, szFilename);	// save new file name
	hCurrentFile = hFile;					// and the corresponding handle
	#if defined _USRDLL						// DLL version
		// notify main proc about current document file
		if (pEmuDocumentNotify) pEmuDocumentNotify(szCurrentFilename);
	#endif
	SetWindowPathTitle(szCurrentFilename);	// update window title line
	return SaveDocument();					// save current content
}



//################
//#
//#    Backup
//#
//################

BOOL SaveBackup(VOID)
{
	WINDOWPLACEMENT wndpl;
	UINT i;

	if (!bDocumentAvail) return FALSE;

	_ASSERT(nState != SM_RUN);				// emulation engine is running

	// save window position
	_ASSERT(hWnd);							// window open
	wndpl.length = sizeof(wndpl);			// update saved window position
	GetWindowPlacement(hWnd, &wndpl);
	Chipset.nPosX = (SWORD) wndpl.rcNormalPosition.left;
	Chipset.nPosY = (SWORD) wndpl.rcNormalPosition.top;

	lstrcpy(szBackupFilename, szCurrentFilename);
	lstrcpy(szBackupKml, szCurrentKml);
	CopyMemory(&BackupChipset, &Chipset, sizeof(Chipset));

	for (i = 0; i < ARRAYSIZEOF(psExtPortDataBackup); ++i)
	{
		PPORTDATA psData,*ppsDataBackup;

		_ASSERT(i < ARRAYSIZEOF(psExtPortData));

		// puge old backup content first
		ResetPortData(&psExtPortDataBackup[i]);

		// create backup of port
		psData        = psExtPortData[i];
		ppsDataBackup = &psExtPortDataBackup[i];

		while (psData != NULL)				// walk through all modules
		{
			*ppsDataBackup = (PPORTDATA) calloc(1,sizeof(*psExtPortDataBackup[0]));

			_ASSERT(sizeof(psExtPortData[0]->sInfo) == sizeof(psExtPortDataBackup[0]->sInfo));
			CopyMemory(&(*ppsDataBackup)->sInfo,&psData->sInfo,sizeof(psExtPortData[0]->sInfo));

			if (psData->psCfg != NULL)
			{
				// chip configuration data
				SIZE_T dwBytes = sizeof(*psExtPortData[0]->psCfg) * psData->sInfo.dwChips;
				(*ppsDataBackup)->psCfg = (PSATCFG) malloc(dwBytes);
				CopyMemory((*ppsDataBackup)->psCfg,psData->psCfg,dwBytes);
			}
			if (psData->pszName != NULL)
			{
				(*ppsDataBackup)->dwNameSize = lstrlen(psData->pszName);
				(*ppsDataBackup)->pszName = DuplicateString(psData->pszName);
			}
			if (psData->pbyData != NULL)
			{
				(*ppsDataBackup)->pbyData = (LPBYTE) malloc(psData->sInfo.dwSize);
				CopyMemory((*ppsDataBackup)->pbyData,psData->pbyData,psData->sInfo.dwSize);
			}
			if (psData->psTcp != NULL)
			{
				(*ppsDataBackup)->psTcp = (PPORTTCPIP) malloc(sizeof(*(*ppsDataBackup)->psTcp));
				(*ppsDataBackup)->psTcp->dwAddrSize = psData->psTcp->dwAddrSize;
				(*ppsDataBackup)->psTcp->lpszAddrOut = (LPSTR) malloc(psData->psTcp->dwAddrSize+1);
				CopyMemory((*ppsDataBackup)->psTcp->lpszAddrOut,psData->psTcp->lpszAddrOut,psData->psTcp->dwAddrSize+1);
				(*ppsDataBackup)->psTcp->wPortOut = psData->psTcp->wPortOut;
				(*ppsDataBackup)->psTcp->wPortIn  = psData->psTcp->wPortIn;
			}

			psData        = psData->pNext;
			ppsDataBackup = &(*ppsDataBackup)->pNext;
		}
	}
	CreateBackupBreakpointList();
	bBackup = TRUE;
	return TRUE;
}

BOOL RestoreBackup(VOID)
{
	BOOL bDbgOpen;
	UINT i;

	if (!bBackup) return FALSE;

	bDbgOpen = (nDbgState != DBG_OFF);		// debugger window open?
	ResetDocument();
	// need chipset for contrast setting in InitKML()
	Chipset.dd[MASTER].IORam[DCONTR & 0xFF] = BackupChipset.dd[MASTER].IORam[DCONTR & 0xFF];
	if (!InitKML(szBackupKml,TRUE))
	{
		InitKML(szCurrentKml,TRUE);
		return FALSE;
	}
	lstrcpy(szCurrentKml, szBackupKml);
	lstrcpy(szCurrentFilename, szBackupFilename);
	if (szCurrentFilename[0])
	{
		hCurrentFile = CreateFile(szCurrentFilename, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hCurrentFile == INVALID_HANDLE_VALUE)
		{
			hCurrentFile = NULL;
			szCurrentFilename[0] = 0;
		}
	}
	CopyMemory(&Chipset, &BackupChipset, sizeof(Chipset));

	for (i = 0; i < ARRAYSIZEOF(psExtPortData); ++i)
	{
		PPORTDATA *ppsData,psDataBackup;

		_ASSERT(i < ARRAYSIZEOF(psExtPortDataBackup));

		psDataBackup = psExtPortDataBackup[i];
		ppsData      = &psExtPortData[i];

		while (psDataBackup != NULL)		// walk through all modules
		{
			*ppsData = (PPORTDATA) calloc(1,sizeof(*psExtPortData[0]));

			// restore port info block
			CopyMemory(&(*ppsData)->sInfo,&psDataBackup->sInfo,sizeof(psExtPortData[0]->sInfo));

			// create port
			if (psDataBackup->psCfg != NULL)
			{
				// chip configuration data
				SIZE_T dwBytes = sizeof(*psExtPortDataBackup[0]->psCfg) * psDataBackup->sInfo.dwChips;
				(*ppsData)->psCfg = (PSATCFG) malloc(dwBytes);
				CopyMemory((*ppsData)->psCfg,psDataBackup->psCfg,dwBytes);
			}
			if (psDataBackup->pszName != NULL)
			{
				(*ppsData)->dwNameSize = lstrlen(psDataBackup->pszName);
				(*ppsData)->pszName = DuplicateString(psDataBackup->pszName);
			}
			if (psDataBackup->pbyData != NULL)
			{
				(*ppsData)->pbyData = (LPBYTE) malloc(psDataBackup->sInfo.dwSize);
				CopyMemory((*ppsData)->pbyData,psDataBackup->pbyData,psDataBackup->sInfo.dwSize);
			}
			if (psDataBackup->psTcp != NULL)
			{
				(*ppsData)->psTcp = (PPORTTCPIP) malloc(sizeof(*(*ppsData)->psTcp));
				(*ppsData)->psTcp->dwAddrSize = psDataBackup->psTcp->dwAddrSize;
				(*ppsData)->psTcp->lpszAddrOut = (LPSTR) malloc(psDataBackup->psTcp->dwAddrSize+1);
				CopyMemory((*ppsData)->psTcp->lpszAddrOut,psDataBackup->psTcp->lpszAddrOut,psDataBackup->psTcp->dwAddrSize+1);
				(*ppsData)->psTcp->wPortOut = psDataBackup->psTcp->wPortOut;
				(*ppsData)->psTcp->wPortIn  = psDataBackup->psTcp->wPortIn;
			}

			psDataBackup = psDataBackup->pNext;
			ppsData      = &(*ppsData)->pNext;
		}
	}

	MountPorts();							// mount the ports
	SetWindowPathTitle(szCurrentFilename);	// update window title line
	SetWindowLocation(hWnd,Chipset.nPosX,Chipset.nPosY);
	RestoreBackupBreakpointList();			// restore the debugger breakpoint list
	if (bDbgOpen) OnToolDebug();			// reopen the debugger
	bDocumentAvail = TRUE;					// document available
	return TRUE;
}

BOOL ResetBackup(VOID)
{
	UINT i;

	if (!bBackup) return FALSE;
	szBackupFilename[0] = 0;
	szBackupKml[0] = 0;
	ZeroMemory(&BackupChipset,sizeof(BackupChipset));
	for (i = 0; i < ARRAYSIZEOF(psExtPortDataBackup); ++i)
	{
		ResetPortData(&psExtPortDataBackup[i]);
	}
	bBackup = FALSE;
	return TRUE;
}



//################
//#
//#    Open File Common Dialog Boxes
//#
//################

static VOID InitializeOFN(LPOPENFILENAME ofn)
{
	ZeroMemory((LPVOID)ofn, sizeof(OPENFILENAME));
	ofn->lStructSize = sizeof(OPENFILENAME);
	ofn->hwndOwner = hWnd;
	ofn->Flags = OFN_EXPLORER|OFN_HIDEREADONLY;
	return;
}

BOOL GetOpenFilename(VOID)
{
	TCHAR szBuffer[ARRAYSIZEOF(szBufferFilename)];
	OPENFILENAME ofn;

	InitializeOFN(&ofn);
	ofn.lpstrFilter =
		_T("Emu71 Files (*.e71)\0*.e71\0")
		_T("All Files (*.*)\0*.*\0");
	ofn.nFilterIndex = 1;					// default
	ofn.lpstrFile = szBuffer;
	ofn.lpstrFile[0] = 0;
	ofn.nMaxFile = ARRAYSIZEOF(szBuffer);
	ofn.Flags |= OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
	if (GetOpenFileName(&ofn) == FALSE) return FALSE;
	_ASSERT(ARRAYSIZEOF(szBufferFilename) == ofn.nMaxFile);
	lstrcpy(szBufferFilename, ofn.lpstrFile);
	return TRUE;
}

BOOL GetSaveAsFilename(VOID)
{
	TCHAR szBuffer[ARRAYSIZEOF(szBufferFilename)];
	OPENFILENAME ofn;

	InitializeOFN(&ofn);
	ofn.lpstrFilter =
		_T("HP-71B Files (*.e71)\0*.e71\0")
		_T("All Files (*.*)\0*.*\0");
	ofn.nFilterIndex = 2;					// default
	if (cCurrentRomType == 'T')				// Titan, HP71B
	{
		ofn.lpstrDefExt = _T("e71");
		ofn.nFilterIndex = 1;
	}
	ofn.lpstrFile = szBuffer;
	ofn.lpstrFile[0] = 0;
	ofn.nMaxFile = ARRAYSIZEOF(szBuffer);
	ofn.Flags |= OFN_CREATEPROMPT|OFN_OVERWRITEPROMPT;
	if (GetSaveFileName(&ofn) == FALSE) return FALSE;
	_ASSERT(ARRAYSIZEOF(szBufferFilename) == ofn.nMaxFile);
	lstrcpy(szBufferFilename, ofn.lpstrFile);
	return TRUE;
}



//################
//#
//#    Load Icon
//#
//################

BOOL LoadIconFromFile(LPCTSTR szFilename)
{
	HANDLE hIcon;

	SetCurrentDirectory(szEmuDirectory);
	// not necessary to destroy because icon is shared
	hIcon = LoadImage(NULL, szFilename, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE|LR_LOADFROMFILE|LR_SHARED);
	SetCurrentDirectory(szCurrentDirectory);

	if (hIcon)
	{
		SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM) hIcon);
		SendMessage(hWnd, WM_SETICON, ICON_BIG,   (LPARAM) hIcon);
	}
	return hIcon != NULL;
}

VOID LoadIconDefault(VOID)
{
	// use window class icon
	SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM) NULL);
	SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) NULL);
	return;
}



//################
//#
//#    Load Bitmap
//#
//################

#define WIDTHBYTES(bits) (((bits) + 31) / 32 * 4)

typedef struct _BmpFile
{
	DWORD  dwPos;							// actual reading pos
	DWORD  dwFileSize;						// file size
	LPBYTE pbyFile;							// buffer
} BMPFILE, FAR *LPBMPFILE, *PBMPFILE;

static __inline WORD DibNumColors(BITMAPINFOHEADER CONST *lpbi)
{
	if (lpbi->biClrUsed != 0) return (WORD) lpbi->biClrUsed;

	/* a 24 bitcount DIB has no color table */
	return (lpbi->biBitCount <= 8) ? (1 << lpbi->biBitCount) : 0;
}

static HPALETTE CreateBIPalette(BITMAPINFOHEADER CONST *lpbi)
{
	LOGPALETTE* pPal;
	HPALETTE    hpal = NULL;
	WORD        wNumColors;
	BYTE        red;
	BYTE        green;
	BYTE        blue;
	UINT        i;
	RGBQUAD*    pRgb;

	if (!lpbi)
		return NULL;

	if (lpbi->biSize != sizeof(BITMAPINFOHEADER))
		return NULL;

	// Get a pointer to the color table and the number of colors in it
	pRgb = (RGBQUAD FAR *)((LPBYTE)lpbi + (WORD)lpbi->biSize);
	wNumColors = DibNumColors(lpbi);

	if (wNumColors)
	{
		// Allocate for the logical palette structure
		pPal = (PLOGPALETTE) malloc(sizeof(LOGPALETTE) + wNumColors * sizeof(PALETTEENTRY));
		if (!pPal) return NULL;

		pPal->palVersion    = 0x300;
		pPal->palNumEntries = wNumColors;

		// Fill in the palette entries from the DIB color table and
		// create a logical color palette.
		for (i = 0; i < pPal->palNumEntries; i++)
		{
			pPal->palPalEntry[i].peRed   = pRgb[i].rgbRed;
			pPal->palPalEntry[i].peGreen = pRgb[i].rgbGreen;
			pPal->palPalEntry[i].peBlue  = pRgb[i].rgbBlue;
			pPal->palPalEntry[i].peFlags = (BYTE)0;
		}
		hpal = CreatePalette(pPal);
		free(pPal);
	}
	else
	{
		// create halftone palette for 16, 24 and 32 bitcount bitmaps

		// 16, 24 and 32 bitcount DIB's have no color table entries so, set the
		// number of to the maximum value (256).
		wNumColors = 256;
		pPal = (PLOGPALETTE) malloc(sizeof(LOGPALETTE) + wNumColors * sizeof(PALETTEENTRY));
		if (!pPal) return NULL;

		pPal->palVersion    = 0x300;
		pPal->palNumEntries = wNumColors;

		red = green = blue = 0;

		// Generate 256 (= 8*8*4) RGB combinations to fill the palette
		// entries.
		for (i = 0; i < pPal->palNumEntries; i++)
		{
			pPal->palPalEntry[i].peRed   = red;
			pPal->palPalEntry[i].peGreen = green;
			pPal->palPalEntry[i].peBlue  = blue;
			pPal->palPalEntry[i].peFlags = 0;

			if (!(red += 32))
				if (!(green += 32))
					blue += 64;
		}
		hpal = CreatePalette(pPal);
		free(pPal);
	}
	return hpal;
}

static HBITMAP DecodeBmp(LPBMPFILE pBmp,BOOL bPalette)
{
	LPBITMAPFILEHEADER pBmfh;
	LPBITMAPINFO pBmi;
	HBITMAP hBitmap;
	DWORD   dwFileSize;

	hBitmap = NULL;

	// size of bitmap header information
	dwFileSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	if (pBmp->dwFileSize < dwFileSize) return NULL;

	// check for bitmap
	pBmfh = (LPBITMAPFILEHEADER) pBmp->pbyFile;
	if (pBmfh->bfType != 0x4D42) return NULL; // "BM"

	pBmi = (LPBITMAPINFO) (pBmp->pbyFile + sizeof(BITMAPFILEHEADER));

	// size with color table
	if (pBmi->bmiHeader.biCompression == BI_BITFIELDS)
	{
		dwFileSize += 3 * sizeof(DWORD);
	}
	else
	{
		dwFileSize += DibNumColors(&pBmi->bmiHeader) * sizeof(RGBQUAD);
	}
	if (dwFileSize != pBmfh->bfOffBits) return NULL;

	// size with bitmap data
	if (pBmi->bmiHeader.biCompression != BI_RGB)
	{
		dwFileSize += pBmi->bmiHeader.biSizeImage;
	}
	else
	{
		dwFileSize += WIDTHBYTES(pBmi->bmiHeader.biWidth * pBmi->bmiHeader.biBitCount)
					* labs(pBmi->bmiHeader.biHeight);
	}
	if (pBmp->dwFileSize < dwFileSize) return NULL;

	VERIFY(hBitmap = CreateDIBitmap(
		hWindowDC,
		&pBmi->bmiHeader,
		CBM_INIT,
		pBmp->pbyFile + pBmfh->bfOffBits,
		pBmi, DIB_RGB_COLORS));
	if (hBitmap == NULL) return NULL;

	if (bPalette && hPalette == NULL)
	{
		hPalette = CreateBIPalette(&pBmi->bmiHeader);
		// save old palette
		hOldPalette = SelectPalette(hWindowDC, hPalette, FALSE);
		RealizePalette(hWindowDC);
	}
	return hBitmap;
}

static BOOL ReadGifByte(LPBMPFILE pGif, INT *n)
{
	// outside GIF file
	if (pGif->dwPos >= pGif->dwFileSize)
		return TRUE;

	*n = pGif->pbyFile[pGif->dwPos++];
	return FALSE;
}

static BOOL ReadGifWord(LPBMPFILE pGif, INT *n)
{
	// outside GIF file
	if (pGif->dwPos + 1 >= pGif->dwFileSize)
		return TRUE;

	*n = pGif->pbyFile[pGif->dwPos++];
	*n |= (pGif->pbyFile[pGif->dwPos++] << 8);
	return FALSE;
}

static HBITMAP DecodeGif(LPBMPFILE pBmp,DWORD *pdwTransparentColor,BOOL bPalette)
{
	// this implementation base on the GIF image file
	// decoder engine of Free42 (c) by Thomas Okken

	HBITMAP hBitmap;

	typedef struct cmap
	{
		WORD    biBitCount;					// bits used in color map
		DWORD   biClrUsed;					// no of colors in color map
		RGBQUAD bmiColors[256];				// color map
	} CMAP;

	BOOL    bHasGlobalCmap;
	CMAP    sGlb;							// data of global color map

	INT     nWidth,nHeight,nInfo,nBackground,nZero;
	LONG    lBytesPerLine;

	LPBYTE  pbyPixels;

	BITMAPINFO bmi;							// global bitmap info

	BOOL bDecoding = TRUE;

	hBitmap = NULL;

	pBmp->dwPos = 6;						// position behind GIF header

	/* Bits 6..4 of info contain one less than the "color resolution",
	 * defined as the number of significant bits per RGB component in
	 * the source image's color palette. If the source image (from
	 * which the GIF was generated) was 24-bit true color, the color
	 * resolution is 8, so the value in bits 6..4 is 7. If the source
	 * image had an EGA color cube (2x2x2), the color resolution would
	 * be 2, etc.
	 * Bit 3 of info must be zero in GIF87a; in GIF89a, if it is set,
	 * it indicates that the global colormap is sorted, the most
	 * important entries being first. In PseudoColor environments this
	 * can be used to make sure to get the most important colors from
	 * the X server first, to optimize the image's appearance in the
	 * event that not all the colors from the colormap can actually be
	 * obtained at the same time.
	 * The 'zero' field is always 0 in GIF87a; in GIF89a, it indicates
	 * the pixel aspect ratio, as (PAR + 15) : 64. If PAR is zero,
	 * this means no aspect ratio information is given, PAR = 1 means
	 * 1:4 (narrow), PAR = 49 means 1:1 (square), PAR = 255 means
	 * slightly over 4:1 (wide), etc.
	 */

	if (   ReadGifWord(pBmp,&nWidth)
		|| ReadGifWord(pBmp,&nHeight)
		|| ReadGifByte(pBmp,&nInfo)
		|| ReadGifByte(pBmp,&nBackground)
		|| ReadGifByte(pBmp,&nZero)
		|| nZero != 0)
		goto quit;

	ZeroMemory(&bmi,sizeof(bmi));			// init bitmap info
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = nWidth;
	bmi.bmiHeader.biHeight = nHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;			// create a true color DIB
	bmi.bmiHeader.biCompression = BI_RGB;

	ZeroMemory(&sGlb,sizeof(sGlb));			// init global color map
	bHasGlobalCmap = (nInfo & 0x80) != 0;

	sGlb.biBitCount = (nInfo & 7) + 1;		// bits used in global color map
	sGlb.biClrUsed = (1 << sGlb.biBitCount); // no of colors in global color map

	// color table should not exceed 256 colors
	_ASSERT(sGlb.biClrUsed <= ARRAYSIZEOF(sGlb.bmiColors));

	if (bHasGlobalCmap)						// global color map
	{
		DWORD i;

		for (i = 0; i < sGlb.biClrUsed; ++i)
		{
			int r, g, b;

			if (ReadGifByte(pBmp,&r) || ReadGifByte(pBmp,&g) || ReadGifByte(pBmp,&b))
				goto quit;

			sGlb.bmiColors[i].rgbRed   = r;
			sGlb.bmiColors[i].rgbGreen = g;
			sGlb.bmiColors[i].rgbBlue  = b;
		}
	}
	else									// no color map
	{
		DWORD i;

		for (i = 0; i < sGlb.biClrUsed; ++i)
		{
			BYTE k = (BYTE) ((i * sGlb.biClrUsed) / (sGlb.biClrUsed - 1));

			sGlb.bmiColors[i].rgbRed   = k;
			sGlb.bmiColors[i].rgbGreen = k;
			sGlb.bmiColors[i].rgbBlue  = k;
		}
	}

	// bitmap dimensions
	lBytesPerLine = WIDTHBYTES(bmi.bmiHeader.biWidth * bmi.bmiHeader.biBitCount);
	bmi.bmiHeader.biSizeImage = lBytesPerLine * bmi.bmiHeader.biHeight;

	// create top-down DIB
	bmi.bmiHeader.biHeight = -bmi.bmiHeader.biHeight;

	// allocate buffer for pixels
	VERIFY(hBitmap = CreateDIBSection(hWindowDC,
									  &bmi,
									  DIB_RGB_COLORS,
									  (VOID **)&pbyPixels,
									  NULL,
									  0));
	if (hBitmap == NULL) goto quit;

	// fill pixel buffer with background color
	for (nHeight = 0; nHeight < labs(bmi.bmiHeader.biHeight); ++nHeight)
	{
		LPBYTE pbyLine = pbyPixels + nHeight * lBytesPerLine;

		for (nWidth = 0; nWidth < bmi.bmiHeader.biWidth; ++nWidth)
		{
			*pbyLine++ = sGlb.bmiColors[nBackground].rgbBlue;
			*pbyLine++ = sGlb.bmiColors[nBackground].rgbGreen;
			*pbyLine++ = sGlb.bmiColors[nBackground].rgbRed;
		}

		_ASSERT((DWORD) (pbyLine - pbyPixels) <= bmi.bmiHeader.biSizeImage);
	}

	while (bDecoding)
	{
		INT nBlockType;

		if (ReadGifByte(pBmp,&nBlockType)) goto quit;

		switch (nBlockType)
		{
		case ',': // image
			{
				CMAP sAct;					// data of actual color map

				INT  nLeft,nTop,nWidth,nHeight;
				INT  i,nInfo;

				BOOL bInterlaced;
				INT h,v;
				INT nCodesize;				// LZW codesize in bits
				INT nBytecount;

				SHORT prefix_table[4096];
				SHORT code_table[4096];

				INT  nMaxcode;
				INT  nClearCode;
				INT  nEndCode;

				INT  nCurrCodesize;
				INT  nCurrCode;
				INT  nOldCode;
				INT  nBitsNeeded;
				BOOL bEndCodeSeen;

				// read image dimensions
				if (   ReadGifWord(pBmp,&nLeft)
					|| ReadGifWord(pBmp,&nTop)
					|| ReadGifWord(pBmp,&nWidth)
					|| ReadGifWord(pBmp,&nHeight)
					|| ReadGifByte(pBmp,&nInfo))
					goto quit;

				if (   nTop + nHeight > labs(bmi.bmiHeader.biHeight)
					|| nLeft + nWidth > bmi.bmiHeader.biWidth)
					goto quit;

				/* Bit 3 of info must be zero in GIF87a; in GIF89a, if it
				 * is set, it indicates that the local colormap is sorted,
				 * the most important entries being first. In PseudoColor
				 * environments this can be used to make sure to get the
				 * most important colors from the X server first, to
				 * optimize the image's appearance in the event that not
				 * all the colors from the colormap can actually be
				 * obtained at the same time.
				 */

				if ((nInfo & 0x80) == 0)	// using global color map
				{
					sAct = sGlb;
				}
				else						// using local color map
				{
					DWORD i;

					sAct.biBitCount = (nInfo & 7) + 1;	// bits used in color map
					sAct.biClrUsed = (1 << sAct.biBitCount); // no of colors in color map

					for (i = 0; i < sAct.biClrUsed; ++i)
					{
						int r, g, b;

						if (ReadGifByte(pBmp,&r) || ReadGifByte(pBmp,&g) || ReadGifByte(pBmp,&b))
							goto quit;

						sAct.bmiColors[i].rgbRed   = r;
						sAct.bmiColors[i].rgbGreen = g;
						sAct.bmiColors[i].rgbBlue  = b;
					}
				}

				// interlaced image
				bInterlaced = (nInfo & 0x40) != 0;

				h = 0;
				v = 0;
				if (   ReadGifByte(pBmp,&nCodesize)
					|| ReadGifByte(pBmp,&nBytecount))
					goto quit;

				nMaxcode = (1 << nCodesize);

				// preset LZW table
				for (i = 0; i < nMaxcode + 2; ++i)
				{
					prefix_table[i] = -1;
					code_table[i] = i;
				}
				nClearCode = nMaxcode++;
				nEndCode = nMaxcode++;

				nCurrCodesize = nCodesize + 1;
				nCurrCode = 0;
				nOldCode = -1;
				nBitsNeeded = nCurrCodesize;
				bEndCodeSeen = FALSE;

				while (nBytecount != 0)
				{
					for (i = 0; i < nBytecount; ++i)
					{
						INT nCurrByte;
						INT nBitsAvailable;

						if (ReadGifByte(pBmp,&nCurrByte))
							goto quit;

						if (bEndCodeSeen) continue;

						nBitsAvailable = 8;
						while (nBitsAvailable != 0)
						{
							INT nBitsCopied = (nBitsNeeded < nBitsAvailable)
											? nBitsNeeded
											: nBitsAvailable;

							INT nBits = nCurrByte >> (8 - nBitsAvailable);

							nBits &= 0xFF >> (8 - nBitsCopied);
							nCurrCode |= nBits << (nCurrCodesize - nBitsNeeded);
							nBitsAvailable -= nBitsCopied;
							nBitsNeeded -= nBitsCopied;

							if (nBitsNeeded == 0)
							{
								BYTE byExpanded[4096];
								INT  nExplen;

								do
								{
									if (nCurrCode == nEndCode)
									{
										bEndCodeSeen = TRUE;
										break;
									}

									if (nCurrCode == nClearCode)
									{
										nMaxcode = (1 << nCodesize) + 2;
										nCurrCodesize = nCodesize + 1;
										nOldCode = -1;
										break;
									}

									if (nCurrCode < nMaxcode)
									{
										if (nMaxcode < 4096 && nOldCode != -1)
										{
											INT c = nCurrCode;
											while (prefix_table[c] != -1)
												c = prefix_table[c];
											c = code_table[c];
											prefix_table[nMaxcode] = nOldCode;
											code_table[nMaxcode] = c;
											nMaxcode++;
											if (nMaxcode == (1 << nCurrCodesize) && nCurrCodesize < 12)
												nCurrCodesize++;
										}
									}
									else
									{
										INT c;

										if (nCurrCode > nMaxcode || nOldCode == -1) goto quit;

										_ASSERT(nCurrCode == nMaxcode);

										/* Once maxcode == 4096, we can't get here
										 * any more, because we refuse to raise
										 * nCurrCodeSize above 12 -- so we can
										 * never read a bigger code than 4095.
										 */

										c = nOldCode;
										while (prefix_table[c] != -1)
											c = prefix_table[c];
										c = code_table[c];
										prefix_table[nMaxcode] = nOldCode;
										code_table[nMaxcode] = c;
										nMaxcode++;

										if (nMaxcode == (1 << nCurrCodesize) && nCurrCodesize < 12)
											nCurrCodesize++;
									}
									nOldCode = nCurrCode;

									// output nCurrCode!
									nExplen = 0;
									while (nCurrCode != -1)
									{
										_ASSERT(nExplen < ARRAYSIZEOF(byExpanded));
										byExpanded[nExplen++] = (BYTE) code_table[nCurrCode];
										nCurrCode = prefix_table[nCurrCode];
									}
									_ASSERT(nExplen > 0);

									while (--nExplen >= 0)
									{
										// get color map index
										BYTE byColIndex = byExpanded[nExplen];

										LPBYTE pbyRgbr = pbyPixels + (lBytesPerLine * (nTop + v) + 3 * (nLeft + h));

										_ASSERT(pbyRgbr + 2 < pbyPixels + bmi.bmiHeader.biSizeImage);
										_ASSERT(byColIndex < sAct.biClrUsed);

										*pbyRgbr++ = sAct.bmiColors[byColIndex].rgbBlue;
										*pbyRgbr++ = sAct.bmiColors[byColIndex].rgbGreen;
										*pbyRgbr   = sAct.bmiColors[byColIndex].rgbRed;

										if (++h == nWidth)
										{
											h = 0;
											if (bInterlaced)
											{
												switch (v & 7)
												{
												case 0:
													v += 8;
													if (v < nHeight)
														break;
													/* Some GIF en/decoders go
													 * straight from the '0'
													 * pass to the '4' pass
													 * without checking the
													 * height, and blow up on
													 * 2/3/4 pixel high
													 * interlaced images.
													 */
													if (nHeight > 4)
														v = 4;
													else
														if (nHeight > 2)
															v = 2;
														else
															if (nHeight > 1)
																v = 1;
															else
																bEndCodeSeen = TRUE;
													break;
												case 4:
													v += 8;
													if (v >= nHeight)
														v = 2;
													break;
												case 2:
												case 6:
													v += 4;
													if (v >= nHeight)
														v = 1;
													break;
												case 1:
												case 3:
												case 5:
												case 7:
													v += 2;
													if (v >= nHeight)
														bEndCodeSeen = TRUE;
													break;
												}
												if (bEndCodeSeen)
													break; // while (--nExplen >= 0)
											}
											else // non interlaced
											{
												if (++v == nHeight)
												{
													bEndCodeSeen = TRUE;
													break; // while (--nExplen >= 0)
												}
											}
										}
									}
								}
								while (FALSE);

								nCurrCode = 0;
								nBitsNeeded = nCurrCodesize;
							}
						}
					}

					if (ReadGifByte(pBmp,&nBytecount))
						goto quit;
				}
			}
			break;

		case '!': // extension block
			{
				INT i,nFunctionCode,nByteCount,nDummy;

				if (ReadGifByte(pBmp,&nFunctionCode)) goto quit;
				if (ReadGifByte(pBmp,&nByteCount)) goto quit;

				// Graphic Control Label & correct Block Size
				if (nFunctionCode == 0xF9 && nByteCount == 0x04)
				{
					INT nPackedFields,nColorIndex;

					// packed fields
					if (ReadGifByte(pBmp,&nPackedFields)) goto quit;

					// delay time
					if (ReadGifWord(pBmp,&nDummy)) goto quit;

					// transparent color index
					if (ReadGifByte(pBmp,&nColorIndex)) goto quit;

					// transparent color flag set
					if ((nPackedFields & 0x1) != 0)
					{
						if (pdwTransparentColor != NULL)
						{
							*pdwTransparentColor = RGB(sGlb.bmiColors[nColorIndex].rgbRed,
													   sGlb.bmiColors[nColorIndex].rgbGreen,
													   sGlb.bmiColors[nColorIndex].rgbBlue);
						}
					}

					// block terminator (0 byte)
					if (!(!ReadGifByte(pBmp,&nDummy) && nDummy == 0)) goto quit;
				}
				else // other function
				{
					while (nByteCount != 0)
					{
						for (i = 0; i < nByteCount; ++i)
						{
							if (ReadGifByte(pBmp,&nDummy)) goto quit;
						}

						if (ReadGifByte(pBmp,&nByteCount)) goto quit;
					}
				}
			}
			break;

		case ';': // terminator
			bDecoding = FALSE;
			break;

		default: goto quit;
		}
	}

	_ASSERT(bDecoding == FALSE);			// decoding successful

	// normal decoding exit
	if (bPalette && hPalette == NULL)
	{
		hPalette = CreateBIPalette((PBITMAPINFOHEADER) &bmi);
		// save old palette
		hOldPalette = SelectPalette(hWindowDC, hPalette, FALSE);
		RealizePalette(hWindowDC);
	}

quit:
	if (hBitmap != NULL && bDecoding)		// creation failed
	{
		DeleteObject(hBitmap);				// delete bitmap
		hBitmap = NULL;
	}
	return hBitmap;
}

static HBITMAP DecodePng(LPBMPFILE pBmp,BOOL bPalette)
{
	// this implementation use the PNG image file decoder
	// engine of Copyright (c) 2005-2018 Lode Vandevenne

	HBITMAP hBitmap;

	UINT    uError,uWidth,uHeight;
	INT     nWidth,nHeight;
	LONG    lBytesPerLine;

	LPBYTE  pbyImage;						// PNG RGB image data
	LPBYTE  pbySrc;							// source buffer pointer
	LPBYTE  pbyPixels;						// BMP buffer

	BITMAPINFO bmi;

	hBitmap = NULL;
	pbyImage = NULL;

	// decode PNG image
	uError = lodepng_decode_memory(&pbyImage,&uWidth,&uHeight,pBmp->pbyFile,pBmp->dwFileSize,LCT_RGB,8);
	if (uError) goto quit;

	ZeroMemory(&bmi,sizeof(bmi));			// init bitmap info
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = (LONG) uWidth;
	bmi.bmiHeader.biHeight = (LONG) uHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;			// create a true color DIB
	bmi.bmiHeader.biCompression = BI_RGB;

	// bitmap dimensions
	lBytesPerLine = WIDTHBYTES(bmi.bmiHeader.biWidth * bmi.bmiHeader.biBitCount);
	bmi.bmiHeader.biSizeImage = lBytesPerLine * bmi.bmiHeader.biHeight;

	// allocate buffer for pixels
	VERIFY(hBitmap = CreateDIBSection(hWindowDC,
									  &bmi,
									  DIB_RGB_COLORS,
									  (VOID **)&pbyPixels,
									  NULL,
									  0));
	if (hBitmap == NULL) goto quit;

	pbySrc = pbyImage;						// init source loop pointer

	// fill bottom up DIB pixel buffer with color information
	for (nHeight = bmi.bmiHeader.biHeight - 1; nHeight >= 0; --nHeight)
	{
		LPBYTE pbyLine = pbyPixels + nHeight * lBytesPerLine;

		for (nWidth = 0; nWidth < bmi.bmiHeader.biWidth; ++nWidth)
		{
			*pbyLine++ = pbySrc[2];			// blue
			*pbyLine++ = pbySrc[1];			// green
			*pbyLine++ = pbySrc[0];			// red
			pbySrc += 3;
		}

		_ASSERT((DWORD) (pbyLine - pbyPixels) <= bmi.bmiHeader.biSizeImage);
	}

	if (bPalette && hPalette == NULL)
	{
		hPalette = CreateBIPalette((PBITMAPINFOHEADER) &bmi);
		// save old palette
		hOldPalette = SelectPalette(hWindowDC, hPalette, FALSE);
		RealizePalette(hWindowDC);
	}

quit:
	if (pbyImage != NULL)					// buffer for PNG image allocated
	{
		free(pbyImage);						// free PNG image data
	}

	if (hBitmap != NULL && uError != 0)		// creation failed
	{
		DeleteObject(hBitmap);				// delete bitmap
		hBitmap = NULL;
	}
	return hBitmap;
}

HBITMAP LoadBitmapFile(LPCTSTR szFilename,BOOL bPalette)
{
	HANDLE  hFile;
	HANDLE  hMap;
	BMPFILE Bmp;
	HBITMAP hBitmap;

	SetCurrentDirectory(szEmuDirectory);
	hFile = CreateFile(szFilename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	SetCurrentDirectory(szCurrentDirectory);
	if (hFile == INVALID_HANDLE_VALUE) return NULL;
	Bmp.dwFileSize = GetFileSize(hFile, NULL);
	hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	if (hMap == NULL)
	{
		CloseHandle(hFile);
		return NULL;
	}
	Bmp.pbyFile = (LPBYTE) MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
	if (Bmp.pbyFile == NULL)
	{
		CloseHandle(hMap);
		CloseHandle(hFile);
		return NULL;
	}

	do
	{
		// check for bitmap file header "BM"
		if (Bmp.dwFileSize >= 2 && *(WORD *) Bmp.pbyFile == 0x4D42)
		{
			hBitmap = DecodeBmp(&Bmp,bPalette);
			break;
		}

		// check for GIF file header
		if (   Bmp.dwFileSize >= 6
			&& (memcmp(Bmp.pbyFile,"GIF87a",6) == 0 || memcmp(Bmp.pbyFile,"GIF89a",6) == 0))
		{
			hBitmap = DecodeGif(&Bmp,&dwTColor,bPalette);
			break;
		}

		// check for PNG file header
		if (Bmp.dwFileSize >= 8 && memcmp(Bmp.pbyFile,"\x89PNG\r\n\x1a\n",8) == 0)
		{
			hBitmap = DecodePng(&Bmp,bPalette);
			break;
		}

		// unknown file type
		hBitmap = NULL;
	}
	while (FALSE);

	UnmapViewOfFile(Bmp.pbyFile);
	CloseHandle(hMap);
	CloseHandle(hFile);
	return hBitmap;
}

static BOOL AbsColorCmp(DWORD dwColor1,DWORD dwColor2,DWORD dwTol)
{
	DWORD dwDiff;

	dwDiff =  (DWORD) abs((INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF));
	dwColor1 >>= 8;
	dwColor2 >>= 8;
	dwDiff += (DWORD) abs((INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF));
	dwColor1 >>= 8;
	dwColor2 >>= 8;
	dwDiff += (DWORD) abs((INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF));

	return dwDiff > dwTol;					// FALSE = colors match
}

static BOOL LabColorCmp(DWORD dwColor1,DWORD dwColor2,DWORD dwTol)
{
	DWORD dwDiff;
	INT   nDiffCol;

	nDiffCol = (INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF);
	dwDiff = (DWORD) (nDiffCol * nDiffCol);
	dwColor1 >>= 8;
	dwColor2 >>= 8;
	nDiffCol = (INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF);
	dwDiff += (DWORD) (nDiffCol * nDiffCol);
	dwColor1 >>= 8;
	dwColor2 >>= 8;
	nDiffCol = (INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF);
	dwDiff += (DWORD) (nDiffCol * nDiffCol);
	dwTol *= dwTol;

	return dwDiff > dwTol;					// FALSE = colors match
}

static DWORD EncodeColorBits(DWORD dwColorVal,DWORD dwMask)
{
	#define MAXBIT 32
	UINT uLshift = MAXBIT;
	UINT uRshift = 8;
	DWORD dwBitMask = dwMask;

	dwColorVal &= 0xFF;						// the color component using the lowest 8 bit

	// position of highest bit
	while ((dwBitMask & (1<<(MAXBIT-1))) == 0 && uLshift > 0)
	{
		dwBitMask <<= 1;					// next bit
		--uLshift;							// next position
	}

	if (uLshift > 24)						// avoid overflow on 32bit mask
	{
		uLshift -= uRshift;					// normalize left shift
		uRshift = 0;
	}

	return ((dwColorVal << uLshift) >> uRshift) & dwMask;
	#undef MAXBIT
}

HRGN CreateRgnFromBitmap(HBITMAP hBmp,COLORREF color,DWORD dwTol)
{
	#define ADD_RECTS_COUNT  256

	BOOL (*fnColorCmp)(DWORD dwColor1,DWORD dwColor2,DWORD dwTol);

	DWORD dwRed,dwGreen,dwBlue;
	HRGN hRgn;
	LPRGNDATA pRgnData;
	LPBITMAPINFO bi;
	LPBYTE pbyBits;
	LPBYTE pbyColor;
	DWORD dwAlignedWidthBytes;
	DWORD dwBpp;
	DWORD dwRectsCount;
	LONG x,y,xleft;
	BOOL bFoundLeft;
	BOOL bIsMask;

	if (dwTol >= 1000)						// use CIE L*a*b compare
	{
		fnColorCmp = LabColorCmp;
		dwTol -= 1000;						// remove L*a*b compare selector
	}
	else									// use Abs summation compare
	{
		fnColorCmp = AbsColorCmp;
	}

	// allocate memory for extended image information incl. RGBQUAD color table
	bi = (LPBITMAPINFO) calloc(1,sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
	bi->bmiHeader.biSize = sizeof(bi->bmiHeader);
	_ASSERT(bi->bmiHeader.biBitCount == 0); // for query without color table

	// get information about image
	GetDIBits(hWindowDC,hBmp,0,0,NULL,bi,DIB_RGB_COLORS);

	// DWORD aligned bitmap width in BYTES
	dwAlignedWidthBytes = WIDTHBYTES(  bi->bmiHeader.biWidth
									 * bi->bmiHeader.biPlanes
									 * bi->bmiHeader.biBitCount);

	// biSizeImage is empty
	if (bi->bmiHeader.biSizeImage == 0 && bi->bmiHeader.biCompression == BI_RGB)
	{
		bi->bmiHeader.biSizeImage = dwAlignedWidthBytes * bi->bmiHeader.biHeight;
	}

	// allocate memory for image data (colors)
	pbyBits = (LPBYTE) malloc(bi->bmiHeader.biSizeImage);

	// fill bits buffer
	GetDIBits(hWindowDC,hBmp,0,bi->bmiHeader.biHeight,pbyBits,bi,DIB_RGB_COLORS);

	// convert color if current DC is 16-bit/32-bit bitfield coded
	if (bi->bmiHeader.biCompression == BI_BITFIELDS)
	{
		dwRed   = *(LPDWORD) &bi->bmiColors[0];
		dwGreen = *(LPDWORD) &bi->bmiColors[1];
		dwBlue  = *(LPDWORD) &bi->bmiColors[2];
	}
	else // RGB coded
	{
		// convert color if current DC is 16-bit RGB coded
		if (bi->bmiHeader.biBitCount == 16)
		{
			// for 15 bit (5:5:5)
			dwRed   = 0x00007C00;
			dwGreen = 0x000003E0;
			dwBlue  = 0x0000001F;
		}
		else
		{
			// convert COLORREF to RGBQUAD color
			dwRed   = 0x00FF0000;
			dwGreen = 0x0000FF00;
			dwBlue  = 0x000000FF;
		}
	}
	color = EncodeColorBits((color >> 16), dwBlue)
		  | EncodeColorBits((color >>  8), dwGreen)
		  | EncodeColorBits((color >>  0), dwRed);

	dwBpp = bi->bmiHeader.biBitCount >> 3;	// bytes per pixel

	// DIB is bottom up image so we begin with the last scanline
	pbyColor = pbyBits + (bi->bmiHeader.biHeight - 1) * dwAlignedWidthBytes;

	dwRectsCount = bi->bmiHeader.biHeight;	// number of rects in allocated buffer

	bFoundLeft = FALSE;						// set when mask has been found in current scan line

	// allocate memory for region data
	pRgnData = (PRGNDATA) malloc(sizeof(RGNDATAHEADER) + dwRectsCount * sizeof(RECT));

	// fill it by default
	ZeroMemory(&pRgnData->rdh,sizeof(pRgnData->rdh));
	pRgnData->rdh.dwSize = sizeof(pRgnData->rdh);
	pRgnData->rdh.iType	 = RDH_RECTANGLES;
	SetRect(&pRgnData->rdh.rcBound,MAXLONG,MAXLONG,0,0);

	for (y = 0; y < bi->bmiHeader.biHeight; ++y)
	{
		LPBYTE pbyLineStart = pbyColor;

		for (x = 0; x < bi->bmiHeader.biWidth; ++x)
		{
			// get color
			switch (bi->bmiHeader.biBitCount)
			{
			case 8:
				bIsMask = fnColorCmp(*(LPDWORD)(&bi->bmiColors)[*pbyColor],color,dwTol);
				break;
			case 16:
				// it makes no sense to allow a tolerance here
				bIsMask = (*(LPWORD)pbyColor != (WORD) color);
				break;
			case 24:
				bIsMask = fnColorCmp((*(LPDWORD)pbyColor & 0x00ffffff),color,dwTol);
				break;
			case 32:
				bIsMask = fnColorCmp(*(LPDWORD)pbyColor,color,dwTol);
			}
			pbyColor += dwBpp;				// shift pointer to next color

			if (!bFoundLeft && bIsMask)		// non transparent color found
			{
				xleft = x;
				bFoundLeft = TRUE;
			}

			if (bFoundLeft)					// found non transparent color in scanline
			{
				// transparent color or last column
				if (!bIsMask || x + 1 == bi->bmiHeader.biWidth)
				{
					// non transparent color and last column
					if (bIsMask && x + 1 == bi->bmiHeader.biWidth)
						++x;

					// save current RECT
					((LPRECT) pRgnData->Buffer)[pRgnData->rdh.nCount].left = xleft;
					((LPRECT) pRgnData->Buffer)[pRgnData->rdh.nCount].top  = y;
					((LPRECT) pRgnData->Buffer)[pRgnData->rdh.nCount].right = x;
					((LPRECT) pRgnData->Buffer)[pRgnData->rdh.nCount].bottom = y + 1;
					pRgnData->rdh.nCount++;

					if (xleft < pRgnData->rdh.rcBound.left)
						pRgnData->rdh.rcBound.left = xleft;

					if (y < pRgnData->rdh.rcBound.top)
						pRgnData->rdh.rcBound.top = y;

					if (x > pRgnData->rdh.rcBound.right)
						pRgnData->rdh.rcBound.right = x;

					if (y + 1 > pRgnData->rdh.rcBound.bottom)
						pRgnData->rdh.rcBound.bottom = y + 1;

					// if buffer full reallocate it with more room
					if (pRgnData->rdh.nCount >= dwRectsCount)
					{
						dwRectsCount += ADD_RECTS_COUNT;

						pRgnData = (LPRGNDATA) realloc(pRgnData,sizeof(RGNDATAHEADER) + dwRectsCount * sizeof(RECT));
					}

					bFoundLeft = FALSE;
				}
			}
		}

		// previous scanline
		pbyColor = pbyLineStart - dwAlignedWidthBytes;
	}
	// release image data
	free(pbyBits);
	free(bi);

	// create region
	hRgn = ExtCreateRegion(NULL,sizeof(RGNDATAHEADER) + pRgnData->rdh.nCount * sizeof(RECT),pRgnData);

	free(pRgnData);
	return hRgn;
	#undef ADD_RECTS_COUNT
}
