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

#include <jni.h>
#include <stdio.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/bitmap.h>
#include <malloc.h>

#include "core/pch.h"
#include "emu.h"
#include "core/io.h"
#include "core/kml.h"
#include "core/ops.h"
#include "win32-layer.h"
#include "android-portcfg.h"

extern AAssetManager * assetManager;
static jobject mainActivity = NULL;
jobject bitmapMainScreen = NULL;
AndroidBitmapInfo androidBitmapInfo;
enum DialogBoxMode currentDialogBoxMode;
enum ChooseKmlMode  chooseCurrentKmlMode;
TCHAR szChosenCurrentKml[MAX_PATH];
TCHAR szKmlLog[10240];
TCHAR szKmlLogBackup[10240];
TCHAR szKmlTitle[10240];
BOOL securityExceptionOccured;
BOOL settingsPort2en;
BOOL settingsPort2wr;
BOOL soundAvailable = FALSE;
BOOL soundEnabled = FALSE;



extern void win32Init();

extern void draw();
extern BOOL buttonDown(int x, int y);
extern void buttonUp(int x, int y);
extern void keyDown(int virtKey);
extern void keyUp(int virtKey);


JavaVM *java_machine;
JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    java_machine = vm;
    win32Init();
    return JNI_VERSION_1_6;
}

JNIEnv *getJNIEnvironment() {
    JNIEnv * jniEnv;
    jint ret;
    BOOL needDetach = FALSE;
    ret = (*java_machine)->GetEnv(java_machine, (void **) &jniEnv, JNI_VERSION_1_6);
    if (ret == JNI_EDETACHED) {
        // GetEnv: not attached
        ret = (*java_machine)->AttachCurrentThread(java_machine, &jniEnv, NULL);
        if (ret == JNI_OK) {
            needDetach = TRUE;
        }
    }
    return jniEnv;
}

int jniDetachCurrentThread() {
    jint ret = (*java_machine)->DetachCurrentThread(java_machine);
    return ret;
}

enum CALLBACK_TYPE {
    CALLBACK_TYPE_INVALIDATE = 0,
    CALLBACK_TYPE_WINDOW_RESIZE = 1
};

// https://stackoverflow.com/questions/9630134/jni-how-to-callback-from-c-or-c-to-java
int mainViewCallback(int type, int param1, int param2, const TCHAR * param3, const TCHAR * param4) {
    if (mainActivity) {
        JNIEnv *jniEnv = getJNIEnvironment();
        if(jniEnv) {
            jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
            if(mainActivityClass) {
                jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "updateCallback", "(IIILjava/lang/String;Ljava/lang/String;)I");
                jstring utfParam3 = (*jniEnv)->NewStringUTF(jniEnv, param3);
                jstring utfParam4 = (*jniEnv)->NewStringUTF(jniEnv, param4);
                int result = (*jniEnv)->CallIntMethod(jniEnv, mainActivity, midStr, type, param1, param2, utfParam3, utfParam4);
                (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
                //if(needDetach) ret = (*java_machine)->DetachCurrentThread(java_machine);
                return result;
            }
        }
    }
    return 0;
}

void mainViewUpdateCallback() {
    mainViewCallback(CALLBACK_TYPE_INVALIDATE, 0, 0, NULL, NULL);
}

void mainViewResizeCallback(int x, int y) {
    mainViewCallback(CALLBACK_TYPE_WINDOW_RESIZE, x, y, NULL, NULL);

    JNIEnv * jniEnv;
    int ret = (*java_machine)->GetEnv(java_machine, (void **) &jniEnv, JNI_VERSION_1_6);
    if(jniEnv) {
        ret = AndroidBitmap_getInfo(jniEnv, bitmapMainScreen, &androidBitmapInfo);
        if (ret < 0) {
            LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        }
    }
}

// Must be called in the main thread
int openFileFromContentResolver(const TCHAR * fileURL, int writeAccess) {
    int result = -1;
    JNIEnv *jniEnv = getJNIEnvironment();
    if(jniEnv) {
        jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
        if(mainActivityClass) {
            jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "openFileFromContentResolver", "(Ljava/lang/String;I)I");
            jstring utfFileURL = (*jniEnv)->NewStringUTF(jniEnv, fileURL);
            result = (*jniEnv)->CallIntMethod(jniEnv, mainActivity, midStr, utfFileURL, writeAccess);
            (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
        }
    }
    return result;
}
int openFileInFolderFromContentResolver(const TCHAR * filename, const TCHAR * folderURL, int writeAccess) {
    int result = -1;
    JNIEnv *jniEnv = getJNIEnvironment();
    if(jniEnv) {
        jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
        if(mainActivityClass) {
            jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "openFileInFolderFromContentResolver", "(Ljava/lang/String;Ljava/lang/String;I)I");
            jstring utfFilename = (*jniEnv)->NewStringUTF(jniEnv, filename);
            jstring utfFolderURL = (*jniEnv)->NewStringUTF(jniEnv, folderURL);
            result = (*jniEnv)->CallIntMethod(jniEnv, mainActivity, midStr, utfFilename, utfFolderURL, writeAccess);
            (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
        }
    }
    return result;
}
int closeFileFromContentResolver(int fd) {
    int result = -1;
    JNIEnv *jniEnv = getJNIEnvironment();
    if(jniEnv) {
        jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
        if(mainActivityClass) {
            jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "closeFileFromContentResolver", "(I)I");
            result = (*jniEnv)->CallIntMethod(jniEnv, mainActivity, midStr, fd);
            (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
        }
    }
    return result;
}

int showAlert(const TCHAR * messageText, int flags) {
    JNIEnv *jniEnv = getJNIEnvironment();
    if(jniEnv) {
        jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
        if(mainActivityClass) {
            jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "showAlert", "(Ljava/lang/String;)V");
            jstring messageUTF = (*jniEnv)->NewStringUTF(jniEnv, messageText);
            (*jniEnv)->CallVoidMethod(jniEnv, mainActivity, midStr, messageUTF);
            (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
        }
    }
    return IDOK;
}

void sendMenuItemCommand(int menuItem) {
    JNIEnv *jniEnv = getJNIEnvironment();
    if(jniEnv) {
        jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
        if(mainActivityClass) {
            jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "sendMenuItemCommand", "(I)V");
            (*jniEnv)->CallVoidMethod(jniEnv, mainActivity, midStr, menuItem);
            (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
        }
    }
}

TCHAR lastKMLFilename[MAX_PATH];

BOOL getFirstKMLFilenameForType(BYTE chipsetType, TCHAR * firstKMLFilename, size_t firstKMLFilenameSize) {
    if(firstKMLFilename) {
        JNIEnv *jniEnv = getJNIEnvironment();
        if(jniEnv) {
            jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
            if(mainActivityClass) {
                jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "getFirstKMLFilenameForType", "(C)Ljava/lang/String;");
                jobject resultString = (*jniEnv)->CallObjectMethod(jniEnv, mainActivity, midStr, (char)chipsetType);
                (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
                if (resultString) {
                    const char *strReturn = (*jniEnv)->GetStringUTFChars(jniEnv, resultString, 0);
                    if(_tcscmp(lastKMLFilename, strReturn) == 0) {
                        (*jniEnv)->ReleaseStringUTFChars(jniEnv, resultString, strReturn);
                        return FALSE;
                    }
                    _tcscpy(lastKMLFilename, strReturn);
                    _tcsncpy(firstKMLFilename, strReturn, firstKMLFilenameSize);
                    (*jniEnv)->ReleaseStringUTFChars(jniEnv, resultString, strReturn);
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

void clipboardCopyText(const TCHAR * text) {
    JNIEnv *jniEnv = getJNIEnvironment();
    if(jniEnv) {
        jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
        if(mainActivityClass) {
            jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "clipboardCopyText", "(Ljava/lang/String;)V");
            jint length = _tcslen(text);
            unsigned short * utf16String =  malloc(2 * (length + 1));
            for (int i = 0; i <= length; ++i)
                utf16String[i] = ((unsigned char *)text)[i];
            jstring messageUTF = (*jniEnv)->NewString(jniEnv, utf16String, length);
            (*jniEnv)->CallVoidMethod(jniEnv, mainActivity, midStr, messageUTF);
            (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
            free(utf16String);
        }
    }
}
const TCHAR * clipboardPasteText() {
    JNIEnv *jniEnv = getJNIEnvironment();
    if(jniEnv) {
        jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
        if(mainActivityClass) {
            jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "clipboardPasteText", "()Ljava/lang/String;");
            jobject result = (*jniEnv)->CallObjectMethod(jniEnv, mainActivity, midStr);
            (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
            if(result) {
                const jchar *strReturn = (*jniEnv)->GetStringChars(jniEnv, result, 0);
                jsize length = (*jniEnv)->GetStringLength(jniEnv, result);
                TCHAR * pasteText = (TCHAR *) GlobalAlloc(0, length + 2);
                for (int i = 0; i <= length; ++i)
                    pasteText[i] = strReturn[i] & 0xFF;
                pasteText[length] = 0;
                (*jniEnv)->ReleaseStringChars(jniEnv, result, strReturn);
                return pasteText;
            }
        }
    }
    return NULL;
}

void performHapticFeedback() {
    JNIEnv *jniEnv = getJNIEnvironment();
    if(jniEnv) {
        jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
        if(mainActivityClass) {
            jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "performHapticFeedback", "()V");
            (*jniEnv)->CallVoidMethod(jniEnv, mainActivity, midStr);
            (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
        }
    }
}

void sendByteUdp(unsigned char byteSent) {
    JNIEnv *jniEnv = getJNIEnvironment();
    if(jniEnv) {
        jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
        if(mainActivityClass) {
            jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "sendByteUdp", "(I)V");
            (*jniEnv)->CallVoidMethod(jniEnv, mainActivity, midStr, (int)byteSent);
            (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
        }
    }
}

void setKMLIcon(int imageWidth, int imageHeight, LPBYTE buffer, int bufferSize) {
    JNIEnv *jniEnv = getJNIEnvironment();
    if(jniEnv) {
        jclass mainActivityClass = (*jniEnv)->GetObjectClass(jniEnv, mainActivity);
        if(mainActivityClass) {
            jmethodID midStr = (*jniEnv)->GetMethodID(jniEnv, mainActivityClass, "setKMLIcon", "(II[B)V");

            jbyteArray pixels = NULL;
            if(buffer) {
                pixels = (*jniEnv)->NewByteArray(jniEnv, bufferSize);
                (*jniEnv)->SetByteArrayRegion(jniEnv, pixels, 0, bufferSize, (jbyte *) buffer);
            }
            (*jniEnv)->CallVoidMethod(jniEnv, mainActivity, midStr, imageWidth, imageHeight, pixels);
            (*jniEnv)->DeleteLocalRef(jniEnv, mainActivityClass);
        }
    }
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_start(JNIEnv *env, jobject thisz, jobject assetMgr, jobject activity) {

    chooseCurrentKmlMode = ChooseKmlMode_UNKNOWN;
    szChosenCurrentKml[0] = '\0';

    mainActivity = (*env)->NewGlobalRef(env, activity);

    assetManager = AAssetManager_fromJava(env, assetMgr);



    // OnCreate
    InitializeCriticalSection(&csGDILock);
    InitializeCriticalSection(&csLcdLock);
    InitializeCriticalSection(&csKeyLock);
    InitializeCriticalSection(&csTLock);
    InitializeCriticalSection(&csBitLock);
    InitializeCriticalSection(&csSlowLock);
    InitializeCriticalSection(&csDbgLock);





    DWORD dwThreadId;

    // read emulator settings
    GetCurrentDirectory(ARRAYSIZEOF(szCurrentDirectory),szCurrentDirectory);
    //ReadSettings();
    //bRomWriteable = FALSE;

    _tcscpy(szCurrentDirectory, "");
    _tcscpy(szEmuDirectory, "assets/calculators/");
//    _tcscpy(szRomDirectory, "assets/calculators/");
//    _tcscpy(szPort2Filename, "");

    // initialization
    QueryPerformanceFrequency(&lFreq);		// init high resolution counter
    //QueryPerformanceCounter(&lAppStart);



    hWnd = CreateWindow();
    //hWindowDC = CreateCompatibleDC(NULL);
    hWindowDC = GetDC(hWnd);


    // initialization
    ZeroMemory(psExtPortData,sizeof(psExtPortData)); // port data
    szCurrentKml[0] = 0;					// no KML file selected
    SetSpeed(bRealSpeed);					// set speed
	//MruInit(4);								// init MRU entries

    // create auto event handle
    hEventShutdn = CreateEvent(NULL,FALSE,FALSE,NULL);
    if (hEventShutdn == NULL)
    {
        AbortMessage(_T("Event creation failed."));
//		DestroyWindow(hWnd);
        return;
    }

    nState     = SM_RUN;					// init state must be <> nNextState
    nNextState = SM_INVALID;				// go into invalid state
    hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&WorkerThread, NULL, CREATE_SUSPENDED, &dwThreadId);
    if (hThread == NULL)
    {
        CloseHandle(hEventShutdn);			// close event handle
        AbortMessage(_T("Thread creation failed."));
//		DestroyWindow(hWnd);
        return;
    }

    soundEnabled = soundAvailable = SoundOpen(uWaveDevId);					// open waveform-audio output device

    ResumeThread(hThread);					// start thread
    while (nState!=nNextState) Sleep(0);	// wait for thread initialized
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_stop(JNIEnv *env, jobject thisz) {

    //if (hThread)
    SwitchToState(SM_RETURN);	// exit emulation thread

    ReleaseDC(hWnd, hWindowDC);
    DestroyWindow(hWnd);
	hWindowDC = NULL;						// hWindowDC isn't valid any more
	hWnd = NULL;

    DeleteCriticalSection(&csGDILock);
    DeleteCriticalSection(&csLcdLock);
    DeleteCriticalSection(&csKeyLock);
    DeleteCriticalSection(&csTLock);
    DeleteCriticalSection(&csBitLock);
    DeleteCriticalSection(&csSlowLock);
    DeleteCriticalSection(&csDbgLock);

    SoundClose();							// close waveform-audio output device
    soundEnabled = FALSE;

    if(bitmapMainScreen) {
        (*env)->DeleteGlobalRef(env, bitmapMainScreen);
        bitmapMainScreen = NULL;
    }
}


JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_changeBitmap(JNIEnv *env, jobject thisz, jobject bitmapMainScreen0) {

    if(bitmapMainScreen) {
        (*env)->DeleteGlobalRef(env, bitmapMainScreen);
        bitmapMainScreen = NULL;
    }

    bitmapMainScreen = (*env)->NewGlobalRef(env, bitmapMainScreen0);
    int ret = AndroidBitmap_getInfo(env, bitmapMainScreen, &androidBitmapInfo);
    if (ret < 0) {
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
    }
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_draw(JNIEnv *env, jobject thisz) {
    draw();
}
JNIEXPORT jboolean JNICALL Java_org_emulator_calculator_NativeLib_buttonDown(JNIEnv *env, jobject thisz, jint x, jint y) {
    return buttonDown(x, y) ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_buttonUp(JNIEnv *env, jobject thisz, jint x, jint y) {
    buttonUp(x, y);
}
JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_keyDown(JNIEnv *env, jobject thisz, jint virtKey) {
    keyDown(virtKey);
}
JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_keyUp(JNIEnv *env, jobject thisz, jint virtKey) {
    keyUp(virtKey);
}



JNIEXPORT jboolean JNICALL Java_org_emulator_calculator_NativeLib_isDocumentAvailable(JNIEnv *env, jobject thisz) {
    return bDocumentAvail ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT jstring JNICALL Java_org_emulator_calculator_NativeLib_getCurrentFilename(JNIEnv *env, jobject thisz) {
    jstring result = (*env)->NewStringUTF(env, szCurrentFilename);
    return result;
}

JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getCurrentModel(JNIEnv *env, jobject thisz) {
    return cCurrentRomType;
}

JNIEXPORT jboolean JNICALL Java_org_emulator_calculator_NativeLib_isBackup(JNIEnv *env, jobject thisz) {
    return (jboolean) (bBackup ? JNI_TRUE : JNI_FALSE);
}

JNIEXPORT jstring JNICALL Java_org_emulator_calculator_NativeLib_getKMLLog(JNIEnv *env, jobject thisz) {
    jstring result = (*env)->NewStringUTF(env, szKmlLog);
    return result;
}

JNIEXPORT jstring JNICALL Java_org_emulator_calculator_NativeLib_getKMLTitle(JNIEnv *env, jobject thisz) {
    jstring result = (*env)->NewStringUTF(env, szKmlTitle);
    return result;
}

JNIEXPORT jboolean JNICALL Java_org_emulator_calculator_NativeLib_getPort1Plugged(JNIEnv *env, jobject thisz) {
    return (jboolean) FALSE; //((Chipset.cards_status & PORT1_PRESENT) != 0);
}

JNIEXPORT jboolean JNICALL Java_org_emulator_calculator_NativeLib_getPort1Writable(JNIEnv *env, jobject thisz) {
    return (jboolean) FALSE; //((Chipset.cards_status & PORT1_WRITE) != 0);
}

JNIEXPORT jboolean JNICALL Java_org_emulator_calculator_NativeLib_getSoundEnabled(JNIEnv *env, jobject thisz) {
    return (jboolean) soundAvailable;
}

JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getGlobalColor(JNIEnv *env, jobject thisz) {
    return (jint) dwTColor;
}

JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getMacroState(JNIEnv *env, jobject thisz) {
    return nMacroState;
}


JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_onFileNew(JNIEnv *env, jobject thisz, jstring kmlFilename) {
    if (bDocumentAvail)
    {
        SwitchToState(SM_INVALID);
        if(bAutoSave) {
            SaveDocument();
        }
    }

    const char *filenameUTF8 = (*env)->GetStringUTFChars(env, kmlFilename , NULL) ;
    chooseCurrentKmlMode = ChooseKmlMode_FILE_NEW;
    _tcscpy(szChosenCurrentKml, filenameUTF8);
    (*env)->ReleaseStringUTFChars(env, kmlFilename, filenameUTF8);

    TCHAR * fileScheme = _T("document:");
    TCHAR * urlSchemeFound = _tcsstr(szChosenCurrentKml, fileScheme);
    if(urlSchemeFound) {
        _tcscpy(szEmuDirectory, szChosenCurrentKml + _tcslen(fileScheme) * sizeof(TCHAR));
        TCHAR * filename = _tcschr(szEmuDirectory, _T('|'));
        if(filename) {
            *filename = _T('\0');
        }
        //_tcscpy(szRomDirectory, szEmuDirectory);
    } else {
        _tcscpy(szEmuDirectory, "assets/calculators/");
        //_tcscpy(szRomDirectory, "assets/calculators/");
    }

    BOOL result = NewDocument();

    chooseCurrentKmlMode = ChooseKmlMode_UNKNOWN;

    if(result) {
        if(hLcdDC && hLcdDC->selectedBitmap) {
            hLcdDC->selectedBitmap->bitmapInfoHeader->biHeight = -abs(hLcdDC->selectedBitmap->bitmapInfoHeader->biHeight);
        }

        mainViewResizeCallback(nBackgroundW, nBackgroundH);
        draw();

        if (bStartupBackup) SaveBackup();        // make a RAM backup at startup

        if (pbyRom) SwitchToState(SM_RUN);
    }
    return result;
}
JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_onFileOpen(JNIEnv *env, jobject thisz, jstring stateFilename) {
    if (bDocumentAvail)
    {
        SwitchToState(SM_INVALID);
        if(bAutoSave) {
            SaveDocument();
        }
    }
    const char *stateFilenameUTF8 = (*env)->GetStringUTFChars(env, stateFilename , NULL) ;
    _tcscpy(szBufferFilename, stateFilenameUTF8);

    chooseCurrentKmlMode = ChooseKmlMode_FILE_OPEN;
    lastKMLFilename[0] = '\0';
    BOOL result = OpenDocument(szBufferFilename);
    if (result) {
        if(hLcdDC && hLcdDC->selectedBitmap) {
            hLcdDC->selectedBitmap->bitmapInfoHeader->biHeight = -abs(hLcdDC->selectedBitmap->bitmapInfoHeader->biHeight);
        }

        MruAdd(szBufferFilename);
    }
    chooseCurrentKmlMode = ChooseKmlMode_UNKNOWN;
    mainViewResizeCallback(nBackgroundW, nBackgroundH);
    if(result) {
        if (pbyRom) SwitchToState(SM_RUN);
    }
    draw();
    (*env)->ReleaseStringUTFChars(env, stateFilename, stateFilenameUTF8);
    if(securityExceptionOccured) {
        securityExceptionOccured = FALSE;
        result = -2;
    }
    return result;
}
JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_onFileSave(JNIEnv *env, jobject thisz) {
    // szBufferFilename must be set before calling that!!!
    BOOL result = FALSE;
    if (bDocumentAvail)
    {
        SwitchToState(SM_INVALID);
        result = SaveDocument();
        //SaveMapViewToFile(pbyPort2);
        SwitchToState(SM_RUN);
    }
    return result;
}
JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_onFileSaveAs(JNIEnv *env, jobject thisz, jstring newStateFilename) {
    const char *newStateFilenameUTF8 = (*env)->GetStringUTFChars(env, newStateFilename , NULL) ;

    BOOL result = FALSE;
    if (bDocumentAvail)
    {
        SwitchToState(SM_INVALID);
        _tcscpy(szBufferFilename, newStateFilenameUTF8);
        result = SaveDocumentAs(szBufferFilename);
        //SaveMapViewToFile(pbyPort2);
        SwitchToState(SM_RUN);
    }

    (*env)->ReleaseStringUTFChars(env, newStateFilename, newStateFilenameUTF8);
    return result;
}

JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_onFileClose(JNIEnv *env, jobject thisz) {
    if (bDocumentAvail)
    {
        SwitchToState(SM_INVALID);
        if(bAutoSave)
            SaveDocument();
        ResetDocument();
        SetWindowTitle(NULL);
        szKmlTitle[0] = '\0';
        mainViewResizeCallback(nBackgroundW, nBackgroundH);
        draw();
        return TRUE;
    }
    return FALSE;
}

JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_onObjectLoad(JNIEnv *env, jobject thisz, jstring filename) {
//    const char *filenameUTF8 = (*env)->GetStringUTFChars(env, filename , NULL) ;

//    SuspendDebugger();						// suspend debugger
//    bDbgAutoStateCtrl = FALSE;				// disable automatic debugger state control
//
//    if (!(Chipset.IORam[DSPCTL]&DON))		// calculator off, turn on
//    {
//        KeyboardEvent(TRUE,0,0x8000);
//        Sleep(dwWakeupDelay);
//        KeyboardEvent(FALSE,0,0x8000);
//
//        // wait for sleep mode
//        while (Chipset.Shutdn == FALSE) Sleep(0);
//    }
//
//    if (nState != SM_RUN)
//    {
//        InfoMessage(_T("The emulator must be running to load an object."));
//        goto cancel;
//    }
//
//    if (WaitForSleepState())				// wait for cpu SHUTDN then sleep state
//    {
//        InfoMessage(_T("The emulator is busy."));
//        goto cancel;
//    }
//
//    _ASSERT(nState == SM_SLEEP);
//
////    if (bLoadObjectWarning)
////    {
////        UINT uReply = YesNoCancelMessage(
////                _T("Warning: Trying to load an object while the emulator is busy\n")
////                _T("will certainly result in a memory lost. Before loading an object\n")
////                _T("you should be sure that the calculator is in idle state.\n")
////                _T("Do you want to see this warning next time you try to load an object?"),0);
////        switch (uReply)
////        {
////            case IDYES:
////                break;
////            case IDNO:
////                bLoadObjectWarning = FALSE;
////                break;
////            case IDCANCEL:
////                SwitchToState(SM_RUN);
////                goto cancel;
////        }
////    }
//    if (   cCurrentRomType == 'N'			// HP32SII
//           || cCurrentRomType == 'D')			// HP42S
//    {
////        if (!GetLoadObjectFilename(_T(RAW_FILTER),_T("RAW")))
////        {
////            SwitchToState(SM_RUN);
////            goto cancel;
////        }
//        if (cCurrentRomType == 'N')
//            GetUserCode32(filenameUTF8);
//        else
//            GetUserCode42(filenameUTF8);
//        SwitchToState(SM_RUN);
//    }
//    else									// HP28S
//    {
////        if (!GetLoadObjectFilename(_T(HP_FILTER),_T("HP")))
////        {
////            SwitchToState(SM_RUN);
////            goto cancel;
////        }
//        if (!LoadObject(filenameUTF8))
//        {
//            SwitchToState(SM_RUN);
//            goto cancel;
//        }
//
//        SwitchToState(SM_RUN);				// run state
//        while (nState!=nNextState) Sleep(0);
//        _ASSERT(nState == SM_RUN);
//        KeyboardEvent(TRUE,0,0x8000);
//        Sleep(dwWakeupDelay);
//        KeyboardEvent(FALSE,0,0x8000);
//        while (Chipset.Shutdn == FALSE) Sleep(0);
//    }
//
//    cancel:
//    bDbgAutoStateCtrl = TRUE;				// enable automatic debugger state control
//    ResumeDebugger();
//
//
//    (*env)->ReleaseStringUTFChars(env, filename, filenameUTF8);

    return TRUE;
}

JNIEXPORT jobjectArray JNICALL Java_org_emulator_calculator_NativeLib_getObjectsToSave(JNIEnv *env, jobject thisz) {

//    if (nState != SM_RUN)
//    {
//        InfoMessage(_T("The emulator must be running to save an object."));
//        return 0;
//    }
//
//    if (WaitForSleepState())				// wait for cpu SHUTDN then sleep state
//    {
//        InfoMessage(_T("The emulator is busy."));
//        return 0;
//    }
//
//    _ASSERT(nState == SM_SLEEP);
//
//    labels[0] = 0;
//    if (cCurrentRomType == 'N') { // HP32SII
//        currentDialogBoxMode = DialogBoxMode_GET_USRPRG32;
//        OnSelectProgram32();
//    } else if(cCurrentRomType == 'D') { // HP42S
//        currentDialogBoxMode = DialogBoxMode_GET_USRPRG42;
//        OnSelectProgram42();
//    }
//    currentDialogBoxMode = DialogBoxMode_UNKNOWN;
//
//    int labelCount = 0;
//    if(labels[0]) {
//        int i = 0;
//        while(i < MAX_LABEL_SIZE && labels[i]) {
//            if(labels[i] == 9)
//                labelCount++;
//            i++;
//        }
//    }
//
//    jobjectArray objectArray = (jobjectArray)(*env)->NewObjectArray(env,
//       labelCount,
//       (*env)->FindClass(env, "java/lang/String"),
//       (*env)->NewStringUTF(env, ""));
//
//    if(labelCount) {
//        TCHAR * label = labels;
//        int l = 0;
//        int i = 0;
//        while(i < MAX_LABEL_SIZE && labels[i]) {
//            if(labels[i] == 9) {
//                labels[i] = 0;
//                (*env)->SetObjectArrayElement(env, objectArray,
//                        l, (*env)->NewStringUTF(env, label));
//                label = &labels[i + 1];
//                l++;
//            }
//            i++;
//        }
//    }
//
//    SwitchToState(SM_RUN);

//    return objectArray;
    return NULL;
}

JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_onObjectSave(JNIEnv *env, jobject thisz, jstring filename, jbooleanArray objectsToSaveItemChecked) {
    //OnObjectSave();

//    const char *filenameUTF8 = (*env)->GetStringUTFChars(env, filename , NULL);
//
//    if (nState != SM_RUN)
//    {
//        InfoMessage(_T("The emulator must be running to save an object."));
//        return 0;
//    }
//
//    if (WaitForSleepState())				// wait for cpu SHUTDN then sleep state
//    {
//        InfoMessage(_T("The emulator is busy."));
//        return 0;
//    }
//
//    _ASSERT(nState == SM_SLEEP);
//
//
//    if(objectsToSaveItemChecked &&
//        (cCurrentRomType == 'N' // HP32SII
//        || cCurrentRomType == 'D') // HP42S
//    ) {
//        jsize len = (*env)->GetArrayLength(env, objectsToSaveItemChecked);
//        jboolean *body = (*env)->GetBooleanArrayElements(env, objectsToSaveItemChecked, 0);
//        selItemDataCount = 0;
//        for (int i = 0; i < len; i++) {
//            selItemDataIndex[i] = body[i] ? TRUE : FALSE;
//            if(selItemDataIndex[i])
//                selItemDataCount++;
//        }
//        (*env)->ReleaseBooleanArrayElements(env, objectsToSaveItemChecked, body, 0);
//
//        _tcscpy(getSaveObjectFilenameResult, filenameUTF8);
//
//        if (cCurrentRomType == 'N') {
//            currentDialogBoxMode = DialogBoxMode_SET_USRPRG32;
//            OnSelectProgram32();
//        } else {
//            currentDialogBoxMode = DialogBoxMode_SET_USRPRG42;
//            OnSelectProgram42();
//        }
//        getSaveObjectFilenameResult[0] = 0;
//        currentDialogBoxMode = DialogBoxMode_UNKNOWN;
//    }
//    else									// HP28S
//    {
//        _ASSERT(cCurrentRomType == 'O');
////        if (GetSaveObjectFilename(_T(HP_FILTER),_T("HP")))
////        {
//            SaveObject(filenameUTF8);
////        }
//    }
//
//    SwitchToState(SM_RUN);
//
//    (*env)->ReleaseStringUTFChars(env, filename, filenameUTF8);

    return TRUE;
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_onViewCopy(JNIEnv *env, jobject thisz, jobject bitmapScreen) {

    AndroidBitmapInfo bitmapScreenInfo;
    int ret = AndroidBitmap_getInfo(env, bitmapScreen, &bitmapScreenInfo);
    if (ret < 0) {
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }

    void * pixelsDestination;
    if ((ret = AndroidBitmap_lockPixels(env, bitmapScreen, &pixelsDestination)) < 0) {
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }

    INT nxSize, nySize;

    GetSizeLcdBitmap(&nxSize,&nySize); // get LCD size

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
    GetObject((HGDIOBJ) hBmp, sizeof(bm), &bm);

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





    size_t strideSource = (size_t)(4 * ((hBmp->bitmapInfoHeader->biWidth * hBmp->bitmapInfoHeader->biBitCount + 31) / 32));
    size_t strideDestination = bitmapScreenInfo.stride;
    VOID * bitmapBitsSource = (VOID *)hBmp->bitmapBits;
    VOID * bitmapBitsDestination = pixelsDestination;
    for(int y = 0; y < hBmp->bitmapInfoHeader->biHeight; y++) {
        memcpy(bitmapBitsDestination, bitmapBitsSource, strideSource);
        bitmapBitsSource += strideSource;
        bitmapBitsDestination += strideDestination;
    }


    DeleteDC(hBmpDC);
    DeleteObject(hBmp);
    #undef WIDTHBYTES
    #undef PALVERSION


    AndroidBitmap_unlockPixels(env, bitmapScreen);
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_onStackCopy(JNIEnv *env, jobject thisz) {
    OnStackCopy();
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_onStackPaste(JNIEnv *env, jobject thisz) {
    //TODO Memory leak -> No GlobalFree of the paste data!!!!
    OnStackPaste();
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_onViewReset(JNIEnv *env, jobject thisz) {
    if (nState != SM_RUN)
        return;
    SwitchToState(SM_SLEEP);
    CpuReset();							// register setting after Cpu Reset
    SwitchToState(SM_RUN);
}

JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_onViewScript(JNIEnv *env, jobject thisz, jstring kmlFilename) {

    TCHAR szKmlFile[MAX_PATH];
//    BOOL  bKMLChanged,bSucc;
    BYTE cType = cCurrentRomType;
    SwitchToState(SM_INVALID);

    // make a copy of the current KML script file name
    _ASSERT(sizeof(szKmlFile) == sizeof(szCurrentKml));
    lstrcpyn(szKmlFile,szCurrentKml,ARRAYSIZEOF(szKmlFile));

    const char *filenameUTF8 = (*env)->GetStringUTFChars(env, kmlFilename , NULL) ;
    _tcscpy(szCurrentKml, filenameUTF8);
    (*env)->ReleaseStringUTFChars(env, kmlFilename, filenameUTF8);

//    bKMLChanged = FALSE;					// KML script not changed
    BOOL bSucc = TRUE;							// KML script successful loaded

    chooseCurrentKmlMode = ChooseKmlMode_CHANGE_KML;

//    do
//    {
//        if (!DisplayChooseKml(cType))		// quit with Cancel
//        {
//            if (!bKMLChanged)				// KML script not changed
//                break;						// exit loop with current loaded KML script
//
//            // restore KML script file name
//            lstrcpyn(szCurrentKml,szKmlFile,ARRAYSIZEOF(szCurrentKml));
//
//            // try to restore old KML script
//            if ((bSucc = InitKML(szCurrentKml,FALSE)))
//                break;						// exit loop with success
//
//            // restoring failed, save document
//            if (IDCANCEL != SaveChanges(bAutoSave))
//                break;						// exit loop with no success
//
//            _ASSERT(bSucc == FALSE);		// for continuing loop
//        }
//        else								// quit with Ok
//        {
//            bKMLChanged = TRUE;				// KML script changed
            bSucc = InitKML(szCurrentKml,FALSE);
//        }
//    }
//    while (!bSucc);							// retry if KML script is invalid

    BOOL result = bSucc;

    if(!bSucc) {
        // restore KML script file name
        lstrcpyn(szCurrentKml,szKmlFile,ARRAYSIZEOF(szCurrentKml));

        _tcsncpy(szKmlLogBackup, szKmlLog, sizeof(szKmlLog) / sizeof(TCHAR));

        // try to restore old KML script
        bSucc = InitKML(szCurrentKml,FALSE);

        _tcsncpy(szKmlLog, szKmlLogBackup, sizeof(szKmlLog) / sizeof(TCHAR));
    }
    chooseCurrentKmlMode = ChooseKmlMode_UNKNOWN;

    if (bSucc)
    {
        if(hLcdDC && hLcdDC->selectedBitmap) {
            hLcdDC->selectedBitmap->bitmapInfoHeader->biHeight = -abs(hLcdDC->selectedBitmap->bitmapInfoHeader->biHeight);
        }

        mainViewResizeCallback(nBackgroundW, nBackgroundH);
        draw();
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

    return result;
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_onBackupSave(JNIEnv *env, jobject thisz) {
    UINT nOldState;
    if (pbyRom == NULL) return;
    nOldState = SwitchToState(SM_INVALID);
    SaveBackup();
    SwitchToState(nOldState);
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_onBackupRestore(JNIEnv *env, jobject thisz) {
    SwitchToState(SM_INVALID);
    RestoreBackup();
    if(hLcdDC && hLcdDC->selectedBitmap) {
        hLcdDC->selectedBitmap->bitmapInfoHeader->biHeight = -abs(hLcdDC->selectedBitmap->bitmapInfoHeader->biHeight);
    }
    if (pbyRom) SwitchToState(SM_RUN);
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_onBackupDelete(JNIEnv *env, jobject thisz) {
    ResetBackup();
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_onToolMacroNew(JNIEnv *env, jobject thisz, jstring filename) {
    const char *filenameUTF8 = (*env)->GetStringUTFChars(env, filename , NULL);
    _tcscpy(getSaveObjectFilenameResult, filenameUTF8);
    (*env)->ReleaseStringUTFChars(env, filename, filenameUTF8);
    currentDialogBoxMode = DialogBoxMode_SAVE_MACRO;
    OnToolMacroNew();
    getSaveObjectFilenameResult[0] = 0;
    currentDialogBoxMode = DialogBoxMode_UNKNOWN;
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_onToolMacroPlay(JNIEnv *env, jobject thisz, jstring filename) {
    const char *filenameUTF8 = (*env)->GetStringUTFChars(env, filename , NULL);
    _tcscpy(getSaveObjectFilenameResult, filenameUTF8);
    (*env)->ReleaseStringUTFChars(env, filename, filenameUTF8);
    currentDialogBoxMode = DialogBoxMode_OPEN_MACRO;
    OnToolMacroPlay();
    getSaveObjectFilenameResult[0] = 0;
    currentDialogBoxMode = DialogBoxMode_UNKNOWN;
}

JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_onToolMacroStop(JNIEnv *env, jobject thisz) {
    OnToolMacroStop();
}


JNIEXPORT void JNICALL Java_org_emulator_calculator_NativeLib_setConfiguration(JNIEnv *env, jobject thisz, jstring key, jint isDynamic, jint intValue1, jint intValue2, jstring stringValue) {
    const char *configKey = (*env)->GetStringUTFChars(env, key, NULL) ;
    const char *configStringValue = stringValue ? (*env)->GetStringUTFChars(env, stringValue, NULL) : NULL;

    bAutoSave = FALSE;
    bAutoSaveOnExit = FALSE;
    //bLoadObjectWarning = FALSE;
    bAlwaysDisplayLog = TRUE;

    if(_tcscmp(_T("settings_realspeed"), configKey) == 0) {
        bRealSpeed = (BOOL) intValue1;
        SetSpeed(bRealSpeed);			// set speed
    } else if(_tcscmp(_T("settings_sound_volume"), configKey) == 0) {
        dwWaveVol = (DWORD)intValue1;
        if(soundEnabled && intValue1 == 0) {
            SoundClose();
            soundEnabled = FALSE;
        } else if(!soundEnabled && intValue1 > 0) {
            SoundOpen(uWaveDevId);
            soundEnabled = TRUE;
        }
    } else if(_tcscmp(_T("settings_macro"), configKey) == 0) {
        bMacroRealSpeed = (BOOL)intValue1;
        nMacroTimeout = 500 - intValue2;
    }

    if(configKey)
        (*env)->ReleaseStringUTFChars(env, key, configKey);
    if(configStringValue)
        (*env)->ReleaseStringUTFChars(env, stringValue, configStringValue);
}

JNIEXPORT jboolean JNICALL Java_org_emulator_calculator_NativeLib_isPortExtensionPossible(JNIEnv *env, jobject thisz) {
    return (cCurrentRomType=='S' || cCurrentRomType=='G' || cCurrentRomType==0 ? JNI_TRUE : JNI_FALSE);
}

JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getState(JNIEnv *env, jobject thisz) {
    return nState;
}

JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getScreenPositionX(JNIEnv *env, jobject thisz) {
    return nLcdX - nBackgroundX;
}
JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getScreenPositionY(JNIEnv *env, jobject thisz) {
    return nLcdY - nBackgroundY;
}
JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getScreenWidth(JNIEnv *env, jobject thisz) {
    INT nxSize,nySize;
    GetSizeLcdBitmap(&nxSize,&nySize);	// get LCD size
    return nxSize; //*nLcdZoom;
}
JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getScreenHeight(JNIEnv *env, jobject thisz) {
    INT nxSize,nySize;
    GetSizeLcdBitmap(&nxSize,&nySize);	// get LCD size
    return nySize; //*nLcdZoom;
}
JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getScreenWidthNative(JNIEnv *env, jobject thisz) {
	INT nxSize,nySize;
	GetSizeLcdBitmap(&nxSize,&nySize);	// get LCD size
	return nxSize / nLcdZoom;
}
JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getScreenHeightNative(JNIEnv *env, jobject thisz) {
	INT nxSize,nySize;
	GetSizeLcdBitmap(&nxSize,&nySize);	// get LCD size
	return nySize / nLcdZoom;
}
JNIEXPORT jint JNICALL Java_org_emulator_calculator_NativeLib_getLCDBackgroundColor(JNIEnv *env, jobject thisz) {
	if (hMainDC) {
		COLORREF brushColor = GetPixel(hMainDC, nLcdX, nLcdY);
		return ((brushColor & 0xFF0000) >> 16) | ((brushColor & 0xFF) << 16) | (brushColor & 0xFF00);
	}
	return -1;
}


JNIEXPORT void JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_loadCurrPortConfig(JNIEnv *env, jobject thisz) {
    LoadCurrPortConfig();
}

JNIEXPORT void JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_saveCurrPortConfig(JNIEnv *env, jobject thisz) {
    SaveCurrPortConfig();
}

JNIEXPORT void JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_cleanup(JNIEnv *env, jobject thisz) {
    Cleanup();
}




enum PORT_DATA_TYPE {
    PORT_DATA_INDEX = 0,
    PORT_DATA_APPLY,
    PORT_DATA_TYPE,
    PORT_DATA_BASE,
    PORT_DATA_SIZE,
    PORT_DATA_CHIPS,
    PORT_DATA_DATA,
    PORT_DATA_FILENAME,
    PORT_DATA_ADDR_OUT,
    PORT_DATA_PORT_OUT,
    PORT_DATA_PORT_IN,
    PORT_DATA_TCP_ADDR_OUT,
    PORT_DATA_TCP_PORT_OUT,
    PORT_DATA_TCP_PORT_IN,
    PORT_DATA_NEXT_INDEX,
    PORT_DATA_EXIST,
    PORT_DATA_IRAMSIG
};

extern PPORTCFG psPortCfg[];
PPORTCFG *CfgModule(UINT nPort);
VOID DelPort(UINT nPort);
VOID DelPortCfg(UINT nPort);

JNIEXPORT jint JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_editPortConfigStart(JNIEnv *env, jobject thisz) {
    UINT nOldState = SwitchToState(SM_INVALID);

    DismountPorts();						// dismount the ports
    return nOldState;
}

JNIEXPORT void JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_editPortConfigEnd(JNIEnv *env, jobject thisz, jint nOldState) {
    MountPorts();							// remount the ports

    SwitchToState(nOldState);
}

JNIEXPORT jint JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_getPortCfgModuleIndex(JNIEnv *env, jobject thisz, jint port) {
    PPORTCFG psCfg = psPortCfg[port];
    if(!psCfg)
        return -1;
    int currentPortIndex = 0;
    while(psCfg->pNext) {
        if (psCfg->bApply == FALSE)		// module not applied
            break;
        currentPortIndex++;
        psCfg = psCfg->pNext;
    }
    return currentPortIndex;
}

PPORTCFG getPortCfg(int port, int portIndex) {
    PPORTCFG psCfg = psPortCfg[port];
    if(!psCfg)
        return NULL;
    int currentPortIndex = 0;
    while(currentPortIndex != portIndex && psCfg->pNext) {
        currentPortIndex++;
        psCfg = psCfg->pNext;
    }
    if(currentPortIndex == portIndex)
        return psCfg;
    return NULL;
}

JNIEXPORT jint JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_getPortCfgInteger(JNIEnv *env, jobject thisz, jint port, jint portIndex, jint portDataType) {
//    PPORTCFG psCfg = psPortCfg[port];
//    if(!psCfg)
//        return -1;
//    int currentPortIndex = 0;
//    while(currentPortIndex != portIndex && psCfg->pNext) {
//        currentPortIndex++;
//        psCfg = psCfg->pNext;
//    }
//    if(currentPortIndex == portIndex) {
    PPORTCFG psCfg = getPortCfg(port, portIndex);
    if(psCfg) {
        switch (portDataType) {
            case PORT_DATA_INDEX:
                // Logical index. May not be linear.
                return psCfg->nIndex;
            case PORT_DATA_APPLY:
                return psCfg->bApply;
            case PORT_DATA_TYPE:
                return psCfg->nType;
            case PORT_DATA_BASE:
                return psCfg->dwBase;
            case PORT_DATA_SIZE:
                return psCfg->dwSize;
            case PORT_DATA_CHIPS:
                return psCfg->dwChips;
//            case PORT_DATA_DATA:
//                return psCfg->pbyData;
            case PORT_DATA_PORT_OUT:
                return psCfg->wPortOut;
            case PORT_DATA_PORT_IN:
                return psCfg->wPortIn;
            case PORT_DATA_TCP_PORT_OUT:
                return psCfg->psTcp->wPortOut;
            case PORT_DATA_TCP_PORT_IN:
                return psCfg->psTcp->wPortIn;
            case PORT_DATA_NEXT_INDEX:
                if(psCfg->pNext)
                    return portIndex + 1;
                else
                    return -1;
            case PORT_DATA_EXIST:
                return TRUE;
            case PORT_DATA_IRAMSIG:
                // RAM with data
                // independent RAM signature?
                return psCfg->nType == TYPE_RAM && psCfg->pbyData != NULL && psCfg->dwSize >= 8 && Npack(psCfg->pbyData,8) == IRAMSIG;
            default:
                ;
        }
    }

    return -1;
}

JNIEXPORT jstring JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_getPortCfgString(JNIEnv *env, jobject thisz, jint port, jint portIndex, jint portDataType) {
//    PPORTCFG psCfg = psPortCfg[port];
//    int currentPortIndex = 0;
//    while(currentPortIndex != portIndex && psCfg->pNext) {
//        currentPortIndex++;
//        psCfg = psCfg->pNext;
//    }
//    if(currentPortIndex == portIndex) {
    PPORTCFG psCfg = getPortCfg(port, portIndex);
    if(psCfg) {
        switch (portDataType) {
            case PORT_DATA_FILENAME:
                if(psCfg->szFileName == NULL) return NULL;
                return (*env)->NewStringUTF(env, psCfg->szFileName);
            case PORT_DATA_ADDR_OUT:
                if(psCfg->szFileName == NULL) return NULL;
                return (*env)->NewStringUTF(env, psCfg->lpszAddrOut);
            case PORT_DATA_TCP_ADDR_OUT:
                if(psCfg->szFileName == NULL) return NULL;
                return (*env)->NewStringUTF(env, psCfg->psTcp->lpszAddrOut);
            default:
                ;
        }
    }

    return NULL;
}


JNIEXPORT jboolean JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_setPortCfgInteger(JNIEnv *env, jobject thisz, jint port, jint portIndex, jint portDataType, jint value) {
//    PPORTCFG psCfg = psPortCfg[port];
//    int currentPortIndex = 0;
//    while(currentPortIndex != portIndex && psCfg->pNext) {
//        currentPortIndex++;
//        psCfg = psCfg->pNext;
//    }
//    if(currentPortIndex == portIndex) {
    PPORTCFG psCfg = getPortCfg(port, portIndex);
    if(psCfg) {
        switch (portDataType) {
            case PORT_DATA_INDEX:
                // Logical index. May not be linear.
                psCfg->nIndex = (UINT)value;
                return JNI_TRUE;
            case PORT_DATA_APPLY:
                psCfg->bApply = value;
                return JNI_TRUE;
            case PORT_DATA_TYPE:
                psCfg->nType = (UINT)value;
                return JNI_TRUE;
            case PORT_DATA_BASE:
                psCfg->dwBase = (DWORD)value;
                return JNI_TRUE;
            case PORT_DATA_SIZE:
                psCfg->dwSize = (DWORD)value;
                return JNI_TRUE;
            case PORT_DATA_CHIPS:
                psCfg->dwChips = (DWORD)value;
                return JNI_TRUE;
//            case PORT_DATA_DATA:
//                psCfg->pbyData = value;
//                return JNI_TRUE;
//            case PORT_DATA_FILENAME:
//                psCfg->szFileName = value;
//                return JNI_TRUE;
//            case PORT_DATA_ADDR_OUT:
//                psCfg->lpszAddrOut = value;
//                return JNI_TRUE;
            case PORT_DATA_PORT_OUT:
                psCfg->wPortOut = (WORD)value;
                return JNI_TRUE;
            case PORT_DATA_PORT_IN:
                psCfg->wPortIn = (WORD)value;
                return JNI_TRUE;
//            case PORT_DATA_TCP_ADDR_OUT:
//                psCfg->psTcp->lpszAddrOut = value;
//                return JNI_TRUE;
            case PORT_DATA_TCP_PORT_OUT:
                psCfg->psTcp->wPortOut = (WORD)value;
                return JNI_TRUE;
            case PORT_DATA_TCP_PORT_IN:
                psCfg->psTcp->wPortIn = (WORD)value;
                return JNI_TRUE;
//            case PORT_DATA_NEXT_INDEX:
//                return currentPortIndex + 1;
            default:
                ;
        }
    }
    return JNI_FALSE;
}
JNIEXPORT jboolean JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_setPortCfgString(JNIEnv *env, jobject thisz, jint port, jint portIndex, jint portDataType, jstring value) {
//    PPORTCFG psCfg = psPortCfg[port];
//    int currentPortIndex = 0;
//    while(currentPortIndex != portIndex && psCfg->pNext) {
//        currentPortIndex++;
//        psCfg = psCfg->pNext;
//    }
//    if(currentPortIndex == portIndex) {
    PPORTCFG psCfg = getPortCfg(port, portIndex);
    if(psCfg) {
        const char *valueUTF8 = value ? (*env)->GetStringUTFChars(env, value, NULL) : NULL;
        //_tcscpy(szBufferFilename, valueUTF8);
        BOOL result = FALSE;
        switch (portDataType) {
            case PORT_DATA_FILENAME:
                if (value)
                    _tcsncpy(psCfg->szFileName, valueUTF8, MAX_PATH);
                else
                    psCfg->szFileName[0] = 0;
                result = TRUE;
                break;
            case PORT_DATA_ADDR_OUT:
                if (psCfg->lpszAddrOut)
                    free(psCfg->lpszAddrOut);
                if (value) {
                    int length = strlen(valueUTF8) + 2;
                    psCfg->lpszAddrOut = malloc(length);
                    _tcsncpy(psCfg->lpszAddrOut, valueUTF8, length);
                } else
                    psCfg->lpszAddrOut = NULL;
                result = TRUE;
                break;
//            case PORT_DATA_TCP_ADDR_OUT: // Readonly!
//                psCfg->psTcp->lpszAddrOut = value;
//                result = TRUE;
//                break;
            default:
                ;
        }
        if(value)
            (*env)->ReleaseStringUTFChars(env, value, valueUTF8);
        if(result)
            return JNI_TRUE;
    }
    return JNI_FALSE;
}
JNIEXPORT jboolean JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_setPortCfgBytes(JNIEnv *env, jobject thisz, jint port, jint portIndex, jint portDataType, jbyteArray value) {
//    PPORTCFG psCfg = psPortCfg[port];
//    int currentPortIndex = 0;
//    while(currentPortIndex != portIndex && psCfg->pNext) {
//        currentPortIndex++;
//        psCfg = psCfg->pNext;
//    }
//    if(currentPortIndex == portIndex) {
    PPORTCFG psCfg = getPortCfg(port, portIndex);
    if(psCfg) {
        switch (portDataType) {
            case PORT_DATA_DATA: {
                jsize len = (*env)->GetArrayLength(env, value);
                jboolean isCopy = JNI_FALSE;
                jbyte *javaBytes = (*env)->GetByteArrayElements(env, value, &isCopy);
                if (psCfg->pbyData)
                    free(psCfg->pbyData);
                psCfg->pbyData = malloc(len);
                memcpy(psCfg->pbyData, javaBytes, len);
                (*env)->ReleaseByteArrayElements(env, value, javaBytes, 0);
                return JNI_TRUE;
            }
            default:
                ;
        }
    }
    return JNI_FALSE;
}
JNIEXPORT void JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_setPortChanged(JNIEnv *env, jobject thisz, jint port, jint changed) {
    bChanged[port] = changed;
}

JNIEXPORT void JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_addNewPort(JNIEnv *env, jobject thisz, jint nActPort, jint nItem) {
    PPORTCFG *ppsCfg;
    ppsCfg = &psPortCfg[nActPort];	// root of module
    {
        INT nItem,nIndex;

        // something selected
        if (nItem != -1)
        {
            PPORTCFG psCfgIns;

            // goto selected entry in the queue
            for (nIndex = 0; nIndex < nItem && *ppsCfg != NULL; ++nIndex)
            {
                ppsCfg = &(*ppsCfg)->pNext;
            }

            // allocate memory for new module definition and insert it at position
            psCfgIns = (PPORTCFG) calloc(1,sizeof(*psPortCfg[0]));
            psCfgIns->pNext = *ppsCfg;
            *ppsCfg = psCfgIns;
        }
        else						// nothing selected
        {
            // goto last entry in the queue
            while (*ppsCfg != NULL)
            {
                ppsCfg = &(*ppsCfg)->pNext;
            }

            // allocate memory for new module definition and add it at last position
            *ppsCfg = (PPORTCFG) calloc(1,sizeof(*psPortCfg[0]));
        }

        // new module
        (*ppsCfg)->nIndex = (UINT) -1;
    }

    // default 32KB RAM with 1LQ4 interface chip
    (*ppsCfg)->nType   = TYPE_RAM;
    (*ppsCfg)->dwSize  = 32 * 2048;
    (*ppsCfg)->dwChips = 1;
    (*ppsCfg)->dwBase  = 0x00000;
}

JNIEXPORT void JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_configModuleAbort(JNIEnv *env, jobject thisz, jint nActPort) {
    DelPortCfg(nActPort);		// delete the not applied module
}

JNIEXPORT void JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_configModuleDelete(JNIEnv *env, jobject thisz, jint nActPort, jint nItemSelectedModule) {
    INT nItem,nIndex;

    _ASSERT(nActPort < ARRAYSIZEOF(bChanged));
    bChanged[nActPort] = TRUE;

    // something selected
    if ((nItem = nItemSelectedModule) != -1 /*LB_ERR*/)
    {
        // root of module
        PPORTCFG *ppsCfg = &psPortCfg[nActPort];

        // goto selected entry in the queue
        for (nIndex = 0; nIndex < nItem && *ppsCfg != NULL; ++nIndex)
        {
            ppsCfg = &(*ppsCfg)->pNext;
        }

        if (*ppsCfg != NULL)
        {
            // mark entry as not applied that DelPortCfg() can delete it
            (*ppsCfg)->bApply = FALSE;
            DelPortCfg(nActPort); // delete the not applied module
        }
    }
    else						// nothing selected
    {
        DelPort(nActPort);		// delete port data
    }
}

JNIEXPORT void JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_deleteNotAppliedModule(JNIEnv *env, jobject thisz, jint nActPort) {
    if (psPortCfg[nActPort] != NULL)
    {
        if ((*CfgModule(nActPort))->bApply == FALSE)
        {
            // delete the not applied module
            DelPortCfg(nActPort);
        }
    }
}

JNIEXPORT jboolean JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_applyPort(JNIEnv *env, jobject thisz, jint nPort,
        jint portDataType, jstring portDataFilename, jint portDataSize, jint portDataHardAddr, jint portDataChips) {

    PPORTCFG psCfg;
    DWORD    dwChipSize;
    BOOL     bSucc;
    INT      i;

    _ASSERT(nPort < ARRAYSIZEOF(psPortCfg));
    _ASSERT(psPortCfg[nPort] != NULL);

    psCfg = *CfgModule(nPort);				// module in queue to configure

    // module type combobox
    //VERIFY((i = (INT) SendDlgItemMessage(hDlg,IDC_CFG_TYPE,CB_GETCURSEL,0,0)) != CB_ERR);
    psCfg->nType = (UINT)portDataType; //sModType[i].dwData;

    // hard wired address
    psCfg->dwBase = 0x00000;

    // filename
    //GetDlgItemText(hDlg,IDC_CFG_FILE,psCfg->szFileName,ARRAYSIZEOF(psPortCfg[0]->szFileName));
    const char *portDataFilenameUTF8 = (*env)->GetStringUTFChars(env, portDataFilename, NULL) ;
    _tcsncpy(psCfg->szFileName, portDataFilenameUTF8, ARRAYSIZEOF(psPortCfg[0]->szFileName));
    (*env)->ReleaseStringUTFChars(env, portDataFilename, portDataFilenameUTF8);

    switch (psCfg->nType)
    {
        case TYPE_RAM:
            if (*psCfg->szFileName == 0)		// empty filename field
            {
                // size combobox
                //VERIFY((i = (INT) SendDlgItemMessage(hDlg,IDC_CFG_SIZE,CB_GETCURSEL,0,0)) != CB_ERR);
                dwChipSize = portDataSize; //sMod[i].dwData;
                bSucc = (dwChipSize != 0);
            }
            else								// given filename
            {
                LPBYTE pbyData;

                // get RAM size from filename content
                if ((bSucc = MapFile(psCfg->szFileName,&pbyData,&dwChipSize)))
                {
                    // independent RAM signature in file header?
                    bSucc = dwChipSize >= 8 && (Npack(pbyData,8) == IRAMSIG);
                    free(pbyData);
                }
            }
            break;
        case TYPE_HRD:
            // hard wired address
            //VERIFY((i = (INT) SendDlgItemMessage(hDlg,IDC_CFG_HARDADDR,CB_GETCURSEL,0,0)) != CB_ERR);
            psCfg->dwBase = portDataHardAddr; //sHrdAddr[i].dwData;
            // no break;
        case TYPE_ROM:
        case TYPE_HPIL:
            // filename
            bSucc = MapFile(psCfg->szFileName,NULL,&dwChipSize);
            break;
        default:
            _ASSERT(FALSE);
            dwChipSize = 0;
            bSucc = FALSE;
    }

    // no. of chips combobox
    //if ((i = (INT) SendDlgItemMessage(hDlg,IDC_CFG_CHIPS,CB_GETCURSEL,0,0)) == CB_ERR)
    if ((i = portDataChips) == -1)
        i = 0;								// no one selected, choose "Auto"

    if (bSucc && i == 0)					// "Auto"
    {
        DWORD dwSize;

        switch (psCfg->nType)
        {
            case TYPE_RAM:
                // can be build out of 32KB chips
                dwSize = ((dwChipSize % (32 * 2048)) == 0)
                         ? (32 * 2048)			// use 32KB chips
                         : ( 1 * 2048);			// use 1KB chips

                if (dwChipSize < dwSize)		// 512 Byte Memory
                    dwSize = dwChipSize;
                break;
            case TYPE_HRD:
            case TYPE_ROM:
            case TYPE_HPIL:
                // can be build out of 16KB chips
                dwSize = ((dwChipSize % (16 * 2048)) == 0)
                         ? (16 * 2048)			// use 16KB chips
                         : dwChipSize;			// use a single chip
                break;
            default:
                _ASSERT(FALSE);
                dwSize = 1;
        }

        i = dwChipSize / dwSize;			// calculate no. of chips
    }

    psCfg->dwChips = i;						// set no. of chips

    if (bSucc)								// check size vs. no. of chips
    {
        DWORD dwSingleSize;

        // check if the overall size is a multiple of a chip size
        bSucc = (dwChipSize % psCfg->dwChips) == 0;

        // check if the single chip has a power of 2 size
        VERIFY((dwSingleSize = dwChipSize / psCfg->dwChips));
        bSucc = bSucc && dwSingleSize != 0 && (dwSingleSize & (dwSingleSize - 1)) == 0;

        if (!bSucc)
        {
            InfoMessage(_T("Number of chips don't fit to the overall size!"));
        }
    }

    if (bSucc)
    {
        _ASSERT(nPort < ARRAYSIZEOF(bChanged));
        bChanged[nPort]   = TRUE;
        psCfg->dwSize = dwChipSize;
        psCfg->bApply = TRUE;
    }

    return bSucc ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT jboolean JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_dataLoad(JNIEnv *env, jobject thisz, jint port, jint portIndex, jstring filename) {
    const char *filenameUTF8 = (*env)->GetStringUTFChars(env, filename , NULL) ;

    LPBYTE pbyData;
    DWORD  dwSize;

    PPORTCFG psCfg = getPortCfg(port, portIndex);

    _ASSERT(psCfg != NULL);			// item has data

    // RAM with data
    _ASSERT(psCfg->nType == TYPE_RAM && psCfg->pbyData != NULL);

    if (MapFile(filenameUTF8,&pbyData,&dwSize) == TRUE)
    {
        // different size or not independent RAM signature
        if (psCfg->dwSize != dwSize || Npack(pbyData,8) != IRAMSIG)
        {
            free(pbyData);
            AbortMessage(_T("This file cannot be loaded."));
            (*env)->ReleaseStringUTFChars(env, filename, filenameUTF8);
            return JNI_FALSE;
        }

        Chipset.HST |= MP;			// module pulled

        // overwrite the data in the port memory
        CopyMemory(psCfg->pbyData,pbyData,psCfg->dwSize);
        free(pbyData);
    }

    (*env)->ReleaseStringUTFChars(env, filename, filenameUTF8);
    return JNI_TRUE;
}
JNIEXPORT jboolean JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_dataSave(JNIEnv *env, jobject thisz, jint port, jint portIndex, jstring filename) {
    const char *filenameUTF8 = (*env)->GetStringUTFChars(env, filename , NULL) ;

    HANDLE hFile;
    DWORD  dwPos,dwWritten;
    BYTE   byData;

    PPORTCFG psCfg = getPortCfg(port, portIndex);
    _ASSERT(psCfg != NULL);			// item has data

    // RAM with data
    _ASSERT(psCfg->nType == TYPE_RAM && psCfg->pbyData != NULL);

    SetCurrentDirectory(szEmuDirectory);
    hFile = CreateFile(filenameUTF8,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    SetCurrentDirectory(szCurrentDirectory);

    // error, couldn't create a new file
    if (hFile == INVALID_HANDLE_VALUE)
    {
        AbortMessage(_T("This file cannot be created."));
        (*env)->ReleaseStringUTFChars(env, filename, filenameUTF8);
        return JNI_FALSE;
    }

    for (dwPos = 0; dwPos < psCfg->dwSize; dwPos += 2)
    {
        byData = (psCfg->pbyData[dwPos+1] << 4) | psCfg->pbyData[dwPos];
        WriteFile(hFile,&byData,sizeof(byData),&dwWritten,NULL);
    }
    CloseHandle(hFile);

    (*env)->ReleaseStringUTFChars(env, filename, filenameUTF8);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_org_emulator_seventy_one_PortSettingsFragment_modifyOriginalTCPData(JNIEnv *env, jobject thisz, jint port, jint portIndex) {

    PPORTCFG psCfg = getPortCfg(port, portIndex);
    if (psCfg != NULL && psCfg->psTcp != NULL)
    {
        // modify the original data to avoid a configuration changed on the whole module
        free(psCfg->psTcp->lpszAddrOut);
        psCfg->psTcp->dwAddrSize = (DWORD) strlen(psCfg->lpszAddrOut);
        psCfg->psTcp->lpszAddrOut = (LPSTR) malloc(psCfg->psTcp->dwAddrSize+1);
        CopyMemory(psCfg->psTcp->lpszAddrOut,psCfg->lpszAddrOut,psCfg->psTcp->dwAddrSize+1);
        psCfg->psTcp->wPortOut = psCfg->wPortOut;
        psCfg->psTcp->wPortIn  = psCfg->wPortIn;
    }
}