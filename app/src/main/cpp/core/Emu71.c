/*
 *   Emu71.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gieﬂelink
 *
 */
#include "pch.h"
#include "resource.h"
#include "Emu71.h"
#include "io.h"
#include "kml.h"
#include "debugger.h"

#define VERSION   "1.12"

#ifdef _DEBUG
LPCTSTR szNoTitle = _T("Emu71 ")_T(VERSION)_T(" Debug");
#else
LPCTSTR szNoTitle = _T("Emu71 ")_T(VERSION);
#endif
LPTSTR szTitle   = NULL;

static const LPCTSTR szLicence =
	_T("This program is free software; you can redistribute it and/or modify\r\n")
	_T("it under the terms of the GNU General Public License as published by\r\n")
	_T("the Free Software Foundation; either version 2 of the License, or\r\n")
	_T("(at your option) any later version.\r\n")
	_T("\r\n")
	_T("This program is distributed in the hope that it will be useful,\r\n")
	_T("but WITHOUT ANY WARRANTY; without even the implied warranty of\r\n")
	_T("MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\r\n")
	_T("See the GNU General Public License for more details.\r\n")
	_T("\r\n")
	_T("You should have received a copy of the GNU General Public License along\r\n")
	_T("with this program; if not, write to the Free Software Foundation, Inc.,\r\n")
	_T("51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.");

static BOOL bOwnCursor = FALSE;
static BOOL bTitleBar = TRUE;
static BOOL bMouseButton = FALSE;


CRITICAL_SECTION csGDILock;					// critical section for hWindowDC
CRITICAL_SECTION csLcdLock;					// critical section for display update
CRITICAL_SECTION csKeyLock;					// critical section for key scan
CRITICAL_SECTION csTLock;					// critical section for timer access
CRITICAL_SECTION csBitLock;					// critical section for bit manipulation of a byte
CRITICAL_SECTION csSlowLock;				// critical section for speed slow down
CRITICAL_SECTION csDbgLock;					// critical section for	debugger purpose
INT              nArgc;						// no. of command line arguments
LPCTSTR          *ppArgv;					// command line arguments
LARGE_INTEGER    lFreq;						// high performance counter frequency
HANDLE           hThread;
HANDLE           hEventShutdn;				// event handle to stop cpu thread

HINSTANCE        hApp = NULL;
HWND             hWnd = NULL;
HWND             hDlgDebug = NULL;			// handle for debugger dialog
HWND             hDlgFind = NULL;			// handle for debugger find dialog
HWND             hDlgProfile = NULL;		// handle for debugger profile dialog
HWND             hDlgMemMap = NULL;			// handle for debugger memory mapping  dialog
HDC              hWindowDC = NULL;
HPALETTE         hPalette = NULL;
HPALETTE         hOldPalette = NULL;		// old palette of hWindowDC
DWORD            dwTColor = (DWORD) -1;		// transparency color
DWORD            dwTColorTol = 0;			// transparency color tolerance
HRGN             hRgn = NULL;
HCURSOR          hCursorArrow = NULL;
HCURSOR          hCursorHand = NULL;
UINT             uWaveDevId = WAVE_MAPPER;	// default audio device
DWORD            dwWakeupDelay = 200;		// ON key hold time to switch on calculator
BOOL             bAutoSave = FALSE;
BOOL             bAutoSaveOnExit = TRUE;
BOOL             bSaveDefConfirm = TRUE;	// yes
BOOL             bStartupBackup = FALSE;
BOOL             bAlwaysDisplayLog = TRUE;
BOOL             bShowTitle = TRUE;			// show main window title bar
BOOL             bShowMenu = TRUE;			// show main window menu bar
BOOL             bAlwaysOnTop = FALSE;		// emulator window always on top
BOOL             bActFollowsMouse = FALSE;	// emulator window activation follows mouse
BOOL             bClientWinMove = FALSE;	// emulator window can be moved over client area
BOOL             bSingleInstance = FALSE;	// multiple emulator instances allowed


//################
//#
//#    Window Status
//#
//################

VOID SetWindowTitle(LPCTSTR szString)
{
	if (szTitle) free(szTitle);

	_ASSERT(hWnd != NULL);
	if (szString)
	{
		szTitle = DuplicateString(szString);
		SetWindowText(hWnd, szTitle);
	}
	else
	{
		szTitle = NULL;
		SetWindowText(hWnd, szNoTitle);
	}
	return;
}

VOID ForceForegroundWindow(HWND hWnd)
{
	// force window to foreground
	DWORD dwEmuThreadID = GetCurrentThreadId();
	DWORD dwActThreadID = GetWindowThreadProcessId(GetForegroundWindow(),NULL);

	AttachThreadInput(dwEmuThreadID,dwActThreadID,TRUE);
	SetForegroundWindow(hWnd);
	AttachThreadInput(dwEmuThreadID,dwActThreadID,FALSE);
	return;
}

static __inline VOID UpdateWindowBars(VOID)
{
	DWORD dwStyle;
	HMENU hMenu;

	BOOL bUpdate = FALSE;					// no update

	// get current title bar style
	dwStyle = (DWORD) GetWindowLongPtr(hWnd,GWL_STYLE);
	if ((bTitleBar = (bShowTitle || bDocumentAvail == FALSE)))
	{
		// title bar off
		if ((dwStyle & STYLE_TITLE) != STYLE_TITLE)
		{
			SetWindowLongPtr(hWnd,GWL_STYLE,(dwStyle & ~STYLE_NOTITLE) | STYLE_TITLE);
			bUpdate = TRUE;
		}
	}
	else
	{
		// title bar on
		if ((dwStyle & STYLE_NOTITLE) != STYLE_NOTITLE)
		{
			SetWindowLongPtr(hWnd,GWL_STYLE,(dwStyle & ~STYLE_TITLE) | STYLE_NOTITLE);
			bUpdate = TRUE;
		}
	}

	hMenu = GetMenu(hWnd);					// get system menu
	if (bShowMenu || bDocumentAvail == FALSE)
	{
		if (hMenu == NULL)					// menu off
		{
			// restore menu bar
			SetMenu(hWnd,LoadMenu(hApp,MAKEINTRESOURCE(IDR_MENU)));
			bUpdate = TRUE;
		}
	}
	else
	{
		if (hMenu != NULL)					// menu on
		{
			// close menu bar
			SetMenu(hWnd,NULL);
			VERIFY(DestroyMenu(hMenu));
			bUpdate = TRUE;
		}
	}

	if (dwTColor != (DWORD) -1)				// prepare background bitmap with transparency
	{
		if (!bTitleBar && GetMenu(hWnd) == NULL)
		{
			if (hRgn == NULL)
			{
				EnterCriticalSection(&csGDILock); // solving NT GDI problems
				{
					// enable background bitmap transparency
					hRgn = CreateRgnFromBitmap((HBITMAP) GetCurrentObject(hMainDC,OBJ_BITMAP),
											   dwTColor,
											   dwTColorTol);
					if (hRgn != NULL)		// region definition successful
					{
						OffsetRgn(hRgn,-(INT) nBackgroundX,-(INT) nBackgroundY);
						SetWindowRgn(hWnd,hRgn,TRUE);
					}
					else					// region definition failed
					{
						// disable transparency
						dwTColor = (DWORD) -1;
					}
					GdiFlush();
				}
				LeaveCriticalSection(&csGDILock);
			}
		}
		else
		{
			if (hRgn != NULL)				// region active
			{
				EnterCriticalSection(&csGDILock); // solving NT GDI problems
				{
					// disable background bitmap transparency
					SetWindowRgn(hWnd,NULL,TRUE);
					hRgn = NULL;

					GdiFlush();
				}
				LeaveCriticalSection(&csGDILock);
			}
		}
	}

	if (bUpdate)							// changed state of title or menu bar
	{
		ResizeWindow();						// resize & redraw window
	}
	return;
}



//################
//#
//#    Clipboard Tool
//#
//################

VOID CopyItemsToClipboard(HWND hWnd)		// save selected Listbox Items to Clipboard
{
	LONG  i;
	LPINT lpnCount;

	// get number of selections
	if ((i = (LONG) SendMessage(hWnd,LB_GETSELCOUNT,0,0)) == 0)
		return;								// no items selected

	if ((lpnCount = (LPINT) malloc(i * sizeof(INT))) != NULL)
	{
		LPTSTR lpszData;
		HANDLE hClipObj;
		LONG j,lMem = 0;

		// get indexes of selected items
		i = (LONG) SendMessage(hWnd,LB_GETSELITEMS,i,(LPARAM) lpnCount);
		for (j = 0;j < i;++j)				// scan all selected items
		{
			// calculate total amount of characters
			lMem += (LONG) SendMessage(hWnd,LB_GETTEXTLEN,lpnCount[j],0) + 2;
		}
		// allocate clipboard data
		if ((hClipObj = GlobalAlloc(GMEM_MOVEABLE,(lMem + 1) * sizeof(*lpszData))) != NULL)
		{
			if ((lpszData = (LPTSTR) GlobalLock(hClipObj)))
			{
				for (j = 0;j < i;++j)		// scan all selected items
				{
					lpszData += SendMessage(hWnd,LB_GETTEXT,lpnCount[j],(LPARAM) lpszData);
					*lpszData++ = _T('\r');
					*lpszData++ = _T('\n');
				}
				*lpszData = 0;				// set EOS
				GlobalUnlock(hClipObj);		// unlock memory
			}

			if (OpenClipboard(hWnd))
			{
				if (EmptyClipboard())
					#if defined _UNICODE
						SetClipboardData(CF_UNICODETEXT,hClipObj);
					#else
						SetClipboardData(CF_TEXT,hClipObj);
					#endif
				else
					GlobalFree(hClipObj);
				CloseClipboard();
			}
			else							// clipboard open failed
			{
				GlobalFree(hClipObj);
			}
		}
		free(lpnCount);						// free item table
	}
	return;
}



//################
//#
//#    Settings
//#
//################

static INT_PTR CALLBACK SettingsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	LONG i;
	UINT uDevId;
	HWND hWndInsertAfter;

	switch (message)
	{
	case WM_INITDIALOG:
		// init speed checkbox
		CheckDlgButton(hDlg,IDC_REALSPEED,bRealSpeed);
		CheckDlgButton(hDlg,IDC_SHOWTITLE,bShowTitle);
		CheckDlgButton(hDlg,IDC_SHOWMENU,bShowMenu);
		CheckDlgButton(hDlg,IDC_ALWAYSONTOP,bAlwaysOnTop);
		CheckDlgButton(hDlg,IDC_ACTFOLLOWSMOUSE,bActFollowsMouse);
		CheckDlgButton(hDlg,IDC_SINGLEINSTANCE,bSingleInstance);
		CheckDlgButton(hDlg,IDC_AUTOSAVE,bAutoSave);
		CheckDlgButton(hDlg,IDC_AUTOSAVEONEXIT,bAutoSaveOnExit);
		CheckDlgButton(hDlg,IDC_ALWAYSDISPLOG,bAlwaysDisplayLog);
		// set disassembler mode
		CheckDlgButton(hDlg,(disassembler_mode == HP_MNEMONICS) ? IDC_DISASM_HP : IDC_DISASM_CLASS,BST_CHECKED);
		// set sound slider
		SendDlgItemMessage(hDlg,IDC_SOUND_SLIDER,TBM_SETRANGE,FALSE,MAKELONG(0,255));
		SendDlgItemMessage(hDlg,IDC_SOUND_SLIDER,TBM_SETTICFREQ,256/8,0);
		SendDlgItemMessage(hDlg,IDC_SOUND_SLIDER,TBM_SETPOS,TRUE,dwWaveVol);
		// set sound device
		SetSoundDeviceList(GetDlgItem(hDlg,IDC_SOUND_DEVICE),uWaveDevId);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK:
			// get speed checkbox value
			bRealSpeed = IsDlgButtonChecked(hDlg,IDC_REALSPEED);
			bShowTitle = IsDlgButtonChecked(hDlg,IDC_SHOWTITLE);
			bShowMenu = IsDlgButtonChecked(hDlg,IDC_SHOWMENU);
			bAlwaysOnTop = IsDlgButtonChecked(hDlg,IDC_ALWAYSONTOP);
			bActFollowsMouse = IsDlgButtonChecked(hDlg,IDC_ACTFOLLOWSMOUSE);
			bSingleInstance = IsDlgButtonChecked(hDlg,IDC_SINGLEINSTANCE);
			bAutoSave = IsDlgButtonChecked(hDlg,IDC_AUTOSAVE);
			bAutoSaveOnExit = IsDlgButtonChecked(hDlg,IDC_AUTOSAVEONEXIT);
			bAlwaysDisplayLog = IsDlgButtonChecked(hDlg,IDC_ALWAYSDISPLOG);
			SetSpeed(bRealSpeed);			// set speed
			// set disassembler mode
			disassembler_mode = IsDlgButtonChecked(hDlg,IDC_DISASM_HP) ? HP_MNEMONICS : CLASS_MNEMONICS;
			// set sound data
			dwWaveVol = (DWORD) SendDlgItemMessage(hDlg,IDC_SOUND_SLIDER,TBM_GETPOS,0,0);
			i = (LONG) SendDlgItemMessage(hDlg,IDC_SOUND_DEVICE,CB_GETCURSEL,0,0);
			uDevId = (UINT) SendDlgItemMessage(hDlg,IDC_SOUND_DEVICE,CB_GETITEMDATA,i,0);
			if (uWaveDevId != uDevId)		// sound device id changed
			{
				UINT nActState;

				uWaveDevId = uDevId;		// set new sound device id

				nActState = SwitchToState(SM_SLEEP);

				// restart sound engine with new device id
				SoundClose();				// close waveform-audio output device
				SoundOpen(uWaveDevId);		// open waveform-audio output device

				SwitchToState(nActState);
			}
			// set window Z order
			hWndInsertAfter = bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
			SetWindowPos(hWnd,hWndInsertAfter,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE);
			if (hDlgDebug != NULL)
			{
				SetWindowPos(GetLastActivePopup(hDlgDebug),hWndInsertAfter,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE);
			}
			InvalidateRect(hWnd,NULL,TRUE);
			// no break
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
		}
		break;
	}
	return FALSE;
	UNREFERENCED_PARAMETER(lParam);
}


//################
//#
//#    Save Helper
//#
//################

//
// UINT SaveChanges(BOOL bAuto);
// Return code :
// IDYES    File successfuly saved
// IDNO     File not saved
// IDCANCEL Cancel command
//
static UINT SaveChanges(BOOL bAuto)
{
	UINT uReply;

	if (bDocumentAvail == FALSE) return IDNO;

	if (bAuto)
		uReply = IDYES;
	else
	{
		UINT uStyle = bSaveDefConfirm ? 0 : MB_DEFBUTTON2;
		uReply = YesNoCancelMessage(_T("Do you want to save changes?"),uStyle);
	}

	if (uReply != IDYES) return uReply;

	if (szCurrentFilename[0] == 0)
	{ // Save As...
		if (GetSaveAsFilename())
		{
			if (SaveDocumentAs(szBufferFilename))
				return IDYES;
			else
				return IDCANCEL;
		}
		return IDNO;
	}

	SaveDocument();
	return IDYES;
}



//################
//#
//#    Message Handlers
//#
//################

//
// WM_CREATE
//
static LRESULT OnCreate(HWND hWindow)
{
	InitializeCriticalSection(&csGDILock);
	InitializeCriticalSection(&csLcdLock);
	InitializeCriticalSection(&csKeyLock);
	InitializeCriticalSection(&csTLock);
	InitializeCriticalSection(&csBitLock);
	InitializeCriticalSection(&csSlowLock);
	InitializeCriticalSection(&csDbgLock);

	// load cursors
	hCursorArrow = LoadCursor(NULL,IDC_ARROW);
	hCursorHand  = LoadCursor(NULL,IDC_HAND);
	if (hCursorHand == NULL)
	{
		// for Win95, NT4.0
		bOwnCursor = ((hCursorHand = CreateHandCursor()) != NULL);
	}

	hWnd = hWindow;
	hWindowDC = GetDC(hWnd);
	return 0;
}

//
// WM_DESTROY
//
static LRESULT OnDestroy(HWND hWindow)
{
	DragAcceptFiles(hWnd,FALSE);			// no WM_DROPFILES message any more
	SwitchToState(SM_RETURN);				// exit emulation thread
	SetWindowTitle(NULL);					// free memory of title
	ReleaseDC(hWnd, hWindowDC);
	hWindowDC = NULL;						// hWindowDC isn't valid any more
	hWnd = NULL;

	if (bOwnCursor)							// destroy hand cursor
	{
		DestroyCursor(hCursorHand);
		bOwnCursor = FALSE;
	}

	DeleteCriticalSection(&csGDILock);
	DeleteCriticalSection(&csLcdLock);
	DeleteCriticalSection(&csKeyLock);
	DeleteCriticalSection(&csTLock);
	DeleteCriticalSection(&csBitLock);
	DeleteCriticalSection(&csSlowLock);
	DeleteCriticalSection(&csDbgLock);

	#if defined _USRDLL						// DLL version
		DLLDestroyWnd();					// cleanup system
	#else									// EXE version
		PostQuitMessage(0);					// exit message loop
	#endif
	return 0;
	UNREFERENCED_PARAMETER(hWindow);
}

//
// WM_PAINT
//
static LRESULT OnPaint(HWND hWindow)
{
	PAINTSTRUCT Paint;
	HDC hPaintDC;

	UpdateWindowBars();						// update visibility of title and menu bar

	hPaintDC = BeginPaint(hWindow, &Paint);
	if (hMainDC != NULL)
	{
		INT nxSize,nySize;

		RECT rcMainPaint = Paint.rcPaint;
		rcMainPaint.left   += nBackgroundX;	// coordinates in source bitmap
		rcMainPaint.top    += nBackgroundY;
		rcMainPaint.right  += nBackgroundX;
		rcMainPaint.bottom += nBackgroundY;

		GetSizeLcdBitmap(&nxSize,&nySize);	// get LCD size

		EnterCriticalSection(&csGDILock);	// solving NT GDI problems
		{
			// redraw background bitmap
			BitBlt(hPaintDC, Paint.rcPaint.left, Paint.rcPaint.top,
				   Paint.rcPaint.right-Paint.rcPaint.left, Paint.rcPaint.bottom-Paint.rcPaint.top,
				   hMainDC, rcMainPaint.left, rcMainPaint.top, SRCCOPY);
			// redraw display area
			BitBlt(hPaintDC, nLcdX - nBackgroundX, nLcdY  - nBackgroundY, nxSize, nySize,
				   hLcdDC, 0, 0, SRCCOPY);
			GdiFlush();
		}
		LeaveCriticalSection(&csGDILock);
		UpdateAnnunciators(0xFFFFFFFF);
		RefreshButtons(&rcMainPaint);
	}
	EndPaint(hWindow, &Paint);
	return 0;
}

//
// WM_INITMENU
//
static LRESULT OnInitMenu(HMENU hMenu)
{
	#define flCALC	64						// CALC mode?

	// enable stack loading on HP71B with ROM enabled
	BOOL bStackEnable = (cCurrentRomType == 'T') && Chipset.bOD == FALSE;
	BOOL bRun         = nState == SM_RUN || nState == SM_SLEEP;
	BOOL bCalcMode    = GetSystemFlag(flCALC);

	UINT uStackCopyEn  = (bRun && bStackEnable              ) ? MF_ENABLED : MF_GRAYED;
	UINT uStackPasteEn = (bRun && bStackEnable && !bCalcMode) ? MF_ENABLED : MF_GRAYED;
	UINT uRun          = bRun                                 ? MF_ENABLED : MF_GRAYED;
	UINT uBackup       = bBackup                              ? MF_ENABLED : MF_GRAYED;

	EnableMenuItem(hMenu,ID_FILE_NEW,MF_ENABLED);
	EnableMenuItem(hMenu,ID_FILE_OPEN,MF_ENABLED);
	EnableMenuItem(hMenu,ID_FILE_SAVE,(bRun && szCurrentFilename[0]) ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hMenu,ID_FILE_SAVEAS,uRun);
	EnableMenuItem(hMenu,ID_FILE_CLOSE,uRun);
	EnableMenuItem(hMenu,ID_VIEW_COPY,uRun);
	EnableMenuItem(hMenu,ID_EDIT_PORTCONFIG,uRun);
	EnableMenuItem(hMenu,ID_STACK_COPY,uStackCopyEn);
	EnableMenuItem(hMenu,ID_STACK_PASTE,uStackPasteEn);
	EnableMenuItem(hMenu,ID_VIEW_RESET,uRun);
	EnableMenuItem(hMenu,ID_BACKUP_SAVE,uRun);
	EnableMenuItem(hMenu,ID_BACKUP_RESTORE,uBackup);
	EnableMenuItem(hMenu,ID_BACKUP_DELETE,uBackup);
	EnableMenuItem(hMenu,ID_VIEW_SCRIPT,uRun);
	EnableMenuItem(hMenu,ID_TOOL_DISASM,uRun);
	EnableMenuItem(hMenu,ID_TOOL_DEBUG,(bRun && nDbgState == DBG_OFF) ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hMenu,ID_TOOL_MACRO_RECORD,(bRun && nMacroState == MACRO_OFF) ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hMenu,ID_TOOL_MACRO_PLAY,(bRun && nMacroState == MACRO_OFF) ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(hMenu,ID_TOOL_MACRO_STOP,(bRun && nMacroState != MACRO_OFF) ? MF_ENABLED : MF_GRAYED);

	MruUpdateMenu(hMenu);					// update MRU list
	return 0;
	#undef flCALC
}

//
// ID_FILE_NEW
//
static LRESULT OnFileNew(VOID)
{
	if (bDocumentAvail)
	{
		SwitchToState(SM_INVALID);
		if (IDCANCEL == SaveChanges(bAutoSave))
			goto cancel;
	}
	if (NewDocument()) SetWindowTitle(_T("Untitled"));
cancel:
	if (pbyRom) SwitchToState(SM_RUN);
	return 0;
}

//
// ID_FILE_OPEN
//
static LRESULT OnFileOpen(VOID)
{
	if (bDocumentAvail)
	{
		SwitchToState(SM_INVALID);
		if (IDCANCEL == SaveChanges(bAutoSave))
			goto cancel;
	}
	if (GetOpenFilename())
	{
		if (OpenDocument(szBufferFilename))
			MruAdd(szBufferFilename);
	}
cancel:
	if (pbyRom) SwitchToState(SM_RUN);
	return 0;
}

//
// ID_FILE_MRU_FILE1
//
static LRESULT OnFileMruOpen(UINT wID)
{
	LPCTSTR lpszFilename;

	wID -= ID_FILE_MRU_FILE1;				// zero based MRU index
	lpszFilename = MruFilename(wID);		// full filename from MRU list
	if (lpszFilename == NULL) return 0;		// MRU slot not filled

	if (bDocumentAvail)
	{
		SwitchToState(SM_INVALID);
		if (IDCANCEL == SaveChanges(bAutoSave))
			goto cancel;
	}
	if (!OpenDocument(lpszFilename))		// document loading failed
	{
		MruRemove(wID);						// entry not valid any more
	}
	else
	{
		MruMoveTop(wID);					// move entry to top of MRU list
	}
cancel:
	if (pbyRom) SwitchToState(SM_RUN);
	return 0;
}

//
// ID_FILE_SAVE
//
static LRESULT OnFileSave(VOID)
{
	if (bDocumentAvail)
	{
		SwitchToState(SM_INVALID);
		SaveChanges(TRUE);
		SwitchToState(SM_RUN);
	}
	return 0;
}

//
// ID_FILE_SAVEAS
//
static LRESULT OnFileSaveAs(VOID)
{
	if (bDocumentAvail)
	{
		SwitchToState(SM_INVALID);
		if (GetSaveAsFilename())
		{
			if (SaveDocumentAs(szBufferFilename))
				MruAdd(szCurrentFilename);
		}
		SwitchToState(SM_RUN);
	}
	return 0;
}

//
// ID_FILE_CLOSE
//
static LRESULT OnFileClose(VOID)
{
	if (bDocumentAvail)
	{
		SwitchToState(SM_INVALID);
		if (SaveChanges(bAutoSave) != IDCANCEL)
		{
			ResetDocument();
			SetWindowTitle(NULL);
		}
		else
		{
			SwitchToState(SM_RUN);
		}
	}
	return 0;
}

//
// ID_FILE_EXIT
//
// WM_SYS_CLOSE
//
static LRESULT OnFileExit(VOID)
{
	SwitchToState(SM_INVALID);				// hold emulation thread
	if (SaveChanges(bAutoSaveOnExit) == IDCANCEL)
	{
		SwitchToState(SM_RUN);				// on cancel restart emulation thread
		return 0;
	}
	DestroyWindow(hWnd);
	return 0;
}

//
// ID_VIEW_COPY
//
static LRESULT OnViewCopy(VOID)
{
	if (OpenClipboard(hWnd))
	{
		INT nxSize,nySize;

		GetSizeLcdBitmap(&nxSize,&nySize);	// get LCD size

		if (EmptyClipboard())
		{
			// DIB bitmap
			#define WIDTHBYTES(bits) (((bits) + 31) / 32 * 4)
			#define PALVERSION       0x300

			BITMAP bm;
			LPBITMAPINFOHEADER lpbi;
			PLOGPALETTE ppal;
			HBITMAP hBmp;
			HDC hBmpDC;
			HANDLE hClipObj;
			WORD wBits;
			DWORD dwLen, dwSizeImage;

			hBmp = CreateCompatibleBitmap(hLcdDC,nxSize,nySize);
			hBmpDC = CreateCompatibleDC(hLcdDC);
			hBmp = (HBITMAP) SelectObject(hBmpDC,hBmp);
			EnterCriticalSection(&csGDILock); // solving NT GDI problems
			{
				// copy display area
				BitBlt(hBmpDC,0,0,nxSize,nySize,hLcdDC,0,0,SRCCOPY);
				GdiFlush();
			}
			LeaveCriticalSection(&csGDILock);
			hBmp = (HBITMAP) SelectObject(hBmpDC,hBmp);

			// fill BITMAP structure for size information
			GetObject(hBmp, sizeof(bm), &bm);

			wBits = bm.bmPlanes * bm.bmBitsPixel;
			// make sure bits per pixel is valid
			if (wBits <= 1)
				wBits = 1;
			else if (wBits <= 4)
				wBits = 4;
			else if (wBits <= 8)
				wBits = 8;
			else // if greater than 8-bit, force to 24-bit
				wBits = 24;

			dwSizeImage = WIDTHBYTES((DWORD)bm.bmWidth * wBits) * bm.bmHeight;

			// calculate memory size to store CF_DIB data
			dwLen = sizeof(BITMAPINFOHEADER) + dwSizeImage;
			if (wBits != 24)				// a 24 bitcount DIB has no color table
			{
				// add size for color table
				dwLen += (DWORD) (1 << wBits) * sizeof(RGBQUAD);
			}

			// memory allocation for clipboard data
			if ((hClipObj = GlobalAlloc(GMEM_MOVEABLE, dwLen)) != NULL)
			{
				lpbi = (LPBITMAPINFOHEADER) GlobalLock(hClipObj);
				// initialize BITMAPINFOHEADER
				lpbi->biSize = sizeof(BITMAPINFOHEADER);
				lpbi->biWidth = bm.bmWidth;
				lpbi->biHeight = bm.bmHeight;
				lpbi->biPlanes = 1;
				lpbi->biBitCount = wBits;
				lpbi->biCompression = BI_RGB;
				lpbi->biSizeImage = dwSizeImage;
				lpbi->biXPelsPerMeter = 0;
				lpbi->biYPelsPerMeter = 0;
				lpbi->biClrUsed = 0;
				lpbi->biClrImportant = 0;
				// get bitmap color table and bitmap data
				GetDIBits(hBmpDC, hBmp, 0, lpbi->biHeight, (LPBYTE)lpbi + dwLen - dwSizeImage,
						  (LPBITMAPINFO)lpbi, DIB_RGB_COLORS);
				GlobalUnlock(hClipObj);
				SetClipboardData(CF_DIB, hClipObj);

				// get number of entries in the logical palette
				GetObject(hPalette,sizeof(WORD),&wBits);

				// memory allocation for temporary palette data
				if ((ppal = (PLOGPALETTE) calloc(sizeof(LOGPALETTE) + wBits * sizeof(PALETTEENTRY),1)) != NULL)
				{
					ppal->palVersion    = PALVERSION;
					ppal->palNumEntries = wBits;
					GetPaletteEntries(hPalette, 0, wBits, ppal->palPalEntry);
					SetClipboardData(CF_PALETTE, CreatePalette(ppal));
					free(ppal);
				}
			}
			DeleteDC(hBmpDC);
			DeleteObject(hBmp);
			#undef WIDTHBYTES
			#undef PALVERSION
		}
		CloseClipboard();
	}
	return 0;
}

//
// ID_VIEW_RESET
//
static LRESULT OnViewReset(VOID)
{
	if (nState != SM_RUN) return 0;
	if (YesNoMessage(_T("Are you sure you want to press the Reset Button?"))==IDYES)
	{
		SwitchToState(SM_SLEEP);
		CpuReset();							// register setting after Cpu Reset
		SwitchToState(SM_RUN);
	}
	return 0;
}

//
// ID_VIEW_SETTINGS
//
static LRESULT OnViewSettings(VOID)
{
	if (DialogBox(hApp, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, (DLGPROC)SettingsProc) == -1)
		AbortMessage(_T("Settings Dialog Creation Error !"));

	WriteSettings();						// update INI file
	return 0;
}

//
// ID_VIEW_SCRIPT
//
static LRESULT OnViewScript(VOID)
{
	TCHAR szKmlFile[MAX_PATH];
	BOOL  bKMLChanged,bSucc;

	BYTE cType = cCurrentRomType;
	if (nState != SM_RUN)
	{
		InfoMessage(_T("You cannot change the KML script when Emu71 is not running.\n")
					_T("Use the File,New menu item to create a new calculator."));
		return 0;
	}
	SwitchToState(SM_INVALID);

	// make a copy of the current KML script file name
	_ASSERT(sizeof(szKmlFile) == sizeof(szCurrentKml));
	lstrcpyn(szKmlFile,szCurrentKml,ARRAYSIZEOF(szKmlFile));

	bKMLChanged = FALSE;					// KML script not changed
	bSucc = TRUE;							// KML script successful loaded

	do
	{
		if (!DisplayChooseKml(cType))		// quit with Cancel
		{
			if (!bKMLChanged)				// KML script not changed
				break;						// exit loop with current loaded KML script

			// restore KML script file name
			lstrcpyn(szCurrentKml,szKmlFile,ARRAYSIZEOF(szCurrentKml));

			// try to restore old KML script
			if ((bSucc = InitKML(szCurrentKml,FALSE)))
				break;						// exit loop with success

			// restoring failed, save document
			if (IDCANCEL != SaveChanges(bAutoSave))
				break;						// exit loop with no success

			_ASSERT(bSucc == FALSE);		// for continuing loop
		}
		else								// quit with Ok
		{
			bKMLChanged = TRUE;				// KML script changed
			bSucc = InitKML(szCurrentKml,FALSE);
		}
	}
	while (!bSucc);							// retry if KML script is invalid

	if (bSucc)
	{
		if (Chipset.wRomCrc != wRomCrc)		// ROM changed
		{
			CalcWarmstart();				// if calculator ROM enabled do
											// a warmstart else a coldstart
			Chipset.wRomCrc = wRomCrc;		// update current ROM fingerprint
		}
		if (pbyRom) SwitchToState(SM_RUN);	// continue emulation
	}
	else
	{
		ResetDocument();					// close document
		SetWindowTitle(NULL);
	}
	return 0;
}

//
// ID_BACKUP_SAVE
//
static LRESULT OnBackupSave(VOID)
{
	UINT nOldState;
	if (pbyRom == NULL) return 0;
	nOldState = SwitchToState(SM_INVALID);
	SaveBackup();
	SwitchToState(nOldState);
	return 0;
}

//
// ID_BACKUP_RESTORE
//
static LRESULT OnBackupRestore(VOID)
{
	SwitchToState(SM_INVALID);
	RestoreBackup();
	if (pbyRom) SwitchToState(SM_RUN);
	return 0;
}

//
// ID_BACKUP_DELETE
//
static LRESULT OnBackupDelete(VOID)
{
	ResetBackup();
	return 0;
}

//
// ID_TOOL_DISASM
//
static INT_PTR CALLBACK Disasm(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	static DWORD dwAddress, dwAddressMax;

	LONG  i;
	TCHAR szAddress[256] = _T("0");

	switch (message)
	{
	case WM_INITDIALOG:
		// set fonts & cursor
		SendDlgItemMessage(hDlg,IDC_ADDRESS,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),MAKELPARAM(FALSE,0));
		SendDlgItemMessage(hDlg,IDC_DISASM_ADR,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),MAKELPARAM(FALSE,0));
		SendDlgItemMessage(hDlg,IDC_DISASM_NEXT,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),MAKELPARAM(FALSE,0));
		SendDlgItemMessage(hDlg,IDC_DISASM_COPY,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),MAKELPARAM(FALSE,0));
		SendDlgItemMessage(hDlg,IDCANCEL,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),MAKELPARAM(FALSE,0));

		if (hDlgDebug == NULL)				// debugger not open
		{
			VERIFY(SetMemMapType(MEM_MMU));	// disassemble with mapped modules
		}

		SetDlgItemText(hDlg,IDC_DISASM_ADR,szAddress);
		dwAddressMax = GetMemDataSize();
		dwAddress = _tcstoul(szAddress,NULL,16);
		return TRUE;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
			SendDlgItemMessage(hDlg,IDC_DISASM_ADR,EM_SETSEL,0,-1);
			GetDlgItemText(hDlg,IDC_DISASM_ADR,szAddress,ARRAYSIZEOF(szAddress));
			// test if valid hex address
			for (i = 0; i < (LONG) lstrlen(szAddress); ++i)
			{
				if (_istxdigit(szAddress[i]) == FALSE)
					return FALSE;
			}
			dwAddress = _tcstoul(szAddress,NULL,16);
			// no break
		case IDC_DISASM_NEXT:
			if (dwAddress >= dwAddressMax)
				return FALSE;
			i = wsprintf(szAddress,(dwAddress <= 0xFFFFF) ? _T("%05lX   ") : _T("%06lX  "),dwAddress);
			dwAddress = disassemble(dwAddress,&szAddress[i]);
			i = (LONG) SendDlgItemMessage(hDlg,IDC_DISASM_WIN,LB_ADDSTRING,0,(LPARAM) szAddress);
			SendDlgItemMessage(hDlg,IDC_DISASM_WIN,LB_SELITEMRANGE,FALSE,MAKELPARAM(0,i));
			SendDlgItemMessage(hDlg,IDC_DISASM_WIN,LB_SETSEL,TRUE,i);
			SendDlgItemMessage(hDlg,IDC_DISASM_WIN,LB_SETTOPINDEX,i,0);
			return TRUE;
		case IDC_DISASM_COPY:
			// copy selected items to clipboard
			CopyItemsToClipboard(GetDlgItem(hDlg,IDC_DISASM_WIN));
			return TRUE;
		case IDCANCEL:
			EndDialog(hDlg,IDCANCEL);
			return TRUE;
		}
		break;
	}
	return FALSE;
	UNREFERENCED_PARAMETER(lParam);
}

//
// ID_ABOUT
//
static INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		SetDlgItemText(hDlg,IDC_VERSION,szNoTitle);
		SetDlgItemText(hDlg,IDC_LICENSE,szLicence);
		return TRUE;
	case WM_COMMAND:
		wParam = LOWORD(wParam);
		if ((wParam==IDOK)||(wParam==IDCANCEL))
		{
			EndDialog(hDlg, wParam);
			return TRUE;
		}
		break;
	}
	return FALSE;
	UNREFERENCED_PARAMETER(lParam);
}

static LRESULT OnToolDisasm(VOID)			// disasm dialogbox call
{
	if (pbyRom) SwitchToState(SM_SLEEP);
	if (DialogBox(hApp, MAKEINTRESOURCE(IDD_DISASM), hWnd, (DLGPROC)Disasm) == -1)
		AbortMessage(_T("Disassembler Dialog Box Creation Error !"));
	if (pbyRom) SwitchToState(SM_RUN);
	return 0;
}

static LRESULT OnTopics(VOID)
{
	ShellExecute(hWnd,_T("open"),_T("Emu71.htm"),NULL,szEmuDirectory,SW_SHOWNORMAL);
	return 0;
}

static LRESULT OnAbout(VOID)
{
	if (DialogBox(hApp, MAKEINTRESOURCE(IDD_ABOUT), hWnd, (DLGPROC)About) == -1)
		AbortMessage(_T("About Dialog Box Creation Error !"));
	return 0;
}

static VOID OnContextMenu(LPARAM lParam)
{
	HMENU hMenu;
	POINT pt,ptc;

	if (GetMenu(hWnd) == NULL)				// no main window menu
	{
		POINTSTOPOINT(pt,MAKEPOINTS(lParam)); // mouse position

		if (pt.x == -1 && pt.y == -1)		// VK_APPS
		{
			pt.x = 15;						// open context help at client position 15,15
			pt.y = 15;
			VERIFY(ClientToScreen(hWnd,&pt));
		}

		ptc = pt;
		VERIFY(ScreenToClient(hWnd,&ptc));	// convert mouse into client position

		// in client area not over a button
		if (ptc.y >= 0 && !MouseIsButton(ptc.x,ptc.y))
		{
			// load the popup menu resource
			if ((hMenu = LoadMenu(hApp,MAKEINTRESOURCE(IDM_MENU))) != NULL)
			{
				// display the popup menu
				TrackPopupMenu(GetSubMenu(hMenu,0),
					TPM_LEFTALIGN | TPM_LEFTBUTTON,
					pt.x, pt.y, 0, hWnd, NULL);

				DestroyMenu(hMenu);			// destroy the menu
			}
		}
	}
	return;
}

static LRESULT OnLButtonDown(UINT nFlags, WORD x, WORD y)
{
	if (nMacroState == MACRO_PLAY) return 0; // playing macro
	if (nState == SM_RUN) MouseButtonDownAt(nFlags, x,y);

	bMouseButton = MouseIsButton(x,y);		// mouse is over button hit area

	// no title bar or window movement over client enabled and hit area not over a button
	if ((!bTitleBar || bClientWinMove) && nFlags == MK_LBUTTON && !bMouseButton)
	{
		// move window while holding the left mouse button
		PostMessage(hWnd,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(x,y));
	}
	return 0;
}

static LRESULT OnLButtonUp(UINT nFlags, WORD x, WORD y)
{
	if (nMacroState == MACRO_PLAY) return 0; // playing macro
	if (nState == SM_RUN) MouseButtonUpAt(nFlags, x,y);
	bMouseButton = FALSE;
	return 0;
}

static LRESULT OnMouseMove(UINT nFlags, WORD x, WORD y)
{
	// emulator not active but cursor is over emulator window
	if (bActFollowsMouse && GetActiveWindow() != hWnd)
	{
		ForceForegroundWindow(hWnd);		// force emulator window to foreground
	}

	if (nMacroState == MACRO_PLAY) return 0; // playing macro
	if (nState == SM_RUN) MouseMovesTo(nFlags, x,y);
	return 0;
}

static LRESULT OnNcMouseMove(UINT nFlags, WORD x, WORD y)
{
	// emulator not active but cursor is over emulator window
	if (bActFollowsMouse && GetActiveWindow() != hWnd)
	{
		ForceForegroundWindow(hWnd);		// force emulator window to foreground
	}
	return 0;
	UNREFERENCED_PARAMETER(nFlags);
	UNREFERENCED_PARAMETER(x);
	UNREFERENCED_PARAMETER(y);
}

static LRESULT OnKeyDown(int nVirtKey, LPARAM lKeyData)
{
	if (nMacroState == MACRO_PLAY) return 0; // playing macro
	// call RunKey() only once (suppress autorepeat feature)
	if (nState == SM_RUN && (lKeyData & 0x40000000) == 0)
		RunKey((BYTE)nVirtKey, TRUE);
	bMouseButton = FALSE;
	return 0;
}

static LRESULT OnKeyUp(int nVirtKey, LPARAM lKeyData)
{
	if (nMacroState == MACRO_PLAY) return 0; // playing macro
	if (nState == SM_RUN) RunKey((BYTE)nVirtKey, FALSE);
	return 0;
	UNREFERENCED_PARAMETER(lKeyData);
}

static LRESULT OnCopyData(PCOPYDATASTRUCT psCDS)
{
	switch (psCDS->dwData)
	{
	case CDID_FILENAME:
		// current instance has document loaded and got a wide-character file name
		if (bDocumentAvail && psCDS->cbData > 0 && psCDS->lpData != NULL)
		{
			TCHAR  szActFilename[MAX_PATH];
			LPTSTR lpFilePart;				// address of file name in path

			#if defined _UNICODE
			{
				// get full path file name for requested state file
				GetFullPathName((LPCTSTR) psCDS->lpData,ARRAYSIZEOF(szBufferFilename),szBufferFilename,&lpFilePart);
			}
			#else
			{
				CHAR szAscFilename[MAX_PATH];

				WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK,
									(LPCWSTR) psCDS->lpData, -1,
									szAscFilename, ARRAYSIZEOF(szAscFilename), NULL, NULL);

				// get full path file name for requested state file
				GetFullPathName(szAscFilename,ARRAYSIZEOF(szBufferFilename),szBufferFilename,&lpFilePart);
			}
			#endif

			// get full path file name for actual state file
			GetFullPathName(szCurrentFilename,ARRAYSIZEOF(szActFilename),szActFilename,&lpFilePart);

			// check if both file names are unequal
			if (lstrcmpi(szBufferFilename,szActFilename) != 0)
			{
				UINT nCurState;

				if (pbyRom)
				{
					nCurState = SwitchToState(SM_INVALID);
					if (IDCANCEL == SaveChanges(bAutoSave))
						goto cancel;
				}
				if (OpenDocument(szBufferFilename)) // open new file
				{
					MruAdd(szBufferFilename);
				}
cancel:
				if (pbyRom) SwitchToState(nCurState);
			}
		}
		break;
	default:
		return FALSE;						// message not processed
	}
	return TRUE;							// message processed
}

LRESULT CALLBACK MainWndProc(HWND hWindow, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:          return OnCreate(hWindow);
	case WM_DESTROY:         return OnDestroy(hWindow);
	case WM_PAINT:           return OnPaint(hWindow);
	case WM_INITMENU:        return OnInitMenu((HMENU) wParam);
	case WM_ACTIVATE:
		if (LOWORD(wParam)==WA_INACTIVE) break;
	case WM_QUERYNEWPALETTE:
		if (hPalette)
		{
			SelectPalette(hWindowDC, hPalette, FALSE);
			if (RealizePalette(hWindowDC))
			{
				InvalidateRect(hWindow,NULL,TRUE);
				return TRUE;
			}
		}
		return FALSE;
	case WM_PALETTECHANGED:
		if ((HWND)wParam == hWindow) break;
		if (hPalette)
		{
			SelectPalette(hWindowDC, hPalette, FALSE);
			if (RealizePalette(hWindowDC))
			{
				// UpdateColors(hWindowDC);
				InvalidateRect(hWindow,NULL,TRUE);
			}
		}
		return FALSE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_FILE_NEW:            return OnFileNew();
		case ID_FILE_OPEN:           return OnFileOpen();
		case ID_FILE_SAVE:           return OnFileSave();
		case ID_FILE_SAVEAS:         return OnFileSaveAs();
		case ID_FILE_CLOSE:          return OnFileClose();
		case ID_FILE_EXIT:           return OnFileExit();
		case ID_EDIT_PORTCONFIG:     return OnEditPortConfig();
		case ID_STACK_COPY:          return OnStackCopy();
		case ID_STACK_PASTE:         return OnStackPaste();
		case ID_VIEW_COPY:           return OnViewCopy();
		case ID_VIEW_RESET:          return OnViewReset();
		case ID_VIEW_SETTINGS:       return OnViewSettings();
		case ID_VIEW_SCRIPT:         return OnViewScript();
		case ID_BACKUP_SAVE:         return OnBackupSave();
		case ID_BACKUP_RESTORE:      return OnBackupRestore();
		case ID_BACKUP_DELETE:       return OnBackupDelete();
		case ID_TOOL_DISASM:         return OnToolDisasm();
		case ID_TOOL_MACRO_RECORD:   return OnToolMacroNew();
		case ID_TOOL_MACRO_PLAY:     return OnToolMacroPlay();
		case ID_TOOL_MACRO_STOP:     return OnToolMacroStop();
		case ID_TOOL_MACRO_SETTINGS: return OnToolMacroSettings();
		case ID_TOOL_DEBUG:          return OnToolDebug();
		case ID_HELP_TOPICS:         return OnTopics();
		case ID_ABOUT:               return OnAbout();
		}
		// check if command ID belongs to MRU file area
		if (   (LOWORD(wParam) >= ID_FILE_MRU_FILE1)
			&& (LOWORD(wParam) <  ID_FILE_MRU_FILE1 + MruEntries()))
			return OnFileMruOpen(LOWORD(wParam));
		break;
	case WM_SYSCOMMAND:
		switch (wParam & 0xFFF0)
		{
		case SC_CLOSE: return OnFileExit();
		}
		break;
	case WM_ENDSESSION:
		// session will end and any auto saving is enabled
		if (wParam == TRUE && (bAutoSave || bAutoSaveOnExit))
		{
			SwitchToState(SM_INVALID);		// hold emulation thread
			if (szCurrentFilename[0] != 0)	// has current filename
				SaveDocument();
			SwitchToState(SM_RUN);			// on cancel restart emulation thread
		}
		break;
	case WM_CONTEXTMENU:
		if (!bMouseButton) OnContextMenu(lParam);
		break;
	case WM_RBUTTONDOWN:
	case WM_LBUTTONDOWN: return OnLButtonDown((UINT) wParam, LOWORD(lParam), HIWORD(lParam));
	case WM_LBUTTONUP:   return OnLButtonUp((UINT) wParam, LOWORD(lParam), HIWORD(lParam));
	case WM_MOUSEMOVE:   return OnMouseMove((UINT) wParam, LOWORD(lParam), HIWORD(lParam));
	case WM_NCMOUSEMOVE: return OnNcMouseMove((UINT) wParam, LOWORD(lParam), HIWORD(lParam));
	case WM_KEYUP:       return OnKeyUp((int)wParam, lParam);
	case WM_KEYDOWN:     return OnKeyDown((int)wParam, lParam);
	case WM_COPYDATA:    return OnCopyData((PCOPYDATASTRUCT) lParam);
	}
	return DefWindowProc(hWindow, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow)
{
	typedef DWORD (WINAPI *LPFN_STIP)(HANDLE hThread,DWORD dwIdealProcessor);

	MSG msg;
	WNDCLASS wc;
	ATOM classAtom;
	RECT rectWindow;
	HACCEL hAccel;
	DWORD dwThreadId;
	LPFN_STIP fnSetThreadIdealProcessor;
	DWORD dwProcessor;
	LPTSTR lpFilePart;

	// enable memory leak detection
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	hApp = hInst;
	#if defined _UNICODE
	{
		ppArgv = (LPCTSTR*) CommandLineToArgvW(GetCommandLine(),&nArgc);
	}
	#else
	{
		nArgc = __argc;						// no. of command line arguments
		ppArgv = (LPCTSTR*) __argv;			// command line arguments
	}
	#endif

	if(!QueryPerformanceFrequency(&lFreq))	// init high resolution counter
	{
		AbortMessage(
			_T("No high resolution timer available.\n")
			_T("This application will now terminate."));
		return FALSE;
	}

	wc.style = CS_BYTEALIGNCLIENT;
	wc.lpfnWndProc = (WNDPROC)MainWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInst;
	wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_EMU71));
	wc.hCursor = NULL;
	wc.hbrBackground = NULL;
	wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU);
	wc.lpszClassName = _T("CEmu71");

	if (!(classAtom = RegisterClass(&wc)))
	{
		AbortMessage(
			_T("CEmu71 class registration failed.\n")
			_T("This application will now terminate."));
		return FALSE;
	}

	// read emulator settings
	GetCurrentDirectory(ARRAYSIZEOF(szCurrentDirectory),szCurrentDirectory);
	ReadSettings();

	// running an instance of me?
	if (bSingleInstance && (hWnd = FindWindow(MAKEINTATOM(classAtom),NULL)) != NULL)
	{
		COPYDATASTRUCT sCDS;

		if (IsIconic(hWnd))					// window minimized
			ShowWindow(hWnd,SW_RESTORE);	// show window

		// put the window into foreground
		ForceForegroundWindow(GetLastActivePopup(hWnd));

		if (nArgc >= 2)						// use decoded parameter line
		{
			LPTSTR lpFilePart;				// address of file name in path
			DWORD  dwLength;				// file name length

			// get full path file name
			GetFullPathName(ppArgv[1],ARRAYSIZEOF(szBufferFilename),szBufferFilename,&lpFilePart);

			// size of file name incl. EOS
			dwLength = lstrlen(szBufferFilename) + 1;
			sCDS.cbData = dwLength * sizeof(WCHAR);

			#if defined _UNICODE
			{
				sCDS.lpData = szBufferFilename;
			}
			#else
			{
				sCDS.lpData = _alloca(sCDS.cbData);
				if (sCDS.lpData != NULL)
				{
					MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, szBufferFilename, dwLength,
										(LPWSTR) sCDS.lpData, sCDS.cbData);
				}
				else
				{
					sCDS.cbData = 0;		// size of file name
				}
			}
			#endif
		}
		else
		{
			sCDS.lpData = NULL;				// file name
			sCDS.cbData = 0;				// size of file name
		}

		// fill the COPYDATA structure and send file name to other instance
		sCDS.dwData = CDID_FILENAME;		// function identifier
		SendMessage(hWnd,WM_COPYDATA,(WPARAM) NULL,(LPARAM) &sCDS);
		return 0;							// quit program
	}

	// Create window
	rectWindow.left   = 0;
	rectWindow.top    = 0;
	rectWindow.right  = 256;
	rectWindow.bottom = 0;
	AdjustWindowRect(&rectWindow, STYLE_TITLE, TRUE);

	hWnd = CreateWindow(MAKEINTATOM(classAtom),_T("Emu71"),
		STYLE_TITLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rectWindow.right  - rectWindow.left,
		rectWindow.bottom - rectWindow.top,
		NULL,NULL,hApp,NULL
		);

	if (hWnd == NULL)
	{
		AbortMessage(_T("Window creation failed."));
		return FALSE;
	}

	VERIFY(hAccel = LoadAccelerators(hInst, MAKEINTRESOURCE(IDR_MENU)));

	// initialization
	ZeroMemory(psExtPortData,sizeof(psExtPortData)); // port data
	szCurrentKml[0] = 0;					// no KML file selected
	SetSpeed(bRealSpeed);					// set speed
	MruInit(4);								// init MRU entries

	// create auto event handle
	hEventShutdn = CreateEvent(NULL,FALSE,FALSE,NULL);
	if (hEventShutdn == NULL)
	{
		AbortMessage(_T("Event creation failed."));
		DestroyWindow(hWnd);
		return FALSE;
	}

	nState     = SM_RUN;					// init state must be <> nNextState
	nNextState = SM_INVALID;				// go into invalid state
	hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&WorkerThread, NULL, CREATE_SUSPENDED, &dwThreadId);
	if (hThread == NULL)
	{
		CloseHandle(hEventShutdn);			// close event handle
		AbortMessage(_T("Thread creation failed."));
		DestroyWindow(hWnd);
		return FALSE;
	}

	// SetThreadIdealProcessor() is available since Windows NT4.0
	fnSetThreadIdealProcessor = (LPFN_STIP) GetProcAddress(GetModuleHandle(_T("kernel32")),
														   "SetThreadIdealProcessor");

	// bind Saturn CPU emulation thread to current ideal processor
	dwProcessor = (fnSetThreadIdealProcessor != NULL)					// running on NT4.0 or later
				? fnSetThreadIdealProcessor(hThread,MAXIMUM_PROCESSORS)	// get ideal processor no.
				: 0;													// select 1st processor

	// on multiprocessor machines for QueryPerformanceCounter()
	VERIFY(SetThreadAffinityMask(hThread,(DWORD_PTR) (1 << dwProcessor)));
	ResumeThread(hThread);					// start thread
	while (nState!=nNextState) Sleep(0);	// wait for thread initialized

	SoundOpen(uWaveDevId);					// open waveform-audio output device

	_ASSERT(hWnd != NULL);
	_ASSERT(hWindowDC != NULL);

	if (nArgc >= 2)							// use decoded parameter line
		lstrcpyn(szBufferFilename,ppArgv[1],ARRAYSIZEOF(szBufferFilename));
	else									// use last document setting
		ReadLastDocument(szBufferFilename, ARRAYSIZEOF(szBufferFilename));

	if (szBufferFilename[0])				// given default document
	{
		TCHAR szTemp[MAX_PATH+8] = _T("Loading ");
		RECT  rectClient;

		_ASSERT(hWnd != NULL);
		VERIFY(GetClientRect(hWnd,&rectClient));
		GetCutPathName(szBufferFilename,&szTemp[8],MAX_PATH,rectClient.right/11);
		SetWindowTitle(szTemp);
		if (OpenDocument(szBufferFilename))
		{
			MruAdd(szCurrentFilename);
			ShowWindow(hWnd,nCmdShow);
			goto start;
		}
	}

	SetWindowTitle(_T("New Document"));
	ShowWindow(hWnd,nCmdShow);				// show emulator menu

	// no default document, ask for new one
	if (NewDocument()) SetWindowTitle(_T("Untitled"));

start:
	if (bStartupBackup) SaveBackup();		// make a RAM backup at startup
	if (pbyRom) SwitchToState(SM_RUN);

	while (GetMessage(&msg, NULL, 0, 0))
	{
		if(   !TranslateAccelerator(hWnd, hAccel, &msg)
		   && (hDlgDebug   == NULL    || !IsDialogMessage(hDlgDebug,      &msg))
		   && (hDlgFind    == NULL    || !IsDialogMessage(hDlgFind,       &msg))
		   && (hDlgProfile == NULL    || !IsDialogMessage(hDlgProfile,    &msg))
		   && (hDlgMemMap  == NULL    || !IsDialogMessage(hDlgMemMap,     &msg)))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	SoundClose();							// close waveform-audio output device

	// get full path name of szCurrentFilename
	if (GetFullPathName(szCurrentFilename,ARRAYSIZEOF(szBufferFilename),szBufferFilename,&lpFilePart) == 0)
		szBufferFilename[0] = 0;			// no last document name

	WriteLastDocument(szBufferFilename);	// save last document setting
	WriteSettings();						// save emulation settings

	CloseHandle(hThread);					// close thread handle
	CloseHandle(hEventShutdn);				// close event handle
	_ASSERT(nState == SM_RETURN);			// emulation thread down?
	ResetDocument();
	ResetBackup();
	MruCleanup();
	_ASSERT(pKml == NULL);					// KML script not closed
	_ASSERT(szTitle == NULL);				// freed allocated memory
	_ASSERT(hPalette == NULL);				// freed resource memory

	return (int) msg.wParam;
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(hPrevInst);
}
