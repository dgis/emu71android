/*
 *   portcfg.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gieﬂelink
 *
 */
#include "core/pch.h"
#include "core/resource.h"
#include "core/Emu71.h"
#include "core/ops.h"
#include "core/io.h"

#define IRAMSIG		0xEDDDDD3B				// independent RAM signature (byte reverse order)

typedef struct _NUMSTR
{
	DWORD	dwData;							// data as number
	LPCTSTR	lpszData;						// data as string
} NUMSTR, *PNUMSTR;

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

// ports to select
static LPCTSTR lpszPorts[] =
{
	_T("Port0"), _T("Port1"), _T("Port2"), _T("Port3"), _T("Port4"), _T("Port5")
};

// valid module types
static NUMSTR sModType[] =
{
	{ TYPE_RAM,  _T("RAM")  },
	{ TYPE_ROM,  _T("ROM")  },
	{ TYPE_HRD,  _T("HRD")  },
	{ TYPE_HPIL, _T("HPIL") }
};

// valid hard wired module address
static NUMSTR sHrdAddr[] =
{
	{ 0x00000, _T("00000") },
	{ 0xE0000, _T("E0000") }
};

// valid RAM / ROM sizes
static NUMSTR sMod[] =
{
	{          0, _T("Datafile")  },
	{       1024, _T("512 Byte")  },
	{   1 * 2048, _T("1K Byte")   },
	{   2 * 2048, _T("2K Byte")   },
	{   4 * 2048, _T("4K Byte")   },
	{   8 * 2048, _T("8K Byte")   },
	{  16 * 2048, _T("16K Byte")  },
	{  32 * 2048, _T("32K Byte")  },
	{  64 * 2048, _T("64K Byte")  },
	{  96 * 2048, _T("96K Byte")  },
	{ 128 * 2048, _T("128K Byte") },
	{ 160 * 2048, _T("160K Byte") },
	{ 192 * 2048, _T("192K Byte") }
};

// no. of chips in hybrid module
static LPCTSTR lpszChips[] =
{
	_T("Auto"), _T("1"), _T("2"), _T("3"), _T("4"), _T("5"), _T("6"), _T("7"), _T("8")
};

static UINT nActPort = 0;					// the actual port
static UINT nUnits = 0;						// no. of applied port units in the actual port slot

static BOOL		bChanged[ARRAYSIZEOF(lpszPorts)];
static PPORTCFG psPortCfg[ARRAYSIZEOF(lpszPorts)];

static VOID DelPort(UINT nPort);
static INT_PTR OnEditTcpIpSettings(HWND hDlg,PPORTCFG psCfg);

//################
//#
//#    Helper Functions
//#
//################

static UINT GetModuleID(UINT nType)
{
	UINT i;

	for (i = 0; i < ARRAYSIZEOF(sModType); ++i)
	{
		if ((UINT) sModType[i].dwData == nType)
			return i;
	}
	return (UINT) -1;
}

static PPORTCFG *CfgModule(UINT nPort)
{
	PPORTCFG *ppsCfg;

	_ASSERT(psPortCfg[nPort] != NULL);
	ppsCfg = &psPortCfg[nPort];				// root of module
	while ((*ppsCfg)->pNext != NULL)		// not latest module in queue
	{
		if ((*ppsCfg)->bApply == FALSE)		// module not applied
			break;

		ppsCfg = &(*ppsCfg)->pNext;
	}
	return ppsCfg;
}

static VOID LoadCurrPortConfig(VOID)
{
	UINT i,j;

	// scan each port
	for (i = 0; i < ARRAYSIZEOF(psPortCfg); ++i)
	{
		PPORTDATA psData  = psExtPortData[i];
		PPORTCFG  *ppsCfg = &psPortCfg[i];

		_ASSERT(i < ARRAYSIZEOF(bChanged));
		bChanged[i] = FALSE;

		// plugged module in port
		for (j = 0; psData != NULL; ++j)	// walk through all modules
		{
			*ppsCfg = (PPORTCFG) calloc(1,sizeof(*psPortCfg[0]));

			(*ppsCfg)->nIndex = j;
			(*ppsCfg)->bApply = TRUE;
			(*ppsCfg)->nType  = psData->sInfo.nType;

			if ((*ppsCfg)->nType == TYPE_HPIL)
			{
				// in MM I/O dataset only device name is interesting
				(*ppsCfg)->lpszAddrOut = (LPSTR) malloc(psData->psTcp->dwAddrSize+1);
				CopyMemory((*ppsCfg)->lpszAddrOut,psData->psTcp->lpszAddrOut,psData->psTcp->dwAddrSize+1);
				(*ppsCfg)->wPortOut = psData->psTcp->wPortOut;
				(*ppsCfg)->wPortIn  = psData->psTcp->wPortIn;

				// reference to the original data
				(*ppsCfg)->psTcp = psData->psTcp;

				// followed by TYPE_ROM dataset with rest of the valid data
				psData = psData->pNext;
				_ASSERT(psData != NULL && psData->sInfo.nType == TYPE_ROM);
			}

			if ((*ppsCfg)->nType == TYPE_HRD)
			{
				(*ppsCfg)->dwBase = psData->psCfg[0].dwBase;
			}
			else
			{
				(*ppsCfg)->dwBase = 0x00000;
			}
			(*ppsCfg)->dwSize  = psData->sInfo.dwSize;
			(*ppsCfg)->dwChips = psData->sInfo.dwChips;
			(*ppsCfg)->pbyData = psData->pbyData;
			(*ppsCfg)->pNext   = NULL;

			if (psData->pszName != NULL)
			{
				lstrcpyn((*ppsCfg)->szFileName,psData->pszName,ARRAYSIZEOF(psPortCfg[0]->szFileName));
			}
			else
			{
				(*ppsCfg)->szFileName[0] = 0;
			}

			psData = psData->pNext;
			ppsCfg = &(*ppsCfg)->pNext;
		}
	}
	return;
}

static VOID SaveCurrPortConfig(VOID)
{
	UINT i,j,nIndex;
	BOOL bHpil;

	// scan each port
	for (i = 0; i < ARRAYSIZEOF(psPortCfg); ++i)
	{
		_ASSERT(i < ARRAYSIZEOF(bChanged));

		if (bChanged[i])					// port configuration changed
		{
			PPORTCFG  psCfg    = psPortCfg[i];
			PPORTDATA *ppsData = &psExtPortData[i];

			Chipset.HST |= MP;				// module pulled

			nIndex = 0;						// logical no. in instrument port queue

			while (psCfg != NULL)			// walk through all config modules
			{
				// module index not equal
				if (nIndex != psCfg->nIndex)
				{
					// insert new module in ppsData
					if (psCfg->nIndex == (UINT) -1)
					{
						if (psCfg->bApply)
						{
							PPORTDATA psRomData;

							PPORTDATA psDataNext = *ppsData;

							_ASSERT(psCfg->dwSize > 0);
							_ASSERT(psCfg->dwChips > 0);

							// create chip data
							*ppsData = (PPORTDATA) calloc(1,sizeof(*psExtPortData[0]));

							(*ppsData)->sInfo.dwStructSize = sizeof(psExtPortData[0]->sInfo);
							(*ppsData)->sInfo.dwCfgSize    = sizeof(*psExtPortData[0]->psCfg) * psCfg->dwChips;
							(*ppsData)->sInfo.nType        = psCfg->nType;
							(*ppsData)->sInfo.dwSize       = psCfg->dwSize;
							(*ppsData)->sInfo.dwChips      = psCfg->dwChips;
							(*ppsData)->sInfo.bHybrid      = TRUE;

							// chip configuration data
							(*ppsData)->psCfg = (PSATCFG) calloc(1,(*ppsData)->sInfo.dwCfgSize);

							switch ((*ppsData)->sInfo.nType)
							{
							case TYPE_RAM:
								(*ppsData)->dwNameSize = 0;
								(*ppsData)->pszName    = NULL;

								// given filename to preload data?
								if (*psCfg->szFileName != 0)
								{
									// preload data
									MapFile(psCfg->szFileName,&(*ppsData)->pbyData,&(*ppsData)->sInfo.dwSize);
								}
								else
								{
									// normal memory allocation
									(*ppsData)->pbyData = (LPBYTE) calloc((*ppsData)->sInfo.dwSize,sizeof(*(*ppsData)->pbyData));
								}
								break;
							case TYPE_HRD:
								for (j = 0; j < (*ppsData)->sInfo.dwChips; ++j)
								{
									// TYPE_HRD is a hard wired ROM module
									(*ppsData)->psCfg[j].bCfg   = TRUE;
									(*ppsData)->psCfg[j].dwBase = (psCfg->dwBase
																   + j * (  (*ppsData)->sInfo.dwSize
																		  / (*ppsData)->sInfo.dwChips)
																  );
								}
								// no break
							case TYPE_ROM:
								(*ppsData)->dwNameSize = lstrlen(psCfg->szFileName);
								(*ppsData)->pszName = DuplicateString(psCfg->szFileName);
								MapFile((*ppsData)->pszName,&(*ppsData)->pbyData,&(*ppsData)->sInfo.dwSize);
								break;
							case TYPE_HPIL:
								// create a copy of the chip data
								psRomData = (PPORTDATA) malloc(sizeof(*psExtPortData[0]));
								*psRomData = **ppsData;

								// overwrite data for the HPIL entry
								(*ppsData)->sInfo.dwCfgSize    = sizeof(*psExtPortData[0]->psCfg);
								(*ppsData)->sInfo.dwSize       = 16;
								(*ppsData)->sInfo.dwChips      = 1;
								(*ppsData)->sInfo.bHybrid      = FALSE;

								// chip configuration data
								(*ppsData)->psCfg = (PSATCFG) calloc(1,(*ppsData)->sInfo.dwCfgSize);

								// tcp/ip settings
								(*ppsData)->psTcp = (PPORTTCPIP) calloc(1,sizeof(*(*ppsData)->psTcp));
								(*ppsData)->psTcp->dwAddrSize = (DWORD) strlen(psCfg->lpszAddrOut);
								(*ppsData)->psTcp->lpszAddrOut = (LPSTR) malloc((*ppsData)->psTcp->dwAddrSize+1);
								CopyMemory((*ppsData)->psTcp->lpszAddrOut,psCfg->lpszAddrOut,(*ppsData)->psTcp->dwAddrSize+1);
								(*ppsData)->psTcp->wPortOut = psCfg->wPortOut;
								(*ppsData)->psTcp->wPortIn  = psCfg->wPortIn;

								// allocate data
								(*ppsData)->pbyData = (LPBYTE) calloc((*ppsData)->sInfo.dwSize,sizeof(*(*ppsData)->pbyData));
								ppsData = &(*ppsData)->pNext;

								// add the ROM part
								*ppsData = psRomData;

								// modify data top ROM type
								(*ppsData)->sInfo.nType = TYPE_ROM;

								(*ppsData)->dwNameSize = lstrlen(psCfg->szFileName);
								(*ppsData)->pszName = DuplicateString(psCfg->szFileName);
								MapFile((*ppsData)->pszName,&(*ppsData)->pbyData,&(*ppsData)->sInfo.dwSize);
								break;
							default:
								_ASSERT(FALSE);
							}

							(*ppsData)->pNext = psDataNext;
							ppsData = &(*ppsData)->pNext;
						}
						psCfg = psCfg->pNext;
					}
					else
					{
						// delete module in ppsData
						if (nIndex < psCfg->nIndex)
						{
							do
							{
								PPORTDATA psDataNext = (*ppsData)->pNext;

								// is it a HPIL module?
								bHpil = ((*ppsData)->sInfo.nType == TYPE_HPIL);

								// delete the data of my instance
								ResetPortModule(*ppsData);
								*ppsData = psDataNext;	// next module
							}
							while (bHpil);	// on HPIL delete also the corresponding ROM
							++nIndex;		// next logical no. in instrument port queue
						}
						else
						{
							_ASSERT(FALSE);	// existing module in ppsCfg but not in ppsData
						}
					}
				}
				else						// module index equal
				{
					psCfg = psCfg->pNext;

					do
					{
						// is it a HPIL module?
						bHpil = ((*ppsData)->sInfo.nType == TYPE_HPIL);
						ppsData = &(*ppsData)->pNext;
					}
					while (bHpil);			// on HPIL copy also the corresponding ROM
					++nIndex;				// next logical no. in instrument port queue
				}
			}
			ResetPortData(ppsData);			// cleanup the rest of the port line
			_ASSERT(*ppsData == NULL);
		}
	}
	return;
}

static VOID Cleanup(VOID)
{
	UINT i;

	// scan each port
	for (i = 0; i < ARRAYSIZEOF(psPortCfg); ++i)
	{
		if (psPortCfg[i] != NULL)
		{
			DelPort(i);
			_ASSERT(psPortCfg[i] == NULL);
		}
	}
	return;
}

//static LRESULT ShowPortConfig(HWND hDlg,UINT nPort)
//{
//	HWND     hWndFocus;
//	PPORTCFG psCfg;
//	UINT     nIndex;
//	INT      nItem;
//
//	HWND hWnd = GetDlgItem(hDlg,IDC_CFG_PORTDATA);
//
//	nUnits = 0;								// no. of applied units
//
//	hWndFocus = GetFocus();					// window with actual focus
//
//	// clear configuration input fields
//	SendDlgItemMessage(hDlg,IDC_CFG_TYPE,    CB_RESETCONTENT,0,0);
//	SendDlgItemMessage(hDlg,IDC_CFG_SIZE,    CB_RESETCONTENT,0,0);
//	SendDlgItemMessage(hDlg,IDC_CFG_CHIPS,   CB_RESETCONTENT,0,0);
//	SendDlgItemMessage(hDlg,IDC_CFG_HARDADDR,CB_RESETCONTENT,0,0);
//
//	SetDlgItemText(hDlg,IDC_CFG_DEL,_T("&Delete"));
//	SetDlgItemText(hDlg,IDC_CFG_FILE,_T(""));
//	SetDlgItemText(hDlg,IDC_CFG_STATIC_HARDADDR,_T("Hard Wired Address:"));
//
//	// enable configuration list box
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_PORTDATA),TRUE);
//
//	// button control
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_ADD),     TRUE);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_DEL),     psPortCfg[nPort] != NULL);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_APPLY),   FALSE);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_TYPE),    FALSE);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_SIZE),    FALSE);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_CHIPS),   FALSE);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_FILE),    FALSE);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_BROWSE),  FALSE);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_HARDADDR),FALSE);
//
//	ShowWindow(GetDlgItem(hDlg,IDC_CFG_HARDADDR),SW_SHOW);
//	ShowWindow(GetDlgItem(hDlg,IDC_CFG_TCPIP),SW_HIDE);
//
//	// fill the list box with the current data
//	SendMessage(hWnd,LB_RESETCONTENT,0,0);
//	for (psCfg = psPortCfg[nPort]; psCfg != NULL; psCfg = psCfg->pNext)
//	{
//		TCHAR szBuffer[256];
//
//		// module type
//		UINT nPos = GetModuleID(psCfg->nType);
//		nPos = wsprintf(szBuffer,(nPos != (UINT) -1) ? sModType[nPos].lpszData : _T("UNKNOWN"));
//
//		szBuffer[nPos++] = _T(',');
//		szBuffer[nPos++] = _T(' ');
//
//		// hard wired address
//		if (psCfg->nType == TYPE_HRD)
//		{
//			nPos += wsprintf(&szBuffer[nPos],_T("%05X, "),psCfg->dwBase);
//		}
//
//		// size + no. of chips
//		nIndex = psCfg->dwSize / 2048;
//
//		if (nIndex == 0)
//			nPos += wsprintf(&szBuffer[nPos],_T("512B (%u)"),psCfg->dwChips);
//		else
//			nPos += wsprintf(&szBuffer[nPos],_T("%uK (%u)"),nIndex,psCfg->dwChips);
//
//		// filename
//		if (*psCfg->szFileName != 0)		// given filename
//		{
//			szBuffer[nPos++] = _T(',');
//			szBuffer[nPos++] = _T(' ');
//			szBuffer[nPos++] = _T('\"');
//			nPos += GetCutPathName(psCfg->szFileName,&szBuffer[nPos],ARRAYSIZEOF(szBuffer)-nPos,36);
//			szBuffer[nPos++] = _T('\"');
//			szBuffer[nPos]   = 0;
//		}
//
//		// tcp/ip configuration
//		if (psCfg->nType == TYPE_HPIL)
//		{
//			wsprintf(&szBuffer[nPos],_T(", \"%hs\", %u, %u"),
//					 psCfg->lpszAddrOut,psCfg->wPortOut,psCfg->wPortIn);
//			++nUnits;						// HPIL needs two entries (HPIL mailbox & ROM)
//		}
//
//		nItem = (INT) SendMessage(hWnd,LB_ADDSTRING,0,(LPARAM) szBuffer);
//		SendMessage(hWnd,LB_SETITEMDATA,nItem,(LPARAM) psCfg);
//		++nUnits;
//	}
//
//	// append empty field for insert
//	nItem = (INT) SendMessage(hWnd,LB_ADDSTRING,0,(LPARAM) _T(""));
//	SendMessage(hWnd,LB_SETITEMDATA,nItem,(LPARAM) NULL);
//
//	if (hWndFocus != NULL)					// set focus on next enabled window
//	{
//		while (IsWindowEnabled(hWndFocus) == 0)
//		{
//			// window disabled. goto next one
//			hWndFocus = GetWindow(hWndFocus,GW_HWNDNEXT);
//		}
//		SetFocus(hWndFocus);
//	}
//	return 0;
//}
//
//static LRESULT OnAddPort(HWND hDlg,UINT nPort)
//{
//	PPORTCFG psCfg;
//	HWND     hWnd;
//	INT      i;
//	UINT     nIndex;
//	BOOL     bFilename;
//
//	_ASSERT(nPort < ARRAYSIZEOF(psPortCfg));
//	_ASSERT(psPortCfg[nPort] != NULL);
//
//	psCfg = *CfgModule(nPort);				// module in queue to configure
//
//	psCfg->bApply = FALSE;					// module not applied
//
//	// disable configuration list box
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_PORTDATA),FALSE);
//
//	// button control
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_ADD),  FALSE);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_DEL),  TRUE);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_APPLY),TRUE);
//
//	// "Delete" button has now the meaning of "Abort"
//	SetDlgItemText(hDlg,IDC_CFG_DEL,_T("A&bort"));
//
//	// module type combobox
//	hWnd = GetDlgItem(hDlg,IDC_CFG_TYPE);
//	SendMessage(hWnd,CB_RESETCONTENT,0,0);
//	for (i = 0; i < ARRAYSIZEOF(sModType); ++i)
//	{
//		SendMessage(hWnd,CB_ADDSTRING,0,(LPARAM) sModType[i].lpszData);
//	}
//	SendMessage(hWnd,CB_SETCURSEL,GetModuleID(psCfg->nType),0);
//	EnableWindow(hWnd,TRUE);
//
//	// size combobox
//	hWnd = GetDlgItem(hDlg,IDC_CFG_SIZE);
//	SendMessage(hWnd,CB_RESETCONTENT,0,0);
//	if (psCfg->nType == TYPE_RAM)
//	{
//		nIndex = 0;							// default cursor on first element
//		for (i = 0; i < ARRAYSIZEOF(sMod); ++i)
//		{
//			SendMessage(hWnd,CB_ADDSTRING,0,(LPARAM) sMod[i].lpszData);
//			if (sMod[i].dwData == psCfg->dwSize)
//				nIndex = i;
//		}
//		_ASSERT(nIndex < ARRAYSIZEOF(sMod));
//		SendMessage(hWnd,CB_SETCURSEL,nIndex,0);
//		EnableWindow(hWnd,TRUE);
//	}
//	else
//	{
//		EnableWindow(hWnd,FALSE);
//	}
//
//	// no. of chips combobox
//	hWnd = GetDlgItem(hDlg,IDC_CFG_CHIPS);
//	SendMessage(hWnd,CB_RESETCONTENT,0,0);
//	for (i = 0; i < ARRAYSIZEOF(lpszChips); ++i)
//	{
//		SendMessage(hWnd,CB_ADDSTRING,0,(LPARAM) lpszChips[i]);
//	}
//	SendMessage(hWnd,CB_SETCURSEL,0,0);		// select "Auto"
//	EnableWindow(hWnd,TRUE);
//
//	// enable filename when not RAM or RAM size = 0 selected
//	bFilename =  psCfg->nType != TYPE_RAM
//			  || SendDlgItemMessage(hDlg,IDC_CFG_SIZE,CB_GETCURSEL,0,0) == 0;
//
//	if (!bFilename)							// RAM with given size
//	{
//		_ASSERT(psCfg->szFileName != NULL);
//		*psCfg->szFileName = 0;				// no filename
//	}
//	SetDlgItemText(hDlg,IDC_CFG_FILE,psCfg->szFileName);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_FILE),  bFilename);
//	EnableWindow(GetDlgItem(hDlg,IDC_CFG_BROWSE),bFilename);
//
//	// hpil interface or hard wired address
//	hWnd = GetDlgItem(hDlg,IDC_CFG_HARDADDR);
//	SendMessage(hWnd,CB_RESETCONTENT,0,0);
//	if (psCfg->nType == TYPE_HPIL)			// HPIL interface
//	{
//		SetDlgItemText(hDlg,IDC_CFG_STATIC_HARDADDR,_T("TCP/IP Configuration:"));
//		EnableWindow(hWnd,FALSE);
//		ShowWindow(hWnd,SW_HIDE);
//
//		if (psCfg->lpszAddrOut == NULL)		// first call
//		{
//			// init tpc/ip settings with default values
//			LPCSTR lpszServer = "localhost";
//			psCfg->lpszAddrOut = (LPSTR) malloc(strlen(lpszServer)+1);
//			CopyMemory(psCfg->lpszAddrOut,lpszServer,strlen(lpszServer)+1);
//			psCfg->wPortOut = 60001;
//			psCfg->wPortIn  = 60000;
//		}
//
//		// activate configuration button
//		ShowWindow(GetDlgItem(hDlg,IDC_CFG_TCPIP),SW_SHOW);
//	}
//	else									// default
//	{
//		// hard wired address
//		SetDlgItemText(hDlg,IDC_CFG_STATIC_HARDADDR,_T("Hard Wired Address:"));
//		ShowWindow(hWnd,SW_SHOW);
//
//		// deactivate configuration button
//		ShowWindow(GetDlgItem(hDlg,IDC_CFG_TCPIP),SW_HIDE);
//
//		if (psCfg->nType == TYPE_HRD)		// hard wired chip
//		{
//			nIndex = 0;						// default cursor on first element
//			for (i = 0; i < ARRAYSIZEOF(sHrdAddr); ++i)
//			{
//				SendMessage(hWnd,CB_ADDSTRING,0,(LPARAM) sHrdAddr[i].lpszData);
//				if (sHrdAddr[i].dwData == psCfg->dwBase)
//					nIndex = i;
//			}
//			_ASSERT(nIndex < ARRAYSIZEOF(sHrdAddr));
//			SendMessage(hWnd,CB_SETCURSEL,nIndex,0);
//			EnableWindow(hWnd,TRUE);
//		}
//		else
//		{
//			SendMessage(hWnd,CB_RESETCONTENT,0,0);
//			EnableWindow(hWnd,FALSE);
//		}
//	}
//
//	SetFocus(GetDlgItem(hDlg,IDC_CFG_DEL));	// set focus on "Del" button
//	return 0;
//}
//
//static VOID DelPort(UINT nPort)
//{
//	PPORTCFG psCfg,psNext;
//
//	// free allocated module memory queue
//	for (psCfg = psPortCfg[nPort]; psCfg != NULL; psCfg = psNext)
//	{
//		if (psCfg->lpszAddrOut != NULL)
//		{
//			free(psCfg->lpszAddrOut);
//		}
//		psNext = psCfg->pNext;				// next module
//		free(psCfg);
//	}
//	psPortCfg[nPort] = NULL;
//	return;
//}
//
//static VOID DelPortCfg(UINT nPort)
//{
//	PPORTCFG *ppsCfg,psNext;
//
//	_ASSERT(psPortCfg[nPort] != NULL);
//	ppsCfg = &psPortCfg[nPort];				// root of module
//	if (*ppsCfg != NULL)
//	{
//		if ((*ppsCfg)->bApply == FALSE)		// 1st module not applied
//		{
//			psNext = (*ppsCfg)->pNext;		// pointer to next module
//
//			// delete module
//			if ((*ppsCfg)->lpszAddrOut != NULL)
//			{
//				free((*ppsCfg)->lpszAddrOut);
//			}
//			free(*ppsCfg);
//			*ppsCfg = psNext;				// next module is now root
//			return;
//		}
//		while ((*ppsCfg)->pNext != NULL)	// not latest module in queue
//		{
//			psNext = (*ppsCfg)->pNext;		// pointer to next module
//			if (psNext->bApply == FALSE)	// next module not applied
//			{
//				// update link to skip delete module
//				(*ppsCfg)->pNext = (*ppsCfg)->pNext->pNext;
//
//				// delete module
//				if (psNext->lpszAddrOut != NULL)
//				{
//					free(psNext->lpszAddrOut);
//				}
//				free(psNext);
//				return;
//			}
//			ppsCfg = &(*ppsCfg)->pNext;
//		}
//	}
//	return;
//}
//
//static BOOL ApplyPort(HWND hDlg,UINT nPort)
//{
//	PPORTCFG psCfg;
//	DWORD    dwChipSize;
//	BOOL     bSucc;
//	INT      i;
//
//	_ASSERT(nPort < ARRAYSIZEOF(psPortCfg));
//	_ASSERT(psPortCfg[nPort] != NULL);
//
//	psCfg = *CfgModule(nPort);				// module in queue to configure
//
//	// module type combobox
//	VERIFY((i = (INT) SendDlgItemMessage(hDlg,IDC_CFG_TYPE,CB_GETCURSEL,0,0)) != CB_ERR);
//	psCfg->nType = (UINT) sModType[i].dwData;
//
//	// hard wired address
//	psCfg->dwBase = 0x00000;
//
//	// filename
//	GetDlgItemText(hDlg,IDC_CFG_FILE,psCfg->szFileName,ARRAYSIZEOF(psPortCfg[0]->szFileName));
//
//	switch (psCfg->nType)
//	{
//	case TYPE_RAM:
//		if (*psCfg->szFileName == 0)		// empty filename field
//		{
//			// size combobox
//			VERIFY((i = (INT) SendDlgItemMessage(hDlg,IDC_CFG_SIZE,CB_GETCURSEL,0,0)) != CB_ERR);
//			dwChipSize = sMod[i].dwData;
//			bSucc = (dwChipSize != 0);
//		}
//		else								// given filename
//		{
//			LPBYTE pbyData;
//
//			// get RAM size from filename content
//			if ((bSucc = MapFile(psCfg->szFileName,&pbyData,&dwChipSize)))
//			{
//				// independent RAM signature in file header?
//				bSucc = dwChipSize >= 8 && (Npack(pbyData,8) == IRAMSIG);
//				free(pbyData);
//			}
//		}
//		break;
//	case TYPE_HRD:
//		// hard wired address
//		VERIFY((i = (INT) SendDlgItemMessage(hDlg,IDC_CFG_HARDADDR,CB_GETCURSEL,0,0)) != CB_ERR);
//		psCfg->dwBase = sHrdAddr[i].dwData;
//		// no break;
//	case TYPE_ROM:
//	case TYPE_HPIL:
//		// filename
//		bSucc = MapFile(psCfg->szFileName,NULL,&dwChipSize);
//		break;
//	default:
//		_ASSERT(FALSE);
//		dwChipSize = 0;
//		bSucc = FALSE;
//	}
//
//	// no. of chips combobox
//	if ((i = (INT) SendDlgItemMessage(hDlg,IDC_CFG_CHIPS,CB_GETCURSEL,0,0)) == CB_ERR)
//		i = 0;								// no one selected, choose "Auto"
//
//	if (bSucc && i == 0)					// "Auto"
//	{
//		DWORD dwSize;
//
//		switch (psCfg->nType)
//		{
//		case TYPE_RAM:
//			// can be build out of 32KB chips
//			dwSize = ((dwChipSize % (32 * 2048)) == 0)
//				   ? (32 * 2048)			// use 32KB chips
//				   : ( 1 * 2048);			// use 1KB chips
//
//			if (dwChipSize < dwSize)		// 512 Byte Memory
//				dwSize = dwChipSize;
//			break;
//		case TYPE_HRD:
//		case TYPE_ROM:
//		case TYPE_HPIL:
//			// can be build out of 16KB chips
//			dwSize = ((dwChipSize % (16 * 2048)) == 0)
//				   ? (16 * 2048)			// use 16KB chips
//				   : dwChipSize;			// use a single chip
//			break;
//		default:
//			_ASSERT(FALSE);
//			dwSize = 1;
//		}
//
//		i = dwChipSize / dwSize;			// calculate no. of chips
//	}
//
//	psCfg->dwChips = i;						// set no. of chips
//
//	if (bSucc)								// check size vs. no. of chips
//	{
//		DWORD dwSingleSize;
//
//		// check if the overall size is a multiple of a chip size
//		bSucc = (dwChipSize % psCfg->dwChips) == 0;
//
//		// check if the single chip has a power of 2 size
//		VERIFY((dwSingleSize = dwChipSize / psCfg->dwChips));
//		bSucc = bSucc && dwSingleSize != 0 && (dwSingleSize & (dwSingleSize - 1)) == 0;
//
//		if (!bSucc)
//		{
//			InfoMessage(_T("Number of chips don't fit to the overall size!"));
//		}
//	}
//
//	if (bSucc)
//	{
//		_ASSERT(nPort < ARRAYSIZEOF(bChanged));
//		bChanged[nPort]   = TRUE;
//		psCfg->dwSize = dwChipSize;
//		psCfg->bApply = TRUE;
//
//		// set focus on "Add" button
//		SetFocus(GetDlgItem(hDlg,IDC_CFG_ADD));
//	}
//	return bSucc;
//}
//
//BOOL GetOpenImageFile(HWND hWnd,LPTSTR szBuffer,DWORD dwBufferSize)
//{
//	TCHAR  szFilename[MAX_PATH];
//	LPTSTR lpFilePart;
//
//	OPENFILENAME ofn;
//
//	ZeroMemory((LPVOID) &ofn, sizeof(OPENFILENAME));
//	ofn.lStructSize = sizeof(OPENFILENAME);
//	ofn.hwndOwner = hWnd;
//	ofn.lpstrFilter =
//		_T("HP-71B ROM/RAM Files (*.bin)\0*.bin\0")
//		_T("All Files (*.*)\0*.*\0");
//	ofn.lpstrDefExt = _T("bin");
//	ofn.nFilterIndex = 1;
//	ofn.lpstrFile = szBuffer;
//	ofn.lpstrFile[0] = 0;
//	ofn.nMaxFile = dwBufferSize;
//	ofn.Flags = OFN_EXPLORER|OFN_HIDEREADONLY|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
//	if (GetOpenFileName(&ofn) == FALSE) return FALSE;
//
//	// check if file path and Emu71 directory path is identical
//	if (GetFullPathName(szBuffer,ARRAYSIZEOF(szFilename),szFilename,&lpFilePart))
//	{
//		*(lpFilePart-1) = 0;				// devide path and name
//
//		// name is in the Emu71 directory -> use only name
//		if (lstrcmpi(szEmuDirectory,szFilename) == 0)
//			lstrcpy(szBuffer,lpFilePart);
//	}
//	return TRUE;
//}
//
//static BOOL GetSaveImageFile(HWND hWnd,LPTSTR szBuffer,DWORD dwBufferSize)
//{
//	TCHAR  szFilename[MAX_PATH];
//	LPTSTR lpFilePart;
//
//	OPENFILENAME ofn;
//
//	ZeroMemory((LPVOID) &ofn, sizeof(OPENFILENAME));
//	ofn.lStructSize = sizeof(OPENFILENAME);
//	ofn.hwndOwner = hWnd;
//	ofn.lpstrFilter =
//		_T("HP-71B ROM/RAM Files (*.bin)\0*.bin\0")
//		_T("All Files (*.*)\0*.*\0");
//	ofn.lpstrDefExt = _T("bin");
//	ofn.nFilterIndex = 1;
//	ofn.lpstrFile = szBuffer;
//	ofn.lpstrFile[0] = 0;
//	ofn.nMaxFile = dwBufferSize;
//	ofn.Flags = OFN_EXPLORER|OFN_HIDEREADONLY|OFN_CREATEPROMPT|OFN_OVERWRITEPROMPT;
//	if (GetSaveFileName(&ofn) == FALSE) return FALSE;
//
//	// check if file path and Emu71 directory path is identical
//	if (GetFullPathName(szBuffer,ARRAYSIZEOF(szFilename),szFilename,&lpFilePart))
//	{
//		*(lpFilePart-1) = 0;				// devide path and name
//
//		// name is in the Emu71 directory -> use only name
//		if (lstrcmpi(szEmuDirectory,szFilename) == 0)
//			lstrcpy(szBuffer,lpFilePart);
//	}
//	return TRUE;
//}
//
//static LRESULT OnBrowse(HWND hDlg)
//{
//	TCHAR szBuffer[MAX_PATH];
//
//	if (GetOpenImageFile(hDlg,szBuffer,ARRAYSIZEOF(szBuffer)) == TRUE)
//	{
//		SetDlgItemText(hDlg,IDC_CFG_FILE,szBuffer);
//	}
//	return 0;
//}
//
//static LRESULT OnPortCfgDataLoad(HWND hDlg)
//{
//	TCHAR szBuffer[MAX_PATH];
//
//	if (GetOpenImageFile(hDlg,szBuffer,ARRAYSIZEOF(szBuffer)) == TRUE)
//	{
//		INT nItem;
//
//		HWND hWnd = GetDlgItem(hDlg,IDC_CFG_PORTDATA);
//
//		// something selected
//		if ((nItem = (INT) SendMessage(hWnd,LB_GETCURSEL,0,0)) != LB_ERR)
//		{
//			LPBYTE pbyData;
//			DWORD  dwSize;
//
//			PPORTCFG psCfg = (PPORTCFG) SendMessage(hWnd,LB_GETITEMDATA,nItem,0);
//			_ASSERT(psCfg != NULL);			// item has data
//
//			// RAM with data
//			_ASSERT(psCfg->nType == TYPE_RAM && psCfg->pbyData != NULL);
//
//			if (MapFile(szBuffer,&pbyData,&dwSize) == TRUE)
//			{
//				// different size or not independent RAM signature
//				if (psCfg->dwSize != dwSize || Npack(pbyData,8) != IRAMSIG)
//				{
//					free(pbyData);
//					AbortMessage(_T("This file cannot be loaded."));
//					return 0;
//				}
//
//				Chipset.HST |= MP;			// module pulled
//
//				// overwrite the data in the port memory
//				CopyMemory(psCfg->pbyData,pbyData,psCfg->dwSize);
//				free(pbyData);
//			}
//		}
//	}
//	return 0;
//}
//
//static LRESULT OnPortCfgDataSave(HWND hDlg)
//{
//	TCHAR szBuffer[MAX_PATH];
//
//	if (GetSaveImageFile(hDlg,szBuffer,ARRAYSIZEOF(szBuffer)) == TRUE)
//	{
//		INT nItem;
//
//		HWND hWnd = GetDlgItem(hDlg,IDC_CFG_PORTDATA);
//
//		// something selected
//		if ((nItem = (INT) SendMessage(hWnd,LB_GETCURSEL,0,0)) != LB_ERR)
//		{
//			HANDLE hFile;
//			DWORD  dwPos,dwWritten;
//			BYTE   byData;
//
//			PPORTCFG psCfg = (PPORTCFG) SendMessage(hWnd,LB_GETITEMDATA,nItem,0);
//			_ASSERT(psCfg != NULL);			// item has data
//
//			// RAM with data
//			_ASSERT(psCfg->nType == TYPE_RAM && psCfg->pbyData != NULL);
//
//			SetCurrentDirectory(szEmuDirectory);
//			hFile = CreateFile(szBuffer,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
//			SetCurrentDirectory(szCurrentDirectory);
//
//			// error, couldn't create a new file
//			if (hFile == INVALID_HANDLE_VALUE)
//			{
//				AbortMessage(_T("This file cannot be created."));
//				return 0;
//			}
//
//			for (dwPos = 0; dwPos < psCfg->dwSize; dwPos += 2)
//			{
//				byData = (psCfg->pbyData[dwPos+1] << 4) | psCfg->pbyData[dwPos];
//				WriteFile(hFile,&byData,sizeof(byData),&dwWritten,NULL);
//			}
//			CloseHandle(hFile);
//		}
//	}
//	return 0;
//}
//
//static LRESULT OnPortCfgTcpIpSettings(HWND hDlg)
//{
//	INT nItem;
//
//	HWND hWnd = GetDlgItem(hDlg,IDC_CFG_PORTDATA);
//
//	// something selected
//	if ((nItem = (INT) SendMessage(hWnd,LB_GETCURSEL,0,0)) != LB_ERR)
//	{
//		PPORTCFG psCfg = (PPORTCFG) SendMessage(hWnd,LB_GETITEMDATA,nItem,0);
//		_ASSERT(psCfg != NULL);				// item has data
//		_ASSERT(psCfg->nType == TYPE_HPIL);	// must be a HPIL module
//
//		if (OnEditTcpIpSettings(hDlg,psCfg) == IDOK)
//		{
//			if (psCfg->psTcp != NULL)
//			{
//				// modify the original data to avoid a configuration changed on the whole module
//				free(psCfg->psTcp->lpszAddrOut);
//				psCfg->psTcp->dwAddrSize = (DWORD) strlen(psCfg->lpszAddrOut);
//				psCfg->psTcp->lpszAddrOut = (LPSTR) malloc(psCfg->psTcp->dwAddrSize+1);
//				CopyMemory(psCfg->psTcp->lpszAddrOut,psCfg->lpszAddrOut,psCfg->psTcp->dwAddrSize+1);
//				psCfg->psTcp->wPortOut = psCfg->wPortOut;
//				psCfg->psTcp->wPortIn  = psCfg->wPortIn;
//			}
//
//			ShowPortConfig(hDlg,nActPort);	// redraw settings
//		}
//	}
//	return 0;
//}
//
////
//// request for context menu
////
//static VOID OnContextMenu(HWND hDlg, LPARAM lParam, WPARAM wParam)
//{
//	POINT pt;
//	INT   nId;
//
//	POINTSTOPOINT(pt,MAKEPOINTS(lParam));	// mouse position
//	nId = GetDlgCtrlID((HWND) wParam);		// control ID of window
//
//	if (nId == IDC_CFG_PORTDATA)			// handle data window
//	{
//		INT nItem;
//
//		HWND hWnd = GetDlgItem(hDlg,IDC_CFG_PORTDATA);
//
//		// something selected
//		if ((nItem = (INT) SendMessage(hWnd,LB_GETCURSEL,0,0)) != LB_ERR)
//		{
//			PPORTCFG psCfg = (PPORTCFG) SendMessage(hWnd,LB_GETITEMDATA,nItem,0);
//
//			if (psCfg != NULL)				// item has data
//			{
//				HMENU hMenu;				// top-level menu
//				HMENU hMenuTrackPopup;		// shortcut menu
//
//				UINT uEnable = MF_GRAYED;
//
//				if (psCfg->nType != TYPE_HPIL)
//				{
//					// RAM with data
//					if (psCfg->nType == TYPE_RAM && psCfg->pbyData != NULL)
//					{
//						// independent RAM signature?
//						if (psCfg->dwSize >= 8 && Npack(psCfg->pbyData,8) == IRAMSIG)
//						{
//							uEnable = MF_ENABLED;
//						}
//					}
//
//					// load data context menu
//					VERIFY(hMenu = LoadMenu(hApp,MAKEINTRESOURCE(IDR_PORTCFG_DATA)));
//					VERIFY(hMenuTrackPopup = GetSubMenu(hMenu,0));
//
//					EnableMenuItem(hMenuTrackPopup,ID_PORTCFG_DATA_LOAD,uEnable);
//					EnableMenuItem(hMenuTrackPopup,ID_PORTCFG_DATA_SAVE,uEnable);
//				}
//				else
//				{
//					// load TCP/TP context menu
//					VERIFY(hMenu = LoadMenu(hApp,MAKEINTRESOURCE(IDR_PORTCFG_TCPIP)));
//					VERIFY(hMenuTrackPopup = GetSubMenu(hMenu,0));
//				}
//
//				TrackPopupMenu(hMenuTrackPopup,TPM_LEFTALIGN | TPM_LEFTBUTTON,
//							   pt.x,pt.y,0,hDlg,NULL);
//
//				DestroyMenu(hMenu);
//			}
//		}
//	}
//	return;
//}
//
//
////################
////#
////#    Tcp/ip Settings
////#
////################
//
//static INT_PTR CALLBACK TcpIpSettingsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
//{
//	PPORTCFG psCfg;
//	TCHAR szBuffer[2048];
//	UINT  nLength;
//
//	switch (message)
//	{
//	case WM_INITDIALOG:
//		psCfg = (PPORTCFG) lParam;
//		SetWindowLongPtr(hDlg,GWLP_USERDATA,(LONG_PTR) psCfg);
//		_ASSERT(psCfg->nType == TYPE_HPIL);	// must be HPIL module
//		#if defined _UNICODE
//		{
//			UINT nLength = ((DWORD) strlen(psCfg->lpszAddrOut) + 1) * sizeof(TCHAR);
//			LPTSTR szTmp = (LPTSTR) malloc(nLength);
//			MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, psCfg->lpszAddrOut, -1,
//								szTmp, nLength);
//			SetDlgItemText(hDlg,IDC_CFG_ADDR_OUT,szTmp);
//			free(szTmp);
//		}
//		#else
//		{
//			SetDlgItemText(hDlg,IDC_CFG_ADDR_OUT,psCfg->lpszAddrOut);
//		}
//		#endif
//		wsprintf(szBuffer,_T("%u"),psCfg->wPortOut);
//		SetDlgItemText(hDlg,IDC_CFG_PORT_OUT,szBuffer);
//		wsprintf(szBuffer,_T("%u"),psCfg->wPortIn);
//		SetDlgItemText(hDlg,IDC_CFG_PORT_IN,szBuffer);
//		return TRUE;
//	case WM_COMMAND:
//		switch (LOWORD(wParam))
//		{
//		case IDOK:
//			psCfg = (PPORTCFG) GetWindowLongPtr(hDlg,GWLP_USERDATA);
//			GetDlgItemText(hDlg,IDC_CFG_ADDR_OUT,szBuffer,ARRAYSIZEOF(szBuffer));
//			nLength = lstrlen(szBuffer) + 1;
//			free(psCfg->lpszAddrOut);
//			psCfg->lpszAddrOut = (LPSTR) malloc(nLength);
//			#if defined _UNICODE
//			{
//				WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK,
//									szBuffer, nLength,
//									psCfg->lpszAddrOut, nLength, NULL, NULL);
//			}
//			#else
//			{
//				CopyMemory(psCfg->lpszAddrOut,szBuffer,nLength);
//			}
//			#endif
//			GetDlgItemText(hDlg,IDC_CFG_PORT_OUT,szBuffer,ARRAYSIZEOF(szBuffer));
//			psCfg->wPortOut = (WORD) _ttoi(szBuffer);
//			GetDlgItemText(hDlg,IDC_CFG_PORT_IN,szBuffer,ARRAYSIZEOF(szBuffer));
//			psCfg->wPortIn = (WORD) _ttoi(szBuffer);
//			// no break
//		case IDCANCEL:
//			EndDialog(hDlg, LOWORD(wParam));
//		}
//		break;
//	}
//	return FALSE;
//	UNREFERENCED_PARAMETER(lParam);
//}
//
////
//// IDC_CFG_TCPIP
////
//static INT_PTR OnEditTcpIpSettings(HWND hDlg,PPORTCFG psCfg)
//{
//	INT_PTR nResult;
//
//	_ASSERT(psCfg->nType == TYPE_HPIL);		// must be a HPIL module
//
//	if ((nResult = DialogBoxParam(hApp,
//								  MAKEINTRESOURCE(IDD_TCPIP_SETTINGS),
//								  hDlg,
//								  (DLGPROC)TcpIpSettingsProc,
//								  (LPARAM)psCfg)) == -1)
//		AbortMessage(_T("TCP/IP Settings Dialog Creation Error !"));
//	return nResult;
//}
//
//
////################
////#
////#    Port Settings
////#
////################
//
//static INT_PTR CALLBACK PortSettingsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
//{
//	PPORTCFG *ppsCfg;
//	UINT     i;
//
//	switch (message)
//	{
//	case WM_INITDIALOG:
//		// load current port data structure
//		LoadCurrPortConfig();
//
//		// init port combo box
//		for (i = 0; i < ARRAYSIZEOF(lpszPorts); ++i)
//		{
//			SendDlgItemMessage(hDlg,IDC_CFG_SELPORT,CB_ADDSTRING,0,(LPARAM) lpszPorts[i]);
//		}
//		SendDlgItemMessage(hDlg,IDC_CFG_SELPORT,CB_SETCURSEL,1,0);
//		SendMessage(hDlg,WM_COMMAND,MAKEWPARAM(IDC_CFG_SELPORT,CBN_SELENDOK),0);
//		return TRUE;
//	case WM_COMMAND:
//		switch (LOWORD(wParam))
//		{
//		case IDC_CFG_ADD:
//			ppsCfg = &psPortCfg[nActPort];	// root of module
//			{
//				INT nItem,nIndex;
//
//				// something selected
//				if ((nItem = (INT) SendDlgItemMessage(hDlg,IDC_CFG_PORTDATA,LB_GETCURSEL,0,0)) != LB_ERR)
//				{
//					PPORTCFG psCfgIns;
//
//					// goto selected entry in the queue
//					for (nIndex = 0; nIndex < nItem && *ppsCfg != NULL; ++nIndex)
//					{
//						ppsCfg = &(*ppsCfg)->pNext;
//					}
//
//					// allocate memory for new module definition and insert it at position
//					psCfgIns = (PPORTCFG) calloc(1,sizeof(*psPortCfg[0]));
//					psCfgIns->pNext = *ppsCfg;
//					*ppsCfg = psCfgIns;
//				}
//				else						// nothing selected
//				{
//					// goto last entry in the queue
//					while (*ppsCfg != NULL)
//					{
//						ppsCfg = &(*ppsCfg)->pNext;
//					}
//
//					// allocate memory for new module definition and add it at last position
//					*ppsCfg = (PPORTCFG) calloc(1,sizeof(*psPortCfg[0]));
//				}
//
//				// new module
//				(*ppsCfg)->nIndex = (UINT) -1;
//			}
//
//			// default 32KB RAM with 1LQ4 interface chip
//			(*ppsCfg)->nType   = TYPE_RAM;
//			(*ppsCfg)->dwSize  = 32 * 2048;
//			(*ppsCfg)->dwChips = 1;
//			(*ppsCfg)->dwBase  = 0x00000;
//			return OnAddPort(hDlg,nActPort);
//		case IDC_CFG_DEL:
//			_ASSERT(psPortCfg[nActPort] != NULL);
//
//			// if a module is not applied the button is working in the "Abort" context
//			if ((*CfgModule(nActPort))->bApply == FALSE)
//			{
//				DelPortCfg(nActPort);		// delete the not applied module
//			}
//			else							// "Delete" context
//			{
//				INT nItem,nIndex;
//
//				_ASSERT(nActPort < ARRAYSIZEOF(bChanged));
//				bChanged[nActPort] = TRUE;
//
//				// something selected
//				if ((nItem = (INT) SendDlgItemMessage(hDlg,IDC_CFG_PORTDATA,LB_GETCURSEL,0,0)) != LB_ERR)
//				{
//					// root of module
//					ppsCfg = &psPortCfg[nActPort];
//
//					// goto selected entry in the queue
//					for (nIndex = 0; nIndex < nItem && *ppsCfg != NULL; ++nIndex)
//					{
//						ppsCfg = &(*ppsCfg)->pNext;
//					}
//
//					if (*ppsCfg != NULL)
//					{
//						// mark entry as not applied that DelPortCfg() can delete it
//						(*ppsCfg)->bApply = FALSE;
//						DelPortCfg(nActPort); // delete the not applied module
//					}
//				}
//				else						// nothing selected
//				{
//					DelPort(nActPort);		// delete port data
//				}
//			}
//			return ShowPortConfig(hDlg,nActPort);
//		case IDC_CFG_APPLY:
//			// apply port data
//			if (ApplyPort(hDlg,nActPort) == FALSE)
//			{
//				return OnAddPort(hDlg,nActPort);
//			}
//			return ShowPortConfig(hDlg,nActPort);
//		case IDC_CFG_TYPE:
//			// new combo box item selected
//			if (HIWORD(wParam) == CBN_SELENDOK)
//			{
//				// fetch module in queue to configure
//				_ASSERT(psPortCfg[nActPort] != NULL);
//				ppsCfg = CfgModule(nActPort);
//
//				(*ppsCfg)->nType  = (UINT) sModType[SendDlgItemMessage(hDlg,IDC_CFG_TYPE,CB_GETCURSEL,0,0)].dwData;
//				(*ppsCfg)->dwBase = ((*ppsCfg)->nType == TYPE_HRD)
//								  ? 0xE0000
//								  : 0x00000;
//				return OnAddPort(hDlg,nActPort);
//			}
//			return TRUE;
//		case IDC_CFG_SIZE:
//			// new combo box item selected
//			if (HIWORD(wParam) == CBN_SELENDOK)
//			{
//				INT nItem;
//
//				// fetch module in queue to configure
//				_ASSERT(psPortCfg[nActPort] != NULL);
//				ppsCfg = CfgModule(nActPort);
//
//				// fetch combo box selection
//				VERIFY((nItem = (INT) SendDlgItemMessage(hDlg,IDC_CFG_SIZE,CB_GETCURSEL,0,0)) != CB_ERR);
//
//				// get new size
//				_ASSERT(nItem < ARRAYSIZEOF(sMod));
//				(*ppsCfg)->dwSize = sMod[nItem].dwData;
//
//				// reconfigure dialog settings
//				return OnAddPort(hDlg,nActPort);
//			}
//			return TRUE;
//		case IDC_CFG_BROWSE:   return OnBrowse(hDlg);
//		case IDC_CFG_SELPORT:
//			// new combo box item selected
//			if (HIWORD(wParam) == CBN_SELENDOK)
//			{
//				if (psPortCfg[nActPort] != NULL)
//				{
//					if ((*CfgModule(nActPort))->bApply == FALSE)
//					{
//						// delete the not applied module
//						DelPortCfg(nActPort);
//					}
//				}
//
//				nActPort = (UINT) SendDlgItemMessage(hDlg,IDC_CFG_SELPORT,CB_GETCURSEL,0,0);
//				_ASSERT(nActPort < ARRAYSIZEOF(lpszPorts));
//				return ShowPortConfig(hDlg,nActPort);
//			}
//			break;
//		case IDC_CFG_TCPIP:
//			_ASSERT(nActPort < ARRAYSIZEOF(psPortCfg));
//			_ASSERT(psPortCfg[nActPort] != NULL);
//			ppsCfg = CfgModule(nActPort);	// module in queue to configure
//			// must be a HPIL module
//			_ASSERT((*ppsCfg)->nType == TYPE_HPIL);
//			OnEditTcpIpSettings(hDlg,*ppsCfg);
//			return 0;
//		case ID_PORTCFG_DATA_LOAD:      return OnPortCfgDataLoad(hDlg);
//		case ID_PORTCFG_DATA_SAVE:      return OnPortCfgDataSave(hDlg);
//		case ID_PORTCFG_TCPIP_SETTINGS: return OnPortCfgTcpIpSettings(hDlg);
//		case IDOK:
//			SaveCurrPortConfig();			// update port data structure
//			// no break
//		case IDCANCEL:
//			Cleanup();						// clean up settings structure
//			EndDialog(hDlg, LOWORD(wParam));
//		}
//		break;
//	case WM_CONTEXTMENU:
//		OnContextMenu(hDlg,lParam,wParam);
//		break;
//	}
//	return FALSE;
//	UNREFERENCED_PARAMETER(lParam);
//}
//
////
//// ID_EDIT_PORTCONFIG
////
//LRESULT OnEditPortConfig(VOID)
//{
//	UINT nOldState = SwitchToState(SM_INVALID);
//
//	DismountPorts();						// dismount the ports
//
//	if (DialogBox(hApp, MAKEINTRESOURCE(IDD_PORTCFG), hWnd, (DLGPROC)PortSettingsProc) == -1)
//		AbortMessage(_T("Port Settings Dialog Creation Error !"));
//
//	MountPorts();							// remount the ports
//
//	SwitchToState(nOldState);
//	return 0;
//}
