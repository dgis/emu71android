/*
 *   types.h
 *
 *   This file is part of Emu71
 *
 *   Copyright (C) 2010 Christoph Gieﬂelink
 *
 */

// HST bits
#define XM 1
#define SB 2
#define SR 4
#define MP 8

#define SWORD SHORT							// signed   16 Bit variable
#define QWORD ULONGLONG						// unsigned 64 Bit variable

typedef struct
{
	DWORD	dwBase;							// base address of module
	BOOL	bCfg;							// module configuration flag
} SATCFG, *PSATCFG;

// Display Driver Chip
typedef struct
{
	BYTE	Ram[1024];						// user RAM
	BYTE	IORam[256];						// display/timer/control registers
	BOOL	bWkeEn;							// wakeup enabled by rising edge
											// of bit3 in display-timer control
} C1LF3;

// RAM Chip
typedef struct
{
	BYTE	Ram[4*2048];					// user RAM (4 1LG8 chips with 2K nibble)
	SATCFG	sCfg[4];						// module configuration data
} C1LG8;

typedef struct
{
	SWORD	nPosX;							// position of window
	SWORD	nPosY;
	BYTE	type;							// calculator type

	DWORD	pc;
	DWORD	d0;
	DWORD	d1;
	DWORD	rstkp;
	DWORD	rstk[8];
	BYTE	A[16];
	BYTE	B[16];
	BYTE	C[16];
	BYTE	D[16];
	BYTE	R0[16];
	BYTE	R1[16];
	BYTE	R2[16];
	BYTE	R3[16];
	BYTE	R4[16];
	BYTE	ST[4];
	BYTE	HST;
	BYTE	P;
	BYTE	SREQ;							// respond to SREQ? command
	WORD	out;
	WORD	in;
	BOOL	SoftInt;
	BOOL	Shutdn;
	BOOL	mode_dec;
	BOOL	inte;							// interrupt status flag (FALSE = int in service)
	BOOL	intk;							// 1 ms keyboard scan flag (TRUE = enable)
	BOOL	intd;							// keyboard interrupt pending (TRUE = int pending)
	BOOL	carry;

	BOOL	bShutdnWake;					// flag for wake up from SHUTDN mode

#if defined _USRDLL							// DLL version
	QWORD	cycles;							// oscillator cycles
#else										// EXE version
	DWORD	cycles;							// oscillator cycles
	DWORD	cycles_reserved;				// reserved for MSB of oscillator cycles
#endif

	WORD	wRomCrc;						// fingerprint of ROM

	C1LF3	dd[3];							// display driver chips (2 slave + master)
	C1LG8	ir[4];							// internal hybrid RAM chips (4)

	WORD	Keyboard_Row[4];				// keyboard Out lines
	WORD	IR15X;							// ON-key state

	BOOL	bExtModulesPlugged;				// plugged external modules
	BOOL	bOD;							// state of OD line
} CHIPSET;

typedef struct _PORTACC
{
	HANDLE h;								// associated handle
	BOOL   (*pfnIsModule)(HANDLE h,DWORD d); // address is handled by module
	VOID   (*pfnMap)(HANDLE h,DWORD a,DWORD b); // map pages
	BOOL   (*pfnConfig)(HANDLE h);			// configure module (CONFIG)
	VOID   (*pfnUncnfg)(HANDLE h);			// unconfigure module (UNCNFG)
	VOID   (*pfnReset)(HANDLE h);			// reset module (RESET)
	DWORD  (*pfnC_Eq_Id)(HANDLE h);			// fetch ID (C=ID)
	BYTE   (*pfnSREQ)(HANDLE h);			// service request of module
	// MM I/O buffer access
	VOID   (*pfnWriteIO)(HANDLE h,BYTE *a,DWORD d,LPDWORD ps);
	VOID   (*pfnReadIO) (HANDLE h,BYTE *a,DWORD d,LPDWORD ps,BOOL bUpdate);

	// destructor
	VOID   (*pfnDetachMem)(struct _PORTACC **ppsPort);
	struct _PORTACC *pNext;					// next module in same queue
} PORTACC, *PPORTACC;
