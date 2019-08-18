// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "core/pch.h"
#include "emu.h"

// portcfg.c
BOOL GetOpenImageFile(HWND hWnd,LPTSTR szBuffer,DWORD dwBufferSize) {
    return FALSE;
}

//// satmem.c
//HANDLE AllocSaturnMem(UINT nType,DWORD dwChipSize,DWORD dwChips,BOOL bHybrid,LPBYTE pbyMem,PSATCFG psCfg) {
//    return NULL;
//}
//BOOL AttachSaturnMem(PPORTACC *ppsPort,HANDLE hMemModule) {
//    return FALSE;
//}
//
//// hpil.c
//BOOL  bEnableRFC = TRUE;					// send a RFC frame behind a CMD frame
//BOOL  bHpilRealDevices  = TRUE;				// real IL hardware maybe connected over Pilbox
//DWORD dwHpilLoopTimeout = 500;				// standard timeout for finishing the virtual IL
//HANDLE AllocHpilMem(UINT nType,LPDWORD pdwSize,LPBYTE *ppbyMem,PSATCFG psCfg,LPCSTR pszAddrOut,WORD wPortOut,WORD wPortIn) {
//    return NULL;
//}
//BOOL AttachHpilMem(PPORTACC *ppsPort,HANDLE hMemModule) {
//    return FALSE;
//}

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
