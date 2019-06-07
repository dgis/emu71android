/*
 *   display.c
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gießelink
 *
 */
#include "pch.h"
#include "resource.h"
#include "Emu71.h"
#include "ops.h"
#include "io.h"
#include "kml.h"

#define DISPLAY_FREQ	16					// display update 1/frequency (1/64) in ms

#define B 0x00000000						// black
#define W 0x00FFFFFF						// white
#define I 0xFFFFFFFF						// ignore

#define LCD_WIDTH	132
#define LCD_HEIGHT	8

#define LCD_ROW		(33*4)					// max. pixel per line

#define ROP_PDSPxax 0x00D80745				// ternary ROP

// convert color from RGBQUAD to COLORREF
#define RGBtoCOLORREF(c) ((((c) & 0xFF0000) >> 16) \
						| (((c) & 0xFF)     << 16) \
						|  ((c) & 0xFF00))

UINT nBackgroundX = 0;
UINT nBackgroundY = 0;
UINT nBackgroundW = 0;
UINT nBackgroundH = 0;
UINT nLcdX = 0;
UINT nLcdY = 0;
UINT nLcdZoom = 1;
HDC  hLcdDC = NULL;
HDC  hMainDC = NULL;
HDC  hAnnunDC = NULL;						// annunciator DC

static HBITMAP hLcdBitmap;
static HBITMAP hMainBitmap;
static HBITMAP hAnnunBitmap;

static HDC     hBmpBkDC;					// current background
static HBITMAP hBmpBk;

static HDC     hMaskDC;						// mask bitmap
static HBITMAP hMaskBitmap;
static LPBYTE  pbyMask;

static HBRUSH  hBrush = NULL;				// current segment drawing brush

static UINT nLcdXSize = 0;					// display size
static UINT nLcdYSize = 0;

static UINT nLcdxZoom = (UINT) -1;			// x zoom factor

static UINT nBlinkCnt = 0;					// display blink counter

static UINT uLcdTimerId = 0;

static CONST BYTE byRowTable[] =			// addresses of display row memory
{
	0x5C, 0x58, 0x54, 0x50, 0x52, 0x56, 0x5A, 0x5E
};

static DWORD dwKMLColor[32] =				// color table loaded by KML script
{
	I,I,I,I,I,I,I,I,I,I,I,I,I,I,I,I,
	I,I,I,I,I,I,I,I,I,I,I,I,I,I,I,I
};

static struct
{
	BITMAPINFOHEADER Lcd_bmih;
	RGBQUAD bmiColors[2];
} bmiLcd =
{
	{sizeof(BITMAPINFOHEADER),0/*x*/,0/*y*/,1,8,BI_RGB,0,0,0,ARRAYSIZEOF(bmiLcd.bmiColors),0},
	{{0xFF,0xFF,0xFF,0x00},{0x00,0x00,0x00,0x00}}
};

static VOID (*WritePixel)(BYTE *p, BOOL a) = NULL;

static VOID WritePixelZoom4(BYTE *p, BOOL a);
static VOID WritePixelZoom3(BYTE *p, BOOL a);
static VOID WritePixelZoom2(BYTE *p, BOOL a);
static VOID WritePixelZoom1(BYTE *p, BOOL a);
static VOID WritePixelBYTE(BYTE *p, BOOL a);
static VOID WritePixelWORD(BYTE *p, BOOL a);
static VOID WritePixelDWORD(BYTE *p, BOOL a);

VOID UpdateContrast(VOID)
{
	BYTE byContrast = Chipset.dd[MASTER].IORam[DCONTR & 0xFF];
	BOOL bOn = (Chipset.dd[MASTER].IORam[DD1CTL & 0xFF] & DON) != 0;

	// update palette information
	EnterCriticalSection(&csGDILock);		// asynchronous display update!
	{
		// display on and background contrast defined?
		if (bOn && dwKMLColor[byContrast+16] != I)
		{
			DWORD dwColor = RGBtoCOLORREF(dwKMLColor[byContrast+16]);

			HBRUSH hBrush = (HBRUSH) SelectObject(hBmpBkDC,CreateSolidBrush(dwColor));
			PatBlt(hBmpBkDC, 0, 0, nLcdXSize, nLcdYSize, PATCOPY);
			DeleteObject(SelectObject(hBmpBkDC,hBrush));
		}
		else
		{
			// get original background from bitmap
			BitBlt(hBmpBkDC,
				   0, 0, nLcdXSize, nLcdYSize,
				   hMainDC,
				   nLcdX, nLcdY,
				   SRCCOPY);
		}

		_ASSERT(hLcdDC);

		if (hBrush)							// has already a brush
		{
			// delete it first
			DeleteObject(SelectObject(hLcdDC,hBrush));
			hBrush = NULL;
		}

		if (dwKMLColor[byContrast] != I)	// have brush color?
		{
			// set brush for display pattern
			VERIFY(hBrush = (HBRUSH) SelectObject(hLcdDC,CreateSolidBrush(RGBtoCOLORREF(dwKMLColor[byContrast]))));
		}
		GdiFlush();
	}
	LeaveCriticalSection(&csGDILock);
	UpdateAnnunciators();					// adjust annunciator color
	return;
}

VOID SetLcdColor(UINT nId, UINT nRed, UINT nGreen, UINT nBlue)
{
	dwKMLColor[nId&0x1F] = ((nRed&0xFF)<<16)|((nGreen&0xFF)<<8)|(nBlue&0xFF);
	return;
}

VOID GetSizeLcdBitmap(INT *pnX, INT *pnY)
{
	*pnX = *pnY = 0;						// unknown

	if (hLcdBitmap)
	{
		*pnX = nLcdXSize;
		*pnY = nLcdYSize;
	}
	return;
}

VOID CreateLcdBitmap(VOID)
{
	UINT nPatSize;
	INT  i;
	BOOL bEmpty;

	// select pixel drawing routine
	switch (nLcdZoom)
	{
	case 1:
		WritePixel = WritePixelZoom1;
		break;
	case 2:
		WritePixel = WritePixelZoom2;
		break;
	case 3:
		WritePixel = WritePixelZoom3;
		break;
	case 4:
		WritePixel = WritePixelZoom4;
		break;
	default:
		// select pixel pattern size (BYTE, WORD, DWORD)
		nLcdxZoom = nLcdZoom;				// BYTE pattern size adjusted x-Zoom
		nPatSize = sizeof(BYTE);			// use BYTE pattern
		while ((nLcdxZoom & 0x1) == 0 && nPatSize < sizeof(DWORD))
		{
			nLcdxZoom >>= 1;
			nPatSize <<= 1;
		}
		switch (nPatSize)
		{
		case sizeof(BYTE):
			WritePixel = WritePixelBYTE;
			break;
		case sizeof(WORD):
			WritePixel = WritePixelWORD;
			break;
		case sizeof(DWORD):
			WritePixel = WritePixelDWORD;
			break;
		default:
			_ASSERT(FALSE);
		}
	}

	// all KML contrast palette fields undefined?
	for (bEmpty = TRUE, i = 0; bEmpty && i < ARRAYSIZEOF(dwKMLColor); ++i)
	{
		bEmpty = (dwKMLColor[i] == I);
	}
	if (bEmpty)								// preset KML contrast palette
	{
		// black on character
		for (i = 0; i < ARRAYSIZEOF(dwKMLColor) / 2; ++i)
		{
			_ASSERT(i < ARRAYSIZEOF(dwKMLColor));
			dwKMLColor[i] = B;
		}
	}

	nLcdXSize = LCD_WIDTH  * nLcdZoom;
	nLcdYSize = LCD_HEIGHT * nLcdZoom;

	// initialize background bitmap
	VERIFY(hBmpBkDC = CreateCompatibleDC(hWindowDC));
	VERIFY(hBmpBk = CreateCompatibleBitmap(hWindowDC,nLcdXSize,nLcdYSize));
	VERIFY(hBmpBk = (HBITMAP) SelectObject(hBmpBkDC,hBmpBk));

	// create mask bitmap
	bmiLcd.Lcd_bmih.biWidth = LCD_ROW * nLcdZoom;
	bmiLcd.Lcd_bmih.biHeight = -(LONG) nLcdYSize;
	VERIFY(hMaskDC = CreateCompatibleDC(hWindowDC));
	VERIFY(hMaskBitmap = CreateDIBSection(hWindowDC,(BITMAPINFO*)&bmiLcd,DIB_RGB_COLORS,(VOID **)&pbyMask,NULL,0));
	VERIFY(hMaskBitmap = (HBITMAP) SelectObject(hMaskDC,hMaskBitmap));

	// create LCD bitmap
	_ASSERT(hLcdDC == NULL);
	VERIFY(hLcdDC = CreateCompatibleDC(hWindowDC));
	VERIFY(hLcdBitmap = CreateCompatibleBitmap(hWindowDC,nLcdXSize,nLcdYSize));
	VERIFY(hLcdBitmap = (HBITMAP) SelectObject(hLcdDC,hLcdBitmap));

	_ASSERT(hPalette != NULL);
	SelectPalette(hLcdDC,hPalette,FALSE);	// set palette for LCD DC
	RealizePalette(hLcdDC);					// realize palette

	UpdateContrast();						// initialize background

	EnterCriticalSection(&csGDILock);		// solving NT GDI problems
	{
		_ASSERT(hMainDC);					// background bitmap must be loaded

		// get original background from bitmap
		BitBlt(hLcdDC,
			   0, 0,
			   nLcdXSize, nLcdYSize,
			   hMainDC,
			   nLcdX, nLcdY,
			   SRCCOPY);
		GdiFlush();
	}
	LeaveCriticalSection(&csGDILock);
	return;
}

VOID DestroyLcdBitmap(VOID)
{
	// set contrast palette to startup colors
	WORD i = 0; while(i < 32) dwKMLColor[i++] = I;

	if (hLcdDC != NULL)
	{
		// destroy background bitmap
		DeleteObject(SelectObject(hBmpBkDC,hBmpBk));
		DeleteDC(hBmpBkDC);

		// destroy display pattern brush
		DeleteObject(SelectObject(hLcdDC,hBrush));

		// destroy mask bitmap
		DeleteObject(SelectObject(hMaskDC,hMaskBitmap));
		DeleteDC(hMaskDC);

		// destroy LCD bitmap
		DeleteObject(SelectObject(hLcdDC,hLcdBitmap));
		DeleteDC(hLcdDC);
		hBrush = NULL;
		hLcdDC = NULL;
		hLcdBitmap = NULL;
	}
	nLcdXSize = 0;
	nLcdYSize = 0;
	nLcdxZoom = (UINT) -1;
	WritePixel = NULL;
	return;
}

BOOL CreateMainBitmap(LPCTSTR szFilename)
{
	_ASSERT(hWindowDC != NULL);
	hMainDC = CreateCompatibleDC(hWindowDC);
	_ASSERT(hMainDC != NULL);
	if (hMainDC == NULL) return FALSE;		// quit if failed
	hMainBitmap = LoadBitmapFile(szFilename);
	if (hMainBitmap == NULL)
	{
		DeleteDC(hMainDC);
		hMainDC = NULL;
		return FALSE;
	}
	hMainBitmap = (HBITMAP) SelectObject(hMainDC,hMainBitmap);
	_ASSERT(hPalette != NULL);
	VERIFY(SelectPalette(hMainDC,hPalette,FALSE) != NULL);
	RealizePalette(hMainDC);
	return TRUE;
}

VOID DestroyMainBitmap(VOID)
{
	if (hMainDC != NULL)
	{
		// destroy Main bitmap
		VERIFY(DeleteObject(SelectObject(hMainDC,hMainBitmap)));
		DeleteDC(hMainDC);
		hMainDC = NULL;
		hMainBitmap = NULL;
	}
	return;
}

//
// load annunciator bitmap
//
BOOL CreateAnnunBitmap(LPCTSTR szFilename)
{
	_ASSERT(hWindowDC != NULL);
	VERIFY(hAnnunDC = CreateCompatibleDC(hWindowDC));
	if (hAnnunDC == NULL) return FALSE;		// quit if failed
	hAnnunBitmap = LoadBitmapFile(szFilename);
	if (hAnnunBitmap == NULL)
	{
		DeleteDC(hAnnunDC);
		hAnnunDC = NULL;
		return FALSE;
	}
	hAnnunBitmap = (HBITMAP) SelectObject(hAnnunDC,hAnnunBitmap);
	return TRUE;
}

//
// set annunciator bitmap
//
VOID SetAnnunBitmap(HDC hDC, HBITMAP hBitmap)
{
	hAnnunDC = hDC;
	hAnnunBitmap = hBitmap;
	return;
}

//
// destroy annunciator bitmap
//
VOID DestroyAnnunBitmap(VOID)
{
	if (hAnnunDC != NULL)
	{
		VERIFY(DeleteObject(SelectObject(hAnnunDC,hAnnunBitmap)));
		DeleteDC(hAnnunDC);
		hAnnunDC = NULL;
		hAnnunBitmap = NULL;
	}
	return;
}

//****************
//*
//* LCD functions
//*
//****************

static VOID WritePixelZoom4(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		// check alignment for ARM CPU
		_ASSERT((DWORD) p % sizeof(DWORD) == 0);

		*(DWORD *)&p[0*LCD_ROW] = *(DWORD *)&p[4*LCD_ROW]  =
		*(DWORD *)&p[8*LCD_ROW] = *(DWORD *)&p[12*LCD_ROW] = 0x01010101;
	}
	return;
}

static VOID WritePixelZoom3(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		p[0*LCD_ROW+0] = p[0*LCD_ROW+1] = p[0*LCD_ROW+2] =
		p[3*LCD_ROW+0] = p[3*LCD_ROW+1] = p[3*LCD_ROW+2] =
		p[6*LCD_ROW+0] = p[6*LCD_ROW+1] = p[6*LCD_ROW+2] = 0x01;
	}
	return;
}

static VOID WritePixelZoom2(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		// check alignment for ARM CPU
		_ASSERT((DWORD) p % sizeof(WORD) == 0);

		*(WORD *)&p[0*LCD_ROW] =
		*(WORD *)&p[2*LCD_ROW] = 0x0101;
	}
	return;
}

static VOID WritePixelZoom1(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		p[0*LCD_ROW] = 0x01;
	}
	return;
}

static VOID WritePixelDWORD(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		UINT x,y;

		_ASSERT(nLcdxZoom > 0);				// x-Zoom factor
		_ASSERT((DWORD) p % sizeof(DWORD) == 0); // check alignment for ARM CPU

		for (y = nLcdZoom; y > 0; --y)
		{
			LPDWORD pdwPixel = (LPDWORD) p;

			x = nLcdxZoom;
			do
			{
				*pdwPixel++ = 0x01010101;
			}
			while (--x > 0);
			p += sizeof(DWORD) * LCD_ROW * nLcdxZoom;
		}
	}
	return;
}

static VOID WritePixelWORD(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		UINT x,y;

		_ASSERT(nLcdxZoom > 0);				// x-Zoom factor
		_ASSERT((DWORD) p % sizeof(WORD) == 0); // check alignment for ARM CPU

		for (y = nLcdZoom; y > 0; --y)
		{
			LPWORD pwPixel = (LPWORD) p;

			x = nLcdxZoom;
			do
			{
				*pwPixel++ = 0x0101;
			}
			while (--x > 0);
			p += sizeof(WORD) * LCD_ROW * nLcdxZoom;
		}
	}
	return;
}

static VOID WritePixelBYTE(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		UINT x,y;

		_ASSERT(nLcdxZoom > 0);				// x-Zoom factor
		
		for (y = nLcdZoom; y > 0; --y)
		{
			LPBYTE pbyPixel = p;

			x = nLcdxZoom;
			do
			{
				*pbyPixel++ = 0x01;
			}
			while (--x > 0);
			p += LCD_ROW * nLcdxZoom;
		}
	}
	return;
}

static __inline VOID WriteDisplayCol(BYTE *p, WORD w)
{
	INT  i;
	BOOL c;

	// next memory position in LCD bitmap
	INT nNextLine = LCD_ROW * nLcdZoom * nLcdZoom;

	// build display column base on the display ROW table
	for (i = 0; i < ARRAYSIZEOF(byRowTable); ++i)
	{
		// set pixel state
		c = (w & *(WORD *) &Chipset.dd[MASTER].IORam[byRowTable[i]]) != 0;
		WritePixel(p,c);					// write pixel zoom independent
		p += nNextLine;						// next memory position in LCD bitmap
	}
	return;
}

VOID UpdateMainDisplay(VOID)
{
	DWORD d;
	BYTE  *p;

	// display is in blink mode
	if ((Chipset.dd[MASTER].IORam[DD1CTL & 0xFF] & (DBLINK | DON)) == (DBLINK | DON))
	{
		nBlinkCnt = (nBlinkCnt + 1) & 0x3F;	// update blink counter

		if ((nBlinkCnt & 0x1F) == 0)		// change of blink state
		{
			UpdateAnnunciators();			// redraw annunciators
		}

		if (nBlinkCnt >= 32)				// display off
		{
			EnterCriticalSection(&csGDILock);
			{
				// get original background from bitmap
				BitBlt(hWindowDC, nLcdX, nLcdY, nLcdXSize, nLcdYSize,
					   hMainDC,   nLcdX, nLcdY, SRCCOPY);
				GdiFlush();
			}
			LeaveCriticalSection(&csGDILock);
			return;
		}
	}
	else
	{
		nBlinkCnt = 0;						// not blinking, keep annuciators on
	}

	// switch all dot matrix pixel off
	ZeroMemory(pbyMask, bmiLcd.Lcd_bmih.biWidth * -bmiLcd.Lcd_bmih.biHeight);

	// display on, draw dot matrix display part
	if (   (Chipset.dd[MASTER].IORam[DD1CTL & 0xFF]&DON) != 0
		&& dwKMLColor[Chipset.dd[MASTER].IORam[DCONTR & 0xFF]] != I)
	{
		p = pbyMask;						// 1st column memory position in LCD bitmap

		// scan complete display area of SLAVE1
		for (d = (DD3ST & 0xFF); d < (DD3END & 0xFF); d += 2)
		{
			WriteDisplayCol(p, *(WORD *) &Chipset.dd[SLAVE1].IORam[d]);
			p += nLcdZoom;
		}

		// scan complete display area of SLAVE2
		for (d = (DD2ST & 0xFF); d < (DD2END & 0xFF); d += 2)
		{
			WriteDisplayCol(p, *(WORD *) &Chipset.dd[SLAVE2].IORam[d]);
			p += nLcdZoom;
		}

		// scan complete display area of MASTER
		for (d = (DD1ST & 0xFF); d < (DD1END & 0xFF); d += 2)
		{
			WriteDisplayCol(p, *(WORD *) &Chipset.dd[MASTER].IORam[d]);
			p += nLcdZoom;
		}
	}

	EnterCriticalSection(&csGDILock);		// solving NT GDI problems
	{
		// load lcd with mask bitmap
		BitBlt(hLcdDC, 0, 0, nLcdXSize, nLcdYSize, hMaskDC, 0, 0, SRCCOPY);

		// mask segment mask with background and brush
		BitBlt(hLcdDC, 0, 0, nLcdXSize, nLcdYSize, hBmpBkDC, 0, 0, ROP_PDSPxax);

		// redraw display area
		BitBlt(hWindowDC, nLcdX, nLcdY, nLcdXSize, nLcdYSize, hLcdDC, 0, 0, SRCCOPY);
		GdiFlush();
	}
	LeaveCriticalSection(&csGDILock);
	return;
}

VOID UpdateAnnunciators(VOID)
{
/*
	ANNAD1		0x2E100						// Annunciator [<- - - -]
	ANN1_5		0x2E101						// Annunciator [- RAD USER AC]
	ANNAD2		0x2E102						// Annunciator [g f - -]
	ANN2_5		0x2E103						// Annunciator [- - - BAT]
	ANNAD3		0x2E34C						// Annunciator [0 - - -]
	ANN3_5		0x2E34D						// Annunciator [4 3 2 1]
	ANNAD4		0x2E34E						// Annunciator [((*)) - - -]
	ANN4_5		0x2E34F						// Annunciator [CALC SUSP PRGM ->]
*/

	UINT  i,nAnnId;
	DWORD d,dwColor;
	BYTE  byContrast;
	BOOL  bDispOn;

	if ((Chipset.dd[MASTER].IORam[DD1CTL & 0xFF]&DON) != 0)
	{
		// actual display contrast
		byContrast = Chipset.dd[MASTER].IORam[DCONTR & 0xFF];

		bDispOn = (nBlinkCnt < 32);			// display on from blink state

		nAnnId = 1;							// first annunciator

		// scan annunciator area of SLAVE1
		for (d = (ANNAD1 & 0xFF); d <= (ANN2_5 & 0xFF); d+=2)
		{
			WORD wCol = *(WORD *) &Chipset.dd[SLAVE1].IORam[d];

			// build 8 annunciators base on the display ROW table
			for (i = 0; i < ARRAYSIZEOF(byRowTable); ++i)
			{
				dwColor = dwKMLColor[((wCol & *(WORD *) &Chipset.dd[MASTER].IORam[byRowTable[i]]) != 0)
									 ? byContrast
									 : byContrast + 16
									];
				DrawAnnunciator(nAnnId++, bDispOn && dwColor != I, dwColor);
			}
		}

		// scan annunciator area of MASTER
		for (d = (ANNAD3 & 0xFF); d <= (ANN4_5 & 0xFF); d+=2)
		{
			WORD wCol = *(WORD *) &Chipset.dd[MASTER].IORam[d];

			// build 8 annunciators base on the display ROW table
			for (i = 0; i < ARRAYSIZEOF(byRowTable); ++i)
			{
				dwColor = dwKMLColor[((wCol & *(WORD *) &Chipset.dd[MASTER].IORam[byRowTable[i]]) != 0)
									 ? byContrast
									 : byContrast + 16
									];
				DrawAnnunciator(nAnnId++, bDispOn && dwColor != I, dwColor);
			}
		}
	}
	return;
}

static VOID CALLBACK LcdProc(UINT uEventId, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2)
{
	EnterCriticalSection(&csLcdLock);
	{
		if (uLcdTimerId)					// display update task still active
		{
			UpdateMainDisplay();
		}
	}
	LeaveCriticalSection(&csLcdLock);
	return;
	UNREFERENCED_PARAMETER(uEventId);
	UNREFERENCED_PARAMETER(uMsg);
	UNREFERENCED_PARAMETER(dwUser);
	UNREFERENCED_PARAMETER(dw1);
	UNREFERENCED_PARAMETER(dw2);
}

VOID StartDisplay(VOID)
{
	if (uLcdTimerId)						// LCD update timer running
		return;								// -> quit

	nBlinkCnt = 0;							// reset blink counter

	// display on?
	if (Chipset.dd[MASTER].IORam[DD1CTL & 0xFF]&DON)
	{
		UpdateAnnunciators();				// switch on annunciators
		VERIFY(uLcdTimerId = timeSetEvent(DISPLAY_FREQ,0,(LPTIMECALLBACK)&LcdProc,0,TIME_PERIODIC));
	}
	return;
}

VOID StopDisplay(VOID)
{
	if (uLcdTimerId == 0)					// timer stopped
		return;								// -> quit

	timeKillEvent(uLcdTimerId);				// stop display update
	uLcdTimerId = 0;						// set flag display update stopped

	EnterCriticalSection(&csLcdLock);
	{
		EnterCriticalSection(&csGDILock);	// solving NT GDI problems
		{
			// get original background from bitmap
			BitBlt(hLcdDC, 0, 0, nLcdXSize, nLcdYSize, hMainDC, nLcdX, nLcdY, SRCCOPY);
			GdiFlush();
		}
		LeaveCriticalSection(&csGDILock);
	}
	LeaveCriticalSection(&csLcdLock);
	InvalidateRect(hWnd,NULL,FALSE);
	return;
}

VOID ResizeWindow(VOID)
{
	if (hWnd != NULL)						// if window created
	{
		RECT rectWindow;
		RECT rectClient;

		rectWindow.left   = 0;
		rectWindow.top    = 0;
		rectWindow.right  = nBackgroundW;
		rectWindow.bottom = nBackgroundH;

		AdjustWindowRect(&rectWindow,
			(DWORD) GetWindowLongPtr(hWnd,GWL_STYLE),
			GetMenu(hWnd) != NULL || IsRectEmpty(&rectWindow));
		SetWindowPos(hWnd, bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0,
			rectWindow.right  - rectWindow.left,
			rectWindow.bottom - rectWindow.top,
			SWP_NOMOVE);

		// check if menu bar wrapped to two or more rows
		GetClientRect(hWnd, &rectClient);
		if (rectClient.bottom < (LONG) nBackgroundH)
		{
			rectWindow.bottom += (nBackgroundH - rectClient.bottom);
			SetWindowPos (hWnd, NULL, 0, 0,
				rectWindow.right  - rectWindow.left,
				rectWindow.bottom - rectWindow.top,
				SWP_NOMOVE | SWP_NOZORDER);
		}

		EnterCriticalSection(&csGDILock);	// solving NT GDI problems
		{
			_ASSERT(hWindowDC);				// move origin of destination window
			VERIFY(SetWindowOrgEx(hWindowDC, nBackgroundX, nBackgroundY, NULL));
			GdiFlush();
		}
		LeaveCriticalSection(&csGDILock);
		InvalidateRect(hWnd,NULL,TRUE);
	}
	return;
}
