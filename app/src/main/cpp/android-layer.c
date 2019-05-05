#include "core/pch.h"
#include "core/Emu71.h"

// portcfg.c
BOOL GetOpenImageFile(HWND hWnd,LPTSTR szBuffer,DWORD dwBufferSize) {
    return FALSE;
}

// satmem.c
HANDLE AllocSaturnMem(UINT nType,DWORD dwChipSize,DWORD dwChips,BOOL bHybrid,LPBYTE pbyMem,PSATCFG psCfg) {
    return NULL;
}
BOOL AttachSaturnMem(PPORTACC *ppsPort,HANDLE hMemModule) {
    return FALSE;
}

// hpil.c
BOOL  bEnableRFC = TRUE;					// send a RFC frame behind a CMD frame
BOOL  bHpilRealDevices  = TRUE;				// real IL hardware maybe connected over Pilbox
DWORD dwHpilLoopTimeout = 500;				// standard timeout for finishing the virtual IL
HANDLE AllocHpilMem(UINT nType,LPDWORD pdwSize,LPBYTE *ppbyMem,PSATCFG psCfg,LPCSTR pszAddrOut,WORD wPortOut,WORD wPortIn) {
    return NULL;
}
BOOL AttachHpilMem(PPORTACC *ppsPort,HANDLE hMemModule) {
    return FALSE;
}

// debugger.c
VOID UpdateDbgCycleCounter(VOID) {
    return;
}
BOOL CheckBreakpoint(DWORD dwAddr, DWORD wRange, UINT nType) {
    return 0;
}
VOID NotifyDebugger(INT nType) {
    return;
}
VOID DisableDebugger(VOID) {
    return;
}
LRESULT OnToolDebug(VOID) {
    return 0;
}
VOID LoadBreakpointList(HANDLE hFile) {
    return;
}
VOID SaveBreakpointList(HANDLE hFile) {
    return;
}
VOID CreateBackupBreakpointList(VOID) {
    return;
}
VOID RestoreBackupBreakpointList(VOID) {
    return;
}
