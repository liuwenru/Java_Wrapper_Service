/*
 * Copyright (c) 1999, 2020 Tanuki Software, Ltd.
 * http://www.tanukisoftware.com
 * All rights reserved.
 *
 * This software is the proprietary information of Tanuki Software.
 * You shall use it only in accordance with the terms of the
 * license agreement you entered into with Tanuki Software.
 * http://wrapper.tanukisoftware.com/doc/english/licenseOverview.html
 *
 *
 * Portions of the Software have been derived from source code
 * developed by Silver Egg Technology under the following license:
 *
 * Copyright (c) 2001 Silver Egg Technology
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sub-license, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 */

/**
 * Author:
 *   Leif Mortenson <leif@tanukisoftware.com>
 */

#ifdef WIN32
#include <shlwapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <direct.h>
#include <io.h>
#include <math.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include <wtsapi32.h>
#include <winnt.h>
#include <Sddl.h>
#include <sys/timeb.h>
#include <conio.h>
#include <Softpub.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <DbgHelp.h>
#include <lm.h>
#include <Pdh.h>
#include <ntsecapi.h>
#include <Fcntl.h>
#include "psapi.h"

#include "wrapper_i18n.h"
#include "resource.h"
#include "wrapper.h"
#include "wrapperinfo.h"
#include "property.h"
#include "logger.h"
#include "wrapper_file.h"
#include "wrapper_encoding.h"

/*#define WRAPPER_DEBUG_CONTROL_HANDLER */
/*#define WRAPPER_DEBUG_MESSAGES */

/* The largest possible command line length on Windows. */
#define MAX_COMMAND_LINE_LEN 32766

#define ENCODING (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)

typedef struct {
    LPWSTR lpszProgramName;
    LPWSTR lpszPublisherLink;
    LPWSTR lpszMoreInfoLink;
} SPROG_PUBLISHERINFO, *PSPROG_PUBLISHERINFO;



#ifndef POLICY_AUDIT_SUBCATEGORY_COUNT
/* The current SDK is pre-Vista.  Add the required definitions. */
typedef struct _TOKEN_ELEVATION {
  DWORD TokenIsElevated;
} TOKEN_ELEVATION, *PTOKEN_ELEVATION;
#define TokenElevation TokenOrigin + 3
#endif

/*****************************************************************************
 * Win32 specific variables and procedures                                   *
 *****************************************************************************/
SERVICE_STATUS          ssStatus;
SERVICE_STATUS_HANDLE   sshStatusHandle;

#define SYSTEM_PATH_MAX_LEN 256
static TCHAR *systemPath[SYSTEM_PATH_MAX_LEN];
static HANDLE wrapperChildStdoutWr = INVALID_HANDLE_VALUE;
static HANDLE wrapperChildStdoutRd = INVALID_HANDLE_VALUE;

TCHAR wrapperClasspathSeparator = TEXT(';');

HANDLE startupThreadHandle;
DWORD startupThreadId;
int startupThreadStarted = FALSE;
int startupThreadStopped = FALSE;

HANDLE javaIOThreadHandle;
DWORD javaIOThreadId;
int javaIOThreadStarted = FALSE;
int stopJavaIOThread = FALSE;
int javaIOThreadStopped = FALSE;

HANDLE timerThreadHandle;
DWORD timerThreadId;
int timerThreadStarted = FALSE;
int stopTimerThread = FALSE;
int timerThreadStopped = FALSE;
TICKS timerTicks = WRAPPER_TICK_INITIAL;

HANDLE messageThreadHandle;
DWORD messageThreadId;
int messageThreadStarted = FALSE;
int stopMessageThread = FALSE;
int messageThreadStopped = FALSE;

/** Flag which keeps track of whether or not the CTRL-C key has been pressed. */
int ctrlCTrapped = FALSE;
TICKS ctrlCTrappedLastTick = WRAPPER_TICK_INITIAL;

/** Flag which keeps track of whether or not PID files should be deleted on shutdown. */
int cleanUpPIDFilesOnExit = FALSE;

TCHAR* getExceptionName(DWORD exCode, int nullOnUnknown);

/* Dynamically loadedfunction types. */
typedef SERVICE_STATUS_HANDLE(*FTRegisterServiceCtrlHandlerEx)(LPCTSTR, LPHANDLER_FUNCTION_EX, LPVOID);
typedef BOOL(*FTWTSQuerySessionInformation)(IN HANDLE, IN DWORD, IN WTS_INFO_CLASS, LPWSTR*, DWORD*);
typedef void(*FTWTSFreeMemory)(IN PVOID);
typedef BOOL(*FTWTSRegisterSessionNotification)(HWND, DWORD);
typedef BOOL(*FTWTSUnRegisterSessionNotification)(HWND);
typedef BOOL(*FTShutdownBlockReasonCreate)(HWND, LPCWSTR);
typedef BOOL(*FTShutdownBlockReasonDestroy)(HWND);

/* Dynamically loaded functions. */
FARPROC OptionalGetProcessTimes = NULL;
FARPROC OptionalGetProcessMemoryInfo = NULL;
FTRegisterServiceCtrlHandlerEx OptionalRegisterServiceCtrlHandlerEx = NULL;
FTWTSQuerySessionInformation OptionalWTSQuerySessionInformation = NULL;
FTWTSFreeMemory OptionalWTSFreeMemory = NULL;
FTWTSRegisterSessionNotification OptionalWTSRegisterSessionNotification = NULL;
FTWTSUnRegisterSessionNotification OptionalWTSUnRegisterSessionNotification = NULL;
FTShutdownBlockReasonCreate OptionalShutdownBlockReasonCreate = NULL;
FTShutdownBlockReasonDestroy OptionalShutdownBlockReasonDestroy = NULL;

/******************************************************************************
 * Windows specific code
 ******************************************************************************/
PDH_HQUERY pdhQuery = NULL;
PDH_HCOUNTER pdhCounterPhysicalDiskAvgQueueLen = NULL;
PDH_HCOUNTER pdhCounterPhysicalDiskAvgWriteQueueLen = NULL;
PDH_HCOUNTER pdhCounterPhysicalDiskAvgReadQueueLen = NULL;
PDH_HCOUNTER pdhCounterMemoryPageFaultsPSec = NULL;
PDH_HCOUNTER pdhCounterMemoryTransitionFaultsPSec = NULL;
PDH_HCOUNTER pdhCounterProcessWrapperPageFaultsPSec = NULL;
PDH_HCOUNTER pdhCounterProcessJavaPageFaultsPSec = NULL;

#if(_WIN32_WINNT < 0x0501)
 #define WM_WTSSESSION_CHANGE       0x02B1
 #define WTS_CONSOLE_CONNECT        0x1
 #define WTS_CONSOLE_DISCONNECT     0x2
 #define WTS_REMOTE_CONNECT         0x3
 #define WTS_REMOTE_DISCONNECT      0x4
 #define WTS_SESSION_LOGON          0x5
 #define WTS_SESSION_LOGOFF         0x6
 #define WTS_SESSION_LOCK           0x7
 #define WTS_SESSION_UNLOCK         0x8
 #define WTS_SESSION_REMOTE_CONTROL 0x9
 #define WTS_SESSION_CREATE         0xa
 #define WTS_SESSION_TERMINATE      0xb
 #define SM_REMOTECONTROL           0x2001
#endif
#if(_WIN32_WINNT < 0x0600)
 #define WM_DWMNCRENDERINGCHANGED 0x031f
#endif

#ifdef WRAPPER_DEBUG_MESSAGES
const TCHAR *_wrapper_getMessageName(UINT message) {
    /* https://wiki.winehq.org/List_Of_Windows_Messages */
    switch (message) {
        case WM_CREATE:                 return TEXT("WM_CREATE");               /* 0x0001 */
        case WM_DESTROY:                return TEXT("WM_DESTROY");              /* 0x0002 */
        case WM_SIZE:                   return TEXT("WM_SIZE");                 /* 0x0005 */
        case WM_ACTIVATE:               return TEXT("WM_ACTIVATE");             /* 0x0006 */
        case WM_SETFOCUS:               return TEXT("WM_SETFOCUS");             /* 0x0007 */
        case WM_KILLFOCUS:              return TEXT("WM_KILLFOCUS");            /* 0x0008 */
        case WM_PAINT:                  return TEXT("WM_PAINT");                /* 0x000f */
        case WM_GETTEXT:                return TEXT("WM_GETTEXT");              /* 0x000d */
        case WM_CLOSE:                  return TEXT("WM_CLOSE");                /* 0x0010 */
        case WM_ERASEBKGND:             return TEXT("WM_ERASEBKGND");           /* 0x0014 */
        case WM_ENDSESSION:             return TEXT("WM_ENDSESSION");           /* 0x0016 */
        case WM_QUERYENDSESSION:        return TEXT("WM_QUERYENDSESSION");      /* 0x0017 */
        case WM_SHOWWINDOW:             return TEXT("WM_SHOWWINDOW");           /* 0x0018 */
        case WM_WININICHANGE:           return TEXT("WM_WININICHANGE");         /* 0x001a */
        case WM_ACTIVATEAPP:            return TEXT("WM_ACTIVATEAPP");          /* 0x001c */
        case WM_TIMECHANGE:             return TEXT("WM_TIMECHANGE");           /* 0x001e */
        case WM_SETCURSOR:              return TEXT("WM_SETCURSOR");            /* 0x0020 */
        case WM_MOUSEACTIVATE:          return TEXT("WM_MOUSEACTIVATE");        /* 0x0021 */
        case WM_GETMINMAXINFO:          return TEXT("WM_GETMINMAXINFO");        /* 0x0024 */
        case 0x003b:                    return TEXT("WM_UNKNOWN_003B");         /* 0x003b - Not sure what this is. */
        case WM_WINDOWPOSCHANGING:      return TEXT("WM_WINDOWPOSCHANGING");    /* 0x0046 */
        case WM_WINDOWPOSCHANGED:       return TEXT("WM_WINDOWPOSCHANGED");     /* 0x0047 */
        case WM_GETICON:                return TEXT("WM_GETICON");              /* 0x007f */
        case WM_NCCREATE:               return TEXT("WM_NCCREATE");             /* 0x0081 */
        case WM_NCDESTROY:              return TEXT("WM_NCDESTROY");            /* 0x0082 */
        case WM_NCCALCSIZE:             return TEXT("WM_NCCALCSIZE");           /* 0x0083 */
        case WM_NCHITTEST:              return TEXT("WM_NCHITTEST");            /* 0x0084 */
        case WM_NCPAINT:                return TEXT("WM_NCPAINT");              /* 0x0085 */
        case WM_NCACTIVATE:             return TEXT("WM_NCACTIVATE");           /* 0x0086 */
        case WM_NCMOUSEMOVE:            return TEXT("WM_NCMOUSEMOVE");          /* 0x00a0 */
        case WM_INITDIALOG:             return TEXT("WM_INITDIALOG");           /* 0x0110 */
        case WM_COMMAND:                return TEXT("WM_COMMAND");              /* 0x0111 */
        case WM_SYSCOMMAND:             return TEXT("WM_SYSCOMMAND");           /* 0x0112 */
        case WM_MENUSELECT:             return TEXT("WM_MENUSELECT");           /* 0x011f */
        case WM_ENTERIDLE:              return TEXT("WM_ENTERIDLE");            /* 0x0121 */
        case WM_UNINITMENUPOPUP:        return TEXT("WM_UNINITMENUPOPUP");      /* 0x0125 */
        case WM_CTLCOLORBTN:            return TEXT("WM_CTLCOLORBTN");          /* 0x0135 */
        case WM_CTLCOLORDLG:            return TEXT("WM_CTLCOLORDLG");          /* 0x0136 */
        case WM_CTLCOLORSTATIC:         return TEXT("WM_CTLCOLORSTATIC");       /* 0x0138 */
        case WM_MOUSEMOVE:              return TEXT("WM_MOUSEMOVE");            /* 0x0200 */
        case WM_ENTERMENULOOP:          return TEXT("WM_ENTERMENULOOP");        /* 0x0211 */
        case WM_EXITMENULOOP:           return TEXT("WM_EXITMENULOOP");         /* 0x0212 */
        case WM_CAPTURECHANGED:         return TEXT("WM_CAPTURECHANGED");       /* 0x0215 */
        case WM_POWERBROADCAST:         return TEXT("WM_POWERBROADCAST");       /* 0x0218 */
        case WM_DEVICECHANGE:           return TEXT("WM_DEVICECHANGE");         /* 0x0219 */
        case WM_IME_SETCONTEXT:         return TEXT("WM_IME_SETCONTEXT");       /* 0x0281 */
        case WM_IME_NOTIFY:             return TEXT("WM_IME_NOTIFY");           /* 0x0282 */
        case WM_NCMOUSELEAVE:           return TEXT("WM_NCMOUSELEAVE");         /* 0x02a2 */
        case WM_WTSSESSION_CHANGE:      return TEXT("WM_WTSSESSION_CHANGE");    /* 0x02b1 */
        case WM_DWMNCRENDERINGCHANGED:  return TEXT("WM_DWMNCRENDERINGCHANGED");/* 0x031f */
        case WM_USER:                   return TEXT("WM_USER");                 /* 0x0400 */
        case 0xc1c6:                    return TEXT("WM_UNKNOWN_C1C6");         /* 0xc1c6 - Not sure what this is. */
        default:                        return TEXT("UNKNOWN");
    }
}

void _wrapper_debugMessage(TCHAR *handlerName, HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s(hwnd=%p, message=%s(0x%04x), wParam(l:0x%04x, h:0x%04x), lParam(l:0x%04x, h:0x%04x))"), handlerName, hwnd, _wrapper_getMessageName(message), message, LOWORD(wParam), HIWORD(wParam), LOWORD(lParam), HIWORD(lParam));
}
#else
 #define _wrapper_debugMessage(handlerName, hwnd, message, wParam, lParam)
#endif

HWND _wrapper_messageWindowHWND;
const TCHAR messageWindowClassName[] = TEXT("wrapperMessageWindowClass");

#define FILEPATHSIZE 1024
/**
 * Tests whether or not the current OS is at or below the version of Windows NT.
 *
 * @return TRUE if NT 4.0 or earlier, FALSE otherwise.
 */
BOOL isWindowsNT4_0OrEarlier()
{
   OSVERSIONINFOEX osvi;
   BOOL bOsVersionInfoEx;
   BOOL retval;

   /* Try calling GetVersionEx using the OSVERSIONINFOEX structure.
    *  If that fails, try using the OSVERSIONINFO structure. */
    retval = TRUE;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

#pragma warning(push)
#pragma warning(disable : 4996) /* Visual Studio 2013 deprecates GetVersionEx but we still want to use it. */
    if (!(bOsVersionInfoEx = GetVersionEx ((OSVERSIONINFO *)&osvi))) {
       /* If OSVERSIONINFOEX doesn't work, try OSVERSIONINFO. */
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        if (!GetVersionEx((OSVERSIONINFO *)&osvi)) {
            retval = TRUE;
        }
    }
#pragma warning(pop)

    if (osvi.dwMajorVersion <= 4) {
        retval = TRUE;
    } else {
        retval = FALSE;
    }

    return retval;
}

void loadDLLProcs() {
    HMODULE kernel32Mod;
    HMODULE psapiMod;
    HMODULE advapi32Mod;
    HMODULE user32Mod;
    HMODULE wtsapi32Mod;

    /* The KERNEL32 module was added in NT 3.5. */
    if ((kernel32Mod = GetModuleHandle(TEXT("KERNEL32.DLL"))) == NULL) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
            TEXT("The %s file was not found.  Some functions will be disabled."),
            TEXT("KERNEL32.DLL"));
    } else {
        if ((OptionalGetProcessTimes = GetProcAddress(kernel32Mod, "GetProcessTimes")) == NULL) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("The %s function is not available in this %s version.  Some functions will be disabled."),
                TEXT("GetProcessTimes"), TEXT("KERNEL32.DLL"));
        }
    }

    /* The PSAPI module was added in NT 4.0. */
    if ((psapiMod = LoadLibrary(TEXT("PSAPI.DLL"))) == NULL) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
            TEXT("The %s file was not found.  Some functions will be disabled."),
            TEXT("PSAPI.DLL"));
    } else {
        if ((OptionalGetProcessMemoryInfo = GetProcAddress(psapiMod, "GetProcessMemoryInfo")) == NULL) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("The %s function is not available in this %s version.  Some functions will be disabled."),
                TEXT("GetProcessMemoryInfo"), TEXT("PSAPI.DLL"));
        }
    }

    /* The ADVAPI32 module was added in NT 5.0. */
    if ((advapi32Mod = LoadLibrary(TEXT("ADVAPI32.DLL"))) == NULL) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
            TEXT("The %s file was not found.  Some functions will be disabled."),
            TEXT("ADVAPI32.DLL"));
    } else {
        if ((OptionalRegisterServiceCtrlHandlerEx = (FTRegisterServiceCtrlHandlerEx)GetProcAddress(advapi32Mod, "RegisterServiceCtrlHandlerExW")) == NULL) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("The %s function is not available in this %s version.  Some functions will be disabled."),
                TEXT("RegisterServiceCtrlHandlerExW"), TEXT("ADVAPI32.DLL"));
        }
    }
    
    /* The USER32 module was added in Vista, Server 2008. */
    if ((user32Mod = LoadLibrary(TEXT("USER32.DLL"))) == NULL) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
            TEXT("The %s file was not found.  Some functions will be disabled."),
            TEXT("USER32.DLL"));
    } else {
        if ((OptionalShutdownBlockReasonCreate = (FTShutdownBlockReasonCreate)GetProcAddress(user32Mod, "ShutdownBlockReasonCreate")) == NULL) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("The %s function is not available in this %s version.  Some functions will be disabled."),
                TEXT("ShutdownBlockReasonCreate"), TEXT("USER32.DLL"));
        }
        if ((OptionalShutdownBlockReasonDestroy = (FTShutdownBlockReasonDestroy)GetProcAddress(user32Mod, "ShutdownBlockReasonDestroy")) == NULL) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("The %s function is not available in this %s version.  Some functions will be disabled."),
                TEXT("ShutdownBlockReasonDestroy"), TEXT("USER32.DLL"));
        }
    }
    
    /* The WTSAPI32 module was added in Vista, Server 2008. */
    if ((wtsapi32Mod = LoadLibrary(TEXT("WTSAPI32.DLL"))) == NULL) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
            TEXT("The %s file was not found.  Some functions will be disabled."),
            TEXT("WTSAPI32.DLL"));
    } else {
        if ((OptionalWTSQuerySessionInformation = (FTWTSQuerySessionInformation)GetProcAddress(wtsapi32Mod, "WTSQuerySessionInformationW")) == NULL) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("The %s function is not available in this %s version.  Some functions will be disabled."),
                TEXT("WTSQuerySessionInformationW"), TEXT("WTSAPI32.DLL"));
        }
        if ((OptionalWTSFreeMemory = (FTWTSFreeMemory)GetProcAddress(wtsapi32Mod, "WTSFreeMemory")) == NULL) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("The %s function is not available in this %s version.  Some functions will be disabled."),
                TEXT("WTSFreeMemory"), TEXT("WTSAPI32.DLL"));
        }
        if ((OptionalWTSRegisterSessionNotification = (FTWTSRegisterSessionNotification)GetProcAddress(wtsapi32Mod, "WTSRegisterSessionNotification")) == NULL) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("The %s function is not available in this %s version.  Some functions will be disabled."),
                TEXT("WTSRegisterSessionNotification"), TEXT("WTSAPI32.DLL"));
        }
        if ((OptionalWTSUnRegisterSessionNotification = (FTWTSUnRegisterSessionNotification)GetProcAddress(wtsapi32Mod, "WTSUnRegisterSessionNotification")) == NULL) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("The %s function is not available in this %s version.  Some functions will be disabled."),
                TEXT("WTSUnRegisterSessionNotification"), TEXT("WTSAPI32.DLL"));
        }
    }
}

/**
 * Builds an array in memory of the system path.
 */
int buildSystemPath() {
    TCHAR *envBuffer;
    size_t len, i;
    TCHAR *c, *lc;

    /* Get the length of the PATH environment variable. */
    len = GetEnvironmentVariable(TEXT("PATH"), NULL, 0);
    if (len == 0) {
        /* PATH not set on this system.  Not an error. */
        systemPath[0] = NULL;
        return 0;
    }

    /* Allocate the memory to hold the PATH */
    envBuffer = malloc(sizeof(TCHAR) * len);
    if (!envBuffer) {
        outOfMemory(TEXT("BSP"), 1);
        return 1;
    }
    GetEnvironmentVariable(TEXT("PATH"), envBuffer, (DWORD)len);

#ifdef _DEBUG
    _tprintf(TEXT("Getting the system path: %s\n"), envBuffer);
#endif

    /* Build an array of the path elements.  To make it easy, just
     *  assume there won't be more than 255 path elements. Verified
     *  in the loop. */
    i = 0;
    lc = envBuffer;
    /* Get the elements ending in a ';' */
    while (((c = _tcschr(lc, TEXT(';'))) != NULL) && (i < SYSTEM_PATH_MAX_LEN - 2)) {
        len = (int)(c - lc);
        systemPath[i] = malloc(sizeof(TCHAR) * (len + 1));
        if (!systemPath[i]) {
            outOfMemory(TEXT("BSP"), 2);
            return 1;
        }

        memcpy(systemPath[i], lc, sizeof(TCHAR) * len);
        systemPath[i][len] = TEXT('\0');
#ifdef _DEBUG
        _tprintf(TEXT("PATH[%d]=%s\n"), i, systemPath[i]);
#endif
        lc = c + 1;
        i++;
    }
    /* There should be one more value after the last ';' */
    len = _tcslen(lc);
    systemPath[i] = malloc(sizeof(TCHAR) * (len + 1));
    if (!systemPath[i]) {
        outOfMemory(TEXT("BSP"), 3);
        return 1;
    }
    _tcsncpy(systemPath[i], lc, len + 1);
#ifdef _DEBUG
    _tprintf(TEXT("PATH[%d]=%s\n"), i, systemPath[i]);
#endif
    i++;
    /* NULL terminate the array. */
    systemPath[i] = NULL;
#ifdef _DEBUG
    _tprintf(TEXT("PATH[%d]=<null>\n"), i);
#endif
    i++;

    /* Release the environment variable memory. */
    free(envBuffer);

    return 0;
}

void disposeSystemPath() {
    int i = 0;
    
    /* Loop over and free each of the strings in the array */
    while(systemPath[i] != NULL) {
        free(systemPath[i]);
        systemPath[i] = NULL;
        i++;
    }
}

TCHAR** wrapperGetSystemPath() {
    return systemPath;
}

/**
 * Initializes the invocation mutex.  Returns 1 if the mutex already exists
 *  or can not be created.  0 if this is the first instance.
 */
HANDLE invocationMutexHandle = NULL;
int initInvocationMutex() {
    TCHAR *mutexName;
    if (wrapperData->isSingleInvocation) {
        mutexName = malloc(sizeof(TCHAR) * (30 + _tcslen(wrapperData->serviceName) + 1));
        if (!mutexName) {
            outOfMemory(TEXT("IIM"), 1);
            wrapperData->exitCode = wrapperData->errorExitCode;
            return 1;
        }
        _sntprintf(mutexName, 30 + _tcslen(wrapperData->serviceName) + 1, TEXT("Global\\Java Service Wrapper - %s"), wrapperData->serviceName);

        if (!(invocationMutexHandle = CreateMutex(NULL, FALSE, mutexName))) {
            free(mutexName);

            if (GetLastError() == ERROR_ACCESS_DENIED) {
                /* Most likely the app is running as a service and we tried to run it as a console. */
                if (wrapperServiceStatus(wrapperData->serviceName, wrapperData->serviceDisplayName, FALSE) & 0x2) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                        TEXT("ERROR: Another instance of the %s application is already running as a service."),
                        wrapperData->serviceName);
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                        TEXT("ERROR: Another instance of the %s application is already running on a different user account."),
                        wrapperData->serviceName);
                }
                wrapperData->exitCode = wrapperData->errorExitCode;
                return 1;
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                    TEXT("ERROR: Unable to create the single invocation mutex. %s"),
                    getLastErrorText());
                wrapperData->exitCode = wrapperData->errorExitCode;
                return 1;
            }
        } else {
            free(mutexName);
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("ERROR: Another instance of the %s application is already running."),
                wrapperData->serviceName);
            wrapperData->exitCode = wrapperData->errorExitCode;
            return 1;
        }
    }

    wrapperData->exitCode = 0;
    return 0;
}

HANDLE mainExitMutexHandle = NULL;

/** 
 * Obtains a lock on the Main Exit mutex 
 *  Used to make sure that WM_ENDSESSION doesn't return before the Wrapper is disposed.
 */
int lockMainExitMutex() {
    switch (WaitForSingleObject(mainExitMutexHandle, INFINITE)) {
    case WAIT_ABANDONED:
        _tprintf(TEXT("Main Exit mutex was abandoned.\n"));
        return -1;
    case WAIT_FAILED:
        _tprintf(TEXT("Main Exit mutex wait failed.\n"));
        return -1;
    case WAIT_TIMEOUT:
        _tprintf(TEXT("Main Exit mutex wait timed out.\n"));
        return -1;
    default:
        /* Ok */
        break;
    }

    return 0;
}

/** Releases a lock on the Main Exit mutex. */
int releaseMainExitMutex() {
    if (!ReleaseMutex(mainExitMutexHandle)) {
        _tprintf( TEXT("Failed to release Main Exit mutex. %s\n"), getLastErrorText());
        return -1;
    }
    return 0;
}

void disposeMessageThread() {
    stopMessageThread = TRUE;

    /* Wait until the message thread is actually stopped to avoid timing problems. */
    if (messageThreadStarted) {
        while (!messageThreadStopped) {
#ifdef _DEBUG
            wprintf(TEXT("Waiting for %s thread to stop.\n"), TEXT("Message"));
#endif
            Sleep(10);
        }
    }
}

/**
 * exits the application after running shutdown code.
 */
void appExit(int exitCode) {
    /* We only want to delete the pid files if we created them. Some Wrapper
     *  invocations are meant to run in parallel with Wrapper instances
     *  controlling a JVM. */
    if (cleanUpPIDFilesOnExit) {
        /* Remove pid file.  It may no longer exist. */
        if (wrapperData->pidFilename) {
            _tunlink(wrapperData->pidFilename);
        }

        /* Remove lock file.  It may no longer exist. */
        if (wrapperData->lockFilename) {
            _tunlink(wrapperData->lockFilename);
        }

        /* Remove status file.  It may no longer exist. */
        if (wrapperData->statusFilename) {
            _tunlink(wrapperData->statusFilename);
        }

        /* Remove java status file if it was registered and created by this process. */
        if (wrapperData->javaStatusFilename) {
            _tunlink(wrapperData->javaStatusFilename);
        }

        /* Remove java id file if it was registered and created by this process. */
        if (wrapperData->javaIdFilename) {
            _tunlink(wrapperData->javaIdFilename);
        }

        /* Remove anchor file.  It may no longer exist. */
        if (wrapperData->anchorFilename) {
            _tunlink(wrapperData->anchorFilename);
        }
    }

    /* Close the invocation mutex if we created or looked it up. */
    if (invocationMutexHandle) {
        CloseHandle(invocationMutexHandle);
        invocationMutexHandle = NULL;
    }

#if defined(WRAPPER_DEBUG_MESSAGES)
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Disposing..."));
#endif
    /* Common wrapper cleanup code. */
    wrapperDispose();

    /* Do this here to unregister the syslog resources on exit.*/
    /*unregisterSyslogMessageFile(); */

    if (mainExitMutexHandle) {
#ifdef WRAPPER_DEBUG_MESSAGES
        _tprintf(TEXT("Sleep for 3 seconds to make sure that WM_ENDSESSION is waiting.")); fflush(NULL);
        wrapperSleep(3000);
        _tprintf(TEXT("Release the Main Exit mutex.")); fflush(NULL);
#endif
        /* Release the mutex of the main thread to allow the message thread to terminate. */
        releaseMainExitMutex();
        
        /* At this point there is not guaranty that the system is still running. Once WM_ENDSESSION has returned, the session can exit anytime. */
#ifdef WRAPPER_DEBUG_MESSAGES
        _tprintf(TEXT("Close the Main Exit mutex and dispose the message thread.")); fflush(NULL);
#endif
        /* We need to dispose the message Thread after releasing the lock on mainExitMutexHandle, else the thread
         *  will be blocked while handling WM_ENDSESSION and disposeMessageThread() will wait indefinitely. */
        disposeMessageThread();
        
        /* Flush everything in case some messages were printed with _tprintf() */
        fflush(NULL);
    }

    exit(exitCode);
}

/**
 * This function should be called when the Wrapper needs to exit after performing actions that did not involve the JVM.
 *  The exit code should always be the exit code of the Wrapper!
 */
void wrapperExit(int exitCode) {
    if (exitCode == 0) {
        appExit(0);
    } else {
        appExit(wrapperData->errorExitCode);
    }
}

#ifndef WRAPPERW
int canRunInteractive() {
    static int firstCall = TRUE;
    static int result = FALSE;
    HKEY hKey;
    DWORD data;
    DWORD cbData = sizeof(DWORD);
    DWORD error;
    
    /* It is ok to store the result in a static variable, because a service needs to be restarted for NoInteractiveServices to take effect. */
    if (firstCall) {
        firstCall = FALSE;
        if (isElevated()) {
            if (!isVista()) {
                /* Windows XP and lower support interactive services. */
                result = TRUE;
            } else {
                /* Starting from Windows Vista, interactive services are not allowed except if the registry was edited. */
                if ((error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SYSTEM\\CurrentControlSet\\Control\\Windows"), 0, KEY_READ, (PHKEY) &hKey)) == ERROR_SUCCESS) {
                    if ((error = RegQueryValueEx(hKey, TEXT("NoInteractiveServices"), NULL, NULL, (LPBYTE) &data, &cbData)) == ERROR_SUCCESS) {
                        if (data == 0) {
                            result = TRUE;
                        }
                    } else {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Failed to read the value of 'NoInteractiveServices' in the registry (0x%x)."), error);
                    }
                    RegCloseKey(hKey);
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Failed to open the registry key to check if services can be run interactively (0x%x)."), error);
                }
            }
        }
    }
    return result;
}
#endif

/**
 * Returns TRUE if the Wrapper process is associated with a console or if one
 *  will be allocated a later stage. This function should be called after the
 *  configuration file has been read.
 */
int wrapperProcessHasVisibleConsole() {
#ifndef WRAPPERW
    if (wrapperData->isConsole) {
        return TRUE;
    }
#endif
    
    /* Not a console application. */
    if (wrapperData->configured) {
        /* wrapperData->ntShowWrapperConsole=TRUE => wrapperData->ntAllocConsole=TRUE
         *  (list up all cases with wrapperData->generateConsole, wrapperData->ntAllocConsole, wrapperData->ntShowWrapperConsole to verify it) */
        return (wrapperData->ntShowWrapperConsole
#ifndef WRAPPERW
             && wrapperData->ntServiceInteractive && canRunInteractive()
#endif
            );
    } else {
        /* This will work if the configuration is not loaded yet, but the conf file needs to be read at least. */
        return (getBooleanProperty(properties, TEXT("wrapper.ntservice.console"), FALSE)
#ifndef WRAPPERW
             && getBooleanProperty(properties, TEXT("wrapper.ntservice.interactive"), FALSE) && canRunInteractive()
#endif
            );
    }
}


/**
 * Writes the specified Id or PID to disk.
 *
 * filename: File to write to.
 * pid: pid to write in the file.
 * strict: If true then an error will be reported and the call will fail if the
 *         file already exists.
 *
 * return 1 if there was an error, 0 if Ok.
 */
int writePidFile(const TCHAR *filename, DWORD pid, int newUmask) {
    FILE *pid_fp = NULL;
    int old_umask;

    old_umask = _umask(newUmask);
    pid_fp = _tfopen(filename, TEXT("w"));
    _umask(old_umask);

    if (pid_fp != NULL) {
        _ftprintf(pid_fp, TEXT("%d\n"), pid);
        fclose(pid_fp);
    } else {
        return 1;
    }
    return 0;
}

/**
 * Initialize the pipe which will be used to capture the output from the child
 * process.
 */
int wrapperInitChildPipe() {
    SECURITY_ATTRIBUTES saAttr;
    HANDLE childStdoutRd = INVALID_HANDLE_VALUE;

    /* Set the bInheritHandle flag so pipe handles are inherited. */
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.lpSecurityDescriptor = NULL;
    saAttr.bInheritHandle = TRUE;

    /* Create a pipe for the child process's STDOUT. */
    if (!CreatePipe(&childStdoutRd, &wrapperChildStdoutWr, &saAttr, wrapperData->javaIOBufferSize)) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Stdout pipe creation failed  Err(%ld : %s)"),
            GetLastError(), getLastErrorText());
        return -1;
    }

    /* Create a noninheritable read handle and close the inheritable read handle. */
    if (!DuplicateHandle(GetCurrentProcess(), childStdoutRd, GetCurrentProcess(), &wrapperChildStdoutRd, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("DuplicateHandle failed"));
        return -1;
    }
    CloseHandle(childStdoutRd);

    return 0;
}

/**
 * Handler to take care of the case where the user hits CTRL-C when the wrapper
 * is being run as a console.
 *
 * Handlers are called in the reverse order that they are registered until one
 *  returns TRUE.  So last registered is called first until the default handler
 *  is called.
 */
int wrapperConsoleHandler(int key) {
    /* Immediately register this thread with the logger. */
    logRegisterThread(WRAPPER_THREAD_SIGNAL);

    /* Enclose the contents of this call in a try catch block so we can
     *  display and log useful information should the need arise. */
    __try {
        switch (key) {
        case CTRL_C_EVENT:
            /* The user hit CTRL-C.  Can only happen when run as a console. */
#ifdef WRAPPER_DEBUG_CONTROL_HANDLER
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Console Handler: trapped signal 'CTRL_C_EVENT'"));
#endif
            if (wrapperData->ignoreSignals & WRAPPER_IGNORE_SIGNALS_WRAPPER) {
                log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                    TEXT("CTRL-C trapped, but ignored."));
            } else {
                wrapperData->ctrlEventCTRLCTrapped = TRUE;
            }
            break;

        case CTRL_CLOSE_EVENT:
            /* The user tried to close the console.  Can only happen when run as a console. */
#ifdef WRAPPER_DEBUG_CONTROL_HANDLER
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Console Handler: trapped signal 'CTRL_CLOSE_EVENT'"));
#endif
            if (wrapperData->ignoreSignals & WRAPPER_IGNORE_SIGNALS_WRAPPER) {
                log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                    TEXT("Close trapped, but ignored."));
            } else {
                wrapperData->ctrlEventCloseTrapped = TRUE;
            }
            break;

        case CTRL_BREAK_EVENT:
            /* The user hit CTRL-BREAK */
#ifdef WRAPPER_DEBUG_CONTROL_HANDLER
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Console Handler: trapped signal 'CTRL_BREAK_EVENT'"));
#endif
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                TEXT("CTRL-BREAK/PAUSE trapped.  Asking the JVM to dump its state."));

            /* If the java process was launched using the same console, ie where
             *  processflags=CREATE_NEW_PROCESS_GROUP; then the java process will
             *  also get this message, so it can be ignored here. */
            /*
            If we ever do something here, remember that this can't be called directly from here.
            wrapperRequestDumpJVMState();
            */
            break;

        case CTRL_LOGOFF_EVENT:
#ifdef WRAPPER_DEBUG_CONTROL_HANDLER
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Console Handler: trapped signal 'CTRL_LOGOFF_EVENT' Tick %d"), wrapperGetTicks());
#endif
            wrapperData->ctrlEventLogoffTrapped = TRUE;
            break;

        case CTRL_SHUTDOWN_EVENT:
#ifdef WRAPPER_DEBUG_CONTROL_HANDLER
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Console Handler: trapped signal 'CTRL_SHUTDOWN_EVENT' Tick %d"), wrapperGetTicks());
#endif
            wrapperData->ctrlEventShutdownTrapped = TRUE;
            break;

        default:
            /* Unknown.  Don't quit here. */
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("Trapped unexpected console signal (%d).  Ignored."), key);
        }
    } __except (exceptionFilterFunction(GetExceptionInformation())) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
            TEXT("<-- Wrapper Stopping due to error in console handler."));
        appExit(wrapperData->errorExitCode);
    }

    return TRUE; /* We handled the event. */
}

/******************************************************************************
 * Platform specific methods
 *****************************************************************************/

/**
 * Send a signal to the JVM process asking it to dump its JVM state.
 */
void wrapperRequestDumpJVMState() {
    if (wrapperData->javaProcess != NULL) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
            TEXT("Dumping JVM state."));
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
            TEXT("Sending BREAK event to process group %ld."), wrapperData->javaPID);
        if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, wrapperData->javaPID) == 0) {
            if (wrapperData->generateConsole || wrapperData->ntAllocConsole) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                    TEXT("Unable to send BREAK event to JVM process to generate a thread dump.  Err(%ld : %s)"),
                    GetLastError(), getLastErrorText());
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                    TEXT("Unable to send BREAK event to JVM process to generate a thread dump because a console does not exist.\n  Please see the wrapper.ntservice.generate_console property."));
            }
        }
    }
}

/**
 * Build the command line used to get the Java version.
 *
 * @return TRUE if there were any problems.
 */
int wrapperBuildJavaVersionCommand() {
    size_t commandLen;
    TCHAR **strings;
    
    /* If this is not the first time through, then dispose the old command */
    if (wrapperData->jvmVersionCommand) {
#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Clearing up old java version command line"));
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Old Command Line \"%s\""), wrapperData->jvmCommand);
#endif
        free(wrapperData->jvmVersionCommand);
        wrapperData->jvmVersionCommand = NULL;
    }

    strings = malloc(sizeof(TCHAR*));
    if (!strings) {
        outOfMemory(TEXT("WBJVC"), 1);
        return TRUE;
    }
    memset(strings, 0, sizeof(TCHAR *));

    if (wrapperBuildJavaCommandArrayJavaCommand(strings, TRUE, FALSE, 0) < 0) {
        wrapperFreeStringArray(strings, 1);
        return TRUE;
    }
    
    /* Build a single string from the array that will be used to request the Java version.
     *  The first element of the command array will always be the java binary. */
    /* Calculate the length */
    commandLen = _tcslen(strings[0]);
    commandLen += 1; /* Space */
    commandLen += _tcslen(TEXT("-version"));
    commandLen++; /* '\0' */
    /* Build the actual command */
    wrapperData->jvmVersionCommand = malloc(sizeof(TCHAR) * commandLen);
    if (!wrapperData->jvmVersionCommand) {
        outOfMemory(TEXT("WBJVC"), 2);
        wrapperFreeStringArray(strings, 1);
        return TRUE;
    }
    _sntprintf(wrapperData->jvmVersionCommand, commandLen, TEXT("%s -version"), strings[0]);

    wrapperFreeStringArray(strings, 1);

    return FALSE;
}

/**
 * Build the java command line.
 *
 * @return TRUE if there were any problems.
 */
int wrapperBuildJavaCommand() {
    size_t commandLen;
    size_t commandLen2;
    TCHAR **strings;
    int length;
    int i;

    /* If this is not the first time through, then dispose the old command */
    if (wrapperData->jvmCommand) {
#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Clearing up old command line"));
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Old Command Line \"%s\""), wrapperData->jvmCommand);
#endif
        free(wrapperData->jvmCommand);
        wrapperData->jvmCommand = NULL;
    }

    /* First generate the classpath. */
    if (wrapperData->classpath) {
        free(wrapperData->classpath);
        wrapperData->classpath = NULL;
    }
    if (wrapperBuildJavaClasspath(&wrapperData->classpath) < 0) {
        return TRUE;
    }

    /* Build the Java Command Strings */
    strings = NULL;
    length = 0;
    if (wrapperBuildJavaCommandArray(&strings, &length, TRUE, wrapperData->classpath)) {
        /* Failed. */
        wrapperFreeStringArray(strings, length);
        return TRUE;
    }

#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("JVM Command Line Parameters"));
    for (i = 0; i < length; i++) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("%d : %s"), i, strings[i]);
    }
#endif

    /* Build a single string from the array */
    /* Calculate the length */
    commandLen = 0;
    for (i = 0; i < length; i++) {
        if (i > 0) {
            commandLen++; /* Space */
        }
        commandLen += _tcslen(strings[i]);
    }
    commandLen++; /* '\0' */
    commandLen2 = commandLen;
    /* Build the actual command */
    wrapperData->jvmCommand = malloc(sizeof(TCHAR) * commandLen2);
    if (!wrapperData->jvmCommand) {
        outOfMemory(TEXT("WBJC"), 1);
        wrapperFreeStringArray(strings, length);
        return TRUE;
    }
    commandLen = 0;
    for (i = 0; i < length; i++) {
        if (i > 0) {
            wrapperData->jvmCommand[commandLen++] = TEXT(' ');
        }
        _sntprintf(wrapperData->jvmCommand + commandLen, commandLen2 - commandLen, TEXT("%s"), strings[i]);
        commandLen += _tcslen(strings[i]);
    }
    wrapperData->jvmCommand[commandLen++] = TEXT('\0');
    
    wrapperData->jvmCommand = wrapperPostProcessCommandElement(wrapperData->jvmCommand);
    
    /* Free up the temporary command array */
    wrapperFreeStringArray(strings, length);

    return FALSE;
}

/**
 * Allocates an hidden console. (fix for the console flicker bug).
 *  The size of the console will be minimized and its position will be set outside of the screen.
 *  This function should be used when running as a Windows interactive service or with wrapperw.
 *
 * @return TRUE if the console was allocated, FALSE if it could not be allocated.
 */
int wrapperAllocHiddenConsole() {
    static RECT defaultWorkarea = { 0, 0, 7680, 4320};
    int result;
    int propertiesInShortcut = FALSE;
    DWORD errorAlloc = ERROR_SUCCESS;
    RECT workarea;
    DWORD size, posi;
    TCHAR hexPosi[9];
    LONG nError;
    HKEY hRootKey = HKEY_CURRENT_USER;
    HKEY hKey;
    const TCHAR* consoleSubKeyBase = TEXT("Console\\");
    const TCHAR* valueSize = TEXT("WindowSize");
    const TCHAR* valuePosi = TEXT("WindowPosition");
    TCHAR* consoleSubKey;
    size_t nSubKeyLen = 0;
    int i = 0;
    const int nMaxRestoreAttempts = 10;
    int nAttempts = 1;
    STARTUPINFO startupInfo;
    TCHAR *startupTitle;

    /* First get the coordinates of the right-bottom corner of the screen. We will then set the console origin to this position.
     *  There are cases where the console reappears on the screen if its position is set entierely outside the work area, 
     *  so it is best to have at least one corner of the console touching the boundaries of the screen.
     *  We choose the right-bottom corner because there is no need to subtract the console dimensions (which are actually unknown). */
    if(!SystemParametersInfo(SPI_GETWORKAREA, 0, &workarea, 0)) {
        /* If the function fails, lets assume a very big workarea (8K) to make sure the console position will be set out of the screen. */
        workarea = defaultWorkarea;
    }
    
    /* Edit the console properties before allocation. Those properties can either be stored in the registry or in the shortcut that launched the application. */
    GetStartupInfo(&startupInfo);
    startupTitle = startupInfo.lpTitle;
    
    if (!propertiesInShortcut) {
        /* In the registry we can store console properties by adding keys with names matching the console title. */
        
        /* First build the key name */
        nSubKeyLen = _tcslen(consoleSubKeyBase) + _tcslen(startupTitle) + 1;
    
        consoleSubKey = (TCHAR*)malloc(sizeof(TCHAR) * nSubKeyLen);
        if (consoleSubKey == NULL) {
            outOfMemory(TEXT("WHCBA"), 1);
            return FALSE;
        }
        
        _tcsncpy(consoleSubKey, consoleSubKeyBase, nSubKeyLen);
        _tcsncat(consoleSubKey, startupTitle, nSubKeyLen - _tcslen(consoleSubKey));

        /* The characters '\\' are reserved for sub key delimiters and thus should replaced by '_' in the key name (starting after consoleSubKeyBase). */
        for (i = (int)_tcslen(consoleSubKeyBase); i < (int)nSubKeyLen; i++) {
            if (consoleSubKey[i] == TEXT('\\')) {
                consoleSubKey[i] = TEXT('_');
            }
        }

        /* Now create the registry key with its size and position properties
         *  (as a precaution, the key is set volatile and will not persist on system restart) */
        nError = RegCreateKeyEx(hRootKey, consoleSubKey, 0, NULL, REG_OPTION_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
        
        if (nError != ERROR_SUCCESS) {
            RegCloseKey( hKey );
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Failed to create key 'HKEY_CURRENT_USER\\%s' in registry. Error: %ld"), consoleSubKey, nError);
        } else {
            size = isWin10OrHigher() ? 0x00000001 : 0x00010001; /* windows 10 allows height of 0px */
            nError = RegSetValueEx(hKey, valueSize, 0, REG_DWORD, (LPBYTE)&size, sizeof(DWORD));
            
            if (nError != ERROR_SUCCESS) {
                RegCloseKey( hKey );
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                    TEXT("Failed to set the value %s for the key 'HKEY_CURRENT_USER\\%s' in registry. Error: %ld"), valueSize, consoleSubKey, nError);
            } else {
                _sntprintf(hexPosi, 9, TEXT("%04x%04x"), workarea.bottom, workarea.right);
                /* The first four bytes represent the position of the window on the X axis. The last four bytes represent the position of the window on the Y axis.*/
                posi = (DWORD)_tcstol((const TCHAR*)hexPosi, NULL, 16);
                nError = RegSetValueEx(hKey, valuePosi, 0, REG_DWORD, (LPBYTE)&posi, sizeof(DWORD));
                RegCloseKey( hKey );
            
                if (nError != ERROR_SUCCESS) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                        TEXT("Failed to set the value %s for the key 'HKEY_CURRENT_USER\\%s' in registry. Error: %ld"), valuePosi, consoleSubKey, nError);
                }
            }
        }
        /* Rem: If the key could not be set properly, a brief flicker may be visible when the console is shown and then hidden. */
    }
    
    /* Allocates a new console for the calling process.*/
    result = AllocConsole();
    if (!result) {
        errorAlloc = GetLastError();
    }
    
    if (!propertiesInShortcut) {
        /* Restore the registry.
         *  No matter if the previous actions succeeded or not, lets clean up the keys so that the modified properties do not apply on future console allocations.
         *  This is required as the console may later be set visible. */
        while (TRUE) {
            /* RegDeleteKeyEx() is for deleting platform specific key, which is not our case.*/
            nError = RegDeleteKey(hRootKey, consoleSubKey);
            if (nError != ERROR_SUCCESS) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                    TEXT("Attempt #%d to delete key 'HKEY_CURRENT_USER\\%s' in registry failed. Error: %ld"), nAttempts, consoleSubKey, nError);
                if (++nAttempts < nMaxRestoreAttempts) {
                    wrapperSleep(200);
                    continue;
                }
            }
            break;
        }
        /* Rem: If the key could not be restored properly, an exception could (unlikely!) happen:
         *  Next time AllocConsole() is called with the exact same application parameters, 
         *  if the registry key remains and if wrapperData->ntShowWrapperConsole is set to TRUE, then the console will appear out of the screen.
         *   => As a precaution we could try again to remove the registry key at that time (not implemented yet). */

        if (nAttempts == nMaxRestoreAttempts) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Unable to delete key 'HKEY_CURRENT_USER\\%s' in registry."), consoleSubKey);
        }

        free(consoleSubKey);
    }
    
    /* Finally, lets set the errorAlloc as the last error and return whether if failed or not.*/
    if (!result) {
        SetLastError(errorAlloc);
    }
    return (errorAlloc == ERROR_SUCCESS);
}

int hideConsoleWindow(HWND consoleHandle, const TCHAR *name) {
    RECT workarea;
    RECT normalPositionRect;
    WINDOWPLACEMENT consolePlacement;

    if (IsWindowVisible(consoleHandle)) {
        memset(&consolePlacement, 0, sizeof(WINDOWPLACEMENT));
        consolePlacement.length = sizeof(WINDOWPLACEMENT);
        
        /* on Windows 10 the console will reappear at the position 'rcNormalPosition' when calling SetWindowPlacement(). To avoid another brief flicker, lets set this position out of the screen. */
        if(SystemParametersInfo(SPI_GETWORKAREA, 0, &workarea, 0)) {
            normalPositionRect.left = workarea.right;
            normalPositionRect.top = workarea.bottom;
            normalPositionRect.right = workarea.right;
            normalPositionRect.bottom = workarea.bottom;
        } else {
            normalPositionRect.left = 99999;
            normalPositionRect.top = 99999;
            normalPositionRect.right = 99999;
            normalPositionRect.bottom = 99999;
        }
        consolePlacement.rcNormalPosition = normalPositionRect;
#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("%s console window is visible, attempt to hide."), name);
#endif

        /* Hide the Window. */
        consolePlacement.showCmd = SW_HIDE;

        if (!SetWindowPlacement(consoleHandle, &consolePlacement)) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Unable to set window placement information: %s"), getLastErrorText());
        }

        if (IsWindowVisible(consoleHandle)) {
            if (wrapperData->isDebugging) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Failed to hide the %s console window."), name);
            }
            return FALSE;
        } else {
            if (wrapperData->isDebugging) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("%s console window hidden successfully."), name);
            }
            return TRUE;
        }
    } else {
        /* Already hidden */
        return TRUE;
    }
}

/* Define the number of recent Java windows to store. Keep this value reasonably low to avoid
 *  affecting performance. If the application opens more windows than this number, the code
 *  below may consider the foreground window as new and show/hide the console again. a brief
 *  flicker may be observed but the Wrapper will continue to run normally. */
#define JAVA_WINDOWS_BUFFER_SIZE 16

/**
 * This function fixes a bug where the hidden console reappears in the taskbar whenever
 *  the Java application creates a new full screen window with the focus set on it.
 *  This would only occur if there was no other icon belonging to the application in
 *  the taskbar (so potentially on any new windows if previous ones were dialogs).
 *  This function performs some check on the foreground window and re-hides the console
 *  if the conditions of the bug are met.
 */
void fixConsoleTaskBar() {
    static HWND prevHwd;
    static int  prevHwdIsJavaWin = FALSE;
    static int  prevHwdIsFixed = FALSE;
    static HWND prevJavaHwds[JAVA_WINDOWS_BUFFER_SIZE];
    static int  prevJavaHwdsIndex;
    HWND hwd;
    RECT hwdRect;
    RECT workAreaRect;
    DWORD pid;
    int i;

    hwd = GetForegroundWindow();
    if (hwd) {
        if (hwd != prevHwd) {
            /* The focus has changed. Is this a new Java window? */
            prevHwd = hwd;
            prevHwdIsFixed = FALSE;
            for (i = 0; i < JAVA_WINDOWS_BUFFER_SIZE; i++) {
                if (prevJavaHwds[i] == hwd) {
                    /* This is a Java window but it already caused the bug before and we fixed it.
                     *  As far as I could test, a window that caused the bug once will not cause
                     *  it again, even when it is hidden and showed or resized to full screen. */
                    prevHwdIsJavaWin = TRUE;
                    return;
                }
            }
            GetWindowThreadProcessId(hwd, &pid);
            if (!wrapperCheckPPid(pid, wrapperData->javaPID)) {
                /* The window belongs to the Java application. */
                prevHwdIsJavaWin = FALSE;
                return;
            }
        } else if (!prevHwdIsJavaWin || prevHwdIsFixed) {
            /* The window has not changed and we know it will not cause a bug. */
            return;
        }   /* Else: the focus did not change and the window belongs to the java application.
             *  It may cause the bug if it was resized since we last check it, so continue. */
        
        /* This is a Java window. Is it full screen? */
        prevHwdIsJavaWin = TRUE;
        if (!GetWindowRect(hwd, &hwdRect) || !SystemParametersInfo(SPI_GETWORKAREA, 0, &workAreaRect, 0) ||
            (((hwdRect.right - hwdRect.left) >= (workAreaRect.right - workAreaRect.left)) &&
             ((hwdRect.bottom - hwdRect.top) >= (workAreaRect.bottom - workAreaRect.top)))) {
            /* This window is larger than the work area or we failed to retrieve its size or the size
             *  of the work area. This may have caused the hidden console to reappear in the taskbar.
             *  Show and hide it again to fix this issue. A brief flicker may be observed as it may
             *  have appeared anytime since the last iteration of the main loop. Remember the window
             *  to not process it again. (also remember it if we fail to retrieve its size or the size
             *  of the work area as this would most likely happen again on the next check) */
            prevJavaHwds[prevJavaHwdsIndex] = hwd;
            prevJavaHwdsIndex = (prevJavaHwdsIndex + 1) % JAVA_WINDOWS_BUFFER_SIZE;
            ShowWindow(wrapperData->wrapperConsoleHWND, SW_SHOW);
            ShowWindow(wrapperData->wrapperConsoleHWND, SW_HIDE);
            prevHwdIsFixed = TRUE;
        }
    }
}

/**
 * Look for and hide the wrapper or JVM console windows if they should be hidden.
 * Some users have reported that if the user logs on to windows quickly after booting up,
 *  the console window will be redisplayed even though it was hidden once.  This function
 *  is called on each iteration of the main loop to ensure the consoles are always hidden.
 *  Users have reported that the console can be redisplayed when a user logs back in or
 *  switches users, or when the Java application opens full screen windows.
 */
void wrapperCheckConsoleWindows() {
    /* See if the Wrapper console needs to be hidden. */
    if (wrapperData->wrapperConsoleHide && (wrapperData->wrapperConsoleHWND != NULL)) {
        if (hideConsoleWindow(wrapperData->wrapperConsoleHWND, TEXT("Wrapper"))) {
            fixConsoleTaskBar();
            wrapperData->wrapperConsoleVisible = FALSE;
        }
    }

    /* See if the Java console needs to be hidden. */
    if (wrapperData->jvmConsoleHandle != NULL) {
        if (hideConsoleWindow(wrapperData->jvmConsoleHandle, TEXT("JVM"))) {
            wrapperData->jvmConsoleVisible = FALSE;
        }
    }
}

HWND findConsoleWindow( TCHAR *title ) {
    HWND consoleHandle;
    int i = 0;

    /* Allow up to 2 seconds for the window to show up, but don't hang
     *  up if it doesn't */
    consoleHandle = NULL;
    while ((!consoleHandle) && (i < 200)) {
        wrapperSleep(10);
        consoleHandle = FindWindow(TEXT("ConsoleWindowClass"), title);
        i++;
    }

    return consoleHandle;
}

void showConsoleWindow(HWND consoleHandle, const TCHAR *name) {
    WINDOWPLACEMENT consolePlacement;

    if (wrapperData->isDebugging) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Show %s console window with which JVM is launched."), name);
    }
    if (GetWindowPlacement(consoleHandle, &consolePlacement)) {
        /* Show the Window. */
        consolePlacement.showCmd = SW_SHOW;

        if (!SetWindowPlacement(consoleHandle, &consolePlacement)) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Unable to set window placement information: %s"), getLastErrorText());
        }
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
            TEXT("Unable to obtain window placement information: %s"), getLastErrorText());
    }
}

/**
 * The main entry point for the startup thread which is started by
 *  wrapperRunCommon().  Once started, this thread will run for the
 *  life of the startup and then exit.
 *
 * This thread only exists so that certain tasks which take an
 *  undetermined amount of time can run without affecting the startup
 *  time of the Wrapper.
 */
DWORD WINAPI startupRunner(LPVOID parameter) {
    /* In case there are ever any problems in this thread, enclose it in a try catch block. */
    __try {
        startupThreadStarted = TRUE;

        /* Immediately register this thread with the logger. */
        logRegisterThread(WRAPPER_THREAD_STARTUP);

        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("%s thread started."), TEXT("Startup"));
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Attempting to verify the binary signature."));

        verifyEmbeddedSignature();
    } __except (exceptionFilterFunction(GetExceptionInformation())) {
        /* This call is not queued to make sure it makes it to the log prior to a shutdown. */
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Fatal error in the %s thread."), TEXT("Startup"));
        startupThreadStopped = TRUE; /* Before appExit() */
        appExit(wrapperData->errorExitCode);
        return 1; /* For the compiler, we will never get here. */
    }

    startupThreadStopped = TRUE;
    if (wrapperData->isDebugging) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("%s thread stopped."), TEXT("Startup"));
    }
    return 0;
}

/**
 * Creates a thread whose job is to process some startup actions which could take a while to
 *  complete.  This function will automatically wait for a configured length of time for the
 *  thread to complete.  If it does not complete within the predetermined amount of time then
 *  it will continue to avoid slowing down the Wrapper startup.
 *
 * This startup timeout can be controlled with the wrapper.startup_thread.timeout property.
 */
int initializeStartup() {
    int startupThreadTimeout;
    TICKS nowTicks;
    TICKS timeoutTicks;
    
    if (wrapperData->isDebugging) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Launching %s thread."), TEXT("Startup"));
    }

    startupThreadHandle = CreateThread(
        NULL, /* No security attributes as there will not be any child processes of the thread. */
        0,    /* Use the default stack size. */
        startupRunner,
        NULL, /* No parameters need to passed to the thread. */
        0,    /* Start the thread running immediately. */
        &startupThreadId);
    if (!startupThreadHandle) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
            TEXT("Unable to create a %s thread: %s"), TEXT("Startup"), getLastErrorText());
        return 1;
    }
    
    /* Wait until the startup thread completes or the timeout expires. */
    startupThreadTimeout = propIntMin(propIntMax(getIntProperty(properties, TEXT("wrapper.startup_thread.timeout"), 2), 0), 3600);
    nowTicks = wrapperGetTicks();
    timeoutTicks = wrapperAddToTicks(nowTicks, startupThreadTimeout);
    while ((!startupThreadStopped) && (wrapperGetTickAgeSeconds(timeoutTicks, nowTicks) < 0)) {
#if DEBUG_STARTUP
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  Waiting for startup... %08x < %08x"), nowTicks, timeoutTicks);
#endif
        wrapperSleep(10);
        nowTicks = wrapperGetTicks();
    }
    if (startupThreadStopped) {
#if DEBUG_STARTUP
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("%s completed."), TEXT("Startup"));
#endif
        if ((wrapperData->wState == WRAPPER_WSTATE_STOPPING) ||
            (wrapperData->wState == WRAPPER_WSTATE_STOPPED)) {
            appExit(wrapperData->errorExitCode);
        }
    } else {
        if (wrapperData->isDebugging) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("%s timed out.  Continuing in background."), TEXT("Startup"));
        }
    }
    
    return 0;
}

void disposeStartup() {
    /* Wait until the javaIO thread is actually stopped to avoid timing problems. */
    if (startupThreadStarted && !startupThreadStopped) {
        if (wrapperData->isDebugging) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Waiting for %s thread to complete..."), TEXT("Startup"));
        }
        while (!startupThreadStopped) {
#ifdef _DEBUG
            wprintf(TEXT("Waiting for %s thread to stop.\n"), TEXT("Startup"));
#endif
            wrapperSleep(100);
        }
    }
}

int wrapperHandleMessageQueue() {
    MSG msg;
    int cnt = 0;
    
    if (_wrapper_messageWindowHWND) {
        while (cnt < 10) {
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } else {
                return cnt;
            }
            cnt++;
        }
    }
    return cnt;
}

#ifndef ENDSESSION_CLOSEAPP
 #define ENDSESSION_CLOSEAPP    0x00000001
#endif
#ifndef ENDSESSION_CRITICAL
 #define ENDSESSION_CRITICAL    0x40000000
#endif
#ifndef ENDSESSION_LOGOFF
 #define ENDSESSION_LOGOFF      0x80000000
#endif

LRESULT CALLBACK messageWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    TCHAR *username;
    DWORD usernameSize;
    
    _wrapper_debugMessage(TEXT("messageWndProc"), hwnd, message, wParam, lParam);

    switch(message) {
    case WM_QUERYENDSESSION:
        /* A request to end the session is being made.  We can try to block that by returning 0. */
        if (wrapperData->isMessageOutputEnabled) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session End Query%s%s%s"),
                    (wParam & ENDSESSION_CLOSEAPP ? TEXT(" CloseApp") : TEXT("")),
                    (wParam & ENDSESSION_CRITICAL ? TEXT(" Critical") : TEXT("")),
                    (wParam & ENDSESSION_LOGOFF ? TEXT(" Logoff") : TEXT("")));
        }
        
        /* Always return TRUE to indicate that the application is OK to exit the session.
         *  Do not clean up now because another application may abort the exit.
         *  If the session exit should indeed happen, then the system will send a WM_ENDSESSION
         *  that we will trap to handle a clean shutdown. */
        return TRUE;
        
    case WM_ENDSESSION:
        /* This is a notification that the session is actually ending.  The session will end at any time after this returns.
         *  We need to block in here to process our own shutdown to make sure the session stays open long enough for us to shutdown cleanly. */
        if (wrapperData->isMessageOutputEnabled) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session End%s%s%s"),
                    (wParam & ENDSESSION_CLOSEAPP ? TEXT(" CloseApp") : TEXT("")),
                    (wParam & ENDSESSION_CRITICAL ? TEXT(" Critical") : TEXT("")),
                    (wParam & ENDSESSION_LOGOFF ? TEXT(" Logoff") : TEXT("")));
        }
        if (wParam) {
            /* We already checked that OptionalShutdownBlockReasonCreate is not NULL when starting the message thread. */
            OptionalShutdownBlockReasonCreate(hwnd, getStringProperty(properties, TEXT("wrapper.user_logoffs.message"), TEXT("Waiting for the Wrapper to shutdown.")));
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("User logged out.  Shutting down."));
            
            /* Send messages to notify the WrapperManager in case the Java Application has registered CTRL events. */
            wrapperProtocolFunction(WRAPPER_MSG_FIRE_CTRL_EVENT, TEXT("WRAPPER_CTRL_LOGOFF_EVENT"));
            
            if (!(lParam & ENDSESSION_LOGOFF)) {
                /* The system is shutting down or rebooting. */
                wrapperProtocolFunction(WRAPPER_MSG_FIRE_CTRL_EVENT, TEXT("WRAPPER_CTRL_SHUTDOWN_EVENT"));
            }

            wrapperStopProcess(0, TRUE);
            
            /* Wait until the mutex is released, meaning the application has disposed and is about to exit. */
#ifdef WRAPPER_DEBUG_MESSAGES
            _tprintf(TEXT("Waiting for the Main Exit mutex to be released (WM_ENDSESSION, TheadId = #%d)."), GetCurrentThreadId()); fflush(NULL);
#endif
            lockMainExitMutex();
            
            /* We are ready to exit. Release the mutex and dispose it. We don't need it anymore */
            releaseMainExitMutex();
            CloseHandle(mainExitMutexHandle);
            mainExitMutexHandle = NULL;
            
            /* Not sure if the following is needed but the API says we should ShutdownBlockReasonDestroy() after ShutdownBlockReasonCreate(). */
            if (OptionalShutdownBlockReasonDestroy) {
                OptionalShutdownBlockReasonDestroy(hwnd);
            }

#ifdef WRAPPER_DEBUG_MESSAGES
            wrapperSleep(25);
            _tprintf(TEXT("Finished WM_ENDSESSION.")); fflush(NULL);
#endif
            /* Flush everything in case some messages were printed with _tprintf() */
            fflush(NULL);
        }
        
        /* Always return 0. */
        return FALSE;
        
    case WM_WTSSESSION_CHANGE: /* >= Windows Vista, Windows Server 2008 */
        if (OptionalWTSQuerySessionInformation && OptionalWTSFreeMemory) {
            if (OptionalWTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, (DWORD)lParam, WTSUserName, &username, &usernameSize)) {
                if (username) {
                    if (_tcslen(username) <= 0) {
                        /* Empty string, no name. */
                        OptionalWTSFreeMemory(username);
                        username = NULL;
                    }
                }
            }
        }
        
        switch(wParam) {
        case WTS_CONSOLE_CONNECT:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Console Connect: %s"), (username ? username : TEXT("-")));
            }
            break;
        case WTS_CONSOLE_DISCONNECT:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Console Disconnect: %s"), (username ? username : TEXT("-")));
            }
            break;
        case WTS_REMOTE_CONNECT:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Remote Connect: %s"), (username ? username : TEXT("-")));
            }
            break;
        case WTS_REMOTE_DISCONNECT:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Remote Disconnect: %s"), (username ? username : TEXT("-")));
            }
            break;
        case WTS_SESSION_LOGON:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Logon: %s"), (username ? username : TEXT("-")));
            }
            break;
        case WTS_SESSION_LOGOFF:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Logoff: %s"), (username ? username : TEXT("-")));
            }
            break;
        case WTS_SESSION_LOCK:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Lock: %s"), (username ? username : TEXT("-")));
            }
            break;
        case WTS_SESSION_UNLOCK:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Unlock: %s"), (username ? username : TEXT("-")));
            }
            break;
        case WTS_SESSION_REMOTE_CONTROL:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Remote Control: %s (now %d)"), (username ? username : TEXT("-")), GetSystemMetrics(SM_REMOTECONTROL));
            }
            break;
        case WTS_SESSION_CREATE:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Create: %s"), (username ? username : TEXT("-")));
            }
            break;
        case WTS_SESSION_TERMINATE:
            if (wrapperData->isMessageOutputEnabled) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Session Terminate: %s"), (username ? username : TEXT("-")));
            }
            break;
        default:
            break;
        }
        
        if (username) {
            OptionalWTSFreeMemory(username);
        }
        
        return FALSE;
        
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

void _wrapper_createMessageWindow() {
    WNDCLASSEX wc;
    HWND hwnd;

    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = 0;
    wc.lpfnWndProc   = messageWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = wrapperData->wrapperHInstance;
    wc.hIcon         = NULL;
    wc.hCursor       = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = messageWindowClassName;
    wc.hIconSm       = NULL;

    if (!RegisterClassEx(&wc)) {
        return;
    }

    hwnd = CreateWindowEx(
        0,
        messageWindowClassName,
        TEXT(""),
        0,
        CW_USEDEFAULT, CW_USEDEFAULT, 30, 90, NULL, NULL, GetModuleHandle(NULL), NULL);

    if (hwnd != NULL) {
        _wrapper_messageWindowHWND = hwnd;
        /* We don't want to actually show the window.
        ShowWindow(hwnd, SW_SHOWNORMAL); */
        UpdateWindow(hwnd);
    }
}

/**
 * The main entry point for the Message thread which is started by
 *  initializeMessageThread().  Once started, this thread will run for the
 *  life of the process.
 */
DWORD WINAPI messageRunner(LPVOID parameter) {
    int nextSleep;

    /* In case there are ever any problems in this thread, enclose it in a try catch block. */
    __try {
        messageThreadStarted = TRUE;

        /* Immediately register this thread with the logger. */
        logRegisterThread(WRAPPER_THREAD_MESSAGE);

        if (wrapperData->isMessageOutputEnabled) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s thread started."), TEXT("Message"));
        }

        _wrapper_createMessageWindow();
        
        /* Register to receive messages that we are interested in. */
        if (_wrapper_messageWindowHWND && OptionalWTSRegisterSessionNotification) {
            if (!OptionalWTSRegisterSessionNotification(_wrapper_messageWindowHWND, NOTIFY_FOR_THIS_SESSION)) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Failed to register to receive session change notifications. (%d): %s"), GetLastError(), getLastErrorText());
            }
        }

        nextSleep = TRUE;
        /* Loop until we are shutting down, but continue as long as there are messages. */
        while ((!stopMessageThread) || (!nextSleep)) {
            if (nextSleep) {
                /* Sleep as little as possible. */
                Sleep(1);
            }
            
            nextSleep = (wrapperHandleMessageQueue() <= 0);
        }
        
        if (_wrapper_messageWindowHWND) {
            if (OptionalWTSUnRegisterSessionNotification) {
                OptionalWTSUnRegisterSessionNotification(_wrapper_messageWindowHWND);
            }
            
            /* We finished to process all messages, so destroy the window. */
            DestroyWindow(_wrapper_messageWindowHWND);
            _wrapper_messageWindowHWND = NULL;
        }
    } __except (exceptionFilterFunction(GetExceptionInformation())) {
        /* This call is not queued to make sure it makes it to the log prior to a shutdown. */
        if (isLogInitialized()) {
            /* There is a small risk that the log gets disposed while printing this message as we are in a separated thread... */
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Fatal error in the %s thread."), TEXT("Message"));
        } else {
            _tprintf(TEXT("Fatal error in the %s thread."), TEXT("Message"));
        }
        messageThreadStopped = TRUE;
        if (wrapperData && (wrapperData->wState != WRAPPER_WSTATE_STOPPING) && (wrapperData->wState != WRAPPER_WSTATE_STOPPED)) {
            wrapperStopProcess(0, TRUE);
        }
        return 1;
    }

    messageThreadStopped = TRUE;
    if (wrapperData && wrapperData->isMessageOutputEnabled) {
        if (isLogInitialized()) {
            /* There is a small risk that the log gets disposed while printing this message as we are in a separated thread... */
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s thread stopped."), TEXT("Message"));
        } else {
            _tprintf(TEXT("%s thread stopped."), TEXT("Message"));
        }
    }
    return 0;
}

/**
 * Creates a thread whose job is to loop and process system messages,
 *  and handle a clean shutdown when a request to end the session was made.
 */
int initializeMessageThread() {
    if (wrapperData->isMessageOutputEnabled) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Launching %s thread."), TEXT("Message"));
    }

    messageThreadHandle = CreateThread(
        NULL, /* No security attributes as there will not be any child processes of the thread. */
        0,    /* Use the default stack size. */
        messageRunner,
        NULL, /* No parameters need to passed to the thread. */
        0,    /* Start the thread running immediately. */
        &messageThreadId);
    if (!messageThreadHandle) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
            TEXT("Unable to create a %s thread: %s"), TEXT("Message"), getLastErrorText());
        return 1;
    } else {
        return 0;
    }
}

/**
 * This function checks if the Wrapper can/should trap system messages in order
 *  to initiate a clean shutdown when the user logs off. Those messages are
 *  trapped from a hidden window in a separate thread, which both should only
 *  be created when necessary (when this function returns TRUE).
 *
 * @return TRUE if System Messages can be trapped, FALSE otherwise.
 */
int canTrapSystemMessages() {
    DWORD sessionId;
    /* Before Windows Vista/Server 2008, CTRL_SHUTDOWN_EVENT events could be trapped from both services and console apps
     *  to shutdown the Wrapper cleanly. Since Vista, a new API was introduce to detect logoffs via system messages. */
    if (isVista() && OptionalShutdownBlockReasonCreate) {
        /* The WM_QUERYENDSESSION and WM_ENDSESSION messages cannot be trapped when running in the session #0.
         *  Session #0 is the session in which the services are running, but Wrapper instances running as a
         *  console can only run in that session if they are spawn from a service. */
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) {
            _tprintf(TEXT("Failed to retrieve the session id of the current process. %s\n"), getLastErrorText());
            /* Assume that we are not in session #0 if we are running as a console (which is the case if not spawn from a service).
             *  It doesn't hurt even if we are wrong. System messages will simply not be trapped. */
            if (wrapperData->isConsole) {
                return TRUE;
            }
        } else if (sessionId != 0) {
            /* So we are a console application running on a session that is not #0. */
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * The main entry point for the javaio thread which is started by
 *  initializeJavaIO().  Once started, this thread will run for the
 *  life of the process.
 *
 * This thread will only be started if we are configured to use a
 *  dedicated thread to read JVM output.
 */
DWORD WINAPI javaIORunner(LPVOID parameter) {
    int nextSleep;

    /* In case there are ever any problems in this thread, enclose it in a try catch block. */
    __try {
        javaIOThreadStarted = TRUE;

        /* Immediately register this thread with the logger. */
        logRegisterThread(WRAPPER_THREAD_JAVAIO);

        if (wrapperData->isJavaIOOutputEnabled) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s thread started."), TEXT("JavaIO"));
        }

        nextSleep = TRUE;
        /* Loop until we are shutting down, but continue as long as there is more output from the JVM. */
        while ((!stopJavaIOThread) || (!nextSleep)) {
            if (nextSleep) {
                /* Sleep as little as possible. */
                wrapperSleep(1);
            }
            nextSleep = TRUE;
            
            if (wrapperData->pauseThreadJavaIO) {
                wrapperPauseThread(wrapperData->pauseThreadJavaIO, TEXT("javaio"));
                wrapperData->pauseThreadJavaIO = 0;
            }
            
            if (wrapperReadChildOutput(0)) {
                if (wrapperData->isDebugging) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                        TEXT("Pause reading child process output to share cycles."));
                }
                nextSleep = FALSE;
            }
        }
    } __except (exceptionFilterFunction(GetExceptionInformation())) {
        /* This call is not queued to make sure it makes it to the log prior to a shutdown. */
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Fatal error in the %s thread."), TEXT("JavaIO"));
        javaIOThreadStopped = TRUE; /* Before appExit() */
        appExit(wrapperData->errorExitCode);
        return 1; /* For the compiler, we will never get here. */
    }

    javaIOThreadStopped = TRUE;
    if (wrapperData->isJavaIOOutputEnabled) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s thread stopped."), TEXT("JavaIO"));
    }
    return 0;
}

/**
 * Creates a thread whose job is to loop and process the stdio and stderr output from the JVM.
 */
int initializeJavaIO() {
    if (wrapperData->isJavaIOOutputEnabled) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Launching %s thread."), TEXT("JavaIO"));
    }

    javaIOThreadHandle = CreateThread(
        NULL, /* No security attributes as there will not be any child processes of the thread. */
        0,    /* Use the default stack size. */
        javaIORunner,
        NULL, /* No parameters need to passed to the thread. */
        0,    /* Start the thread running immediately. */
        &javaIOThreadId);
    if (!javaIOThreadHandle) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
            TEXT("Unable to create a %s thread: %s"), TEXT("JavaIO"), getLastErrorText());
        return 1;
    } else {
        return 0;
    }
}

void disposeJavaIO() {
    stopJavaIOThread = TRUE;

    /* Wait until the javaIO thread is actually stopped to avoid timing problems. */
    if (javaIOThreadStarted) {
        while (!javaIOThreadStopped) {
#ifdef _DEBUG
            wprintf(TEXT("Waiting for %s thread to stop.\n"), TEXT("JavaIO"));
#endif
            wrapperSleep(100);
        }
    }
}

/**
 * The main entry point for the timer thread which is started by
 *  initializeTimer().  Once started, this thread will run for the
 *  life of the process.
 *
 * This thread will only be started if we are configured NOT to
 *  use the system time as a base for the tick counter.
 */
DWORD WINAPI timerRunner(LPVOID parameter) {
    TICKS sysTicks;
    TICKS lastTickOffset = 0;
    TICKS tickOffset;
    TICKS nowTicks;
    int offsetDiff;
    int first = TRUE;

    /* In case there are ever any problems in this thread, enclose it in a try catch block. */
    __try {
        timerThreadStarted = TRUE;

        /* Immediately register this thread with the logger. */
        logRegisterThread(WRAPPER_THREAD_TIMER);

        if (wrapperData->isTickOutputEnabled) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Timer thread started."));
        }

        wrapperGetSystemTicks();

        while (!stopTimerThread) {
            wrapperSleep(WRAPPER_TICK_MS);
            
            if (wrapperData->pauseThreadTimer) {
                wrapperPauseThread(wrapperData->pauseThreadTimer, TEXT("timer"));
                wrapperData->pauseThreadTimer = 0;
            }

            /* Get the tick count based on the system time. */
            sysTicks = wrapperGetSystemTicks();

            /* Lock the tick mutex whenever the "timerTicks" variable is accessed. */
            if (wrapperData->useTickMutex && wrapperLockTickMutex()) {
                timerThreadStopped = TRUE;
                return 1;
            }

            /* Advance the timer tick count. */
            nowTicks = timerTicks++;

            if (wrapperData->useTickMutex && wrapperReleaseTickMutex()) {
                timerThreadStopped = TRUE;
                return 1;
            }

            /* Calculate the offset between the two tick counts. This will always work due to overflow. */
            tickOffset = sysTicks - nowTicks;

            /* The number we really want is the difference between this tickOffset and the previous one. */
            offsetDiff = wrapperGetTickAgeTicks(lastTickOffset, tickOffset);

            if (first) {
                first = FALSE;
            } else {
                if (offsetDiff > wrapperData->timerSlowThreshold) {
                    log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT(
                        "The timer fell behind the system clock by %dms."), (int)(offsetDiff * WRAPPER_TICK_MS));
                } else if (offsetDiff < -1 * wrapperData->timerFastThreshold) {
                    log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT(
                        "The system clock fell behind the timer by %dms."), (int)(-1 * offsetDiff * WRAPPER_TICK_MS));
                }

                if (wrapperData->isTickOutputEnabled) {
                    log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT(
                        "    Timer: ticks=%08x, system ticks=%08x, offset=%08x, offsetDiff=%08x"),
                        nowTicks, sysTicks, tickOffset, offsetDiff);
                }
            }

            /* Store this tick offset for the next time through the loop. */
            lastTickOffset = tickOffset;
        }
    } __except (exceptionFilterFunction(GetExceptionInformation())) {
        /* This call is not queued to make sure it makes it to the log prior to a shutdown. */
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Fatal error in the Timer thread."));
        timerThreadStopped = TRUE; /* Before appExit() */
        appExit(wrapperData->errorExitCode);
        return 1; /* For the compiler, we will never get here. */
    }

    timerThreadStopped = TRUE;
    if (wrapperData->isTickOutputEnabled) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Timer thread stopped."));
    }
    return 0;
}

/**
 * Creates a process whose job is to loop and simply increment a ticks
 *  counter.  The tick counter can then be used as a clock as an alternative
 *  to using the system clock.
 */
int initializeTimer() {
    if (wrapperData->isTickOutputEnabled) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Launching Timer thread."));
    }

    timerThreadHandle = CreateThread(
        NULL, /* No security attributes as there will not be any child processes of the thread. */
        0,    /* Use the default stack size. */
        timerRunner,
        NULL, /* No parameters need to passed to the thread. */
        0,    /* Start the thread running immediately. */
        &timerThreadId);
    if (!timerThreadHandle) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
            TEXT("Unable to create a timer thread: %s"), getLastErrorText());
        return 1;
    } else {
        return 0;
    }
}

void disposeTimer() {
    stopTimerThread = TRUE;

    /* Wait until the timer thread is actually stopped to avoid timing problems. */
    if (timerThreadStarted) {
        while (!timerThreadStopped) {
#ifdef _DEBUG
            wprintf(TEXT("Waiting for timer thread to stop.\n"));
#endif
            wrapperSleep(100);
        }
    }
    CloseHandle(timerThreadHandle);
}

int initializeWinSock() {
    WORD ws_version=MAKEWORD(1, 1);
    WSADATA ws_data;
    int res;

    /* Initialize Winsock */
    if ((res = WSAStartup(ws_version, &ws_data)) != 0) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Cannot initialize Windows socket DLLs."));
        return res;
    }

    return 0;
}

/**
 * Collects the current process's username and domain name.
 *
 * @return TRUE if there were any problems.
 */
int collectUserInfo() {
    int result;

    DWORD processId;
    HANDLE hProcess;
    HANDLE hProcessToken;
    TOKEN_USER *tokenUser;
    DWORD tokenUserSize;

    TCHAR *sidText;
    DWORD userNameSize;
    DWORD domainNameSize;
    SID_NAME_USE sidType;

    processId = wrapperData->wrapperPID;
    wrapperData->userName = NULL;
    wrapperData->domainName = NULL;

    if (hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processId)) {
        if (OpenProcessToken(hProcess, TOKEN_QUERY, &hProcessToken)) {
            GetTokenInformation(hProcessToken, TokenUser, NULL, 0, &tokenUserSize);
            tokenUser = (TOKEN_USER *)malloc(tokenUserSize);
            if (!tokenUser) {
                outOfMemory(TEXT("CUI"), 1);
                result = TRUE;
            } else {
                if (GetTokenInformation(hProcessToken, TokenUser, tokenUser, tokenUserSize, &tokenUserSize)) {
                    /* Get the text representation of the sid. */
                    if (ConvertSidToStringSid(tokenUser->User.Sid, &sidText) == 0) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Failed to convert SId to String: %s"), getLastErrorText());
                        result = TRUE;
                    } else {
                        /* We now have an SID, use it to lookup the account. */
                        userNameSize = 0;
                        domainNameSize = 0;
                        LookupAccountSid(NULL, tokenUser->User.Sid, NULL, &userNameSize, NULL, &domainNameSize, &sidType);
                        wrapperData->userName = (TCHAR*)malloc(sizeof(TCHAR) * userNameSize);
                        if (!wrapperData->userName) {
                            outOfMemory(TEXT("CUI"), 2);
                            result = TRUE;
                        } else {
                            wrapperData->domainName = (TCHAR*)malloc(sizeof(TCHAR) * domainNameSize);
                            if (!wrapperData->domainName) {
                                outOfMemory(TEXT("CUI"), 3);
                                result = TRUE;
                            } else {
                                if (LookupAccountSid(NULL, tokenUser->User.Sid, wrapperData->userName, &userNameSize, wrapperData->domainName, &domainNameSize, &sidType)) {
                                    /* Success. */
                                    result = FALSE;
                                } else {
                                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to get the current username and domain: %s"), getLastErrorText());
                                    result = TRUE;
                                }
                            }
                        }

                        LocalFree(sidText);
                    }
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to get token information: %s"), getLastErrorText());
                    result = TRUE;
                }

                free(tokenUser);
            }

            CloseHandle(hProcessToken);
        } else {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to open process token: %s"), getLastErrorText());
            result = TRUE;
        }

        CloseHandle(hProcess);
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to open process: %s"), getLastErrorText());
        result = TRUE;
    }

    return result;
}

/**
 * Execute initialization code to get the wrapper set up.
 */
int wrapperInitializeRun() {
    HANDLE hStdout;
    HANDLE hStdErr;
    HANDLE hStdIn;
    struct _timeb timebNow;
    FILE *pfile;
    time_t      now;
    int         nowMillis;
    int res;
    TCHAR titleBuffer[80];
    int allocConsoleSucceed;
    int canDisplayConsole;
    HANDLE process = GetCurrentProcess();
    TCHAR* szOS;

    /* Set the process priority. */
    if (!SetPriorityClass(process, wrapperData->ntServicePriorityClass)) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
            TEXT("Unable to set the process priority:  %s"), getLastErrorText());
    }

    /* Initialize the random seed. */
    _ftime(&timebNow);
    now = (time_t)timebNow.time;
    nowMillis = timebNow.millitm;
    srand(nowMillis);

    /* Initialize the pipe to capture the child process output */
    if ((res = wrapperInitChildPipe()) != 0) {
        return res;
    }

    /* Initialize the Wrapper console handle to null */
    wrapperData->wrapperConsoleHWND = NULL;

    /* The Wrapper will not have its own console when running as a service.  We need to create one here. */
    if ((!wrapperData->isConsole) && (wrapperData->ntAllocConsole)) {
        canDisplayConsole = wrapperData->ntServiceInteractive && canRunInteractive();
        if (wrapperData->isDebugging) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Allocating a console for the service."));
        }

        /* Set a flag to keep track of whether the console should be hidden. */
        wrapperData->wrapperConsoleHide = canDisplayConsole && !wrapperData->ntShowWrapperConsole;
        
        if (wrapperData->wrapperConsoleHide) {
            /* Create the console out of the screen. We'll still need to hide its window so that the icon disappear from the taskbar. */
            allocConsoleSucceed = wrapperAllocHiddenConsole();

            if (allocConsoleSucceed) {
                /* Generate a unique time for the console so we can look for it below. */
                _sntprintf(titleBuffer, 80, TEXT("Wrapper Console Id %d-%d (Do not close)"), wrapperData->wrapperPID, rand());
#ifdef _DEBUG
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Wrapper console title: %s"), titleBuffer);
#endif
                SetConsoleTitle(titleBuffer);

                if (wrapperData->wrapperConsoleHWND = findConsoleWindow(titleBuffer)) {
                    wrapperData->wrapperConsoleVisible = TRUE;
                    if (wrapperData->isDebugging) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Found console window."));
                    }

                    /* Attempt to hide the console window here once so it goes away as quickly as possible.
                     *  This may not succeed yet however.  If the system is still coming up. */
                    wrapperCheckConsoleWindows();
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("Failed to locate the console window so it can be hidden."));
                }
            }
        } else {
            allocConsoleSucceed = AllocConsole();
        }

        if (!allocConsoleSucceed) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("ERROR: Unable to allocate a console for the service: %s"), getLastErrorText());
            if (getLastError() == ERROR_GEN_FAILURE) {
                szOS = calloc(OSBUFSIZE, sizeof(TCHAR));
                if (szOS) {
                    GetOSDisplayString(&szOS);
                }
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                    TEXT("       It has been observed that some systems fail to allocate a console with error code 0x%04x.\n       We are still investigating why this is happening. Please report this message to support@tanukisoftware.com, as it will help us fix the issue.\n       The Wrapper PID was %d, the system was %s.\n       A workaround is to set wrapper.ntservice.generate_console to FALSE, but the Wrapper won't be able to perform thread dumps without a console."),
                    getLastError(), wrapperData->wrapperPID, szOS ? szOS : TEXT("unknown"));
                if (szOS) {
                    free(szOS);
                }
            }
            return 1;
        }

        /* A console, which got created by AllocConsole, does not have stdin/out/err set,
           so all printf's are not being displayed. Set the buffer explicitly here. */
        hStdIn = GetStdHandle(STD_INPUT_HANDLE);
        if (hStdIn == INVALID_HANDLE_VALUE) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("ERROR: Unable to get the new stdin handle: %s"), getLastErrorText());
           return 1;
        }
        pfile = _tfdopen( _open_osfhandle((long)hStdIn, _O_TEXT), TEXT("r") );
        /* Assign the STD_INPUT_HANDLE fd to stdin*/
        *stdin = *pfile;
        /* set the stream to non buffering  */
        setvbuf( stdin, NULL, _IONBF, 0 );

        hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hStdout == INVALID_HANDLE_VALUE) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("ERROR: Unable to get the new stdout handle: %s"), getLastErrorText());
           return 1;
        }
        pfile = _tfdopen( _open_osfhandle((long)hStdout, _O_TEXT), TEXT("w") );
        *stdout = *pfile;
        setvbuf( stdout, NULL, _IONBF, 0 );

        hStdErr = GetStdHandle(STD_ERROR_HANDLE);
        if (hStdErr == INVALID_HANDLE_VALUE) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("ERROR: Unable to get the new stderr handle: %s"), getLastErrorText());
           return 1;
        }
        pfile = _tfdopen( _open_osfhandle((long)hStdErr, _O_TEXT), TEXT("w") );
        *stderr = *pfile;
        setvbuf( stderr, NULL, _IONBF, 0 );
        
        /* If we get here then we created a new console for the Wrapper.  If direct console was enabled then we need
         *  to reenable it here as any previous attempted log entries will have reset the direct mode. */
        setConsoleDirect(getBooleanProperty(properties, TEXT("wrapper.console.direct"), TRUE));
    }

    wrapperSetConsoleTitle();

    /* Set the handler to trap console signals.  This must be done after the console
     *  is created or it will not be applied to that console. */
#ifdef WRAPPER_DEBUG_CONTROL_HANDLER
    log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Console Handler: Register handler."));
#endif
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)wrapperConsoleHandler, TRUE);
    
    /* Collect the HINSTANCE and HWND references. */
    wrapperData->wrapperHInstance = GetModuleHandle(NULL);
    if (!wrapperData->wrapperConsoleHWND) {
        wrapperData->wrapperConsoleHWND = GetConsoleWindow();
    }
        
    if (wrapperData->useSystemTime) {
        /* We are going to be using system time so there is no reason to start up a timer thread. */
        timerThreadHandle = NULL;
        timerThreadId = 0;
    } else {
        /* Create and initialize a timer thread. */
        if ((res = initializeTimer()) != 0) {
            return res;
        }
    }
    
    if (canTrapSystemMessages()) {
        if (!(mainExitMutexHandle = CreateMutex(NULL, FALSE, NULL))) {
            _tprintf(TEXT("Failed to create Main Exit mutex. %s\n"), getLastErrorText());
            return 1;
        }
        if (lockMainExitMutex()) {
            return 1;
        }
        if ((res = initializeMessageThread()) != 0) {
            return res;
        }
    }
    
    if (wrapperData->isPageFaultOutputEnabled) {
        wrapperInitializeProfileCounters();
    }

    return 0;
}

/**
 * Cause the current thread to sleep for the specified number of milliseconds.
 *  Sleeps over one second are not allowed.
 *
 * @param ms Number of milliseconds to wait for.
 *
 * @return TRUE if the was interrupted, FALSE otherwise.  Neither is an error.
 */
int wrapperSleep(int ms) {
    if (wrapperData && wrapperData->isSleepOutputEnabled) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
            TEXT("    Sleep: sleep %dms"), ms);
    }

    Sleep(ms);

    if (wrapperData && wrapperData->isSleepOutputEnabled) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("    Sleep: awake"));
    }

    return FALSE;
}

/**
 * Detaches the Java process so the Wrapper will if effect forget about it.
 */
void wrapperDetachJava() {
    wrapperSetJavaState(WRAPPER_JSTATE_DOWN_CLEAN, 0, -1);
}


/**
 * Reports the status of the wrapper to the service manager
 * Possible status values:
 *   WRAPPER_WSTATE_STARTING
 *   WRAPPER_WSTATE_STARTED
 *   WRAPPER_WSTATE_PAUSING
 *   WRAPPER_WSTATE_PAUSED
 *   WRAPPER_WSTATE_RESUMING
 *   WRAPPER_WSTATE_STOPPING
 *   WRAPPER_WSTATE_STOPPED
 */
void wrapperReportStatus(int useLoggerQueue, int status, int errorCode, int waitHint) {
    int natState;
    TCHAR *natStateName;
    static DWORD dwCheckPoint = 1;
    BOOL bResult = TRUE;

    if (!wrapperData->isConsole) {
        switch (status) {
        case WRAPPER_WSTATE_STARTING:
            natState = SERVICE_START_PENDING;
            natStateName = TEXT("SERVICE_START_PENDING");
            break;
        case WRAPPER_WSTATE_STARTED:
            natState = SERVICE_RUNNING;
            natStateName = TEXT("SERVICE_RUNNING");
            break;
        case WRAPPER_WSTATE_PAUSING:
            natState = SERVICE_PAUSE_PENDING;
            natStateName = TEXT("SERVICE_PAUSE_PENDING");
            break;
        case WRAPPER_WSTATE_PAUSED:
            natState = SERVICE_PAUSED;
            natStateName = TEXT("SERVICE_PAUSED");
            break;
        case WRAPPER_WSTATE_RESUMING:
            natState = SERVICE_CONTINUE_PENDING;
            natStateName = TEXT("SERVICE_CONTINUE_PENDING");
            break;
        case WRAPPER_WSTATE_STOPPING:
            natState = SERVICE_STOP_PENDING;
            natStateName = TEXT("SERVICE_STOP_PENDING");
            break;
        case WRAPPER_WSTATE_STOPPED:
            natState = SERVICE_STOPPED;
            natStateName = TEXT("SERVICE_STOPPED");
            break;
        default:
            log_printf_queue(useLoggerQueue, WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unknown status: %d"), status);
            return;
        }

        ssStatus.dwControlsAccepted = 0;
        if (natState != SERVICE_START_PENDING) {
            ssStatus.dwControlsAccepted |= SERVICE_ACCEPT_STOP;
#ifdef SUPPORT_PRESHUTDOWN
            if (wrapperData->ntPreshutdown) {
                ssStatus.dwControlsAccepted |= SERVICE_ACCEPT_PRESHUTDOWN;
            } else {
#endif
                ssStatus.dwControlsAccepted |= SERVICE_ACCEPT_SHUTDOWN;
#ifdef SUPPORT_PRESHUTDOWN
            }
#endif
            if (wrapperData->pausable) {
                ssStatus.dwControlsAccepted |= SERVICE_ACCEPT_PAUSE_CONTINUE;
            }
        }
        if (isWindowsNT4_0OrEarlier()) {
            /* Old Windows - Does not support power events. */
        } else {
            /* Supports power events. */
            ssStatus.dwControlsAccepted |= SERVICE_ACCEPT_POWEREVENT;
        }
        /*
        if (wrapperData->isDebugging) {
            log_printf_queue(useLoggerQueue, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                "  Service %s accepting STOP=%s, SHUTDOWN=%s, PAUSE/CONTINUE=%s, POWEREVENT=%s",
                natStateName,
                (ssStatus.dwControlsAccepted & SERVICE_ACCEPT_STOP ? "True" : "False"),
                (ssStatus.dwControlsAccepted & SERVICE_ACCEPT_SHUTDOWN ? "True" : "False"),
                (ssStatus.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE ? "True" : "False"),
                (ssStatus.dwControlsAccepted & SERVICE_ACCEPT_POWEREVENT ? "True" : "False"));
        }
        */

        ssStatus.dwCurrentState = natState;
        if (errorCode == 0) {
            ssStatus.dwWin32ExitCode = NO_ERROR;
            ssStatus.dwServiceSpecificExitCode = 0;
        } else {
            ssStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
            ssStatus.dwServiceSpecificExitCode = errorCode;
        }
        ssStatus.dwWaitHint = waitHint;

        if ((natState == SERVICE_RUNNING) || (natState == SERVICE_STOPPED) || (natState == SERVICE_PAUSED)) {
            ssStatus.dwCheckPoint = 0;
        } else {
            ssStatus.dwCheckPoint = dwCheckPoint++;
        }

        if (wrapperData->isStateOutputEnabled) {
            log_printf_queue(useLoggerQueue, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                TEXT("calling SetServiceStatus with status=%s, waitHint=%d, checkPoint=%u, errorCode=%d"),
                natStateName, waitHint, dwCheckPoint, errorCode);
        }

        if (!(bResult = SetServiceStatus(sshStatusHandle, &ssStatus))) {
            log_printf_queue(useLoggerQueue, WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("SetServiceStatus failed"));
        }
    }
}

/**
 * Reads a single block of data from the child pipe.
 *
 * @param blockBuffer Pointer to the buffer where the block will be read.
 * @param blockSize Maximum number of bytes to read.
 * @param readCount Pointer to an int which will hold the number of bytes
 *                  actually read by the call.
 *
 * Returns TRUE if there were any problems, FALSE otherwise.
 */
int wrapperReadChildOutputBlock(char *blockBuffer, int blockSize, int *readCount) {
    DWORD currentBlockAvail;

    /* See how many characters are available in the pipe so we can say how much to read. */
    if (!PeekNamedPipe(wrapperChildStdoutRd, NULL, 0, NULL, &currentBlockAvail, NULL)) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
            TEXT("Failed to peek at output from the JVM: %s"), getLastErrorText());
        return TRUE;
    }

#ifdef DEBUG_CHILD_OUTPUT
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Peeked %d chars from pipe."), currentBlockAvail);
#endif

    if (currentBlockAvail > 0) {
        /* Attempt to read in an additional CHILD_BLOCK_SIZE characters. */
        if (!ReadFile(wrapperChildStdoutRd, blockBuffer, blockSize, readCount, NULL)) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                    TEXT("Failed to read output from the JVM: %s"), getLastErrorText());
            return TRUE;
        }
#ifdef DEBUG_CHILD_OUTPUT
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Read %d chars from pipe."), *readCount);
#endif
    } else {
        *readCount = 0;
    }

    return FALSE;
}

/**
 * Checks on the status of the JVM Process.
 * Returns WRAPPER_PROCESS_UP or WRAPPER_PROCESS_DOWN
 */
int wrapperGetProcessStatus(TICKS nowTicks, int sigChild) {
    int res;
    DWORD exitCode;
    TCHAR *exName;

    switch (WaitForSingleObject(wrapperData->javaProcess, 0)) {
    case WAIT_ABANDONED:
    case WAIT_OBJECT_0:
        res = WRAPPER_PROCESS_DOWN;

        /* Get the exit code of the process. */
        if (!GetExitCodeProcess(wrapperData->javaProcess, &exitCode)) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                TEXT("Critical error: unable to obtain the exit code of the JVM process: %s"), getLastErrorText());
            appExit(wrapperData->errorExitCode);
        }

        if (exitCode == STILL_ACTIVE) {
            /* Should never happen, but check for it. */
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("The JVM returned JVM exit code was STILL_ACTIVE.") );
        }

        /* If the JVM crashed then GetExitCodeProcess could have returned an uncaught exception. */
        exName = getExceptionName(exitCode, TRUE);
        if (exName != NULL) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("The JVM process terminated due to an uncaught exception: %s (0x%08x)"), exName, exitCode);

            /* Reset the exit code as the exeption value will confuse users. */
            exitCode = wrapperData->errorExitCode;
        }

        wrapperJVMProcessExited(nowTicks, exitCode);
        break;

    case WAIT_TIMEOUT:
        res = WRAPPER_PROCESS_UP;
        break;

    default:
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
            TEXT("Critical error: wait for JVM process failed: %s"), getLastErrorText());
        appExit(wrapperData->errorExitCode);
        break;
    }

    return res;
}

/**
 * Launch a JVM and collect the process information.
 *
 * @return TRUE if there were any problems, FALSE otherwise.
 */
int wrapperLaunchJvm(TCHAR* command, PROCESS_INFORMATION *pprocess_info) {
    STARTUPINFO startup_info;
    int ret;
    TCHAR titleBuffer[80];
    int hideConsole;
    int old_umask;
    
    /* Do not show another console for the new process */
    /*int processflags=CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS; */

    /* Show a console for the new process */
    /*int processflags=CREATE_NEW_PROCESS_GROUP | CREATE_NEW_CONSOLE; */

    /* Create a new process group as part of this console so that signals can */
    /*  be sent to the JVM. */
    DWORD processflags = CREATE_NEW_PROCESS_GROUP;

    /* Add the priority class of the new process to the processflags */
    processflags |= wrapperData->ntServicePriorityClass;

    /* Generate a unique time for the console so we can look for it below. */
    _sntprintf(titleBuffer, 80, TEXT("Wrapper Controlled JVM Console Id %d-%d (Do not close)"), wrapperData->wrapperPID, rand());

    /* Initialize a STARTUPINFO structure to use for the new process. */
    startup_info.cb=sizeof(STARTUPINFO);
    startup_info.lpReserved=NULL;
    startup_info.lpDesktop=NULL;
    startup_info.lpTitle=titleBuffer;
    startup_info.dwX=0;
    startup_info.dwY=0;
    startup_info.dwXSize=0;
    startup_info.dwYSize=0;
    startup_info.dwXCountChars=0;
    startup_info.dwYCountChars=0;
    startup_info.dwFillAttribute=0;

    /* Set the default flags which will not hide any windows opened by the JVM. */
    /* Using Show Window and SW_HIDE seems to make it impossible to show any windows when the 32-bit version runs as a service.
    startup_info.dwFlags=STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup_info.wShowWindow=SW_HIDE;
    */
    startup_info.dwFlags=STARTF_USESTDHANDLES;
    startup_info.wShowWindow=0;

    hideConsole = FALSE;
    if (wrapperData->isConsole) {
        /* We are running as a console so no special console handling needs to be done. */
    } else {
        /* Running as a service. */
        if (wrapperData->ntAllocConsole) {
            /* A console was allocated when the service was started so the JVM will not create
             *  its own. */
            if ((wrapperData->wrapperConsoleHWND) && (wrapperData->wrapperConsoleHide)) {
                /* The console exists but is currently hidden. */
                if (!wrapperData->ntHideJVMConsole) {
                    /* In order to support older JVMs we need to show the console when the
                     *  JVM is launched.  We need to remember to hide it below. */
                    showConsoleWindow(wrapperData->wrapperConsoleHWND, TEXT("Wrapper"));
                    wrapperData->wrapperConsoleVisible = TRUE;
                    wrapperData->wrapperConsoleHide = FALSE; /* Temporarily disable the hide flag so the event loop won't hide it while we are launching the JVM. */
                    hideConsole = TRUE;
                }
            }
        } else {
            /* A console does not yet exist so the JVM will create and display one itself. */
            if (wrapperData->ntHideJVMConsole) {
                /* The console that the JVM creates should be surpressed and never shown.
                 *  JVMs of version 1.4.0 and above will still display a GUI.  But older JVMs
                 *  will not. */
                startup_info.dwFlags=STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
                startup_info.wShowWindow=SW_HIDE;
            } else {
                /* The new JVM console should be allowed to be displayed.  But we need to
                 *  remember to hide it below. */
                hideConsole = TRUE;
            }
        }
    }

    startup_info.cbReserved2 = 0;
    startup_info.lpReserved2 = NULL;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = wrapperChildStdoutWr;
    startup_info.hStdError = wrapperChildStdoutWr;

    /* Initialize a PROCESS_INFORMATION structure to use for the new process */
    pprocess_info->hProcess = NULL;
    pprocess_info->hThread = NULL;
    pprocess_info->dwProcessId = 0;
    pprocess_info->dwThreadId = 0;

    /* Make sure the log file is closed before the Java process is created.  Failure to do
     *  so will give the Java process a copy of the open file.  This means that this process
     *  will not be able to rename the file even after closing it because it will still be
     *  open in the Java process.  Also set the auto close flag to make sure that other
     *  threads do not reopen the log file as the new process is being created. */
    setLogfileAutoClose(TRUE);
    closeLogfile();

    /* Reset the log duration so we get new counts from the time the JVM is launched. */
    resetDuration();

    /* Set the umask of the JVM (it doesn't seem to work on Windows) */
    old_umask = _umask(wrapperData->javaUmask);
    
    /* Create the new process */
    ret=CreateProcess(NULL,
                      command,        /* the command line to start */
                      NULL,           /* process security attributes */
                      NULL,           /* primary thread security attributes */
                      TRUE,           /* handles are inherited */
                      processflags,   /* we specify new process group */
                      NULL,           /* use parent's environment */
                      NULL,           /* use the Wrapper's current working directory */
                      &startup_info,  /* STARTUPINFO pointer */
                      pprocess_info); /* PROCESS_INFORMATION pointer */

    /* Restore the umask. */
    _umask(old_umask);

    /* As soon as the new process is created, restore the auto close flag. */
    setLogfileAutoClose(wrapperData->logfileCloseTimeout == 0);

    /* Check if virtual machine started */
    if (ret==FALSE) {
        int err=GetLastError();
        /* Make sure the process was launched correctly. */
        if (err!=NO_ERROR) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                TEXT("Unable to execute Java command.  %s"), getLastErrorText());
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("    %s"), command);
            wrapperData->javaProcess = NULL;

            if (wrapperData->isAdviserEnabled) {
                if ((err == ERROR_FILE_NOT_FOUND) || (err == ERROR_PATH_NOT_FOUND)) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                        TEXT("--------------------------------------------------------------------") );
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                        TEXT("Advice:" ));
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                        TEXT("Usually when the Wrapper fails to start the JVM process, it is\nbecause of a problem with the value of the configured Java command.\nCurrently:" ));
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                        TEXT("wrapper.java.command=%s"), getStringProperty(properties, TEXT("wrapper.java.command"), TEXT("java")));
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                        TEXT("Please make sure that the PATH or any other referenced environment\nvariables are correctly defined for the current environment." ));
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                        TEXT("--------------------------------------------------------------------") );
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                } else if (err == ERROR_ACCESS_DENIED) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                        TEXT("--------------------------------------------------------------------") );
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT(
                        "Advice:" ));
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT(
                        "Access denied errors when attempting to launch the Java process are\nusually caused by strict access permissions assigned to the\ndirectory in which Java is installed." ));
                    if (!wrapperData->isConsole) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT(
                            "Unless you have configured the Wrapper to run as a different user\nwith wrapper.ntservice.account property, the Wrapper and its JVM\nwill be as the SYSTEM user by default when run as a service." ));
                    }
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                        TEXT("--------------------------------------------------------------------") );
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                }
            }
            
            /* This is always a permanent problem. */
            CloseHandle(pprocess_info->hProcess);
            CloseHandle(pprocess_info->hThread);
            return TRUE;
        }
    }

    /* Now check if we have a process handle again for the Swedish WinNT bug */
    if (pprocess_info->hProcess == NULL) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("can not execute \"%s\""), command);
        CloseHandle(pprocess_info->hThread);
        return TRUE;
    }

    if (hideConsole) {
        /* Now that the JVM has been launched we need to hide the console that it
         *  is using. */
        if (wrapperData->wrapperConsoleHWND) {
            /* The wrapper's console needs to be hidden. */
            wrapperData->wrapperConsoleHide = TRUE;
        } else {
            /* We need to locate the console that was created by the JVM on launch
             *  and hide it. */
            wrapperData->jvmConsoleHandle = findConsoleWindow(titleBuffer);
            wrapperData->jvmConsoleVisible = TRUE; /* This will be cleared if the check call successfully hides it. */
        }
        wrapperCheckConsoleWindows();
    }

    return FALSE;
}

/**
 * Create a child process to print the Java version running the command:
 *    /path/to/java -version
 *  After printing the java version, the process is terminated.
 * 
 * In case the JVM is slow to start, it will time out after
 * the number of seconds set in "wrapper.java.version.timeout".
 */
int wrapperLaunchJavaVersion() {
    PROCESS_INFORMATION process_info;
    int blockTimeout;
    DWORD result;
    
    if (wrapperData->isDebugging) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Java Command Line (Query Java Version):"));
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  Command: %s"), wrapperData->jvmVersionCommand);
    }
    
    /* Force using the encoding of the current locale to read the output of the Java version
     * (we know this this JVM is launched without system properties specifying a different encoding). */
    resetJvmOutputEncoding(FALSE);
    
    if (wrapperLaunchJvm(wrapperData->jvmVersionCommand, &process_info)) {
        return TRUE;
    }

    /* If the user set the value to 0, then we will wait indefinitely.
     *  NOTE: it would be better to create a new state in the main event loop as this would allow the user to press CTRL-C. */
    blockTimeout = getIntProperty(properties, TEXT("wrapper.java.version.timeout"), DEFAULT_JAVA_VERSION_TIMEOUT) * 1000;
    
    if (blockTimeout <= 0) {
        blockTimeout = INFINITE;
    }
    
    result = WaitForSingleObject(process_info.hProcess, blockTimeout);
    
    if (result == WAIT_OBJECT_0) {
        /* Process completed - we know that nothing more can be written to stdout/stderr. */
        wrapperReadJavaVersionOutput();
    } else {
        if (result == WAIT_TIMEOUT) {
            /* Timed out. */
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Child process: Java version: timed out"));
        } else {
            /* Wait failed. */
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Child process: Java version: wait failed"));
        }
        if (TerminateProcess(process_info.hProcess, 1) == 0) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Child process: Java version: kill failed - %s"), getLastErrorText());
            
            /* Don't read the output but resolve the Java version to its default value. */
            wrapperSetJavaVersion(NULL);
        } else {
            /* The process is now killed. There might be no output but read all the pipe anyway.
             *  If the Java version can't be found, it will be resolved to the default value. */
            wrapperReadJavaVersionOutput();
        }
    }

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);

    return FALSE;
}

/**
 * Start the Java application and store internaly the JVM process.
 *
 * @return TRUE if there were any problems.  When this happens the Wrapper will not try to restart.
 */
int wrapperLaunchJavaApp() {
    static int javaIOThreadSet = FALSE;
    PROCESS_INFORMATION process_info;
    size_t len;
    
    /* Update the CLASSPATH in the environment if requested so the JVM can access it. */ 
    if (wrapperData->environmentClasspath) {
        if (setEnv(TEXT("CLASSPATH"), wrapperData->classpath, ENV_SOURCE_APPLICATION)) {
            /* This can happen if the classpath is too long on Windows. */
            wrapperData->javaProcess = NULL;
            wrapperData->exitCode = wrapperData->errorExitCode;
            return TRUE;
        }
    }
    
    /* Make sure the classpath is not too long. */
    len = _tcslen(wrapperData->jvmCommand);
    if (len > MAX_COMMAND_LINE_LEN) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("The generated Java command line has a length of %d, which is longer than the Windows maximum of %d characters."), len, MAX_COMMAND_LINE_LEN);
        if (!wrapperData->environmentClasspath) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  You may be able to shorten your command line by setting wrapper.java.classpath.use_environment."));
        }
        wrapperData->javaProcess = NULL;
        wrapperData->exitCode = wrapperData->errorExitCode;
        return TRUE;
    }
    
    /* Log the application java command line */
    if (wrapperData->commandLogLevel != LEVEL_NONE) {
        log_printf(WRAPPER_SOURCE_WRAPPER, wrapperData->commandLogLevel, TEXT("Java Command Line:"));
        log_printf(WRAPPER_SOURCE_WRAPPER, wrapperData->commandLogLevel, TEXT("  Command: %s"), wrapperData->jvmCommand);

        if (wrapperData->environmentClasspath) {
            log_printf(WRAPPER_SOURCE_WRAPPER, wrapperData->commandLogLevel,
                TEXT("  Classpath in Environment : %s"), wrapperData->classpath);
        }
    }
    
    if (wrapperData->runWithoutJVM) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
            TEXT("Not launching a JVM because %s was set to TRUE."), TEXT("wrapper.test.no_jvm"));
        wrapperData->exitCode = 0;
        return TRUE;
    }
    
    if (wrapperData->useJavaIOThread) {
        /* Create and initialize a javaIO thread. */
        if (!javaIOThreadSet) {
            if (initializeJavaIO()) {
                return TRUE;
            }
            javaIOThreadSet = TRUE;
        }
    } else {
        javaIOThreadHandle = NULL;
        javaIOThreadId = 0;
    }
    
    /* Now launch the JVM process. */
    if (wrapperLaunchJvm(wrapperData->jvmCommand, &process_info)) {
        wrapperData->javaProcess = NULL;
        wrapperData->exitCode = wrapperData->errorExitCode;
        return TRUE;
    }
    
    /* Reset the exit code when we launch a new JVM. */
    wrapperData->exitCode = 0;

    if (wrapperData->isDebugging) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("JVM started (PID=%d)"), process_info.dwProcessId);
    }

    /* We keep a reference to the process handle, but need to close the thread handle. */
    wrapperData->javaProcess = process_info.hProcess;
    wrapperData->javaPID = process_info.dwProcessId;
    CloseHandle(process_info.hThread);

    /* If a java pid filename is specified then write the pid of the java process. */
    if (wrapperData->javaPidFilename) {
        if (writePidFile(wrapperData->javaPidFilename, wrapperData->javaPID, wrapperData->javaPidFileUmask)) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Unable to write the Java PID file: %s"), wrapperData->javaPidFilename);
        }
    }

    /* If a java id filename is specified then write the id of the java process. */
    if (wrapperData->javaIdFilename) {
        if (writePidFile(wrapperData->javaIdFilename, wrapperData->jvmRestarts, wrapperData->javaIdFileUmask)) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Unable to write the Java Id file: %s"), wrapperData->javaIdFilename);
        }
    }
    
    return FALSE;
}

/**
 * Returns a tick count that can be used in combination with the
 *  wrapperGetTickAgeSeconds() function to perform time keeping.
 */
TICKS wrapperGetTicks() {
    TICKS ticks;

    if (wrapperData->useSystemTime) {
        /* We want to return a tick count that is based on the current system time. */
        ticks = wrapperGetSystemTicks();

    } else {
        /* Lock the tick mutex whenever the "timerTicks" variable is accessed. */
        if (wrapperData->useTickMutex && wrapperLockTickMutex()) {
            return 0;
        }

        /* Return a snapshot of the current tick count. */
        ticks = timerTicks;

        if (wrapperData->useTickMutex && wrapperReleaseTickMutex()) {
            return 0;
        }
    }

    return ticks;
}

/**
 * Outputs a a log entry describing what the memory dump columns are.
 */
void wrapperDumpMemoryBanner() {
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
        TEXT("Wrapper memory: PageFaultcount, WorkingSetSize (Peak), QuotaPagePoolUsage (Peak), QuotaNonPagedPoolUsage (Peak), PageFileUsage (Peak)  Java memory: PageFaultcount, WorkingSetSize (Peak), QuotaPagePoolUsage (Peak), QuotaNonPagedPoolUsage (Peak), PageFileUsage (Peak)  System memory: MemoryLoad, Available/PhysicalSize (%%), Available/PageFileSize (%%), Available/VirtualSize (%%), ExtendedVirtualSize"));
}

/**
 * Outputs a log entry at regular intervals to track the memory usage of the
 *  Wrapper and its JVM.
 */
void wrapperDumpMemory() {
    PROCESS_MEMORY_COUNTERS wCounters;
    PROCESS_MEMORY_COUNTERS jCounters;
    MEMORYSTATUSEX statex;

    if (OptionalGetProcessMemoryInfo) {
        /* Start with the Wrapper process. */
        if (OptionalGetProcessMemoryInfo(wrapperData->wrapperProcess, &wCounters, sizeof(wCounters)) == 0) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("Call to GetProcessMemoryInfo failed for Wrapper process %08x: %s"),
                wrapperData->wrapperPID, getLastErrorText());
            return;
        }

        if (wrapperData->javaProcess != NULL) {
            /* Next the Java process. */
            if (OptionalGetProcessMemoryInfo(wrapperData->javaProcess, &jCounters, sizeof(jCounters)) == 0) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                    TEXT("Call to GetProcessMemoryInfo failed for Java process %08x: %s"),
                    wrapperData->javaPID, getLastErrorText());
                return;
            }
        } else {
            memset(&jCounters, 0, sizeof(jCounters));
        }

        statex.dwLength = sizeof(statex);
        GlobalMemoryStatusEx(&statex);

        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
            TEXT("Wrapper memory: %lu, %lu (%lu), %lu (%lu), %lu (%lu), %lu (%lu)  Java memory: %lu, %lu (%lu), %lu (%lu), %lu (%lu), %lu (%lu)  System memory: %lu%%, %I64u/%I64u (%u%%), %I64u/%I64u (%u%%), %I64u/%I64u (%u%%), %I64u"),
            wCounters.PageFaultCount,
            wCounters.WorkingSetSize, wCounters.PeakWorkingSetSize,
            wCounters.QuotaPagedPoolUsage, wCounters.QuotaPeakPagedPoolUsage,
            wCounters.QuotaNonPagedPoolUsage, wCounters.QuotaPeakNonPagedPoolUsage,
            wCounters.PagefileUsage, wCounters.PeakPagefileUsage,
            jCounters.PageFaultCount,
            jCounters.WorkingSetSize, jCounters.PeakWorkingSetSize,
            jCounters.QuotaPagedPoolUsage, jCounters.QuotaPeakPagedPoolUsage,
            jCounters.QuotaNonPagedPoolUsage, jCounters.QuotaPeakNonPagedPoolUsage,
            jCounters.PagefileUsage, jCounters.PeakPagefileUsage,
            statex.dwMemoryLoad,
            statex.ullAvailPhys,
            statex.ullTotalPhys,
            (int)(100 * statex.ullAvailPhys / statex.ullTotalPhys),
            statex.ullAvailPageFile,
            statex.ullTotalPageFile,
            (int)(100 * statex.ullAvailPageFile / statex.ullTotalPageFile),
            statex.ullAvailVirtual,
            statex.ullTotalVirtual,
            (int)(100 * statex.ullAvailVirtual / statex.ullTotalVirtual),
            statex.ullAvailExtendedVirtual);
    }
}

DWORD filetimeToMS(FILETIME* filetime) {
    LARGE_INTEGER li;

    memcpy(&li, filetime, sizeof(li));
    li.QuadPart /= 10000;

    return li.LowPart;
}

/**
 * Outputs a log entry at regular intervals to track the CPU usage over each
 *  interval for the Wrapper and its JVM.
 *
 * In order to make sense of the timing values, it is also necessary to see how
 *  far the system performance counter has progressed.  By carefully comparing
 *  these values, it is possible to very accurately calculate the CPU usage over
 *  any period of time.
 */
LONGLONG lastPerformanceCount = 0;
LONGLONG lastWrapperKernelTime = 0;
LONGLONG lastWrapperUserTime = 0;
LONGLONG lastJavaKernelTime = 0;
LONGLONG lastJavaUserTime = 0;
LONGLONG lastIdleKernelTime = 0;
LONGLONG lastIdleUserTime = 0;
void wrapperDumpCPUUsage() {
    LARGE_INTEGER count;
    LARGE_INTEGER frequency;
    LARGE_INTEGER li;
    LONGLONG performanceCount;

    FILETIME creationTime;
    FILETIME exitTime;
    FILETIME wKernelTime;
    FILETIME wUserTime;
    FILETIME jKernelTime;
    FILETIME jUserTime;

    DWORD wKernelTimeMs; /* Will overflow in 49 days of usage. */
    DWORD wUserTimeMs;
    DWORD wTimeMs;
    DWORD jKernelTimeMs;
    DWORD jUserTimeMs;
    DWORD jTimeMs;

    double age;
    double wKernelPercent;
    double wUserPercent;
    double wPercent;
    double jKernelPercent;
    double jUserPercent;
    double jPercent;

    if (OptionalGetProcessTimes) {
        if (!QueryPerformanceCounter(&count)) {
            /* no high-resolution performance counter support. */
            return;
        }
        if (!QueryPerformanceFrequency(&frequency)) {
        }

        performanceCount = count.QuadPart;

        /* Start with the Wrapper process. */
        if (!OptionalGetProcessTimes(wrapperData->wrapperProcess, &creationTime, &exitTime, &wKernelTime, &wUserTime)) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("Call to GetProcessTimes failed for Wrapper process %08x: %s"),
                wrapperData->wrapperPID, getLastErrorText());
            return;
        }

        if (wrapperData->javaProcess != NULL) {
            /* Next the Java process. */
            if (!OptionalGetProcessTimes(wrapperData->javaProcess, &creationTime, &exitTime, &jKernelTime, &jUserTime)) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                    TEXT("Call to GetProcessTimes failed for Java process %08x: %s"),
                    wrapperData->javaPID, getLastErrorText());
                return;
            }
        } else {
            memset(&jKernelTime, 0, sizeof(jKernelTime));
            memset(&jUserTime, 0, sizeof(jUserTime));
            lastJavaKernelTime = 0;
            lastJavaUserTime = 0;
        }


        /* Convert the times to ms. */
        wKernelTimeMs = filetimeToMS(&wKernelTime);
        wUserTimeMs = filetimeToMS(&wUserTime);
        wTimeMs = wKernelTimeMs + wUserTimeMs;
        jKernelTimeMs = filetimeToMS(&jKernelTime);
        jUserTimeMs = filetimeToMS(&jUserTime);
        jTimeMs = jKernelTimeMs + jUserTimeMs;

        /* Calculate the number of seconds since the last call. */
        age = (double)(performanceCount - lastPerformanceCount) / frequency.QuadPart;

        /* Calculate usage percentages. */
        memcpy(&li, &wKernelTime, sizeof(li));
        wKernelPercent = 100.0 * ((li.QuadPart - lastWrapperKernelTime) / 10000000.0) / age;
        lastWrapperKernelTime = li.QuadPart;

        memcpy(&li, &wUserTime, sizeof(li));
        wUserPercent = 100.0 * ((li.QuadPart - lastWrapperUserTime) / 10000000.0) / age;
        lastWrapperUserTime = li.QuadPart;

        wPercent = wKernelPercent + wUserPercent;

        memcpy(&li, &jKernelTime, sizeof(li));
        jKernelPercent = 100.0 * ((li.QuadPart - lastJavaKernelTime) / 10000000.0) / age;
        lastJavaKernelTime = li.QuadPart;

        memcpy(&li, &jUserTime, sizeof(li));
        jUserPercent = 100.0 * ((li.QuadPart - lastJavaUserTime) / 10000000.0) / age;
        lastJavaUserTime = li.QuadPart;

        jPercent = jKernelPercent + jUserPercent;

        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
            TEXT("Wrapper CPU: kernel %ldms (%5.2f%%), user %ldms (%5.2f%%), total %ldms (%5.2f%%)  Java CPU: kernel %ldms (%5.2f%%), user %ldms (%5.2f%%), total %ldms (%5.2f%%)"),
            wKernelTimeMs, wKernelPercent, wUserTimeMs, wUserPercent, wTimeMs, wPercent,
            jKernelTimeMs, jKernelPercent, jUserTimeMs, jUserPercent, jTimeMs, jPercent);

        lastPerformanceCount = performanceCount;
    }
}
    
void wrapperInitializeProfileCounters() {
    PDH_STATUS pdhStatus;
    FARPROC pdhAddUnlocalizedCounter;
    BOOL couldLoad;
    HMODULE dbgHelpDll = GetModuleHandle(TEXT("Pdh.dll"));


    if( dbgHelpDll == NULL) {
        couldLoad = FALSE;
    } else {
        if (isVista()) {
#ifdef UNICODE
            pdhAddUnlocalizedCounter = GetProcAddress(dbgHelpDll, "PdhAddEnglishCounterW");
#else
            pdhAddUnlocalizedCounter = GetProcAddress(dbgHelpDll, "PdhAddEnglishCounterA");
#endif
        } else {
#ifdef UNICODE
            pdhAddUnlocalizedCounter = GetProcAddress(dbgHelpDll, "PdhAddCounterW");
#else
            pdhAddUnlocalizedCounter = GetProcAddress(dbgHelpDll, "PdhAddCounterA");
#endif
        }
        if(pdhAddUnlocalizedCounter == NULL) {
            couldLoad = FALSE;
        } else {
            couldLoad = TRUE;
        }
    }
    /* We want to set up system profile monitoring to keep track of the state of the system. */
    pdhStatus = PdhOpenQuery(NULL, 0, &pdhQuery);
    if (pdhStatus != ERROR_SUCCESS) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
            TEXT("Failed to initialize profiling: 0x%x"), pdhStatus);
        pdhQuery = NULL;
    } else {
        pdhStatus = (PDH_STATUS)pdhAddUnlocalizedCounter(pdhQuery, TEXT("\\PhysicalDisk(_Total)\\Avg. Disk Queue Length"), 0, &pdhCounterPhysicalDiskAvgQueueLen);
        if (pdhStatus != ERROR_SUCCESS) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Failed to initialize profiling counter %d: 0x%x"), 1, pdhStatus);
        }
        
        pdhStatus = (PDH_STATUS)pdhAddUnlocalizedCounter(pdhQuery, TEXT("\\PhysicalDisk(_Total)\\Avg. Disk Write Queue Length"), 0, &pdhCounterPhysicalDiskAvgWriteQueueLen);
        if (pdhStatus != ERROR_SUCCESS) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Failed to initialize profiling counter %d: 0x%x"), 2, pdhStatus);
        }
        
        pdhStatus = (PDH_STATUS)pdhAddUnlocalizedCounter(pdhQuery, TEXT("\\PhysicalDisk(_Total)\\Avg. Disk Read Queue Length"), 0, &pdhCounterPhysicalDiskAvgReadQueueLen);
        if (pdhStatus != ERROR_SUCCESS) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Failed to initialize profiling counter %d: 0x%x"), 3, pdhStatus);
        }
        
        pdhStatus = (PDH_STATUS)pdhAddUnlocalizedCounter(pdhQuery, TEXT("\\Memory\\Page Faults/sec"), 0, &pdhCounterMemoryPageFaultsPSec);
        if (pdhStatus != ERROR_SUCCESS) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Failed to initialize profiling counter %d: 0x%x"), 4, pdhStatus);
        }
        
        pdhStatus = (PDH_STATUS)pdhAddUnlocalizedCounter(pdhQuery, TEXT("\\Memory\\Transition Faults/sec"), 0, &pdhCounterMemoryTransitionFaultsPSec);
        if (pdhStatus != ERROR_SUCCESS) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Failed to initialize profiling counter %d: 0x%x"), 5, pdhStatus);
        }
        
        pdhStatus = (PDH_STATUS)pdhAddUnlocalizedCounter(pdhQuery, TEXT("\\Process(wrapper)\\Page Faults/sec"), 0, &pdhCounterProcessWrapperPageFaultsPSec);
        if (pdhStatus != ERROR_SUCCESS) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Failed to initialize profiling counter %d: 0x%x"), 6, pdhStatus);
        }
        
        pdhStatus = (PDH_STATUS)pdhAddUnlocalizedCounter(pdhQuery, TEXT("\\Process(java)\\Page Faults/sec"), 0, &pdhCounterProcessJavaPageFaultsPSec);
        if (pdhStatus != ERROR_SUCCESS) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                TEXT("Failed to initialize profiling counter %d: 0x%x"), 7, pdhStatus);
        }
        if (couldLoad && dbgHelpDll != NULL) {
        FreeLibrary(dbgHelpDll);
        }
        /* This is the first call, since for some equations (e.g. for average) 2 values need to be polled */
        PdhCollectQueryData(pdhQuery);
        /* PdhGetCounterInfo to get info about the counters like scale, etc. */
    }
}

void wrapperDumpPageFaultUsage() {
    PDH_STATUS pdhStatus;
    DWORD counterType;
    PDH_FMT_COUNTERVALUE counterValue;
    double diskQueueLen = 0;
    double diskQueueWLen = 0;
    double diskQueueRLen = 0;
    double pageFaults = 0;
    double transitionPageFaults = 0;
    double wrapperPageFaults = 0;
    double javaPageFaults = 0;
    
    if (pdhQuery == NULL) {
        return;
    }
    
    pdhStatus = PdhCollectQueryData(pdhQuery);
    if (pdhStatus == ERROR_SUCCESS) {
        pdhStatus = PdhGetFormattedCounterValue(pdhCounterPhysicalDiskAvgQueueLen, PDH_FMT_DOUBLE, &counterType, &counterValue);
        if (pdhStatus == ERROR_SUCCESS) {
            diskQueueLen = counterValue.doubleValue;
            /*log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("\\PhysicalDisk(_Total)\\Avg. Disk Queue Length : %d %10.5f"), counterValue.CStatus, counterValue.doubleValue);*/
        }
        pdhStatus = PdhGetFormattedCounterValue(pdhCounterPhysicalDiskAvgWriteQueueLen, PDH_FMT_DOUBLE, &counterType, &counterValue);
        if (pdhStatus == ERROR_SUCCESS) {
            diskQueueWLen = counterValue.doubleValue;
            /*log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("\\PhysicalDisk(_Total)\\Avg. Disk Write Queue Length : %d %10.5f"), counterValue.CStatus, counterValue.doubleValue);*/
        }
        pdhStatus = PdhGetFormattedCounterValue(pdhCounterPhysicalDiskAvgReadQueueLen, PDH_FMT_DOUBLE, &counterType, &counterValue);
        if (pdhStatus == ERROR_SUCCESS) {
            diskQueueRLen = counterValue.doubleValue;
            /*log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("\\PhysicalDisk(_Total)\\Avg. Disk Read Queue Length : %d %10.5f"), counterValue.CStatus, counterValue.doubleValue);*/
        }

        pdhStatus = PdhGetFormattedCounterValue(pdhCounterMemoryPageFaultsPSec, PDH_FMT_DOUBLE, &counterType, &counterValue);
        if (pdhStatus == ERROR_SUCCESS) {
            pageFaults = counterValue.doubleValue;
            /*log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("\\Memory\\Page Faults/sec : %d %10.5f"), counterValue.CStatus, counterValue.doubleValue);*/
        }

        pdhStatus = PdhGetFormattedCounterValue(pdhCounterMemoryTransitionFaultsPSec, PDH_FMT_DOUBLE, &counterType, &counterValue);
        if (pdhStatus == ERROR_SUCCESS) {
            transitionPageFaults = counterValue.doubleValue;
            /*log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("\\Memory\\Transition Faults/sec : %d %10.5f"), counterValue.CStatus, counterValue.doubleValue);*/
        }
        
        pdhStatus = PdhGetFormattedCounterValue(pdhCounterProcessWrapperPageFaultsPSec, PDH_FMT_DOUBLE, &counterType, &counterValue);
        if (pdhStatus == ERROR_SUCCESS) {
            wrapperPageFaults = counterValue.doubleValue;
            /*log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("\\Process(wrapper)\\Page Faults/sec : %d %10.5f"), counterValue.CStatus, counterValue.doubleValue);*/
        }
        
        pdhStatus = PdhGetFormattedCounterValue(pdhCounterProcessJavaPageFaultsPSec, PDH_FMT_DOUBLE, &counterType, &counterValue);
        if (pdhStatus == ERROR_SUCCESS) {
            javaPageFaults = counterValue.doubleValue;
            /*log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("\\Process(java)\\Page Faults/sec : %d %10.5f"), counterValue.CStatus, counterValue.doubleValue);*/
        }
        
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Page Faults (Total:%8.2f%8.2f:%8.2f Wrapper:%7.2f (%7.2f%%) Java:%7.2f (%7.2f%%))  Queue Len (Total:%7.2f Read:%7.2f Write:%7.2f)"),
            pageFaults, transitionPageFaults, pageFaults - transitionPageFaults,
            wrapperPageFaults, (pageFaults > 0 ? 100 * wrapperPageFaults / pageFaults : 0),
            javaPageFaults, (pageFaults > 0 ? 100 * javaPageFaults / pageFaults : 0),
            diskQueueLen, diskQueueRLen, diskQueueWLen);
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
            TEXT("Failed to collect profile data: 0x%x"), pdhStatus);
    } 
}

void disposeProfileCounters() {
    if (pdhQuery != NULL) {
        PdhCloseQuery(pdhQuery);
        pdhQuery = NULL;
    }
}

/******************************************************************************
 * NT Service Methods
 *****************************************************************************/

/**
 * This function goes through and checks flags for each of several signals to see if they
 *  have been fired since the last time this function was called.  This is the only thread
 *  which will ever clear these flags, but they can be set by other threads within the
 *  signal handlers at ANY time.  So only check the value of each flag once and reset them
 *  immediately to decrease the chance of missing duplicate signals.
 */
void wrapperMaintainControlCodes() {
    /* Allow for a large integer + \0 */
    TCHAR buffer[11];
    int ctrlCodeLast;
    int quit = FALSE;
    int halt = FALSE;

    /* CTRL_C_EVENT */
    if (wrapperData->ctrlEventCTRLCTrapped) {
        wrapperData->ctrlEventCTRLCTrapped = FALSE;

        /*  Always quit.  If the user has pressed CTRL-C previously then we want to force
         *   an immediate shutdown. */
        if (ctrlCTrapped) {
            /* Pressed CTRL-C more than once. */
            if (wrapperGetTickAgeTicks(ctrlCTrappedLastTick, wrapperGetTicks()) >= wrapperData->forcedShutdownDelay) {
                /* We want to ignore double signals which can be sent both by the script and the systems at almost the same time. */
                if (wrapperData->isForcedShutdownDisabled) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("%s trapped.  Already shutting down."), TEXT("CTRL-C"));
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("%s trapped.  Forcing immediate shutdown."), TEXT("CTRL-C"));
                    halt = TRUE;
                }
            }
        } else {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                TEXT("%s trapped.  Shutting down."), TEXT("CTRL-C"));
            ctrlCTrapped = TRUE;
            ctrlCTrappedLastTick = wrapperGetTicks();
        }
        quit = TRUE;
    }

    /* CTRL_CLOSE_EVENT */
    if (wrapperData->ctrlEventCloseTrapped) {
        wrapperData->ctrlEventCloseTrapped = FALSE;

        /*  Always quit.  If the user has tried to close the console previously then we want to force
         *   an immediate shutdown. */
        if (ctrlCTrapped) {
            /* Pressed Close or CTRL-C more than once. */
            if (wrapperGetTickAgeTicks(ctrlCTrappedLastTick, wrapperGetTicks()) >= wrapperData->forcedShutdownDelay) {
                /* We want to ignore double signals which can be sent both by the script and the systems at almost the same time. */
                if (wrapperData->isForcedShutdownDisabled) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("%s trapped.  Already shutting down."), TEXT("Close"));
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("%s trapped.  Forcing immediate shutdown."), TEXT("Close"));
                    halt = TRUE;
                }
            }
        } else {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                TEXT("%s trapped.  Shutting down."), TEXT("Close"));
            ctrlCTrapped = TRUE;
            ctrlCTrappedLastTick = wrapperGetTicks();
        }
        quit = TRUE;
    }

    /* CTRL_LOGOFF_EVENT */
    if (wrapperData->ctrlEventLogoffTrapped) {
        wrapperData->ctrlEventLogoffTrapped = FALSE;

        /* Happens when the user logs off.  We should quit when run as a */
        /*  console, but stay up when run as a service. */
        if ((wrapperData->isConsole) && (!wrapperData->ignoreUserLogoffs)) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("User logged out.  Shutting down."));
            quit = TRUE;
        } else {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("User logged out.  Ignored."));
            quit = FALSE;
        }
    }

    /* CTRL_SHUTDOWN_EVENT */
    if (wrapperData->ctrlEventShutdownTrapped) {
        wrapperData->ctrlEventShutdownTrapped = FALSE;

        /* Happens when the machine is shutdown or rebooted.  Always quit. */
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
            TEXT("Machine is shutting down."));
        quit = TRUE;
    }

    /* Queued control codes. */
    while (wrapperData->ctrlCodeQueueReadIndex != wrapperData->ctrlCodeQueueWriteIndex) {
        ctrlCodeLast = wrapperData->ctrlCodeQueue[wrapperData->ctrlCodeQueueReadIndex];
#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Process queued control code: %d (r:%d w:%d)"), ctrlCodeLast, wrapperData->ctrlCodeQueueReadIndex, wrapperData->ctrlCodeQueueWriteIndex);
#endif
        wrapperData->ctrlCodeQueueReadIndex++;
        if (wrapperData->ctrlCodeQueueReadIndex >= CTRL_CODE_QUEUE_SIZE ) {
            wrapperData->ctrlCodeQueueReadIndex = 0;
        }

        _sntprintf(buffer, 11, TEXT("%d"), ctrlCodeLast);
        wrapperProtocolFunction(WRAPPER_MSG_SERVICE_CONTROL_CODE, buffer);
    }

    /* SERVICE_CONTROL_PAUSE */
    if (wrapperData->ctrlCodePauseTrapped) {
        wrapperData->ctrlCodePauseTrapped = FALSE;

        /* Tell the wrapper to pause */
        wrapperPauseProcess(WRAPPER_ACTION_SOURCE_CODE_WINDOWS_SERVICE_MANAGER);
    }

    /* SERVICE_CONTROL_CONTINUE */
    if (wrapperData->ctrlCodeContinueTrapped) {
        wrapperData->ctrlCodeContinueTrapped = FALSE;

        /* Tell the wrapper to resume */
        wrapperResumeProcess(WRAPPER_ACTION_SOURCE_CODE_WINDOWS_SERVICE_MANAGER);
    }

    /* SERVICE_CONTROL_STOP */
    if (wrapperData->ctrlCodeStopTrapped) {
        wrapperData->ctrlCodeStopTrapped = FALSE;

        /* Request to stop the service. Report SERVICE_STOP_PENDING */
        /* to the service control manager before calling ServiceStop() */
        /* to avoid a "Service did not respond" error. */
        wrapperReportStatus(FALSE, WRAPPER_WSTATE_STOPPING, wrapperData->exitCode, wrapperData->ntShutdownWaitHint * 1000);

        /* Tell the wrapper to shutdown normally */
        /* Always force the shutdown as this is an external event. */
        wrapperStopProcess(0, TRUE);

        /* To make sure that the JVM will not be restarted for any reason,
         *  start the Wrapper shutdown process as well.
         *  In this case we do not want to allow any exit filters to be used
         *  so setting this here will force the shutdown. */
        if ((wrapperData->wState == WRAPPER_WSTATE_STOPPING) ||
            (wrapperData->wState == WRAPPER_WSTATE_STOPPED)) {
            /* Already stopping. */
        } else {
            wrapperSetWrapperState(WRAPPER_WSTATE_STOPPING);
        }
    }

#ifdef SUPPORT_PRESHUTDOWN
    /* SERVICE_CONTROL_PRESHUTDOWN */
    if (wrapperData->ctrlCodePreShutdownTrapped) {
        wrapperData->ctrlCodePreShutdownTrapped = FALSE;

        /* Request to stop the service. Report SERVICE_STOP_PENDING */
        /* to the service control manager before calling ServiceStop() */
        /* to avoid a "Service did not respond" error. */
        wrapperReportStatus(FALSE, WRAPPER_WSTATE_STOPPING, wrapperData->exitCode, wrapperData->ntShutdownWaitHint * 1000);

        /* Tell the wrapper to shutdown normally */
        /* Always force the shutdown as this is an external event. */
        wrapperStopProcess(0, TRUE);

        /* To make sure that the JVM will not be restarted for any reason,
         *  start the Wrapper shutdown process as well. */
        if ((wrapperData->wState == WRAPPER_WSTATE_STOPPING) ||
            (wrapperData->wState == WRAPPER_WSTATE_STOPPED)) {
            /* Already stopping. */
        } else {
            wrapperSetWrapperState(WRAPPER_WSTATE_STOPPING);
        }
    }
#endif

    /* SERVICE_CONTROL_SHUTDOWN */
    if (wrapperData->ctrlCodeShutdownTrapped) {
        wrapperData->ctrlCodeShutdownTrapped = FALSE;

        /* Request to stop the service. Report SERVICE_STOP_PENDING */
        /* to the service control manager before calling ServiceStop() */
        /* to avoid a "Service did not respond" error. */
        wrapperReportStatus(FALSE, WRAPPER_WSTATE_STOPPING, wrapperData->exitCode, wrapperData->ntShutdownWaitHint * 1000);

        /* Tell the wrapper to shutdown normally */
        /* Always force the shutdown as this is an external event. */
        wrapperStopProcess(0, TRUE);

        /* To make sure that the JVM will not be restarted for any reason,
         *  start the Wrapper shutdown process as well. */
        if ((wrapperData->wState == WRAPPER_WSTATE_STOPPING) ||
            (wrapperData->wState == WRAPPER_WSTATE_STOPPED)) {
            /* Already stopping. */
        } else {
            wrapperSetWrapperState(WRAPPER_WSTATE_STOPPING);
        }
    }

    /* The configured thread dump control code */
    if (wrapperData->ctrlCodeDumpTrapped) {
        wrapperData->ctrlCodeDumpTrapped = FALSE;

        wrapperRequestDumpJVMState();
    }

    if (quit) {
        if (halt) {
            /* Disable the thread dump on exit feature if it is set because it
             *  should not be displayed when the user requested the immediate exit. */
            wrapperData->requestThreadDumpOnFailedJVMExit = FALSE;
            wrapperKillProcess(FALSE);
        } else {
            /* Always force the shutdown as this is an external event. */
            wrapperStopProcess(0, TRUE);
        }
        /* Don't actually kill the process here.  Let the application shut itself down */

        /* To make sure that the JVM will not be restarted for any reason,
         *  start the Wrapper shutdown process as well. */
        if ((wrapperData->wState == WRAPPER_WSTATE_STOPPING) ||
            (wrapperData->wState == WRAPPER_WSTATE_STOPPED)) {
            /* Already stopping. */
        } else {
            wrapperSetWrapperState(WRAPPER_WSTATE_STOPPING);
        }
    }
}

/**
 * The service control handler is called by the service manager when there are
 *    events for the service.  registered using a call to
 *    RegisterServiceCtrlHandler in wrapperServiceMain.
 *
 * Note on PowerEvents prior to win2k: http://blogs.msdn.com/heaths/archive/2005/05/18/419791.aspx
 */
DWORD WINAPI wrapperServiceControlHandlerEx(DWORD dwCtrlCode,
                                            DWORD dwEvtType,
                                            LPVOID lpEvtData,
                                            LPVOID lpCntxt) {

    DWORD result = result = NO_ERROR;

    /* Forward the control code off to the JVM. */
    DWORD controlCode = dwCtrlCode;

    /* Enclose the contents of this call in a try catch block so we can
     *  display and log useful information should the need arise. */
    __try {
        /*
        if (wrapperData->isDebugging) {
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("ServiceControlHandlerEx(%d, %d, %p, %p)"), dwCtrlCode, dwEvtType, lpEvtData, lpCntxt);
        }
        */

        /* This thread appears to always be the same as the main thread.
         *  Just to be safe reregister it. */
        logRegisterThread(WRAPPER_THREAD_MAIN);

        if (dwCtrlCode == SERVICE_CONTROL_POWEREVENT) {
            switch (dwEvtType) {
                case PBT_APMQUERYSUSPEND: /* 0x0 */
                    /* system is hiberating
                     * send off power resume event */
                    if (wrapperData->isDebugging) {
                        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT(PBT_APMQUERYSUSPEND)"));
                    }
                    controlCode = 0x0D00;
                    break;

                case PBT_APMQUERYSUSPENDFAILED: /* 0x2 */
                    /* system is waking up
                     * send off power resume event */
                    if (wrapperData->isDebugging) {
                        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT(PBT_APMQUERYSUSPENDFAILED)"));
                    }
                    controlCode = 0x0D02;
                    break;

                case PBT_APMSUSPEND:/* 0x4 */
                    /* system is waking up
                     * send off power resume event */
                    if (wrapperData->isDebugging) {
                        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT(PBT_APMSUSPEND)"));
                    }
                    controlCode = 0x0D04;
                    break;

                case PBT_APMRESUMECRITICAL: /* 0x6 */
                    /* system is waking up
                     * send off power resume event */
                    if (wrapperData->isDebugging) {
                        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT(PBT_APMRESUMECRITICAL)"));
                    }
                    controlCode = 0x0D06;
                    break;

                case PBT_APMRESUMESUSPEND: /* 0x7 */
                    /* system is waking up
                     * send off power resume event */
                    if (wrapperData->isDebugging) {
                        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT(PBT_APMRESUMESUSPEND)"));
                    }
                    controlCode = 0x0D07;
                    break;

                case PBT_APMBATTERYLOW: /* 0x9 */
                    /* batter is low warning. */
                    if (wrapperData->isDebugging) {
                        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT(PBT_APMBATTERYLOW)"));
                    }
                    controlCode = 0x0D09;
                    break;

                case PBT_APMPOWERSTATUSCHANGE: /* 0xA */
                    /* the status of system power changed. */
                    if (wrapperData->isDebugging) {
                        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT(PBT_APMPOWERSTATUSCHANGE)"));
                    }
                    controlCode = 0x0D0A;
                    break;

                case PBT_APMOEMEVENT: /* 0xB */
                    /* there was an OEM event. */
                    if (wrapperData->isDebugging) {
                        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT(PBT_APMOEMEVENT)"));
                    }
                    controlCode = 0x0D0B;
                    break;

                case PBT_APMRESUMEAUTOMATIC: /* 0x12 */
                    /* system is waking up */
                    if (wrapperData->isDebugging) {
                        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT(PBT_APMRESUMEAUTOMATIC)"));
                    }
                    controlCode = 0x0D12;
                    break;

                /* The following STANDBY values do not appear to be used but are defined in WinUser.h. */
                /*case PBT_APMQUERYSTANDBY:*/ /* 0x1 */
                /*case PBT_APMQUERYSTANDBYFAILED:*/ /* 0x3 */
                /*case PBT_APMSTANDBY:*/ /* 0x5 */
                /*case PBT_APMRESUMESTANDBY:*/ /* 0x8 */

                default:
                    /* Unexpected generic powerevent code */
                    if (wrapperData->isDebugging) {
                        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT(%d)"), dwEvtType);
                    }
                    break;
            }
        }

        /* Forward the control code off to the JVM.  Write the signals into a rotating queue so we can process more than one per loop. */
        if ((wrapperData->ctrlCodeQueueWriteIndex == wrapperData->ctrlCodeQueueReadIndex - 1) || ((wrapperData->ctrlCodeQueueWriteIndex == CTRL_CODE_QUEUE_SIZE - 1) && (wrapperData->ctrlCodeQueueReadIndex == 0))) {
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("Control code queue overflow (%d:%d).  Dropping control code: %d\n"), wrapperData->ctrlCodeQueueWriteIndex, wrapperData->ctrlCodeQueueReadIndex, controlCode);
        } else {
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Enqueue control code: %d (r:%d w:%d)"), controlCode, wrapperData->ctrlCodeQueueReadIndex, wrapperData->ctrlCodeQueueWriteIndex);
#endif
            wrapperData->ctrlCodeQueue[wrapperData->ctrlCodeQueueWriteIndex] = controlCode;

            wrapperData->ctrlCodeQueueWriteIndex++;
            if (wrapperData->ctrlCodeQueueWriteIndex >= CTRL_CODE_QUEUE_SIZE) {
                wrapperData->ctrlCodeQueueWriteIndex = 0;
                wrapperData->ctrlCodeQueueWrapped = TRUE;
            }
        }

        switch(dwCtrlCode) {
        case SERVICE_CONTROL_PAUSE:
            if (wrapperData->isDebugging) {
                log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_PAUSE"));
            }

            wrapperData->ctrlCodePauseTrapped = TRUE;

            break;

        case SERVICE_CONTROL_CONTINUE:
            if (wrapperData->isDebugging) {
                log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_CONTINUE"));
            }

            wrapperData->ctrlCodeContinueTrapped = TRUE;

            break;

        case SERVICE_CONTROL_STOP:
            if (wrapperData->isDebugging) {
                log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_STOP"));
            }

            wrapperData->ctrlCodeStopTrapped = TRUE;

            break;

        case SERVICE_CONTROL_INTERROGATE:
            if (wrapperData->isDebugging) {
                log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_INTERROGATE"));
            }

            /* This case MUST be processed, even though we are not */
            /* obligated to do anything substantial in the process. */
            break;

        case SERVICE_CONTROL_POWEREVENT:
            // we handled it
            if (wrapperData->isDebugging) {
                log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_POWEREVENT (handled)"));
            }
            break;

#ifdef SUPPORT_PRESHUTDOWN
        case SERVICE_CONTROL_PRESHUTDOWN:
            if (wrapperData->isDebugging) {
                log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_PRESHUTDOWN"));
            }

            wrapperData->ctrlCodePreShutdownTrapped = TRUE;

            break;
#endif

        case SERVICE_CONTROL_SHUTDOWN:
            if (wrapperData->isDebugging) {
                log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_SHUTDOWN"));
            }

            wrapperData->ctrlCodeShutdownTrapped = TRUE;

            break;

        default:
            if ((wrapperData->threadDumpControlCode > 0) && (dwCtrlCode == wrapperData->threadDumpControlCode)) {
                if (wrapperData->isDebugging) {
                    log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_(%d) Request Thread Dump."), dwCtrlCode);
                }

                wrapperData->ctrlCodeDumpTrapped = TRUE;
            } else {
                /* Any other cases... Did not handle */
                if (wrapperData->isDebugging) {
                    log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("  SERVICE_CONTROL_(%d) Not handled."), dwCtrlCode);
                }
                result = ERROR_CALL_NOT_IMPLEMENTED;
            }
            break;
        }

        /* After invocation of this function, we MUST call the SetServiceStatus */
        /* function, which is accomplished through our ReportStatus function. We */
        /* must do this even if the current status has not changed. */
        wrapperReportStatus(TRUE, wrapperData->wState, 0, 0);

    } __except (exceptionFilterFunction(GetExceptionInformation())) {
        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
            TEXT("<-- Wrapper Stopping due to error in service control handler."));
        appExit(wrapperData->errorExitCode);
    }

    return result;
}

/**
 * The service control handler is called by the service manager when there are
 *    events for the service.  registered using a call to
 *    RegisterServiceCtrlHandler in wrapperServiceMain.
 */
void WINAPI wrapperServiceControlHandler(DWORD dwCtrlCode) {
    /*
    if (wrapperData->isDebugging) {
        log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Service(%d)"), dwCtrlCode);
    }
    */
    wrapperServiceControlHandlerEx(dwCtrlCode, 0, NULL, NULL);
}

/**
 * The wrapperServiceMain function is the entry point for the NT service.
 *    It is called by the service manager.
 */
void WINAPI wrapperServiceMain(DWORD dwArgc, LPTSTR *lpszArgv) {
    int timeout;

    /* Enclose the contents of this call in a try catch block so we can
     *  display and log useful information should the need arise. */
    __try {
#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("wrapperServiceMain()"));
#endif

        /* Immediately register this thread with the logger. */
        logRegisterThread(WRAPPER_THREAD_SRVMAIN);

        /* Call RegisterServiceCtrlHandler immediately to register a service control */
        /* handler function. The returned SERVICE_STATUS_HANDLE is saved with global */
        /* scope, and used as a service id in calls to SetServiceStatus. */
        if (OptionalRegisterServiceCtrlHandlerEx) {
            /* Use RegisterServiceCtrlHandlerEx if available. */
            sshStatusHandle = OptionalRegisterServiceCtrlHandlerEx(
                wrapperData->serviceName, wrapperServiceControlHandlerEx, (LPVOID)1);
        } else {
            sshStatusHandle = RegisterServiceCtrlHandler(
                wrapperData->serviceName, wrapperServiceControlHandler);
        }
        if (!sshStatusHandle) {
            goto finally;
        }

        /* The global ssStatus SERVICE_STATUS structure contains information about the */
        /* service, and is used throughout the program in calls made to SetStatus through */
        /* the ReportStatus function. */
        ssStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        ssStatus.dwServiceSpecificExitCode = 0;

        /* Do setup now that the service is initialized. */
        
        /* Initialize the invocation mutex as necessary, exit if it already exists. */
        if (initInvocationMutex()) {
            appExit(wrapperData->exitCode);
            return; /* For clarity. */
        }

        /* Get the current process. */
        wrapperData->wrapperProcess = GetCurrentProcess();
        wrapperData->wrapperPID = GetCurrentProcessId();

        /* See if the logs should be rolled on Wrapper startup. */
        if ((getLogfileRollMode() & ROLL_MODE_WRAPPER) ||
            (getLogfileRollMode() & ROLL_MODE_JVM)) {
            rollLogs(NULL);
        }

        if (checkPidFile()) {
            /* The pid file exists and we are strict, so exit (cleanUpPIDFilesOnExit has not been turned on yet, so we will exit without cleaning the pid files). */
            appExit(wrapperData->errorExitCode);
            return; /* For clarity. */
        }

        /* From now on:
         *  - all pid files will be cleaned when the Wrapper exits,
         *  - any existing file will be owerwritten. */
        cleanUpPIDFilesOnExit = TRUE;

        if (wrapperWriteStartupPidFiles()) {
            appExit(wrapperData->errorExitCode);
            return; /* For clarity. */
        }

        /* If we could guarantee that all initialization would occur in less than one */
        /* second, we would not have to report our status to the service control manager. */
        /* For good measure, we will assign SERVICE_START_PENDING to the current service */
        /* state and inform the service control manager through our ReportStatus function. */
        if (wrapperData->startupTimeout > 0) {
            timeout = wrapperData->startupTimeout * 1000;
        } else {
            timeout = 86400000; // Set infinity at 1 day.
        }
        /* Before entering the main loop, it makes sens to use wrapperData->startupTimeout instead of wrapperData->ntStartupWaitHint. */
        wrapperReportStatus(FALSE, WRAPPER_WSTATE_STARTING, 0, timeout);

        /* Now actually start the service */
        wrapperRunService();

finally:

        /* Report that the service has stopped and set the correct exit code. */
        wrapperReportStatus(FALSE, WRAPPER_WSTATE_STOPPED, wrapperData->exitCode, 1000);

#ifdef _DEBUG
        /* The following message will not always appear on the screen if the STOPPED
         *  status was set above.  But the code in the appExit function below always
         *  appears to be getting executed.  Looks like some kind of a timing issue. */
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Exiting service process."));
#endif

        /* Actually exit the process, returning the current exit code. */
        appExit(wrapperData->exitCode);

    } __except (exceptionFilterFunction(GetExceptionInformation())) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
            TEXT("<-- Wrapper Stopping due to error in service main."));
        appExit(wrapperData->errorExitCode);
    }
}

/**
 * Reads a password from the console and then returns it as a malloced string.
 *  This is only called once so the memory can leak.
 */
TCHAR *readPassword() {
    TCHAR *buffer;
    TCHAR c;
    int cnt = 0;

    buffer = malloc(sizeof(TCHAR) * 65);
    if (!buffer) {
        outOfMemory(TEXT("RP"), 1);
        appExit(0);
        return NULL;
    }
    buffer[0] = 0;

    do {
        c = _gettch();
        switch (c) {
        case 0x03: /* Ctrl-C */
            _tprintf(TEXT("\n") );
            appExit(0);
            break;

        case 0x08: /* Backspace */
            if (cnt > 0) {
                _tprintf(TEXT("%c %c"), 0x08, 0x08);
                cnt--;
                buffer[cnt] = 0;
            }
            break;

        case 0x00: /* Arrow key. */
        case 0xe0:
            /* Skip the next character as well. */
            _gettch();
            break;

        case 0x0d: /* CR */
        case 0x0a: /* LF */
            /* Done */
            break;

        default:
            if (cnt < 64) {
                /* For now, ignore any non-standard ascii characters. */
                if ((c >= 0x20) && (c < 0x7f)) {
                    if (wrapperData->ntServicePasswordPromptMask) {
                        _tprintf(TEXT("*"));
                    } else {
                        _tprintf(TEXT("%c"), c);
                    }
                    buffer[cnt] = c;
                    buffer[cnt + 1] = 0;
                    cnt++;
                }
            }
            break;
        }
        /*printf("(%02x)", c);*/
    } while ((c != 0x0d) && (c != 0x0a));
    _tprintf(TEXT("\n"));

    return buffer;
}

/**
 * RETURNS TRUE if the current Windows OS supports SHA-2 code-signning certificates
 */
BOOL isSHA2CertificateSupported() {
    OSVERSIONINFOEX osver;

    osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

#pragma warning(push)
#pragma warning(disable : 4996) /* Visual Studio 2013 deprecates GetVersionEx but we still want to use it. */
    if (GetVersionEx((LPOSVERSIONINFO)&osver) && osver.dwPlatformId == VER_PLATFORM_WIN32_NT &&
        (osver.dwMajorVersion >= 6 || /* Windows Vista/Windows Server 2008 and higher */
        (osver.dwMajorVersion == 5 && osver.dwMinorVersion == 1 && osver.wServicePackMajor == 3))) { /* Windows XP SP3 (there is no SP3 and thus no SHA-2 support for Win XP 64-bit), Windows server 2003 is also not supported. */
        return TRUE;
    }
#pragma warning(pop)
    return FALSE;
}

/**
 * RETURNS TRUE if the current Windows OS is Windows 10 or higher...
 */
BOOL isWin10OrHigher() {
    OSVERSIONINFO osver;

    osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

#pragma warning(push)
#pragma warning(disable : 4996) /* Visual Studio 2013 deprecates GetVersionEx but we still want to use it. */
    if (GetVersionEx(&osver) &&
            osver.dwPlatformId == VER_PLATFORM_WIN32_NT &&
            osver.dwMajorVersion >= 10) {
        return TRUE;
    }
#pragma warning(pop)
    return FALSE;
}

/**
 * RETURNS TRUE if the current Windows OS is Windows Vista or later...
 */
BOOL isVista() {
    OSVERSIONINFO osver;

    osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

#pragma warning(push)
#pragma warning(disable : 4996) /* Visual Studio 2013 deprecates GetVersionEx but we still want to use it. */
    if (GetVersionEx(&osver) &&
            osver.dwPlatformId == VER_PLATFORM_WIN32_NT &&
            osver.dwMajorVersion >= 6) {
        return TRUE;
    }
#pragma warning(pop)
    return FALSE;
}

/**
 * RETURNS TRUE if the current Windows OS is Windows XP or later...
 */
BOOL isWinXP() {
    OSVERSIONINFO osver;

    osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

#pragma warning(push)
#pragma warning(disable : 4996) /* Visual Studio 2013 deprecates GetVersionEx but we still want to use it. */
    if (GetVersionEx(&osver) && osver.dwPlatformId == VER_PLATFORM_WIN32_NT) {
        if (osver.dwMajorVersion > 5 || osver.dwMajorVersion == 5 && osver.dwMinorVersion >= 1) {
            return TRUE;
        }
    }
#pragma warning(pop)
    return FALSE;
}


BOOL isSecondary() {
    return (getStringProperty(properties, TEXT("wrapper.internal.namedpipe"), NULL) != NULL) ? TRUE : FALSE;
}

BOOL isElevated() {
    TOKEN_ELEVATION te = {0};
    BOOL bIsElevated = FALSE;
    HRESULT hResult = E_FAIL; // assume an error occurred
    HANDLE hToken   = NULL;
    DWORD dwReturnLength = 0;
    if (isVista()) {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
            return bIsElevated ;
        }
        if (!GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &dwReturnLength)) {
            ;
        } else {
            hResult = te.TokenIsElevated ? S_OK : S_FALSE;
            bIsElevated = (te.TokenIsElevated != 0);
        }
        CloseHandle(hToken);
        return bIsElevated;
    } else {
        return TRUE;
    }
}


void wrapperCheckForMappedDrives() {
    TCHAR **propertyNames;
    TCHAR **propertyValues;
    long unsigned int *propertyIndices;
    int i;
    int advice = 0;
    if (!wrapperData->ntServiceAccount) {
        advice = wrapperGetUNCFilePath(getFileSafeStringProperty(properties, TEXT("wrapper.logfile"), TEXT("wrapper.log")), advice);
        advice = wrapperGetUNCFilePath(getFileSafeStringProperty(properties, TEXT("wrapper.logfile.purge.pattern"), TEXT("")), advice);
        advice = wrapperGetUNCFilePath(getFileSafeStringProperty(properties, TEXT("wrapper.pidfile"), NULL), advice);
        advice = wrapperGetUNCFilePath(getFileSafeStringProperty(properties, TEXT("wrapper.java.pidfile"), NULL), advice);
        advice = wrapperGetUNCFilePath(getFileSafeStringProperty(properties, TEXT("wrapper.lockfile"), NULL), advice);
        advice = wrapperGetUNCFilePath(getFileSafeStringProperty(properties, TEXT("wrapper.java.idfile"), NULL), advice);
        advice = wrapperGetUNCFilePath(getFileSafeStringProperty(properties, TEXT("wrapper.statusfile"), NULL), advice);
        advice = wrapperGetUNCFilePath(getFileSafeStringProperty(properties, TEXT("wrapper.java.statusfile"), NULL), advice);
        advice = wrapperGetUNCFilePath(getFileSafeStringProperty(properties, TEXT("wrapper.commandfile"), NULL), advice);
        advice = wrapperGetUNCFilePath(getFileSafeStringProperty(properties, TEXT("wrapper.anchorfile"), NULL), advice);
        i = 0;
        if (getStringProperties(properties, TEXT("wrapper.java.library.path."), TEXT(""), wrapperData->ignoreSequenceGaps, FALSE, &propertyNames, &propertyValues, &propertyIndices)) {
                /* Failed */
            return ;
        }
        while (propertyNames[i]) {
            if (propertyValues[i]) {
                advice = wrapperGetUNCFilePath(propertyValues[i], advice);
                i++;
            }

        }
        i = 0;
        if (getStringProperties(properties, TEXT("wrapper.java.classpath."), TEXT(""), wrapperData->ignoreSequenceGaps, FALSE, &propertyNames, &propertyValues, &propertyIndices)) {
                /* Failed */
            return ;
        }
        while (propertyNames[i]) {
            if (propertyValues[i]) {
                advice = wrapperGetUNCFilePath(propertyValues[i], advice);
                i++;
            }
        }
    }
}

/**
 * Generates the full binary path to register with the service manager when
 *  installing a service.
 *
 * @param buffer Buffer that will hold the binaryPath.  If NULL, the required
 *               length will be calculated and stored in reqBufferSize
 * @param reqBufferSize Pointer to an int that will store the required length in character
 *                      of the buffer that was used or is required.
 *
 * @return 0 if succeeded.
 */
int buildServiceBinaryPath(TCHAR *buffer, size_t *reqBufferLen) {
    DWORD moduleFileNameSize;
    TCHAR *moduleFileName;
    DWORD usedLen;
    TCHAR drive[4];
    TCHAR* uncTempBuffer;
    DWORD uncSize;
    int pathMapped;
    int pathMapFailed = FALSE;
    UNIVERSAL_NAME_INFO* unc;
    int i;
    int k;
    size_t originalSize;

    if (reqBufferLen) {
        originalSize = *reqBufferLen;
    } else {
        originalSize = 0;
    }

    /* We will calculate the size used. */
    if (buffer) {
        buffer[0] = TEXT('\0');
    }
    *reqBufferLen = 1;
    /* Get the full path and filename of this program.  Need to loop to make sure we get it all. */
    moduleFileNameSize = 0;
    moduleFileName = NULL;
    do {
        moduleFileNameSize += 100;
        moduleFileName = malloc(sizeof(TCHAR) * moduleFileNameSize);
        if (!moduleFileName) {
            outOfMemory(TEXT("BSBP"), 1);
            return 1;
        }

        /* On Windows XP and 2000, GetModuleFileName will return exactly "moduleFileNameSize" and
         *  leave moduleFileName in an unterminated state in the event that the module file name is too long.
         *  Newer versions of Windows will set the error code to ERROR_INSUFFICIENT_BUFFER but we can't rely on that. */
        /* Important : For win XP getLastError() is unchanged if the buffer is too small, so if we don't reset the last error first, we may actually test an old pending error. */
        SetLastError(ERROR_SUCCESS);
        usedLen = GetModuleFileName(NULL, moduleFileName, moduleFileNameSize);
        if (usedLen == 0) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to resolve the full Wrapper path - %s"), getLastErrorText());
            return 1;
        } else if ((usedLen == moduleFileNameSize) || (getLastError() == ERROR_INSUFFICIENT_BUFFER)) {
            /* Buffer too small.  Loop again. */
            free(moduleFileName);
            moduleFileName = NULL;
        }
    } while (!moduleFileName);
    /* Always start with the full path to the binary. */
    /* If the moduleFileName contains spaces, it needs to be quoted */
    /* Resolve to UNC-Name if we are on a mapped drive */
    if ((_tcslen(moduleFileName) >= 3) && (moduleFileName[1] == TEXT(':')) && (moduleFileName[2] == TEXT('\\'))) {
        _tcsncpy(drive, moduleFileName, 3);
        drive[3] = TEXT('\0');
    } else {
        drive[0] = TEXT('\0');
    }
    pathMapped = FALSE;
    if ((drive[0] != TEXT('\0')) && (GetDriveType(drive) == DRIVE_REMOTE)) {
        /* The Wrapper binary is located on a Network Drive.  Try to resolve the original Universal path.  We need to get a buffer big enough. */
        uncSize = 0;
        moduleFileNameSize = 100;
        do{
            uncTempBuffer = malloc((moduleFileNameSize) * sizeof(TCHAR));
            if (!uncTempBuffer) {
                outOfMemory(TEXT("BSBP"), 2);
                return 1;
            }
            unc = (UNIVERSAL_NAME_INFO *) uncTempBuffer;
            k = WNetGetUniversalName(moduleFileName, UNIVERSAL_NAME_INFO_LEVEL, unc, &moduleFileNameSize);
            if (k == ERROR_MORE_DATA) {
                free(uncTempBuffer);
            }
        } while (k == ERROR_MORE_DATA);
        uncSize = moduleFileNameSize;
        if (k != NO_ERROR) {
            if (buffer) { /* Otherwise logged on the next pass. */
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("Unable to resolve Universal Path of mapped network path: %s (%s)"), moduleFileName, getLastErrorText());
            }
            pathMapFailed = TRUE;
        } else {
            /* Now we know the size.  Create the unc buffer. */
            if (_tcschr(unc->lpUniversalName, TEXT(' ')) == NULL) {
                if (buffer) {
                    _tcsncat(buffer, unc->lpUniversalName, originalSize);
                }
            *reqBufferLen += _tcslen(unc->lpUniversalName);
            } else {
                if (buffer) {
                    _tcsncat(buffer, TEXT("\""), originalSize);
                    _tcsncat(buffer, unc->lpUniversalName, originalSize);
                    _tcsncat(buffer, TEXT("\""), originalSize);
                }
                *reqBufferLen += (1 + _tcslen(unc->lpUniversalName) + 1);
            }
            pathMapped = TRUE;
            free(uncTempBuffer);
        }
    }

    if (!pathMapped) {
        if (_tcschr(moduleFileName, TEXT(' ')) == NULL) {
            if (buffer) {
                _tcsncat(buffer, moduleFileName, originalSize);
            }
            *reqBufferLen += _tcslen(moduleFileName);
        } else {
            if (buffer) {
                _tcsncat(buffer, TEXT("\""), originalSize);
                _tcsncat(buffer, moduleFileName, originalSize);
                _tcsncat(buffer, TEXT("\""), originalSize);
            }
            *reqBufferLen += (1 + _tcslen(moduleFileName) + 1);
        }
    }
    free(moduleFileName);

    /* Next write the command to start the service. */
    if (buffer) {
        _tcsncat(buffer, TEXT(" -s "), originalSize);
    }
    *reqBufferLen += 4;

    /* Third, the configuration file. */
    /* If the wrapperData->configFile contains spaces, it needs to be quoted */
    /* Try to convert the config file to a UNC path as well. */
    if (!wrapperData->configFile) {
        if (buffer) {
            _tcsncat(buffer, TEXT("-"), originalSize);
        }
        *reqBufferLen += 1;
    } else {
        if ((_tcslen(wrapperData->configFile) >= 3) && (wrapperData->configFile[1] == TEXT(':')) && (wrapperData->configFile[2] == TEXT('\\'))) {
            _tcsncpy(drive, wrapperData->configFile, 3);
            drive[3] = TEXT('\0');
        } else {
            drive[0] = TEXT('\0');
        }
        pathMapped = FALSE;
        if ((drive[0] != TEXT('\0')) && (GetDriveType(drive) == DRIVE_REMOTE)) {
            /* The Wrapper config file is located on a Network Drive.  Try to resolve the original Universal path.  We need to get a buffer big enough. */
            moduleFileNameSize = 100;
            uncSize = 0;
            do {
                uncTempBuffer = malloc((moduleFileNameSize) * sizeof(TCHAR));
                if (!uncTempBuffer) {
                    outOfMemory(TEXT("BSBP"), 3);
                    return 1;
                }

                unc = (UNIVERSAL_NAME_INFO *) uncTempBuffer;

                k = WNetGetUniversalName(wrapperData->configFile, UNIVERSAL_NAME_INFO_LEVEL, unc, &moduleFileNameSize);
                if (k == ERROR_MORE_DATA) {
                    free(uncTempBuffer);
                }
            } while (k == ERROR_MORE_DATA);
            if (k != NO_ERROR) {
                if (buffer) { /* Otherwise logged on the next pass. */
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("Unable to resolve Universal Path of mapped network path: %s (%s)"), wrapperData->configFile, getLastErrorText());
                }
                pathMapFailed = TRUE;
            } else {
               /* Now we know the size.  Create the unc buffer. */
                if (_tcschr(unc->lpUniversalName, TEXT(' ')) == NULL) {
                    if (buffer) {
                        _tcsncat(buffer, unc->lpUniversalName, originalSize);
                    }
                    *reqBufferLen += _tcslen(unc->lpUniversalName);
                } else {
                    if (buffer) {
                        _tcsncat(buffer, TEXT("\""), originalSize);
                        _tcsncat(buffer, unc->lpUniversalName, originalSize);
                        _tcsncat(buffer, TEXT("\""), originalSize);
                    }
                    *reqBufferLen += (1 + _tcslen(unc->lpUniversalName) + 1);
                }
                pathMapped = TRUE;
                free(uncTempBuffer);
                unc = NULL;
            }
        }
        if (!pathMapped) {
            if (_tcschr(wrapperData->configFile, TEXT(' ')) == NULL) {
                if (buffer) {
                    _tcsncat(buffer, wrapperData->configFile, originalSize);
                }
                *reqBufferLen += _tcslen(wrapperData->configFile);
            } else {
                if (buffer) {
                    _tcsncat(buffer, TEXT("\""), originalSize);
                    _tcsncat(buffer, wrapperData->configFile, originalSize);
                    _tcsncat(buffer, TEXT("\""), originalSize);
                }
                *reqBufferLen += (1 + _tcslen(wrapperData->configFile) + 1);
            }
        }

        if (pathMapFailed) {
            if (buffer) { /* Otherwise logged on the next pass. */
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("There were problems converting mapped network paths the Universal Path format.  This may cause the service to fail to start now or when the system is rebooted."));
            }
        }
    }

    i = 0;
    if ((wrapperData->argCount >= 1) && (_tcsstr(wrapperData->argValues[0], TEXT("wrapper.internal.namedpipe=")) != NULL)) {
        /* This property is used by wrapperm. It should not be copied to the service command line. */
        i++;
    }

    /* All other arguments need to be appended as is. */
    for (; i < wrapperData->argCount; i++) {
        /* For security reasons, skip the wrapper.ntservice.account and
         *  wrapper.ntservice.password properties if they are declared on the
         *  command line.  They will not be needed  once the service is
         *  installed.  Having them in the registry would be an obvious
         *  security leak. */
        if ((_tcsstr(wrapperData->argValues[i], TEXT("wrapper.ntservice.account")) == NULL) &&
            (_tcsstr(wrapperData->argValues[i], TEXT("wrapper.ntservice.password")) == NULL)) {
            if (buffer) {
                _tcsncat(buffer, TEXT(" "), originalSize);
            }
            *reqBufferLen += 1;

            /* If the argument contains spaces, it needs to be quoted */
            if (_tcschr(wrapperData->argValues[i], TEXT(' ')) == NULL) {
                if (buffer) {
                    _tcsncat(buffer, wrapperData->argValues[i], originalSize);
                }
                *reqBufferLen += _tcslen(wrapperData->argValues[i]);
            } else {
                if (buffer) {
                    _tcsncat(buffer, TEXT("\""), originalSize);
                    _tcsncat(buffer, wrapperData->argValues[i], originalSize);
                    _tcsncat(buffer, TEXT("\""), originalSize);
                }
                *reqBufferLen += 1 + _tcslen(wrapperData->argValues[i]) + 1;
            }
        }
    }

    /* If there are any passthrough variables.  Then they also need to be appended as is. */
    if (wrapperData->javaArgValueCount > 0) {
        if (buffer) {
            _tcsncat(buffer, TEXT(" --"), originalSize);
        }
        *reqBufferLen += 3;

        for (i = 0; i < wrapperData->javaArgValueCount; i++) {
            if (buffer) {
                _tcsncat(buffer, TEXT(" "), originalSize);
            }
            *reqBufferLen += 1;

            /* If the argument contains spaces, it needs to be quoted */
            if (_tcschr(wrapperData->javaArgValues[i], TEXT(' ')) == NULL) {
                if (buffer) {
                    _tcsncat(buffer, wrapperData->javaArgValues[i], originalSize);
                }
                *reqBufferLen += _tcslen(wrapperData->javaArgValues[i]);
            } else {
                if (buffer) {
                    _tcsncat(buffer, TEXT("\""), originalSize);
                    _tcsncat(buffer, wrapperData->javaArgValues[i], originalSize);
                    _tcsncat(buffer, TEXT("\""), originalSize);
                }
                *reqBufferLen += (1 + _tcslen(wrapperData->javaArgValues[i]) + 1);
            }
        }
    }
    return 0;
}

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS  ((NTSTATUS)0x00000000L)
#endif

void InitLsaString(PLSA_UNICODE_STRING LsaString, LPWSTR String) {
    size_t StringLength;

    if (String == NULL) {
        LsaString->Buffer = NULL;
        LsaString->Length = 0;
        LsaString->MaximumLength = 0;
        return;
    }

    StringLength = wcslen(String);
    LsaString->Buffer = String;
    LsaString->Length = (USHORT) StringLength * sizeof(WCHAR);
    LsaString->MaximumLength=(USHORT)(StringLength+1) * sizeof(WCHAR);
}

NTSTATUS OpenPolicy(LPWSTR ServerName, DWORD DesiredAccess, PLSA_HANDLE PolicyHandle) {
    LSA_OBJECT_ATTRIBUTES ObjectAttributes;
    LSA_UNICODE_STRING ServerString;
    PLSA_UNICODE_STRING Server = NULL;

    ZeroMemory(&ObjectAttributes, sizeof(ObjectAttributes));

    if (ServerName != NULL) {
        InitLsaString(&ServerString, ServerName);
        Server = &ServerString;
    }

    return LsaOpenPolicy(Server, &ObjectAttributes, DesiredAccess, PolicyHandle);
}


/*
 * Checks if pc is part of Domain, workgroup or standalone 
 * @returns 1 if it's part of Domain, 2 for workgroup, 3 for stand alone, 0 if there was an error
 */
int checkDomain() {
    LSA_HANDLE PolicyHandle;
    NTSTATUS status;
    PPOLICY_PRIMARY_DOMAIN_INFO ppdiDomainInfo;
    PWKSTA_INFO_100 pwkiWorkstationInfo;
    DWORD netret;
    wchar_t* ResName;
    int ret = 0;

    netret = NetWkstaGetInfo(NULL, 100, (LPBYTE *)&pwkiWorkstationInfo);
#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("checkDomain: NetWkstaGetInfo returned %d"), netret);
#endif
    if (netret == NERR_Success) {
        status = OpenPolicy(NULL, GENERIC_READ | POLICY_VIEW_LOCAL_INFORMATION, &PolicyHandle);
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("checkDomain: OpenPolicy returned %d\n"), status);
        if (!status) {
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("checkDomain: LsaQueryInformationPolicy call ahead"));
#endif
            status = LsaQueryInformationPolicy(PolicyHandle, PolicyPrimaryDomainInformation, &ppdiDomainInfo);
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("checkDomain: LsaQueryInformationPolicy returned %d"), status);
#endif
            if (!status) {
#ifdef _DEBUG
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, 
                TEXT("checkDomain: LsaQueryInformationPolicy:ppdiDomainInfo->maxlen = %d, len=%d, buffer=%s, strlen=%d"),
                ppdiDomainInfo->Name.MaximumLength,ppdiDomainInfo->Name.Length ,ppdiDomainInfo->Name.Buffer, wcslen(ppdiDomainInfo->Name.Buffer));
#endif
                ResName = malloc((wcslen(ppdiDomainInfo->Name.Buffer) + 1 ) * sizeof(wchar_t));
                if (ResName) {
                    _tcsncpy(ResName, ppdiDomainInfo->Name.Buffer, wcslen(ppdiDomainInfo->Name.Buffer) + 1);
                    if (ppdiDomainInfo->Sid) {
                        ret = 1;
                    } else {
#ifdef _DEBUG
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, 
                            TEXT("checkDomain: comparing %s vs. %s"), ResName,
                            pwkiWorkstationInfo->wki100_computername);
#endif
                        if (_tcsncmp(ResName, pwkiWorkstationInfo->wki100_computername,
                                     wcslen(pwkiWorkstationInfo->wki100_computername))) {
                            ret = 2;
                        } else {
                           ret = 3;
                       }
                    }
                    free(ResName);
                }
                LsaFreeMemory((LPVOID)ppdiDomainInfo);
            }
            LsaClose(PolicyHandle);
        }
        NetApiBufferFree(pwkiWorkstationInfo);
    }
    return ret;
}


/**
 *  Helperfunction which gets the Security Policy Handle of the specified system
 *  @param referencedDomainName, the system of which the Security Policy Handle should get retrieved
 *
 *  @return the Handle of the Security Policy, NULL in case of any error
 */
LSA_HANDLE wrapperGetPolicyHandle(TCHAR* referencedDomainName) {
    NTSTATUS ntsResult;
    LSA_HANDLE lsahPolicyHandle;
    int k;

    k = checkDomain();
#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("checkDomain returns %d."), k);
#endif
    if (k > 0) {
        if (k > 1) {
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("OpenPolicy call %s "), referencedDomainName);
#endif
            ntsResult = OpenPolicy(referencedDomainName,    /* Name of the target system. */
                              POLICY_LOOKUP_NAMES | POLICY_CREATE_ACCOUNT, /* Desired access permissions. */
                              &lsahPolicyHandle); /*Receives the policy handle. */
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("OpenPolicy returns %d."), ntsResult);
#endif
        } else {
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("OpenPolicy call NULL."));
#endif
            ntsResult = OpenPolicy(NULL,    /* Name of the target system. */
                              POLICY_LOOKUP_NAMES | POLICY_CREATE_ACCOUNT, /* Desired access permissions. */
                              &lsahPolicyHandle); /*Receives the policy handle. */
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("OpenPolicy returns %d."), ntsResult);
#endif
        }
        if (ntsResult != STATUS_SUCCESS) {
            /* An error occurred. Display it as a win32 error code. */
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("OpenPolicy failed %lu"), LsaNtStatusToWinError(ntsResult));
            return NULL;
        }
    }
    return lsahPolicyHandle;
}

/**
 *  Helperfunction which gets the SID and domain of a given account name
 *  @param lpszAccountName, the account namespace
 *  @param referencedDomainName, output buffer for the domain
 *
 *  @return the SID of the account, 0 in case of any error
 */
PSID wrapperLookupName(LPCTSTR lpszAccountName, WCHAR **referencedDomainName) {
    PSID         Sid;
    DWORD        cbReferencedDomainName, cbSid, lastError;
    SID_NAME_USE eUse;  
    LPCTSTR formattedAccountName;

#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("lookupname: %s"), lpszAccountName);
#endif
    if (_tcsstr(lpszAccountName, TEXT(".\\")) == lpszAccountName) {
        formattedAccountName = lpszAccountName + 2;
    } else { 
        formattedAccountName= lpszAccountName;
    }

#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("lookupname:formatedname %s"), formattedAccountName);
#endif

    cbReferencedDomainName = cbSid = 0;
    if (LookupAccountName(NULL, formattedAccountName, NULL, &cbSid, NULL, &cbReferencedDomainName, &eUse)) {
        /* A straight success - that can't be... */
        return 0;
    }
    lastError = GetLastError();
    if (lastError != ERROR_INSUFFICIENT_BUFFER) {
        /* Any error except the one above is fatal.. */
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Failed to lookup the account (%s): %d - %s"), lpszAccountName, lastError, getLastErrorText());
        return 0;
    }
#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("lookupname:cbSID %d ; cbDomain %d"), cbSid, cbReferencedDomainName);
#endif
    if (!(Sid = (PSID)malloc(cbSid))) {
        outOfMemory(TEXT("WLN"), 1);
        return 0;
    }

    *referencedDomainName = (LPTSTR)calloc((cbReferencedDomainName ), sizeof(TCHAR));
    if (!(*referencedDomainName)) {
        LocalFree(Sid);
        outOfMemory(TEXT("WLN"), 2);
        return 0;
    }
    if (!LookupAccountName(NULL, formattedAccountName, Sid, &cbSid, *referencedDomainName, &cbReferencedDomainName, &eUse)) {
        free(*referencedDomainName);
        free(Sid);
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Failed to lookup the account (%s): %d - %s"), lpszAccountName, lastError, getLastErrorText());
        return 0;
    }
#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("lookupname:cbreferencedDomain %s"), *referencedDomainName);
#endif
    return Sid;
}

/**
 * This functions adds the Logon as Service privileges to the user account
 *
 * @param the account name for which the privilege should be added.
 *
 * @return FALSE if successful, TRUE otherwise
 */
BOOL wrapperAddPrivileges(TCHAR *account) {
    PLSA_UNICODE_STRING pointer;
    NTSTATUS ntsResult;
    LSA_HANDLE PolicyHandle;
    PSID AccountSID;
    TCHAR *referencedDomainName;
    ULONG counter = 1;
    WCHAR privileges[] = SE_SERVICE_LOGON_NAME;
    int retVal = TRUE;

    AccountSID = wrapperLookupName(account, &referencedDomainName);

    if (AccountSID) {
        if ((PolicyHandle = wrapperGetPolicyHandle(referencedDomainName)) != NULL) {
            /* Create an LSA_UNICODE_STRING for the privilege names. */
            pointer = malloc(sizeof(LSA_UNICODE_STRING));
            if (pointer == NULL) {
                outOfMemory(TEXT("WAP"), 1);
            } else {
                InitLsaString(pointer, privileges);
                ntsResult = LsaAddAccountRights(PolicyHandle, /* An open policy handle. */
                                            AccountSID, /* The target SID. */
                                            pointer, /* The privileges. */
                                            counter); /* Number of privileges. */
                free(pointer);
                if (ntsResult == STATUS_SUCCESS) {
                    retVal =  FALSE;
                } else {
                   log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Failed to add Logon As Service Permission: %lu"), LsaNtStatusToWinError(ntsResult));
                }
            }
            LsaClose(PolicyHandle);
        } 
        free(AccountSID);
        free(referencedDomainName);
    } 
    return retVal;
} 

static int setupSyslogRegistration(int silent) {
    int result = 0;
    
    if (getSyslogRegister()) {
        /* don't even check if the registration was made, force installation in case some key or value were out of date. */
        if (!silent) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Registering to the Event Log system..."));
        }
        result = registerSyslogMessageFile(TRUE, FALSE);
    } else if (!silent) {
        disableSysLog(TRUE);
        /* it can be useful to deactivate the registration from the configuration file, especially if the setup include more tasks in the future. */
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Do not register to the Event Log because the property wrapper.syslog.ident.enable is set to FALSE."));
    }
    return result;
}

static int teardownSyslogRegistration(int silent) {
    int result = 0;
    
    /* always make sure to clean the registry when calling teardown. */
    if (syslogMessageFileRegistered(FALSE)) {
        if (!silent) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Unregistering from the Event Log system..."));
        }
        
        /* set the syslog level to NONE to avoid a warning in disableSysLog(). */
        setSyslogLevelInt(LEVEL_NONE);
        result = unregisterSyslogMessageFile(FALSE);
    } else if (!silent) {
        disableSysLog(TRUE);
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("%s was not registered to the Event Log system."), getSyslogEventSourceName());
    }
    return result;
}

/**
 * Setup the Wrapper
 *  Execute installation tasks that require to be elevated.
 *
 * @param silent can be used to skip INFO messages (errors & warnings will still be shown).
 *
 * Returns 1 if there were any problems.
 */
int wrapperSetup(int silent) {
    int result = setupSyslogRegistration(silent);

    /* more setup actions can be added here. */
    
    if ((result == 0) && (!silent)) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Setup done successfully."));
    }
    return result;
}

/**
 * Teardown the Wrapper
 *  Execute deletion tasks that require to be elevated.
 *
 * @param silent can be used to skip INFO messages (errors & warnings will still be shown).
 *
 * Returns 1 if there were any problems.
 */
int wrapperTeardown(int silent) {
    int result = teardownSyslogRegistration(silent);

    /* more teardown actions can be added here. */
    
    if ((result == 0) && (!silent)) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Teardown done successfully."));
    }
    return result;
}

/**
 * Install the Wrapper as an NT Service using the information and service
 *  name in the current configuration file.
 *
 * Stores the parameters with the service name so that the wrapper.conf file
 *  can be located at runtime.
 */
int wrapperInstall() {
    SC_HANDLE schService;
    SC_HANDLE schSCManager;
    DWORD serviceType;
    DWORD startType;
#ifdef SUPPORT_PRESHUTDOWN
    SERVICE_PRESHUTDOWN_INFO preShutdownInfo;
#endif
    size_t binaryPathLen;
    TCHAR *binaryPath;
    int result = 0;
    HKEY hKey;
    TCHAR regPath[ 1024 ];
    TCHAR domain[ 1024 ];
    TCHAR account[ 1024 ];
    TCHAR *tempAccount;
    TCHAR *ntServicePassword = NULL;
    DWORD dsize = 1024, dwDesiredAccess;
    HANDLE hToken;
    LPCWSTR lpszUsername;
    LPCWSTR lpszDomain;
    DWORD error;
    size_t len;
    int exit = FALSE;

    /* Initialization */
    dwDesiredAccess = 0;

    /* Before prompting any info, check if the service is already installed. */
    if (wrapperServiceStatus(wrapperData->serviceName, wrapperData->serviceDisplayName, FALSE) & 0x1) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to install the %s service - %s"),
            wrapperData->serviceDisplayName, getErrorText(ERROR_SERVICE_EXISTS, NULL));
        return 1;
    }

    /* Generate the service binary path.  We need to figure out how big the buffer needs to be. */
    if (buildServiceBinaryPath(NULL, &binaryPathLen)) {
        /* Failed a reason should have been given. But show result. */
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to install the %s service"), wrapperData->serviceDisplayName);
        return 1;
    }

    binaryPath = malloc(binaryPathLen * sizeof(TCHAR));
    if (!binaryPath) {
        outOfMemory(TEXT("WI"), 1);
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to install the %s service"), wrapperData->serviceDisplayName);
        return 1;
    }
    if (buildServiceBinaryPath(binaryPath, &binaryPathLen)) {
        /* Failed a reason should have been given. But show result. */
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to install the %s service"), wrapperData->serviceDisplayName);
        free(binaryPath);
        return 1;
    }

    if (wrapperData->isDebugging) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Service command: %s"), binaryPath);
    }
    if (wrapperData->ntServicePrompt) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Prompting for account (DOMAIN\\ACCOUNT)..."));
        _tprintf(TEXT("Please input the domain name [%s]: "), wrapperData->domainName);
        if (isElevated() && isSecondary()) {
           _tprintf(TEXT("n"));
           fflush(NULL);
        }
        _fgetts(domain, dsize, stdin);
        if (!domain || _tcscmp(domain, TEXT("\n")) == 0) {
            _sntprintf(domain, dsize, TEXT("%s"), wrapperData->domainName);
        } else if (domain[_tcslen(domain) - 1] == TEXT('\n')) {
            domain[_tcslen(domain) - 1] = TEXT('\0');
        }

        _tprintf(TEXT("Please input the account name [%s]: "), wrapperData->userName);
        if (isElevated() && isSecondary()) {
           _tprintf(TEXT("n"));
           fflush(NULL);
        }
        _fgetts(account, dsize, stdin);
        if (!account || _tcscmp(account, TEXT("\n")) == 0) {
            _sntprintf(account, dsize, TEXT("%s"), wrapperData->userName);
        } else if (account[_tcslen(account) - 1] == TEXT('\n')) {
            account[_tcslen(account) - 1] = TEXT('\0');
        }
        tempAccount = malloc((_tcslen(domain) + _tcslen(account) + 2) * sizeof(TCHAR));
        if (!tempAccount) {
            outOfMemory(TEXT("WI"), 2);
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to install the %s service"), wrapperData->serviceDisplayName);
            free(binaryPath);
            return 1;
        }
        _sntprintf(tempAccount, _tcslen(domain) + _tcslen(account) + 2, TEXT("%s\\%s"), domain, account);
        updateStringValue(&wrapperData->ntServiceAccount, tempAccount);
        free(tempAccount);
    }

    if (wrapperData->ntServiceAccount) {
        /* Check that the account is correctly formatted and retrieve the domain and username. */
        lpszUsername = _tcschr(wrapperData->ntServiceAccount, TEXT('\\'));
        if (lpszUsername && _tcslen(lpszUsername) > 1) {
            lpszUsername++;
            len = _tcslen(wrapperData->ntServiceAccount) - _tcslen(lpszUsername) - 1;
            if ((len < 1) || (len > 1023)) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Invalid format for the account name '%s'. Please use {Domain}\\{UserName} or .\\{UserName}."), wrapperData->ntServiceAccount);
                return 1;
            } else if ((len == 1) && (lpszUsername[0] == TEXT('.'))) {
                lpszDomain = NULL;
            } else {
                _tcsncpy(domain, wrapperData->ntServiceAccount, len);
                domain[len] = 0;
                lpszDomain = domain;
            }
        } else {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Invalid format for the account name '%s'. Please use {Domain}\\{UserName} or .\\{UserName}."), wrapperData->ntServiceAccount);
            return 1;
        }

        if (wrapperData->ntServicePasswordPrompt) {
            /* Prompt the user for a password. */
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Prompting for account password..."));
            _tprintf(TEXT("Please input the password for account '%s': "), wrapperData->ntServiceAccount);
            if (isElevated() && isSecondary()) {
                _tprintf(TEXT("p"));
                fflush(NULL);
                /* as this here is from the secondary instance we can read with _fgetts */
                wrapperData->ntServicePassword = calloc(65, sizeof(TCHAR));
                if (!wrapperData->ntServicePassword) {
                    outOfMemory(TEXT("WI"), 3);
                    free(binaryPath);
                    return 1;
                }
                _fgetts(wrapperData->ntServicePassword, 65, stdin);
            } else {
                wrapperData->ntServicePassword = readPassword();
            }
        }

        /* Make sure that an empty length password is null. */
        ntServicePassword = wrapperData->ntServicePassword;
        if ((ntServicePassword != NULL) && (_tcslen(ntServicePassword) <= 0)) {
            ntServicePassword = NULL;
        }

        /* Now that we got the domain, username and password, try to authenticate to detect any logon failure during installation. */
        /* NOTE: if the password is NULL, the LogonUserW function will return the following message:
         *  "Account restrictions are preventing this user from signing in. For example: blank passwords aren't allowed,
         *  sign-in times are limited, or a policy restriction has been enforced."
         * => it may be possible to change the policy restriction, and failing with this error is just fine, so let
         *    the LogonUserW function decide what to do with blank passwords. */
        if (!LogonUserW(lpszUsername, lpszDomain, ntServicePassword, LOGON32_LOGON_SERVICE, LOGON32_PROVIDER_DEFAULT, &hToken)) {
            error = GetLastError();
            exit = getBooleanProperty(properties, TEXT("wrapper.ntservice.authentication_strict"), TRUE);
            switch(error) {
            case 0x569:
                log_printf(WRAPPER_SOURCE_WRAPPER, exit ? LEVEL_FATAL : LEVEL_ERROR, TEXT("The account '%s' must have the service privilege enabled. Please add the user in the 'Log on as a service' policy of your Security Policy Settings."), wrapperData->ntServiceAccount);
                break;
                
            default:
                log_printf(WRAPPER_SOURCE_WRAPPER, exit ? LEVEL_FATAL : LEVEL_ERROR, TEXT("Authentication failure - %s"), getErrorText(error, NULL));
                if ((GetKeyState(VK_CAPITAL) & 0x0001) != 0) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("  Caps Lock is On!"), getErrorText(error, NULL));
                }
            }
        }
        if (hToken) {
            CloseHandle(hToken);
        }
        if (exit) {
            wrapperSecureFreeStrW(wrapperData->ntServicePassword);
            wrapperData->ntServicePassword = NULL;
            return 1;
        }
    }

    /* Decide on the service type */
    if (wrapperData->ntServiceInteractive) {
        serviceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
    } else {
        serviceType = SERVICE_WIN32_OWN_PROCESS;
    }

    /* Next, get a handle to the service control manager */
    schSCManager = OpenSCManager(
            NULL,
            NULL,
            SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE
    );

    if (schSCManager) {
        if (wrapperData->ntServiceAccount && wrapperAddPrivileges(wrapperData->ntServiceAccount)) {
            /* adding failed it was reported already above */
        }

        startType = wrapperData->ntServiceStartType;

        if (result != 1) {
#ifdef SUPPORT_PRESHUTDOWN
            if (wrapperData->ntPreshutdown) {
                dwDesiredAccess |= SERVICE_CHANGE_CONFIG;
            }
#endif
            schService = CreateService(schSCManager, /* SCManager database */
                    wrapperData->serviceName, /* name of service */
                    wrapperData->serviceDisplayName, /* name to display */
                    dwDesiredAccess, /* desired access */
                    serviceType, /* service type */
                    startType, /* start type */
                    SERVICE_ERROR_NORMAL, /* error control type */
                    binaryPath, /* service's binary */
                    wrapperData->ntServiceLoadOrderGroup, /* load ordering group */
                    NULL, /* tag identifier not used because they are used for driver level services. */
                    wrapperData->ntServiceDependencies, /* dependencies */
                    wrapperData->ntServiceAccount, /* LocalSystem account if NULL */
                    ntServicePassword); /* NULL or empty for no password */
            
            if (wrapperData->ntServicePassword) {
                /* Clear the password from memory */
                wrapperSecureFreeStrW(wrapperData->ntServicePassword);
                wrapperData->ntServicePassword = NULL;
            }

            if (schService) {
                /* Have the service, add a description to the registry. */
                _sntprintf(regPath, 1024, TEXT("SYSTEM\\CurrentControlSet\\Services\\%s"), wrapperData->serviceName);
                if ((wrapperData->serviceDescription != NULL && _tcslen(wrapperData->serviceDescription) > 0)
                        && (RegOpenKeyEx(HKEY_LOCAL_MACHINE, regPath, 0, KEY_WRITE, (PHKEY) &hKey) == ERROR_SUCCESS)) {

                    /* Set Description key in registry */
                    RegSetValueEx(hKey, TEXT("Description"), (DWORD) 0, (DWORD) REG_SZ,
                            (LPBYTE)wrapperData->serviceDescription,
                            (int)(sizeof(TCHAR) * (_tcslen(wrapperData->serviceDescription) + 1)));
                    RegCloseKey(hKey);
                }
#ifdef SUPPORT_PRESHUTDOWN
                if (result == 0 && wrapperData->ntPreshutdown) {
                    preShutdownInfo.dwPreshutdownTimeout = wrapperData->ntPreshutdownTimeout * 1000;
                    /* Lets always update the preshutdown timeout as future versions of Windows might use a different default value. */
                    if (!ChangeServiceConfig2(schService, SERVICE_CONFIG_PRESHUTDOWN_INFO, &preShutdownInfo)) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to set the preshutdown timeout of the %s service. - %s"),
                                wrapperData->serviceDisplayName, getLastErrorText());
                        wrapperRemove();
                        result = 1;
                    }
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Set the preshutdown timeout of the %s service to %d secs."),
                            wrapperData->serviceDisplayName, wrapperData->ntPreshutdownTimeout);
                }
#endif

                if (result !=1) {
                    /* Service was installed. */
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s service installed."),
                            wrapperData->serviceDisplayName);
                }
                /* Close the handle to this service object */
                CloseServiceHandle(schService);
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to install the %s service - %s"),
                        wrapperData->serviceDisplayName, getLastErrorText());
                result = 1;
            }
        }

        /* Close the handle to the service control manager database */
        CloseServiceHandle(schSCManager);
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to install the %s service - %s"),
                wrapperData->serviceDisplayName, getLastErrorText());
        if (isVista() && !isElevated()) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Performing this action requires that you run as an elevated process."));
        }
        result = 1;
    }

    if (wrapperData->ntServicePassword) {
        /* Clear the password from memory (if not done yet) */
        wrapperSecureFreeStrW(wrapperData->ntServicePassword);
        wrapperData->ntServicePassword = NULL;
    }

    return result;
}

void closeRegistryKey(HKEY hKey) {
    LONG result;
    LPSTR pBuffer = NULL;

    result = RegCloseKey(hKey);
    if (result != ERROR_SUCCESS) {
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, result, 0, (LPTSTR)&pBuffer, 0, NULL);
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to close the registry: %d : %s"), result, pBuffer);
        LocalFree(pBuffer);
    }
}

/**
 * Sets any environment variables stored in the system registry to the current
 *  environment.  The NT service environment only has access to the environment
 *  variables set when the machine was last rebooted.  This makes it possible
 *  to access the latest values in registry without a reboot.
 *
 * Note that this function is always called before the configuration file has
 *  been loaded this means that any logging that takes place will be sent to
 *  the default log file which may be difficult for the user to locate.
 *
 * Return TRUE if there were any problems.
 */
int wrapperLoadEnvFromRegistryInner(HKEY baseHKey, const TCHAR *regPath, int appendPath, int source) {
    LONG result;
    LPSTR pBuffer = NULL;
    int envCount = 0;
    int ret;
    HKEY hKey;
    DWORD dwIndex;
    DWORD valueCount;
    DWORD maxValueNameLength;
    DWORD maxValueLength;
    TCHAR *valueName;
    TCHAR *value;
    DWORD thisValueNameLength;
    DWORD thisValueLength;
    DWORD thisValueType;
    const TCHAR *oldVal;
    TCHAR *newVal;
    BOOL expanded;

    /* NOTE - Any log output here will be placed in the default log file as it happens
     *        before the wrapper.conf is loaded. */

    /* Open the registry entry where the current environment variables are stored. */
    result = RegOpenKeyEx(baseHKey, regPath, 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, (PHKEY)&hKey);
    if (result == ERROR_SUCCESS) {
        /* Read in each of the environment variables and set them into the environment.
         *  These values will be set as is without doing any environment variable
         *  expansion.  In order for the ExpandEnvironmentStrings function to work all
         *  of the environment variables to be replaced must already be set.  To handle
         *  this, after we set the values as is from the registry, we need to go back
         *  through all the ones we set and Expand them if necessary. */

        /* Query the registry to find out how many values there are as well as info about how
         *  large the values names and data are. */
        result = RegQueryInfoKey(hKey, NULL, NULL, NULL, NULL, NULL, NULL, &valueCount, &maxValueNameLength, &maxValueLength, NULL, NULL);
        if (result != ERROR_SUCCESS) {
            FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, result, 0, (LPTSTR)&pBuffer, 0, NULL);
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to query the registry to get the environment: %d : %s"), result, pBuffer);
            LocalFree(pBuffer);
            closeRegistryKey(hKey);
            return TRUE;
        }

#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Registry contains %d variables.  Longest name=%d, longest value=%d"), valueCount, maxValueNameLength, maxValueLength);
#endif
        /* Add space for the null. */
        maxValueNameLength++;
        maxValueLength++;

        /* Allocate buffers to get the value names and values from the registry.  These can
         *  be reused because we are using the setEnv function to store the values into the
         *  environment.  setEnv allocates the memory required by the environment. */
        valueName = malloc(sizeof(TCHAR) * maxValueNameLength);
        if (!valueName) {
            outOfMemory(TEXT("WLEFRI"), 1);
            closeRegistryKey(hKey);
            return TRUE;
        }
        value = malloc(sizeof(TCHAR) * maxValueLength);
        if (!valueName) {
            outOfMemory(TEXT("WLEFRI"), 2);
            closeRegistryKey(hKey);
            return TRUE;
        }

        /* Loop over the values and load each of them into the local environment as is. */
        dwIndex = 0;
        do {
            thisValueNameLength = maxValueNameLength;
            thisValueLength = maxValueLength;

            result = RegEnumValue(hKey, dwIndex, valueName, &thisValueNameLength, NULL, &thisValueType, (LPBYTE)value, &thisValueLength);
            if (result == ERROR_SUCCESS) {
                if ((thisValueType == REG_SZ) || (thisValueType = REG_EXPAND_SZ)) {
                    /* Got a value. */
#ifdef _DEBUG
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Loaded var name=\"%s\", value=\"%s\""), valueName, value);
#endif
                    if (appendPath && (strcmpIgnoreCase(TEXT("path"), valueName) == 0)) {
                        /* The PATH variable is special, it needs to be appended to the existing value. */
                        oldVal = _tgetenv(TEXT("PATH"));
                        if (oldVal) {
                            newVal = malloc(sizeof(TCHAR) * (_tcslen(oldVal) + 1 + _tcslen(value) + 1));
                            if (!newVal) {
                                outOfMemory(TEXT("WLEFRI"), 3);
                                closeRegistryKey(hKey);
                                return TRUE;
                            }
                            _sntprintf(newVal, _tcslen(oldVal) + 1 + _tcslen(value) + 1, TEXT("%s;%s"), oldVal, value);
                            if (setEnv(valueName, newVal, source)) {
                                /* Already reported. */
                                free(newVal);
                                closeRegistryKey(hKey);
                                return TRUE;
                            }
#ifdef _DEBUG
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("    Appended to existing value: %s=%s"), valueName, newVal);
#endif
                            free(newVal);
                        } else {
                            /* Did not exist, set normally. */
                            if (setEnv(valueName, value, source)) {
                                /* Already reported. */
                                closeRegistryKey(hKey);
                                return TRUE;
                            }
                        }
                    } else {
                        if (setEnv(valueName, value, source)) {
                            /* Already reported. */
                            closeRegistryKey(hKey);
                            return TRUE;
                        }
                    }
#ifdef _DEBUG
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Set to local environment."));
#endif
                } else {
#ifdef _DEBUG
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Loaded var name=\"%s\" but type is invalid: %d, skipping."), valueName, thisValueType);
#endif
                }
            } else if (result = ERROR_NO_MORE_ITEMS) {
                /* This means we are at the end.  Fall through. */
            } else {
                FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, result, 0, (LPTSTR)&pBuffer, 0, NULL);
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to query the registry to get environment variable #%d: %d : %s"), dwIndex, result, getLastErrorText());
                LocalFree(pBuffer);
                closeRegistryKey(hKey);
                return TRUE;
            }

            dwIndex++;
        } while (result != ERROR_NO_MORE_ITEMS);

#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("All environment variables loaded.  Loop back over them to evaluate any nested variables."));
#endif
        /* Go back and loop over the environment variables we just set and expand any
         *  variables which contain % characters. Loop until we make a pass which does
         *  not perform any replacements. */
        do {
            expanded = FALSE;

            dwIndex = 0;
            do {
                thisValueNameLength = maxValueNameLength;
                result = RegEnumValue(hKey, dwIndex, valueName, &thisValueNameLength, NULL, &thisValueType, NULL, NULL);
                if (result == ERROR_SUCCESS) {
                    /* Found an environment variable in the registry.  Variables that contain references have a different type. */
                    if (thisValueType = REG_EXPAND_SZ) {
#ifdef _DEBUG
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Get the current local value of variable \"%s\""), valueName);
#endif
                        oldVal = _tgetenv(valueName);
                        if (oldVal == NULL) {
#ifdef _DEBUG
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  The current local value of variable \"%s\" is null, meaning it was not in the registry.  Skipping."), valueName);
#endif
                        } else {
#ifdef _DEBUG
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("     \"%s\"=\"%s\""), valueName, oldVal);
#endif
                            if (_tcschr(oldVal, TEXT('%'))) {
                                /* This variable contains tokens which need to be expanded. */
                                /* Find out how much space is required to store the expanded value. */
                                ret = ExpandEnvironmentStrings(oldVal, NULL, 0);
                                if (ret == 0) {
                                    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                                        /* The ExpandEnvironmentStrings function has an internal 32k size limit.  We hit it.
                                         *  All we can do is skip this particular variable by leaving it unexpanded. */
                                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("Unable to expand environment variable \"%s\" because the result is larger than the system allowed 32k.  Leaving unexpanded and continuing."), valueName);
                                    } else {
                                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to expand environment variable \"%s\": %s"), valueName, getLastErrorText());
                                        closeRegistryKey(hKey);
                                        return TRUE;
                                    }
                                } else {
                                    /* Allocate a buffer to hold to the expanded value. */
                                    newVal = malloc(sizeof(TCHAR) * (ret + 2));
                                    if (!newVal) {
                                        outOfMemory(TEXT("WLEFRI"), 4);
                                        closeRegistryKey(hKey);
                                        return TRUE;
                                    }

                                    /* Actually expand the variable. */
                                    ret = ExpandEnvironmentStrings(oldVal, newVal, ret + 2);
                                    if (ret == 0) {
                                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to expand environment variable \"%s\" (2): %s"), valueName, getLastErrorText());
                                        free(newVal);
                                        closeRegistryKey(hKey);
                                        return TRUE;
                                    }

                                    /* Was anything changed? */
                                    if (_tcscmp(oldVal, newVal) == 0) {
#ifdef _DEBUG
                                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("       Value unchanged.  Referenced environment variable not set."));
#endif
                                    } else {
                                        /* Set the expanded environment variable */
                                        expanded = TRUE;
#ifdef _DEBUG
                                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Update local environment variable.  \"%s\"=\"%s\""), valueName, newVal);
#endif

                                        /* Update the environment. */
                                        if (setEnv(valueName, newVal, source)) {
                                            /* Already reported. */
                                            free(newVal);
                                            closeRegistryKey(hKey);
                                            return TRUE;
                                        }
                                    }
                                    free(newVal);
                                }
                            }
                        }
                    }
                } else if (result == ERROR_NO_MORE_ITEMS) {
                    /* No more environment variables. */
                } else {
                    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, result, 0, (LPTSTR)&pBuffer, 0, NULL);
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to read registry - %s"), getLastErrorText());
                    LocalFree(pBuffer);
                    closeRegistryKey(hKey);
                    return TRUE;
                }
                dwIndex++;
            } while (result != ERROR_NO_MORE_ITEMS);

#ifdef _DEBUG
            if (expanded) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Rescan environment variables to varify that there are no more expansions necessary."));
            }
#endif
        } while (expanded);

#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Done loading environment variables."));
#endif

        /* Close the registry entry */
        closeRegistryKey(hKey);
    } else {
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, result, 0, (LPTSTR)&pBuffer, 0, NULL);
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to access registry to obtain environment variables - %s"), getLastErrorText());
        LocalFree(pBuffer);
        return TRUE;
    }

    return FALSE;
}

/**
 * Loads the environment stored in the registry.
 *
 * (Only called for versions of Windows older than Vista or Server 2008.)
 *
 * Return TRUE if there were any problems.
 */
int wrapperLoadEnvFromRegistry() {
    /* We can't access any properties here as they are not yet loaded when called. */
    /* Always load in the system wide variables. */
#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Loading System environment variables from Registry:"));
#endif

    if (wrapperLoadEnvFromRegistryInner(HKEY_LOCAL_MACHINE, TEXT("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment\\"), FALSE, ENV_SOURCE_REG_SYSTEM)) {
        return TRUE;
    }

    /* Only load in the user specific variables if the USERNAME environment variable is set. */
    if (_tgetenv(TEXT("USERNAME"))) {
#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Loading Account environment variables from Registry:"));
#endif

        if (wrapperLoadEnvFromRegistryInner(HKEY_CURRENT_USER, TEXT("Environment\\"), TRUE, ENV_SOURCE_REG_ACCOUNT)){
            return TRUE;
        }
    }

    return FALSE;
}

const TCHAR* getBaseKeyName(HKEY baseHKey) {
    if (baseHKey == HKEY_CLASSES_ROOT) {
        return TEXT("HKEY_CLASSES_ROOT");
    } else if (baseHKey == HKEY_CURRENT_CONFIG) {
        return TEXT("HKEY_CURRENT_CONFIG");
    } else if (baseHKey == HKEY_CURRENT_USER) {
        return TEXT("HKEY_CURRENT_USER");
    } else if (baseHKey == HKEY_LOCAL_MACHINE) {
        return TEXT("HKEY_LOCAL_MACHINE");
    } else if (baseHKey == HKEY_USERS) {
        return TEXT("HKEY_USERS");
    } else {
        return TEXT("");
    }
}

/**
 * Gets the JavaHome absolute path from the windows registry
 *  using the location of a specific JRE and the key containing the JavaHome path.
 */
TCHAR* wrapperGetJavaHomeFromWindowsRegistryUsingJavaHome(HKEY baseHKey, TCHAR* subKey, TCHAR* javahome, int verbose) {
    HKEY openHKey = NULL;   /* Will receive the handle to the opened registry key */
    const TCHAR* msg;
    LONG result;
    DWORD valueType;
    DWORD valueSize;
    TCHAR *value = NULL;

    /* Opens the Registry Key needed to query the JavaHome */
    result = RegOpenKeyEx(baseHKey, subKey, 0, KEY_QUERY_VALUE, &openHKey);
    if (result != ERROR_SUCCESS) {
        /* NOTE: on Windows 64-bit, if a 32-bit application tries to access HKLM\SOFTWARE\JavaSoft,
         *       it's actually redirected to HKLM\SOFTWARE\Wow6432Node\JavaSoft. This can be confusing
         *       for the user and it may be worth printing a different message for that case. */
        if (verbose) {
            msg = getErrorText(result, NULL);
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("Unable to access configured registry location \"%s\\%s\": %s"), getBaseKeyName(baseHKey), subKey, msg);
        }
        return NULL;
    }

    /* Queries for the JavaHome */
    result = RegQueryValueEx(openHKey, javahome, NULL, &valueType, NULL, &valueSize);
    if (result != ERROR_SUCCESS) {
        if (verbose) {
            msg = getErrorText(result, NULL);
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("Unable to access configured registry location \"%s\\%s\\%s\": %s"), getBaseKeyName(baseHKey), subKey, javahome, msg);
        }
        closeRegistryKey(openHKey);
        return NULL;
    }
    if (valueType != REG_SZ) {
        if (verbose) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("Configured registry location \"%s\\%s\\%s\" is not of type REG_SZ."), getBaseKeyName(baseHKey), subKey, javahome);
        }
        closeRegistryKey(openHKey);
        return NULL;
    }
    value = malloc(sizeof(TCHAR) * valueSize);
    if (!value) {
        outOfMemory(TEXT("WGJFWRUJH"), 1);
        closeRegistryKey(openHKey);
        return NULL;
    }
    result = RegQueryValueEx(openHKey, javahome, NULL, &valueType, (LPBYTE)value, &valueSize);
    if (result != ERROR_SUCCESS) {
        if (verbose) {
            msg = getErrorText(result, NULL);
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("Unable to access configured registry location \"%s\\%s\\%s\": %s"), getBaseKeyName(baseHKey), subKey, javahome, msg);
        }
        closeRegistryKey(openHKey);
        free(value);
        return NULL;
    }

    closeRegistryKey(openHKey);
    
    return value;
}

/**
 * Gets the JavaHome absolute path from the windows registry
 *  using the base location for JREs and a specific version.
 */
TCHAR* wrapperGetJavaHomeFromWindowsRegistryUsingVersion(TCHAR* subKeyJre, TCHAR* jreversion) {
    TCHAR subKey[512];      /* Registry subkey that jvm creates when is installed */

    _tcsncpy(subKey, subKeyJre, 512);
    _tcsncat(subKey, TEXT("\\"), 512);
    _tcsncat(subKey, jreversion, 512);

    return wrapperGetJavaHomeFromWindowsRegistryUsingJavaHome(HKEY_LOCAL_MACHINE, subKey, TEXT("JavaHome"), FALSE);
}

/**
 * Gets the JavaHome absolute path from the windows registry
 *  using the base location for JREs and the 'CurrentVersion'.
 */
TCHAR* wrapperGetJavaHomeFromWindowsRegistryUsingCurrentVersion(TCHAR* subKeyJre) {
    HKEY openHKey = NULL;   /* Will receive the handle to the opened registry key */
    TCHAR jreversion[10];   /* Will receive a registry value that has jvm version */
    LONG result;
    DWORD valueType;
    DWORD valueSize;

    /* Opens the Registry Key needed to query the jvm version */
    result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, subKeyJre, 0, KEY_QUERY_VALUE, &openHKey);
    if (result != ERROR_SUCCESS) {
        return NULL;
    }

    /* Queries for the jvm version */
    valueSize = sizeof(jreversion);
    result = RegQueryValueEx(openHKey, TEXT("CurrentVersion"), NULL, &valueType, (LPBYTE)jreversion, &valueSize);
    if (result != ERROR_SUCCESS) {
        closeRegistryKey(openHKey);
        return NULL;
    }

    closeRegistryKey(openHKey);
    
    return wrapperGetJavaHomeFromWindowsRegistryUsingVersion(subKeyJre, jreversion);
}

/**
 * Gets the JavaHome absolute path from the windows registry
 */
int wrapperGetJavaHomeFromWindowsRegistry(TCHAR *javaHome) {
    const TCHAR *prop;
    TCHAR *c;
    TCHAR subKey[512];  /* Registry subkey that jvm creates when is installed */
    TCHAR *valueKey;
    HKEY baseHKey;
    TCHAR *value;

    prop = getStringProperty(properties, TEXT("wrapper.registry.java_home"), NULL);
    if (prop) {
        /* A registry location was specified. */
        if (_tcsstr(prop, TEXT("HKEY_CLASSES_ROOT\\")) == prop) {
            baseHKey = HKEY_CLASSES_ROOT;
            _tcsncpy(subKey, prop + 18, 512);
        } else if (_tcsstr(prop, TEXT("HKEY_CURRENT_CONFIG\\")) == prop) {
            baseHKey = HKEY_CURRENT_CONFIG;
            _tcsncpy(subKey, prop + 20, 512);
        } else if (_tcsstr(prop, TEXT("HKEY_CURRENT_USER\\")) == prop) {
            baseHKey = HKEY_CURRENT_USER;
            _tcsncpy(subKey, prop + 18, 512);
        } else if (_tcsstr(prop, TEXT("HKEY_LOCAL_MACHINE\\")) == prop) {
            baseHKey = HKEY_LOCAL_MACHINE;
            _tcsncpy(subKey, prop + 19, 512);
        } else if (_tcsstr(prop, TEXT("HKEY_USERS\\")) == prop) {
            baseHKey = HKEY_USERS;
            _tcsncpy(subKey, prop + 11, 512);
        } else {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("wrapper.registry.java_home does not begin with a known root key: %s"), prop);
            return 0;
        }

        /* log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("subKey=%s"), subKey); */

        /* We need to split the value from the key.  Find the last \ */
        c = _tcsrchr(subKey, TEXT('\\'));
        if (!c) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                TEXT("wrapper.registry.java_home is an invalid key: %s"), prop);
            return 0;
        }
        valueKey = c + 1;
        /* Truncate the subKey. */
        *c = TEXT('\0');

        /*log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("subKey=%s valueKey=%s"), subKey, valueKey); */

        value = wrapperGetJavaHomeFromWindowsRegistryUsingJavaHome(baseHKey, subKey, valueKey, TRUE);
    } else {
        /* Look for the java_home in the default location (it is different for Java 9+ compared to older versions). */
        /* There can be a 'CurrentVersion' in each location, but give priority to recent JREs by first trying to search in the new location. */
        value = wrapperGetJavaHomeFromWindowsRegistryUsingCurrentVersion(TEXT("SOFTWARE\\JavaSoft\\JRE"));
        if (!value) {
            value = wrapperGetJavaHomeFromWindowsRegistryUsingCurrentVersion(TEXT("SOFTWARE\\JavaSoft\\Java Runtime Environment"));
        }
    }
    if (value) {
        /* Returns the JavaHome path */
        _tcsncpy(javaHome, value, 512);
        free(value);

        return 1;
    }
    return 0;
}

TCHAR *getNTServiceStatusName(int status) {
    TCHAR *name;
    switch(status) {
    case SERVICE_STOPPED:
        name = TEXT("STOPPED");
        break;
    case SERVICE_START_PENDING:
        name = TEXT("START_PENDING");
        break;
    case SERVICE_STOP_PENDING:
        name = TEXT("STOP_PENDING");
        break;
    case SERVICE_RUNNING:
        name = TEXT("RUNNING");
        break;
    case SERVICE_CONTINUE_PENDING:
        name = TEXT("CONTINUE_PENDING");
        break;
    case SERVICE_PAUSE_PENDING:
        name = TEXT("PAUSE_PENDING");
        break;
    case SERVICE_PAUSED:
        name = TEXT("PAUSED");
        break;
    default:
        name = TEXT("UNKNOWN");
        break;
    }
    return name;
}

/** Starts a Wrapper instance running as an NT Service. */
int wrapperStartService() {
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;
    SERVICE_STATUS serviceStatus;
    const TCHAR *path;
    TCHAR wrapperFullPath[FILEPATHSIZE] = TEXT("");
    TCHAR logFileFullPath[FILEPATHSIZE] = TEXT("");
    TCHAR defaultLogFileFullPath[FILEPATHSIZE] = TEXT("");

    TCHAR *status;
    int msgCntr;
    int stopping;
    int result;
    int errorCode;

    /* Wrapper binary. */
    path = wrapperData->argBinary;
    result = GetFullPathName(path, FILEPATHSIZE, wrapperFullPath, NULL);
    if (result >= FILEPATHSIZE) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("The full path of %s is too large. (%d)"), path, result);
        _tcsncpy(wrapperFullPath, path, FILEPATHSIZE);
    } else if (result == 0) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("Unable to resolve the full path of %s : %s"), path, getLastErrorText());
        _tcsncpy(wrapperFullPath, path, FILEPATHSIZE);
    }
    
    /* Log file path. */
    path = getLogfilePath();
    if (_tcslen(path) > 0) {
        /* The log file may have been set to an empty value to disable it. */
        result = GetFullPathName(path, FILEPATHSIZE, logFileFullPath, NULL);
        if (result >= FILEPATHSIZE) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("The full path of %s is too large. (%d)"), path, result);
            _tcsncpy(logFileFullPath, path, FILEPATHSIZE);
        } else if (result == 0) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("Unable to resolve the full path of %s : %s"), path, getLastErrorText());
            _tcsncpy(logFileFullPath, path, FILEPATHSIZE);
        }
    }

    /* Default Log file path. */
    path = getDefaultLogfilePath();
    result = GetFullPathName(path, FILEPATHSIZE, defaultLogFileFullPath, NULL);
    if (result >= FILEPATHSIZE) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("The full path of %s is too large. (%d)"), path, result);
        _tcsncpy(defaultLogFileFullPath, path, FILEPATHSIZE);
    } else if (result == 0) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("Unable to resolve the full path of %s : %s"), path, getLastErrorText());
        _tcsncpy(defaultLogFileFullPath, path, FILEPATHSIZE);
    }

    result = 0;

    /* First, get a handle to the service control manager */
    schSCManager = OpenSCManager(NULL,
                                 NULL,
                                 SC_MANAGER_CONNECT);
    if (schSCManager) {
        /* Next get the handle to this service... */
        schService = OpenService(schSCManager, wrapperData->serviceName, WRAPPER_SERVICE_START);

        if (schService) {
            if (QueryServiceStatus(schService, &serviceStatus)) {
                /* Make sure that the service is not already started. */
                if (serviceStatus.dwCurrentState == SERVICE_STOPPED) {
                    /* The service is stopped, so try starting it. */
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Starting the %s service..."),
                        wrapperData->serviceDisplayName);
                    if (StartService(schService, 0, NULL)) {
                        /* We will get here immediately if the service process was launched.
                         *  We still need to wait for it to actually start. */
                        msgCntr = 0;
                        stopping = FALSE;
                        do {
                            if (QueryServiceStatus(schService, &serviceStatus)) {
                                if (serviceStatus.dwCurrentState == SERVICE_STOP_PENDING) {
                                    if (!stopping) {
                                        stopping = TRUE;
                                        msgCntr = 5; /* Trigger a message */
                                    }
                                    if (msgCntr >= 5) {
                                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Stopping..."));
                                        msgCntr = 0;
                                    }
                                } else {
                                    if (msgCntr >= 5) {
                                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Waiting to start..."));
                                        msgCntr = 0;
                                    }
                                }
                                wrapperSleep(1000);
                                msgCntr++;
                            } else {
                                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                                    TEXT("Unable to query the status of the %s service - %s"),
                                    wrapperData->serviceDisplayName, getLastErrorText());
                                result = 1;
                                break;
                            }
                        } while ((serviceStatus.dwCurrentState != SERVICE_STOPPED)
                            && (serviceStatus.dwCurrentState != SERVICE_RUNNING)
                            && (serviceStatus.dwCurrentState != SERVICE_PAUSED));

                        /* Was the service started? */
                        if (serviceStatus.dwCurrentState == SERVICE_RUNNING) {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s service started."), wrapperData->serviceDisplayName);
                        } else if (serviceStatus.dwCurrentState == SERVICE_PAUSED) {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s service started but immediately paused.."), wrapperData->serviceDisplayName);
                        } else {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("The %s service was launched, but failed to start."),
                                wrapperData->serviceDisplayName);
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Please check the log file for more information: %s"), logFileFullPath);
                            result = 1;
                        }
                    } else {
                        errorCode = GetLastError();
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to start the %s service - %s"),
                            wrapperData->serviceDisplayName, getLastErrorText());
                        switch (errorCode)
                        {
                        case ERROR_ACCESS_DENIED:
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("--------------------------------------------------------------------") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("Advice:" ));
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("Usually when the Windows Service Manager does not have access to the Wrapper\nbinary, it is caused by a file permission problem preventing the user running\nthe Wrapper from accessing it. Please check the permissions on the file and\nits parent directories." ));
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("  Wrapper Binary : %s"), wrapperFullPath);
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("--------------------------------------------------------------------") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                            break;
                            
                        case ERROR_SERVICE_REQUEST_TIMEOUT:
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("--------------------------------------------------------------------") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("Advice:" ));
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("Usually when the Windows Service Manager times out waiting for the Wrapper\nprocess to launch it is caused by a file permission problem preventing the\nWrapper from reading its configuration file and/or writing to its log file.\nPlease check the permissions on both files and their parent directories.\nIf there are no messages in either the configured or default log file, please\nalso check the Windows Event Log." ));
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("  Configuration File  : %s"), wrapperData->argConfFile);
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("  Configured Log File : %s" ), logFileFullPath);
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("  Default Log File    : %s" ), defaultLogFileFullPath);
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("--------------------------------------------------------------------") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                            break;
                            
                        default:
                            break;
                        }
                        result = 1;
                    }
                } else {
                    status = getNTServiceStatusName(serviceStatus.dwCurrentState);
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("The %s service is already started with status: %s"),
                        wrapperData->serviceDisplayName, status);
                    result = 1;
                }
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to query the status of the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
                result = 1;
            }

            /* Close this service object's handle to the service control manager */
            CloseServiceHandle(schService);
        } else {
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to start the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
                if (isVista() && !isElevated()) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Performing this action requires that you run as an elevated process."));
                }
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("The %s service is not installed - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
            }
            result = 1;
        }

        /* Finally, close the handle to the service control manager's database */
        CloseServiceHandle(schSCManager);
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to start the %s service - %s"),
            wrapperData->serviceDisplayName, getLastErrorText());
        result = 1;
    }

    return result;
}

/** Stops a Wrapper instance running as an NT Service. */
int wrapperStopService(int command) {
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;
    SERVICE_STATUS serviceStatus;

    TCHAR *status;
    int msgCntr;
    int result = 0;

    /* First, get a handle to the service control manager */
    schSCManager = OpenSCManager(NULL,
                                 NULL,
                                 SC_MANAGER_CONNECT);
    if (schSCManager) {

        /* Next get the handle to this service... */
        schService = OpenService(schSCManager, wrapperData->serviceName, WRAPPER_SERVICE_STOP);

        if (schService) {
            /* Find out what the current status of the service is so we can decide what to do. */
            if (QueryServiceStatus(schService, &serviceStatus)) {
                if (serviceStatus.dwCurrentState == SERVICE_STOPPED) {
                    if (command) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("The %s service was not started."),
                            wrapperData->serviceDisplayName);
                    }
                } else {
                    if (serviceStatus.dwCurrentState == SERVICE_STOP_PENDING) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                            TEXT("The %s service was already in the process of stopping."),
                            wrapperData->serviceDisplayName);
                    } else {
                        /* Stop the service. */
                        if (ControlService(schService, SERVICE_CONTROL_STOP, &serviceStatus)) {
                            if (command) {
                                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Stopping the %s service..."),
                                    wrapperData->serviceDisplayName);
                            } else {
                                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Service is running.  Stopping it..."));
                            }
                        } else {
                            if (serviceStatus.dwCurrentState == SERVICE_START_PENDING) {
                                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                                    TEXT("The %s service was in the process of starting.  Stopping it..."),
                                    wrapperData->serviceDisplayName);
                            } else {
                                status = getNTServiceStatusName(serviceStatus.dwCurrentState);
                                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                                    TEXT("Attempt to stop the %s service failed.  Status: %s"),
                                    wrapperData->serviceDisplayName, status);
                                result = 1;
                            }
                        }
                    }
                    if (result == 0) {
                        /* Wait for the service to stop. */
                        msgCntr = 0;
                        do {
                            if (QueryServiceStatus(schService, &serviceStatus)) {
                                if (msgCntr >= 5) {
                                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Waiting to stop..."));
                                    msgCntr = 0;
                                }
                                wrapperSleep(1000);
                                msgCntr++;
                            } else {
                                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                                    TEXT("Unable to query the status of the %s service - %s"),
                                    wrapperData->serviceDisplayName, getLastErrorText());
                                result = 1;
                                break;
                            }
                        } while (serviceStatus.dwCurrentState != SERVICE_STOPPED);

                        if (serviceStatus.dwCurrentState == SERVICE_STOPPED) {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s service stopped."), wrapperData->serviceDisplayName);
                        } else {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Failed to stop the %s service."), wrapperData->serviceDisplayName);
                            result = 1;
                        }
                    }
                }
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to query the status of the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
                result = 1;
            }

            /* Close this service object's handle to the service control manager */
            CloseServiceHandle(schService);
        } else {
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to stop the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
                if (isVista() && !isElevated()) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Performing this action requires that you run as an elevated process."));
                }
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("The %s service is not installed - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
            }
            result = 1;
        }

        /* Finally, close the handle to the service control manager's database */
        CloseServiceHandle(schSCManager);
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to stop the %s service - %s"),
            wrapperData->serviceDisplayName, getLastErrorText());
        result = 1;
    }

    return result;
}

/** Pauses a Wrapper instance running as an NT Service. */
int wrapperPauseService() {
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;
    SERVICE_STATUS serviceStatus;

    TCHAR *status;
    int msgCntr;
    int result = 0;
    int ignore = FALSE;

    /* First, get a handle to the service control manager */
    schSCManager = OpenSCManager(NULL,
                                 NULL,
                                 SC_MANAGER_CONNECT);
    if (schSCManager) {
        /* Next get the handle to this service... */
        schService = OpenService(schSCManager, wrapperData->serviceName, WRAPPER_SERVICE_PAUSE_CONTINUE);

        if (schService) {
            /* Make sure that the service is in a state that can be paused. */
            if (QueryServiceStatus(schService, &serviceStatus)) {
                if (serviceStatus.dwCurrentState == SERVICE_STOPPED) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("The %s service was not started."),
                        wrapperData->serviceDisplayName);
                    result = 1;
                } else if (serviceStatus.dwCurrentState == SERVICE_STOP_PENDING) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service was in the process of stopping."),
                        wrapperData->serviceDisplayName);
                    result = 1;
                } else if (serviceStatus.dwCurrentState == SERVICE_PAUSE_PENDING) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service was in the process of being paused."),
                        wrapperData->serviceDisplayName);
                } else if (serviceStatus.dwCurrentState == SERVICE_PAUSED) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service was already paused."),
                        wrapperData->serviceDisplayName);
                    ignore = TRUE;
                } else {
                    /* The service is started, starting, or resuming, so try pausing it. */
                    if (ControlService(schService, SERVICE_CONTROL_PAUSE, &serviceStatus)) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Pausing the %s service..."),
                            wrapperData->serviceDisplayName);
                    } else if (!wrapperData->pausable) {
                        status = getNTServiceStatusName(serviceStatus.dwCurrentState);
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                            TEXT("The %s service is not allowed to be paused.  Status: %s"),
                            wrapperData->serviceDisplayName, status);
                        if (wrapperData->isAdviserEnabled) {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("--------------------------------------------------------------------") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("Advice:" ));
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("To be able to pause the service, please set 'wrapper.pausable=TRUE'\nand restart it." ),
                                wrapperData->serviceDisplayName);
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("--------------------------------------------------------------------") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                        }
                        result = 1;
                    } else {
                        status = getNTServiceStatusName(serviceStatus.dwCurrentState);
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                            TEXT("Attempt to pause the %s service failed.  Status: %s"),
                            wrapperData->serviceDisplayName, status);
                        result = 1;
                        if (wrapperData->isAdviserEnabled && strcmpIgnoreCase(status, TEXT("running")) == 0) {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("--------------------------------------------------------------------") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("Advice:" ));
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("The reason may be that the service was not restarted after setting\n'wrapper.pausable' to TRUE." ),
                                wrapperData->serviceDisplayName);
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                                TEXT("--------------------------------------------------------------------") );
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE, TEXT("") );
                        }
                    }
                }
                if ((!ignore) && (result == 0)) {
                    /* Wait for the service to pause. */
                    msgCntr = 0;
                    do {
                        if (QueryServiceStatus(schService, &serviceStatus)) {
                            if (msgCntr >= 5) {
                                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Waiting to pause..."));
                                msgCntr = 0;
                            }
                            wrapperSleep(1000);
                            msgCntr++;
                        } else {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                                TEXT("Unable to query the status of the %s service - %s"),
                                wrapperData->serviceDisplayName, getLastErrorText());
                            result = 1;
                            break;
                        }
                    } while (!((serviceStatus.dwCurrentState == SERVICE_PAUSED) || (serviceStatus.dwCurrentState == SERVICE_STOPPED)));

                    if (serviceStatus.dwCurrentState == SERVICE_PAUSED) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s service paused."), wrapperData->serviceDisplayName);
                    } else {
                        status = getNTServiceStatusName(serviceStatus.dwCurrentState);
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT(
                            "Failed to pause %s service.  Status: %s"),
                            wrapperData->serviceDisplayName, status);
                        result = 1;
                    }
                }
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to query the status of the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
                result = 1;
            }

            /* Close this service object's handle to the service control manager */
            CloseServiceHandle(schService);
        } else {
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to pause the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("The %s service is not installed - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
            }
            result = 1;
        }

        /* Finally, close the handle to the service control manager's database */
        CloseServiceHandle(schSCManager);
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to pause the %s service - %s"),
            wrapperData->serviceDisplayName, getLastErrorText());
        if (isVista() && !isElevated()) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Performing this action requires that you run as an elevated process."));
        }
        result = 1;
    }

    return result;
}

/** Resume a Wrapper instance running as an NT Service. */
int wrapperResumeService() {
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;
    SERVICE_STATUS serviceStatus;

    TCHAR *status;
    int msgCntr;
    int result = 0;
    int ignore = FALSE;

    /* First, get a handle to the service control manager */
    schSCManager = OpenSCManager(NULL,
                                 NULL,
                                 SC_MANAGER_CONNECT);
    if (schSCManager) {
        /* Next get the handle to this service... */
        schService = OpenService(schSCManager, wrapperData->serviceName, WRAPPER_SERVICE_PAUSE_CONTINUE);

        if (schService) {
            /* Make sure that the service is in a state that can be resumed. */
            if (QueryServiceStatus(schService, &serviceStatus)) {
                if (serviceStatus.dwCurrentState == SERVICE_STOPPED) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("The %s service was not started."),
                        wrapperData->serviceDisplayName);
                    result = 1;
                } else if (serviceStatus.dwCurrentState == SERVICE_STOP_PENDING) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service was in the process of stopping."),
                        wrapperData->serviceDisplayName);
                    result = 1;
                } else if (serviceStatus.dwCurrentState == SERVICE_PAUSE_PENDING) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service was in the process of being paused."),
                        wrapperData->serviceDisplayName);
                    result = 1;
                } else if (serviceStatus.dwCurrentState == SERVICE_CONTINUE_PENDING) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service was in the process of being resumed."),
                        wrapperData->serviceDisplayName);
                } else if (serviceStatus.dwCurrentState == SERVICE_RUNNING) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service is already running."),
                        wrapperData->serviceDisplayName);
                    ignore = TRUE;
                } else {
                    /* The service is paused, so try resuming it. */
                    if (ControlService(schService, SERVICE_CONTROL_CONTINUE, &serviceStatus)) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Resuming the %s service..."),
                            wrapperData->serviceDisplayName);
                    } else {
                        status = getNTServiceStatusName(serviceStatus.dwCurrentState);
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                            TEXT("Attempt to resume the %s service failed.  Status: %s"),
                            wrapperData->serviceDisplayName, status);
                        result = 1;
                    }
                }
                if ((!ignore) && (result == 0)) {
                    /* Wait for the service to resume. */
                    msgCntr = 0;
                    do {
                        if (QueryServiceStatus(schService, &serviceStatus)) {
                            if (msgCntr >= 5) {
                                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_INFO, TEXT("Waiting to resume..."));
                                msgCntr = 0;
                            }
                            wrapperSleep(1000);
                            msgCntr++;
                        } else {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                                TEXT("Unable to query the status of the %s service - %s"),
                                wrapperData->serviceDisplayName, getLastErrorText());
                            result = 1;
                            break;
                        }
                    } while (!((serviceStatus.dwCurrentState == SERVICE_RUNNING) || (serviceStatus.dwCurrentState == SERVICE_STOPPED)));

                    if (serviceStatus.dwCurrentState == SERVICE_RUNNING) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s service resumed."), wrapperData->serviceDisplayName);
                    } else {
                        status = getNTServiceStatusName(serviceStatus.dwCurrentState);
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT(
                            "Failed to resume %s service.  Status: %s"),
                            wrapperData->serviceDisplayName, status);
                        result = 1;
                    }
                }
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to query the status of the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
                result = 1;
            }

            /* Close this service object's handle to the service control manager */
            CloseServiceHandle(schService);
        } else {
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to resume the %s service - %s"),
                wrapperData->serviceDisplayName, getLastErrorText());
                if (isVista() && !isElevated()) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Performing this action requires that you run as an elevated process."));
                }
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("The %s service is not installed - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
            }
            result = 1;
        }

        /* Finally, close the handle to the service control manager's database */
        CloseServiceHandle(schSCManager);
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to resume the %s service - %s"),
            wrapperData->serviceDisplayName, getLastErrorText());
        result = 1;
    }

    return result;
}

int sendServiceControlCodeInner(int controlCode) {
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;
    SERVICE_STATUS serviceStatus;
    TCHAR *status;
    int result = 0;

    /* First, get a handle to the service control manager */
    schSCManager = OpenSCManager(NULL,
                                 NULL,
                                 SC_MANAGER_CONNECT);
    if (schSCManager) {
        /* Next get the handle to this service... */
        schService = OpenService(schSCManager, wrapperData->serviceName, WRAPPER_SERVICE_CONTROL_CODE);

        if (schService) {
            /* Make sure that the service is in a state that can be resumed. */
            if (QueryServiceStatus(schService, &serviceStatus)) {
                if (serviceStatus.dwCurrentState == SERVICE_STOPPED) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("The %s service was not started."),
                        wrapperData->serviceDisplayName);
                    result = 1;
                } else if (serviceStatus.dwCurrentState == SERVICE_STOP_PENDING) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service was in the process of stopping."),
                        wrapperData->serviceDisplayName);
                    result = 1;
                } else if (serviceStatus.dwCurrentState == SERVICE_PAUSED) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service was currently paused."),
                        wrapperData->serviceDisplayName);
                    result = 1;
                } else if (serviceStatus.dwCurrentState == SERVICE_PAUSE_PENDING) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service was in the process of being paused."),
                        wrapperData->serviceDisplayName);
                    result = 1;
                } else if (serviceStatus.dwCurrentState == SERVICE_CONTINUE_PENDING) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s service was in the process of being resumed."),
                        wrapperData->serviceDisplayName);
                } else {
                    /* The service is running, so try sending the code. */
                    if (ControlService(schService, controlCode, &serviceStatus)) {
                        result = 0;
                    } else {
                        status = getNTServiceStatusName(serviceStatus.dwCurrentState);
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR,
                            TEXT("Attempt to send the %s service control code %d failed.  Status: %s"),
                            wrapperData->serviceDisplayName, controlCode, status);
                        result = 1;
                    }
                }
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to query the status of the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
                result = 1;
            }

            /* Close this service object's handle to the service control manager */
            CloseServiceHandle(schService);
        } else {
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to send control code to the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("OpenService failed - %s"), getLastErrorText());
                if (isVista() && !isElevated()) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Performing this action requires that you run as an elevated process."));
                }
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("The %s service is not installed - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
            }
            result = 1;
        }

        /* Finally, close the handle to the service control manager's database */
        CloseServiceHandle(schSCManager);
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to send control code to the %s service - %s"),
            wrapperData->serviceDisplayName, getLastErrorText());
        result = 1;
    }

    return result;
}

/** Sends a service control code to a running as an NT Service. */
int wrapperSendServiceControlCode(TCHAR **argv, TCHAR *controlCodeS) {
    int controlCode;
    int result;

    /* Make sure the control code is valid. */
    if (controlCodeS == NULL) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Control code to send is missing."));
        wrapperUsage(argv[0]);
        return 1;
    }
    controlCode = _ttoi(controlCodeS);
    if ((controlCode < 128) || (controlCode > 255)) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("The service control code must be in the range 128-255."));
        return 1;
    }

    result = sendServiceControlCodeInner(controlCode);
    if (!result) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Sent the %s service control code %d."),
            wrapperData->serviceDisplayName, controlCode);
    }

    return result;
}

/**
 * Requests that the Wrapper perform a thread dump.
 */
int wrapperRequestThreadDump() {
    int result;

    if (wrapperData->threadDumpControlCode <= 0) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("The thread dump control code is disabled."));
        return 1;
    }

    result = sendServiceControlCodeInner(wrapperData->threadDumpControlCode);
    if (!result) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Requested that the %s service perform a thread dump."),
            wrapperData->serviceDisplayName);
    }

    return result;
}

/**
 * Obtains the current service status.
 * The returned result becomes the exitCode.  The exitCode is made up of
 *  a series of status bits:
 *
 * Bits:
 * 0: Service Installed. (1)
 * 1: Service Running. (2)
 * 2: Service Interactive. (4)
 * 3: Startup Mode: Auto. (8)
 * 4: Startup Mode: Manual. (16)
 * 5: Startup Mode: Disabled. (32)
 * 6: Service Running but Paused. (64)
 * 15: Error. (32768)
 */
int wrapperServiceStatus(const TCHAR* serviceName, const TCHAR* serviceDisplayName, int consoleOutput) {
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;
    SERVICE_STATUS serviceStatus;
    QUERY_SERVICE_CONFIG *pQueryServiceConfig;
    DWORD reqSize;

    int result = 0;


    schSCManager = OpenSCManager(NULL,
                                 NULL,
                                 SC_MANAGER_CONNECT);
    if (schSCManager) {

        /* Next get the handle to this service... */
        schService = OpenService(schSCManager, serviceName, WRAPPER_SERVICE_QUERY_STATUS);

        if (schService) {
            /* Service is installed, so set that bit. */
            if (consoleOutput) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                    TEXT("The %s Service is installed."), serviceDisplayName);
            }
            result |= 1;

            /* Get the service configuration. */
            QueryServiceConfig(schService, NULL, 0, &reqSize);
            pQueryServiceConfig = malloc(reqSize);
            if (!pQueryServiceConfig) {
                outOfMemory(TEXT("WSS"), 1);
                CloseServiceHandle(schSCManager);
                result |= 32768;
                return result;
            }
            if (QueryServiceConfig(schService, pQueryServiceConfig, reqSize, &reqSize)) {
                switch (pQueryServiceConfig->dwStartType) {
                case SERVICE_BOOT_START:   /* Possible? */
                case SERVICE_SYSTEM_START: /* Possible? */
                case SERVICE_AUTO_START:
                    if (consoleOutput) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Start Type: Automatic"));
                    }
                    result |= 8;
                    break;

                case SERVICE_DEMAND_START:
                    if (consoleOutput) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Start Type: Manual"));
                    }
                    result |= 16;
                    break;

                case SERVICE_DISABLED:
                    if (consoleOutput) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Start Type: Disabled"));
                    }
                    result |= 32;
                    break;

                default:
                    if (consoleOutput) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("  Start Type: Unknown"));
                    }
                    break;
                }

                if (pQueryServiceConfig->dwServiceType & SERVICE_INTERACTIVE_PROCESS) {
                    /* This is an interactive service, so set that bit. */
                    if (consoleOutput) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Interactive: Yes"));
                    }
                    result |= 4;
                } else {
                    if (consoleOutput) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Interactive: No"));
                    }
                }

                free(pQueryServiceConfig);
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to query the configuration of the %s service - %s"),
                    serviceDisplayName, getLastErrorText());
                result |= 32768;
            }

            /* Find out what the current status of the service is so we can decide what to do. */
            if (QueryServiceStatus(schService, &serviceStatus)) {
                if (serviceStatus.dwCurrentState == SERVICE_STOPPED) {
                    /* The service is stopped. */
                    if (consoleOutput) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Running: No"));
                    }
                } else {
                    /* Any other state, it is running. Set that bit. */
                    if (consoleOutput) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Running: Yes"));
                    }
                    result |= 2;

                    if (serviceStatus.dwCurrentState == SERVICE_PAUSED) {
                        if (consoleOutput) {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  Paused: Yes"));
                        }
                        result |= 64;
                    }
                }

            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to query the status of the %s service - %s"),
                    serviceDisplayName, getLastErrorText());
                result |= 32768;
            }

            /* Close this service object's handle to the service control manager */
            CloseServiceHandle(schService);
        } else {
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to query the status of the %s service - %s"),
                    serviceDisplayName, getLastErrorText());
                if (isVista() && !isElevated()) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Performing this action requires that you run as an elevated process."));
                }
                result |= 32768;
            } else {
                if (consoleOutput) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS,
                        TEXT("The %s Service is not installed."), serviceDisplayName);
                }
            }
        }

        /* Finally, close the handle to the service control manager's database */
        CloseServiceHandle(schSCManager);
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to query the status of the %s service - %s"),
            serviceDisplayName, getLastErrorText());
        result |= 32768;
    }

    return result;
}

/**
 * Uninstall the service and clean up
 */
int wrapperRemove() {
    SC_HANDLE   schService;
    SC_HANDLE   schSCManager;

    int result = 0;

    /* First attempt to stop the service if it is already running. */
    result = wrapperStopService(FALSE);
    if (result) {
        /* There was a problem stopping the service. */
        return result;
    }

    /* First, get a handle to the service control manager */
    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (schSCManager) {

        /* Next get the handle to this service... */
        schService = OpenService(schSCManager, wrapperData->serviceName, SERVICE_QUERY_STATUS | DELETE);

        if (schService) {
            /* Now try to remove the service... */
            if (DeleteService(schService)) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("%s service removed."), wrapperData->serviceDisplayName);
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to remove the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
                result = 1;
            }

            /* Close this service object's handle to the service control manager */
            CloseServiceHandle(schService);
        } else {
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to remove the %s service - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
                if (isVista() && !isElevated()) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Performing this action requires that you run as an elevated process."));
                }
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("The %s service is not installed - %s"),
                    wrapperData->serviceDisplayName, getLastErrorText());
            }
            result = 1;
        }

        /* Finally, close the handle to the service control manager's database */
        CloseServiceHandle(schSCManager);
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("Unable to remove the %s service - %s"),
            wrapperData->serviceDisplayName, getLastErrorText());
        result = 1;
    }

    /* Remove message file registration on service remove */
    if (result == 0) {
        /* Do this here to unregister the syslog on uninstall of a resource. */
        /* unregisterSyslogMessageFile(); */
    }
    return result;
}

/**
 * Sets the working directory to that of the current executable
 */
int setWorkingDir() {
    int size = 128;
    TCHAR* szPath = NULL;
    DWORD usedLen;
    int result;
    TCHAR* pos;

    /* How large a buffer is needed? The GetModuleFileName function doesn't tell us how much
     *  is needed, only if it is too short. */
    do {
        szPath = malloc(sizeof(TCHAR) * size);
        if (!szPath) {
            outOfMemory(TEXT("SWD"), 1);
            return 1;
        }
        /* Important : For win XP getLastError() is unchanged if the buffer is too small, so if we don't reset the last error first, we may actually test an old pending error. */
        SetLastError(ERROR_SUCCESS);
        usedLen = GetModuleFileName(NULL, szPath, size);
        if (usedLen == 0) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to get the path - %s"), getLastErrorText());
            return 1;
        } else if ((usedLen == size) || (getLastError() == ERROR_INSUFFICIENT_BUFFER)) {
            /* Too small. */
            size += 128;
            free(szPath);
            szPath = NULL;
        }
    } while (!szPath);

    /* The wrapperData->isDebugging flag will never be set here, so we can't really use it. */
#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Executable Name: %s"), szPath);
#endif
    /* To get the path, strip everything off after the last '\' */
    pos = _tcsrchr(szPath, TEXT('\\'));
    if (pos == NULL) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to extract path from: %s"), szPath);
        free(szPath);
        return 1;
    } else {
        /* Clip the path at the position of the last backslash */
        pos[0] = (TCHAR)0;
    }
    /* Set a variable to the location of the binary. */
    setEnv(TEXT("WRAPPER_BIN_DIR"), szPath, ENV_SOURCE_APPLICATION);
    result = wrapperSetWorkingDir(szPath, TRUE);
    free(szPath);
    return result;
}

/******************************************************************************
 * Main function
 *****************************************************************************/

/** Attempts to resolve the name of an exception.  Returns null if it is unknown. */
TCHAR* getExceptionName(DWORD exCode, int nullOnUnknown) {
    TCHAR *exName;

    switch (exCode) {
    case EXCEPTION_ACCESS_VIOLATION:
        exName = TEXT("EXCEPTION_ACCESS_VIOLATION");
        break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        exName = TEXT("EXCEPTION_ARRAY_BOUNDS_EXCEEDED");
        break;
    case EXCEPTION_BREAKPOINT:
        exName = TEXT("EXCEPTION_BREAKPOINT");
        break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        exName = TEXT("EXCEPTION_DATATYPE_MISALIGNMENT");
        break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
        exName = TEXT("EXCEPTION_FLT_DENORMAL_OPERAND");
        break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        exName = TEXT("EXCEPTION_FLT_DIVIDE_BY_ZERO");
        break;
    case EXCEPTION_FLT_INEXACT_RESULT:
        exName = TEXT("EXCEPTION_FLT_INEXACT_RESULT");
        break;
    case EXCEPTION_FLT_INVALID_OPERATION:
        exName = TEXT("EXCEPTION_FLT_INVALID_OPERATION");
        break;
    case EXCEPTION_FLT_OVERFLOW:
        exName = TEXT("EXCEPTION_FLT_OVERFLOW");
        break;
    case EXCEPTION_FLT_STACK_CHECK:
        exName = TEXT("EXCEPTION_FLT_STACK_CHECK");
        break;
    case EXCEPTION_FLT_UNDERFLOW:
        exName = TEXT("EXCEPTION_FLT_UNDERFLOW");
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        exName = TEXT("EXCEPTION_ILLEGAL_INSTRUCTION");
        break;
    case EXCEPTION_IN_PAGE_ERROR:
        exName = TEXT("EXCEPTION_IN_PAGE_ERROR");
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        exName = TEXT("EXCEPTION_INT_DIVIDE_BY_ZERO");
        break;
    case EXCEPTION_INT_OVERFLOW:
        exName = TEXT("EXCEPTION_INT_OVERFLOW");
        break;
    case EXCEPTION_INVALID_DISPOSITION:
        exName = TEXT("EXCEPTION_INVALID_DISPOSITION");
        break;
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        exName = TEXT("EXCEPTION_NONCONTINUABLE_EXCEPTION");
        break;
    case EXCEPTION_PRIV_INSTRUCTION:
        exName = TEXT("EXCEPTION_PRIV_INSTRUCTION");
        break;
    case EXCEPTION_SINGLE_STEP:
        exName = TEXT("EXCEPTION_SINGLE_STEP");
        break;
    case EXCEPTION_STACK_OVERFLOW:
        exName = TEXT("EXCEPTION_STACK_OVERFLOW");
        break;
    default:
        if (nullOnUnknown) {
            exName = NULL;
        } else {
            exName = TEXT("EXCEPTION_UNKNOWN");
        }
        break;
    }

    return exName;
}

/**
 * Logs some dump information to the log output and then generate a minidump file.
 */
int exceptionFilterFunction(PEXCEPTION_POINTERS exceptionPointers) {
    DWORD exCode;
    int i;
    size_t len;
    TCHAR curDir[MAX_PATH];
    TCHAR dumpFile[MAX_PATH];
    BOOL dumpSuccessful;
    HANDLE hDumpFile;
    SYSTEMTIME stLocalTime;
    MINIDUMP_EXCEPTION_INFORMATION expParam;
    int couldLoad;
    FARPROC miniDumpWriteDumpDyn;
    HMODULE dbgHelpDll;
    int isCustomized = FALSE;
    
    dbgHelpDll = LoadLibrary(TEXT("Dbghelp.dll"));
    if (dbgHelpDll == NULL) {
        couldLoad = FALSE;
    } else {
        miniDumpWriteDumpDyn = GetProcAddress(dbgHelpDll, "MiniDumpWriteDump");
        if(miniDumpWriteDumpDyn == NULL) {
            couldLoad = FALSE;
        } else {
            couldLoad = TRUE;
        }
    }
    
    /* Log any queued messages */
    maintainLogger();
    
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("--------------------------------------------------------------------") );
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("encountered a fatal error in Wrapper%s"), (isCustomized ? TEXT(" (Customized)") : TEXT("")));
    
    exCode = exceptionPointers->ExceptionRecord->ExceptionCode;
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  exceptionCode    = %s (%d)"), getExceptionName(exCode, FALSE), exCode);
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  exceptionFlag    = %s"),
            (exceptionPointers->ExceptionRecord->ExceptionFlags == EXCEPTION_NONCONTINUABLE ? TEXT("EXCEPTION_NONCONTINUABLE") : TEXT("EXCEPTION_NONCONTINUABLE_EXCEPTION")));
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  exceptionAddress = 0x%p"), exceptionPointers->ExceptionRecord->ExceptionAddress);
    if (exCode == EXCEPTION_ACCESS_VIOLATION) {
        switch(exceptionPointers->ExceptionRecord->ExceptionInformation[0]) {
        case 0:
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  Read access exception from 0x%p"), exceptionPointers->ExceptionRecord->ExceptionInformation[1]);
            break;
        case 1:
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  Write access exception to 0x%p"), exceptionPointers->ExceptionRecord->ExceptionInformation[1]);
            break;
        case 8:
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  DEP access exception to 0x%p"), exceptionPointers->ExceptionRecord->ExceptionInformation[1]);
            break;
        default:
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  Unexpected access exception to 0x%p (%d)"), exceptionPointers->ExceptionRecord->ExceptionInformation[1], exceptionPointers->ExceptionRecord->ExceptionInformation[0]);
            break;
        }
    } else {
        for (i = 0; i < (int)exceptionPointers->ExceptionRecord->NumberParameters; i++) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  exceptionInformation[%d] = %ld"), i,
                    exceptionPointers->ExceptionRecord->ExceptionInformation[i]);
        }
    }

    if (wrapperData) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  Wrapper Main Loop Status:"));
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("    Current Ticks: 0x%08x"), wrapperGetTicks());
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("    Wrapper State: %s"), wrapperGetWState(wrapperData->wState));
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("    Java State: %s (Timeout: 0x%08x)"), wrapperGetJState(wrapperData->jState), wrapperData->jStateTimeoutTicks);
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("    Exit Requested: %s"), (wrapperData->exitRequested ? TEXT("true") : TEXT("false")));
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("    Restart Mode: %d"), wrapperData->restartRequested);
    }

    /* Get the current directory. */
    len = GetCurrentDirectory(MAX_PATH, curDir);
    if (len == 0) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  Unable to request current directory.  %s"), getLastErrorText());
        _sntprintf(curDir, MAX_PATH, TEXT("."));
    }
    /* Generate the minidump. */
    GetLocalTime(&stLocalTime);

    _sntprintf(dumpFile, MAX_PATH, TEXT("wrapper-%s-%s-%s-%s%s-%04d%02d%02d%02d%02d%02d-%ld-%ld.dmp"),
            wrapperOS, wrapperArch, wrapperBits, wrapperVersion,
            (isCustomized ? TEXT("-customized") : TEXT("")),
            stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
            stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond,
            GetCurrentProcessId(), GetCurrentThreadId());
    if (couldLoad == TRUE) {
        hDumpFile = CreateFile(dumpFile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
        if (hDumpFile == INVALID_HANDLE_VALUE) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  Failed to create dump file:\n    %s\\%s : %s"), curDir, dumpFile, getLastErrorText());
        } else {

            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  Writing dump file: %s\\%s"), curDir, dumpFile);

            expParam.ThreadId = GetCurrentThreadId();
            expParam.ExceptionPointers = exceptionPointers;
            expParam.ClientPointers = TRUE;

            dumpSuccessful = (BOOL)miniDumpWriteDumpDyn(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpWithDataSegs, &expParam, NULL, NULL);
            FreeLibrary(dbgHelpDll);
            if (dumpSuccessful) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("    Dump completed."));
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  Please send the dump file to support@tanukisoftware.com along with\n    your wrapper.conf and wrapper.log files."));
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("    Failed to generate dump file.  %s"), getLastErrorText());
            }

        }
    } else {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  Please send the log file to support@tanukisoftware.com along with\n    your wrapper.conf file."));
    }
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("--------------------------------------------------------------------") );

    return EXCEPTION_EXECUTE_HANDLER;
}

LPWSTR AllocateAndCopyWideString(LPCWSTR inputString)
{
    LPWSTR outputString = NULL;

    outputString = (LPWSTR)LocalAlloc(LPTR,
        (wcslen(inputString) + 1) * sizeof(WCHAR));
    if (outputString != NULL)
    {
        lstrcpyW(outputString, inputString);
    }
    return outputString;
}

BOOL GetProgAndPublisherInfo(PCMSG_SIGNER_INFO pSignerInfo, PSPROG_PUBLISHERINFO Info) {
    DWORD n;
    BOOL fReturn = FALSE;
    PSPC_SP_OPUS_INFO OpusInfo = NULL;  
    DWORD dwData;
    BOOL fResult;
    
    __try {
        /* Loop through authenticated attributes and find
           SPC_SP_OPUS_INFO_OBJID OID. */
        for (n = 0; n < pSignerInfo->AuthAttrs.cAttr; n++) {           
            if (lstrcmpA(SPC_SP_OPUS_INFO_OBJID, pSignerInfo->AuthAttrs.rgAttr[n].pszObjId) == 0) {
                /* Get Size of SPC_SP_OPUS_INFO structure. */
                fResult = CryptDecodeObject(ENCODING,
                            SPC_SP_OPUS_INFO_OBJID,
                            pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].pbData,
                            pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].cbData,
                            0, NULL, &dwData);
                if (!fResult) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CryptDecodeObject failed with %x"), GetLastError());
                    __leave;
                }

                /* Allocate memory for SPC_SP_OPUS_INFO structure. */
                OpusInfo = (PSPC_SP_OPUS_INFO)LocalAlloc(LPTR, dwData);
                if (!OpusInfo) {
                    outOfMemory(TEXT("GPAPI"), 1);
                    __leave;
                }

                /* Decode and get SPC_SP_OPUS_INFO structure. */
                fResult = CryptDecodeObject(ENCODING,
                            SPC_SP_OPUS_INFO_OBJID,
                            pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].pbData,
                            pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].cbData,
                            0, OpusInfo, &dwData);
                if (!fResult) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CryptDecodeObject failed with %x"), GetLastError());
                    __leave;
                }

                /* Fill in Program Name if present. */
                if (OpusInfo->pwszProgramName) {
                    Info->lpszProgramName = AllocateAndCopyWideString(OpusInfo->pwszProgramName);
                } else {
                    Info->lpszProgramName = NULL;
                }
                /* Fill in Publisher Information if present. */
                if (OpusInfo->pPublisherInfo) {
                    switch (OpusInfo->pPublisherInfo->dwLinkChoice) {
                        case SPC_URL_LINK_CHOICE:
                            Info->lpszPublisherLink = AllocateAndCopyWideString(OpusInfo->pPublisherInfo->pwszUrl);
                            break;

                        case SPC_FILE_LINK_CHOICE:
                            Info->lpszPublisherLink = AllocateAndCopyWideString(OpusInfo->pPublisherInfo->pwszFile);
                            break;

                        default:
                            Info->lpszPublisherLink = NULL;
                            break;
                    }
                } else {
                    Info->lpszPublisherLink = NULL;
                }

                /* Fill in More Info if present. */
                if (OpusInfo->pMoreInfo) {
                    switch (OpusInfo->pMoreInfo->dwLinkChoice) {
                        case SPC_URL_LINK_CHOICE:
                            Info->lpszMoreInfoLink = AllocateAndCopyWideString(OpusInfo->pMoreInfo->pwszUrl);
                            break;

                        case SPC_FILE_LINK_CHOICE:
                            Info->lpszMoreInfoLink = AllocateAndCopyWideString(OpusInfo->pMoreInfo->pwszFile);
                            break;

                        default:
                            Info->lpszMoreInfoLink = NULL;
                            break;
                    }
                } else {
                    Info->lpszMoreInfoLink = NULL;
                }

                fReturn = TRUE;

                break; /* Break from for loop. */
            } /* lstrcmp SPC_SP_OPUS_INFO_OBJID */
        } /* for */
    }
    __finally {
        if (OpusInfo != NULL) LocalFree(OpusInfo);      
    }

    return fReturn;
}

BOOL GetDateOfTimeStamp(PCMSG_SIGNER_INFO pSignerInfo, SYSTEMTIME *st) {   
    BOOL fResult;
    FILETIME lft, ft;   
    DWORD dwData;
    BOOL fReturn = FALSE;
    DWORD n;
    /* Loop through authenticated attributes and find
       szOID_RSA_signingTime OID. */
    for (n = 0; n < pSignerInfo->AuthAttrs.cAttr; n++) {           
        if (lstrcmpA(szOID_RSA_signingTime, pSignerInfo->AuthAttrs.rgAttr[n].pszObjId) == 0) {               
            /* Decode and get FILETIME structure. */
            dwData = sizeof(ft);
            fResult = CryptDecodeObject(ENCODING,
                        szOID_RSA_signingTime,
                        pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].pbData,
                        pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].cbData,
                        0, (PVOID)&ft, &dwData);
            if (!fResult) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CryptDecodeObject failed with %x"),
                    GetLastError());
                break;
            }

            /* Convert to local time. */
            FileTimeToLocalFileTime(&ft, &lft);
            FileTimeToSystemTime(&lft, st);

            fReturn = TRUE;

            break; /* Break from for loop. */
                        
        } /* lstrcmp szOID_RSA_signingTime */
    } /* for */

    return fReturn;
}

BOOL GetTimeStampSignerInfo(PCMSG_SIGNER_INFO pSignerInfo, PCMSG_SIGNER_INFO *pCounterSignerInfo) {   
    PCCERT_CONTEXT pCertContext = NULL;
    BOOL fReturn = FALSE;
    BOOL fResult;       
    DWORD dwSize, n;   
   
    __try {
        *pCounterSignerInfo = NULL;

        /* Loop through unathenticated attributes for
           szOID_RSA_counterSign OID. */
        for (n = 0; n < pSignerInfo->UnauthAttrs.cAttr; n++) {
            if (lstrcmpA(pSignerInfo->UnauthAttrs.rgAttr[n].pszObjId, szOID_RSA_counterSign) == 0) {
                /* Get size of CMSG_SIGNER_INFO structure. */
                fResult = CryptDecodeObject(ENCODING,
                           PKCS7_SIGNER_INFO,
                           pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
                           pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].cbData,
                           0,
                           NULL,
                           &dwSize);
                if (!fResult) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CryptDecodeObject failed with %x"), GetLastError());
                    __leave;
                }

                /* Allocate memory for CMSG_SIGNER_INFO. */
                *pCounterSignerInfo = (PCMSG_SIGNER_INFO)LocalAlloc(LPTR, dwSize);
                if (!*pCounterSignerInfo) {
                    outOfMemory(TEXT("GTSSI"), 1);
                    __leave;
                }

                /* Decode and get CMSG_SIGNER_INFO structure
                   for timestamp certificate. */
                fResult = CryptDecodeObject(ENCODING,
                           PKCS7_SIGNER_INFO,
                           pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
                           pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].cbData,
                           0, (PVOID)*pCounterSignerInfo, &dwSize);
                if (!fResult) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CryptDecodeObject failed with %x"), GetLastError());
                    __leave;
                }
                fReturn = TRUE;                
                break; /* Break from for loop. */
            }           
        }
    }
    __finally {
        /* Clean up.*/
        if (pCertContext != NULL) CertFreeCertificateContext(pCertContext);
    }
    return fReturn;
}

/**
 * Prints certificate info into a buffer.
 *
 * @param buffer Buffer to fill, or NULL to calculate the size.
 * @param size Size of the buffer, or 0 when calculating the size.
 * @param serialNr Serial number.
 * @param szName1 Issuer name.
 * @param szName2 Subject name.
 *
 * @return number of characters written to the buffer
 *         (without the NULL termination)
 */
size_t printCertificateInfoInner(TCHAR* buffer, size_t size, LPTSTR serialNr, LPTSTR szName1, LPTSTR szName2) {
    const TCHAR* token;
    size_t len = 0;

    token = TEXT("    Serial Number:\n");
    if (buffer) {
        _tcsncpy(buffer, token, size);
    }
    len += _tcslen(token);

    token = TEXT("      %s\n");
    if (buffer) {
        _sntprintf(buffer + len, size - len, token, serialNr);
    }
    len += _tcslen(token) - 2 + _tcslen(serialNr);
    
    token = TEXT("    Issuer Name: %s\n");
    if (buffer) {
        _sntprintf(buffer + len, size - len, token, szName1);
    }
    len += _tcslen(token) - 2 + _tcslen(szName1);
    
    token = TEXT("    Subject Name: %s");
    if (buffer) {
        _sntprintf(buffer + len, size - len, token, szName2);
        buffer[size - 1] = TEXT('\0');
    }
    len += _tcslen(token) - 2 + _tcslen(szName2);
    
    return len;
}

LPTSTR PrintCertificateInfo(PCCERT_CONTEXT pCertContext, int level) {
    BOOL fReturn = FALSE;
    LPTSTR szName1 = NULL;
    LPTSTR szName2 = NULL;
    LPTSTR serialNr = NULL;
    DWORD dwData, serialNrLength = 0, n, i;
    LPTSTR buffer = NULL;
    size_t size = 0;

    __try {
        dwData = pCertContext->pCertInfo->SerialNumber.cbData;

        for (i = 0; i < 2; i++) {
            for (n = 0; n < dwData; n++) {
                if (serialNr) {
                    _sntprintf(serialNr + (n * 3) , serialNrLength - (n * 3), TEXT("%02x "), pCertContext->pCertInfo->SerialNumber.pbData[dwData - (n + 1)]);
                } else {
                    serialNrLength += 3;
                }
            }
            if (!serialNr) {
                serialNr = calloc(serialNrLength + 1, sizeof(TCHAR));
                if (!serialNr) {
                    outOfMemory(TEXT("PCI"), 1);
                    __leave;
                }
            }
        }
        
        /* Get Issuer name size. */
        if (!(dwData = CertGetNameString(pCertContext, 
                                         CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                         CERT_NAME_ISSUER_FLAG,
                                         NULL, NULL, 0))) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CertGetNameString failed."));
            __leave;
        }

        /* Allocate memory for Issuer name. */
        szName1 = (LPTSTR)LocalAlloc(LPTR, dwData * sizeof(TCHAR));
        if (!szName1) {
            outOfMemory(TEXT("PCI"), 2);
            __leave;
        }

        /* Get Issuer name. */
        if (!(CertGetNameString(pCertContext, 
                                CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                CERT_NAME_ISSUER_FLAG,
                                NULL, szName1, dwData))) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CertGetNameString failed."));
            __leave;
        }

        /* Get Subject name size. */
        if (!(dwData = CertGetNameString(pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, NULL, 0))) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CertGetNameString failed."));
            __leave;
        }

        /* Allocate memory for subject name. */
        szName2 = (LPTSTR)LocalAlloc(LPTR, dwData * sizeof(TCHAR));
        if (!szName2) {
            outOfMemory(TEXT("GTSSI"), 3);
            __leave;
        }

        /* Get subject name. */
        if (!(CertGetNameString(pCertContext,  CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, szName2, dwData))) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CertGetNameString failed."));
            __leave;
        }

        size = printCertificateInfoInner(NULL, 0, serialNr, szName1, szName2);
        buffer = malloc(sizeof(TCHAR) * (size + 1));
        if (!buffer) {
            outOfMemory(TEXT("GTSSI"), 4);
            __leave;
        }
        printCertificateInfoInner(buffer, size + 1, serialNr, szName1, szName2);
    }
    __finally {
        if (szName1 != NULL) LocalFree(szName1);
        if (szName2 != NULL) LocalFree(szName2);
        if (serialNr != NULL) free(serialNr);
    }

    return buffer;
}

/**
 * Prints certificate info into a buffer.
 *
 * @param buffer Buffer to fill, or NULL to calculate the size.
 * @param size Size of the buffer, or 0 when calculating the size.
 * @param lpszProgramName Program name.
 * @param lpszPublisherLink Publisher name.
 * @param lpszMoreInfoLink More info link name.
 * @param szSignerCert Info of the signer certificate.
 * @param szTimeStampCert Info of the timestamp certificate.
 * @param st Date of the timestamp.
 * @param dateSet TRUE if the date of the timestamp should be printed,
 *                FALSE otherwise.
 *
 * @return number of characters written to the buffer
 *         (without the NULL termination)
 */
size_t printWholeCertificateInfoInner(TCHAR* buffer,
                                        size_t size,
                                        LPWSTR lpszProgramName,
                                        LPWSTR lpszPublisherLink,
                                        LPWSTR lpszMoreInfoLink,
                                        LPTSTR szSignerCert,
                                        LPTSTR szTimeStampCert,
                                        SYSTEMTIME st,
                                        int dateSet) {
    const TCHAR* token;
    size_t len = 0;

    if (buffer) {
        buffer[0] = 0;
    }
    
    if (lpszProgramName) {
        token = TEXT("  Program Name: %s\n");
        if (buffer) {
            _sntprintf(buffer, size, token, lpszProgramName);
        }
        len += _tcslen(token) - 2 + _tcslen(lpszProgramName);
    }
    
    if (lpszPublisherLink) {
        token = TEXT("  Publisher Link: %s\n");
        if (buffer) {
            _sntprintf(buffer + len, size - len, token, lpszPublisherLink);
        }
        len += _tcslen(token) - 2 + _tcslen(lpszPublisherLink);
    }
    
    if (lpszMoreInfoLink) {
        token = TEXT("  MoreInfo Link: %s\n");
        if (buffer) {
            _sntprintf(buffer + len, size - len, token, lpszMoreInfoLink);
        }
        len += _tcslen(token) - 2 + _tcslen(lpszMoreInfoLink);
    }
    
    if (szSignerCert) {
        token = TEXT("  Signer Certificate:\n");
        if (buffer) {
            _tcsncat(buffer, token, size - len);
        }
        len += _tcslen(token);
        
        token = TEXT("%s\n");
        if (buffer) {
            _sntprintf(buffer + len, size - len, token, szSignerCert);
        }
        len += _tcslen(token) - 2 + _tcslen(szSignerCert);
    }
    
    if (szTimeStampCert) {
        token = TEXT("  TimeStamp Certificate:\n");
        if (buffer) {
            _tcsncat(buffer, token, size - len);
        }
        len += _tcslen(token);
        
        token = TEXT("%s\n");
        if (buffer) {
            _sntprintf(buffer + len, size - len, token, szTimeStampCert);
        }
        len += _tcslen(token) - 2 + _tcslen(szTimeStampCert);
    }
    
    if (dateSet) {
        token = TEXT("    Date of TimeStamp: %04d/%02d/%02d %02d:%02d:%02d\n");
        if (buffer) {
            _sntprintf(buffer + len, size - len, token, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        }
        len += _tcslen(token) - 10;
    }
    
    if (buffer) {
        /* For safety */
        buffer[size - 1] = TEXT('\0');
    }
    return len;
}

LPTSTR printWholeCertificateInfo(LPCWSTR wrapperExeName, int level) {
    HCERTSTORE hStore = NULL;
    HCRYPTMSG hMsg = NULL; 
    PCCERT_CONTEXT pCertContext = NULL;
    BOOL fResult;   
    DWORD dwEncoding, dwContentType, dwFormatType;
    PCMSG_SIGNER_INFO pSignerInfo = NULL;
    PCMSG_SIGNER_INFO pCounterSignerInfo = NULL;
    DWORD dwSignerInfo;
    CERT_INFO CertInfo;     
    SPROG_PUBLISHERINFO ProgPubInfo;
    SYSTEMTIME st;
    LPWSTR lpszProgramName = NULL;
    LPWSTR lpszPublisherLink = NULL;
    LPWSTR lpszMoreInfoLink = NULL; 
    LPTSTR szSignerCert = NULL;
    LPTSTR szTimeStampCert = NULL;
    LPTSTR buffer = NULL;
    size_t size = 0;
    DWORD dateSet = FALSE;

    ZeroMemory(&ProgPubInfo, sizeof(ProgPubInfo));
    __try {
        /* Get message handle and store handle from the signed file. */
        fResult = CryptQueryObject(CERT_QUERY_OBJECT_FILE,
                                   wrapperExeName,
                                   CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                                   CERT_QUERY_FORMAT_FLAG_BINARY,
                                   0,
                                   &dwEncoding,
                                   &dwContentType,
                                   &dwFormatType,
                                   &hStore,
                                   &hMsg,
                                   NULL);
        if (!fResult) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("No certificate found! Error: %x"), GetLastError());
            __leave;
        }

        /* Get signer information size. */
        fResult = CryptMsgGetParam(hMsg, 
                                   CMSG_SIGNER_INFO_PARAM, 
                                   0, 
                                   NULL, 
                                   &dwSignerInfo);
        if (!fResult) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CryptMsgGetParam failed with %x"), GetLastError());
            __leave;
        }

        /* Allocate memory for signer information. */
        pSignerInfo = (PCMSG_SIGNER_INFO)LocalAlloc(LPTR, dwSignerInfo);
        if (!pSignerInfo) {
            outOfMemory(TEXT("GWCI"), 1);
            __leave;
        }

        /* Get Signer Information. */
        fResult = CryptMsgGetParam(hMsg, 
                                   CMSG_SIGNER_INFO_PARAM, 
                                   0, 
                                   (PVOID)pSignerInfo, 
                                   &dwSignerInfo);
        if (!fResult) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CryptMsgGetParam failed with %x"), GetLastError());
            __leave;
        }
        
        /* Get program name and publisher information from signer info structure. */
        if (GetProgAndPublisherInfo(pSignerInfo, &ProgPubInfo))  {
            lpszProgramName = ProgPubInfo.lpszProgramName;
            lpszPublisherLink = ProgPubInfo.lpszPublisherLink;
            lpszMoreInfoLink = ProgPubInfo.lpszMoreInfoLink;
        }

        /* Search for the signer certificate in the temporary certificate store. */
        CertInfo.Issuer = pSignerInfo->Issuer;
        CertInfo.SerialNumber = pSignerInfo->SerialNumber;

        pCertContext = CertFindCertificateInStore(hStore, ENCODING, 0, CERT_FIND_SUBJECT_CERT, (PVOID)&CertInfo, NULL);
        if (!pCertContext) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CertFindCertificateInStore failed with %x"),
                GetLastError());
            __leave;
        }
        szSignerCert = PrintCertificateInfo(pCertContext, level);

        /* Get the timestamp certificate signerinfo structure. */
        if (GetTimeStampSignerInfo(pSignerInfo, &pCounterSignerInfo)) {

            /* Search for Timestamp certificate in the temporary certificate store. */
            CertInfo.Issuer = pCounterSignerInfo->Issuer;
            CertInfo.SerialNumber = pCounterSignerInfo->SerialNumber;

            pCertContext = CertFindCertificateInStore(hStore, ENCODING, 0, CERT_FIND_SUBJECT_CERT, (PVOID)&CertInfo, NULL);
            if (!pCertContext) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CertFindCertificateInStore failed with %x"),
                    GetLastError());
                __leave;
            }
            szTimeStampCert = PrintCertificateInfo(pCertContext, level);

            /* Find Date of timestamp. */
            if (GetDateOfTimeStamp(pCounterSignerInfo, &st)) {
                dateSet = TRUE;
            }
        }
        size = printWholeCertificateInfoInner(NULL, 0, lpszProgramName, lpszPublisherLink, lpszMoreInfoLink, szSignerCert, szTimeStampCert, st, dateSet);
        buffer = malloc(sizeof(TCHAR) * (size + 1));
        if (!buffer) {
            outOfMemory(TEXT("GWCI"), 2);
            __leave;
        }
        printWholeCertificateInfoInner(buffer, size + 1, lpszProgramName, lpszPublisherLink, lpszMoreInfoLink, szSignerCert, szTimeStampCert, st, dateSet);
        /* Remove the last line break. */
        buffer[size - 1] = 0;
    } __finally {               
        /* Clean up. */
        if (ProgPubInfo.lpszProgramName != NULL) {
            LocalFree(ProgPubInfo.lpszProgramName);
        }
        if (ProgPubInfo.lpszPublisherLink != NULL) {
            LocalFree(ProgPubInfo.lpszPublisherLink);
        }
        if (ProgPubInfo.lpszMoreInfoLink != NULL) {
            LocalFree(ProgPubInfo.lpszMoreInfoLink);
        }
        if (pSignerInfo != NULL) {
            LocalFree(pSignerInfo);
        }
        if (pCounterSignerInfo != NULL) {
            LocalFree(pCounterSignerInfo);
        }
        if (pCertContext != NULL) {
            CertFreeCertificateContext(pCertContext);
        }
        if (hStore != NULL) {
            CertCloseStore(hStore, 0);
        }
        if (hMsg != NULL) {
            CryptMsgClose(hMsg);
        }
        if (szSignerCert != NULL) {
            free(szSignerCert);
        }
        if (szTimeStampCert != NULL) {
            free(szTimeStampCert);
        }
    }

    return buffer;
}

void verifyEmbeddedSignature() {
    LONG lStatus;
    DWORD dwLastError;
    const TCHAR* lastErrMsg;
    TCHAR pwszSourceFile[_MAX_PATH];
    GUID WVTPolicyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA WinTrustData;
    WINTRUST_FILE_INFO FileData;
    LPTSTR buffer = NULL;
    TCHAR* szOS;
    int logLevel;
    
    if (!GetModuleFileName(NULL, pwszSourceFile, _MAX_PATH)) {
        return;
    }
    memset(&FileData, 0, sizeof(FileData));
    FileData.cbStruct = sizeof(WINTRUST_FILE_INFO);
    FileData.pcwszFilePath = pwszSourceFile;
    FileData.hFile = NULL;
    FileData.pgKnownSubject = NULL;
    memset(&WinTrustData, 0, sizeof(WinTrustData));
    WinTrustData.cbStruct = sizeof(WinTrustData);
    WinTrustData.pPolicyCallbackData = NULL;
    WinTrustData.pSIPClientData = NULL;
    WinTrustData.dwUIChoice = WTD_UI_NONE;
    WinTrustData.fdwRevocationChecks = WTD_REVOKE_NONE; 
    WinTrustData.dwUnionChoice = WTD_CHOICE_FILE;
    WinTrustData.dwStateAction = WTD_STATEACTION_VERIFY;
    WinTrustData.hWVTStateData = NULL;
    WinTrustData.pwszURLReference = NULL;
    WinTrustData.dwProvFlags = WTD_USE_DEFAULT_OSVER_CHECK;
    WinTrustData.dwUIContext = 0;
    WinTrustData.pFile = &FileData;
    
    /* On old versions of Windows (tested with 2000), the last error code is not set by WinVerifyTrust(). We will have to use lStatus instead. */
    SetLastError(ERROR_SUCCESS);
    lStatus = WinVerifyTrust(NULL, &WVTPolicyGUID, &WinTrustData);
    
    switch (lStatus) {
        case ERROR_SUCCESS:
            buffer = printWholeCertificateInfo(pwszSourceFile, LEVEL_DEBUG);
            if (buffer) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("The file \"%s\" is signed and the signature was verified.\n%s"), pwszSourceFile, buffer);
                free(buffer);
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("The file \"%s\" is signed and the signature was verified."), pwszSourceFile);
            }
            break;
        
        case TRUST_E_NOSIGNATURE:
            /* The file was not signed or had a signature 
             that was not valid. */

            /* Get the reason for no signature. */
            dwLastError = GetLastError();
            if (dwLastError == ERROR_SUCCESS) {
                dwLastError = TRUST_E_NOSIGNATURE;
            }
            lastErrMsg = getErrorText(dwLastError, NULL);
            
            if ((TRUST_E_SUBJECT_FORM_UNKNOWN == dwLastError) || (TRUST_E_NOSIGNATURE == dwLastError) || (TRUST_E_PROVIDER_UNKNOWN == dwLastError)) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("The file \"%s\" is not signed."), pwszSourceFile);
            } else {
                /* The signature was not valid or there was an error 
                   opening the file. */
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("An unknown error occurred trying to verify the signature of the \"%s\" file: %s"),
                    pwszSourceFile, lastErrMsg);
            }
            break;

        case TRUST_E_EXPLICIT_DISTRUST:
            /* The hash that represents the subject or the publisher 
               is not allowed by the admin or user. */
            buffer = printWholeCertificateInfo(pwszSourceFile, LEVEL_WARN);   
            if (buffer) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("The signature is present, but specifically disallowed.\n%s\nThe Wrapper will shutdown!"), buffer);
                free(buffer);
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("The signature is present, but specifically disallowed.\nThe Wrapper will shutdown!"));
            }
            appExit(0);

            break;

        case TRUST_E_SUBJECT_NOT_TRUSTED:
            /* The user clicked "No" when asked to install and run. */
            buffer = printWholeCertificateInfo(pwszSourceFile, LEVEL_WARN);   
            if (buffer) {            
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("The signature is present, but not trusted.\n%s"), buffer);
                free(buffer);
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("The signature is present, but not trusted."));
            }
            break;

        case CRYPT_E_SECURITY_SETTINGS:
            /*
            The hash that represents the subject or the publisher 
            was not explicitly trusted by the admin and the 
            admin policy has disabled user trust. No signature, 
            publisher or time stamp errors.
            */
            buffer = printWholeCertificateInfo(pwszSourceFile, LEVEL_WARN);   
            if (buffer) {   
               log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CRYPT_E_SECURITY_SETTINGS - The hash\nrepresenting the subject or the publisher wasn't\nexplicitly trusted by the admin and admin policy\nhas disabled user trust. No signature, publisher or timestamp errors.\n%s"), buffer);
               free(buffer);
            } else {
               log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("CRYPT_E_SECURITY_SETTINGS - The hash\nrepresenting the subject or the publisher wasn't\nexplicitly trusted by the admin and admin policy\nhas disabled user trust. No signature, publisher or timestamp errors."));
            }
            break;

        default:
            dwLastError = GetLastError();
            if (dwLastError == ERROR_SUCCESS) {
                dwLastError = (DWORD)lStatus;
            }
            lastErrMsg = getErrorText(dwLastError, NULL);
            logLevel = isSHA2CertificateSupported() ? LEVEL_WARN : LEVEL_DEBUG;
            buffer = printWholeCertificateInfo(pwszSourceFile, logLevel);
            if (buffer) {
                if (dwLastError == TRUST_E_BAD_DIGEST  || dwLastError == TRUST_E_CERT_SIGNATURE) {
                    if (isSHA2CertificateSupported()) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("A signature was found in \"%s\", but checksum failed: (Errorcode: 0x%x) %s\n%s\nThe Wrapper will shutdown!"), pwszSourceFile, lStatus, lastErrMsg, buffer);
                    } else {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("A signature was found in \"%s\", but checksum failed: (Errorcode: 0x%x) %s\n%s"), pwszSourceFile, lStatus, lastErrMsg, buffer);
                    }
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, logLevel, TEXT("A signature was found in \"%s\", but checksum failed: (Errorcode: 0x%x) %s\n%s\nThe error is not directly related to the Wrapper's signature, therefore continue..."), pwszSourceFile, lStatus, lastErrMsg, buffer);
                }
                free(buffer);
            } else {
                if (dwLastError == TRUST_E_BAD_DIGEST  || dwLastError == TRUST_E_CERT_SIGNATURE) {
                    if (isSHA2CertificateSupported()) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ERROR, TEXT("A signature was found in \"%s\", but checksum failed: (Errorcode: 0x%x) %s\nThe Wrapper will shutdown!"), pwszSourceFile, lStatus, lastErrMsg);
                    } else {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("A signature was found in \"%s\", but checksum failed: (Errorcode: 0x%x) %s"), pwszSourceFile, lStatus, lastErrMsg);
                    }
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, logLevel, TEXT("A signature was found in \"%s\", but checksum failed: (Errorcode: 0x%x) %s\nThe error is not directly related to the Wrapper's signature, therefore continue..."), pwszSourceFile, lStatus, lastErrMsg);
                }
            }

            if (dwLastError == TRUST_E_BAD_DIGEST  || dwLastError == TRUST_E_CERT_SIGNATURE) {
                if (isSHA2CertificateSupported()) {
                    /* Stop the Wrapper. */
                    wrapperStopProcess(wrapperData->errorExitCode, TRUE);
                    wrapperData->wState = WRAPPER_WSTATE_STOPPING;
                } else {
                    /* Print the OS version for debugging and continue. */
                    szOS = calloc(OSBUFSIZE, sizeof(TCHAR));
                    if (szOS) {
                        if (GetOSDisplayString(&szOS)) {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Operating System ID: %s\nSHA-2 is not supported on this OS, therefore continue..."), szOS);
                        }
                        free(szOS);
                    }
                }
            }
            break;
    }
    /* free memory allocated by WinVerifyTrust */
    WinTrustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(NULL, &WVTPolicyGUID, &WinTrustData);
    return;
}

/**
 * Does some special setup for when we are running as a launcher.
 */
void enterLauncherMode() {
    /* Tell the logger to use the launcher source in place of the actual one so it is clear those entries are coming from the launcher and not the actual service. */
    setLauncherSource();
}

#ifndef CUNIT
void _tmain(int argc, TCHAR **argv) {
    int localeSet = FALSE;
    int defaultLocaleFailed = FALSE;
    int result;
#if defined(_DEBUG)
    int i;
#endif
    BOOL DEPApiAvailable = FALSE;
    BOOL DEPStatus = FALSE;
    DWORD DEPError = ERROR_SUCCESS;
    HMODULE hk;
    FARPROC pfnSetDEP;
    /* The StartServiceCtrlDispatcher requires this table to specify
     * the ServiceMain function to run in the calling process. The first
     * member in this example is actually ignored, since we will install
     * our service as a SERVICE_WIN32_OWN_PROCESS service type. The NULL
     * members of the last entry are necessary to indicate the end of
     * the table; */
    SERVICE_TABLE_ENTRY serviceTable[2];
    
    /* Enable DEP as soon as possible in the main method.
     *  - Use SetProcessDEPPolicy() instead of the /DYNAMICBASE link
     *    option to allow DEP on WIN XP SP3 (/DYNAMICBASE is from Vista).
     *  - Load it dynamically to allow the Wrapper running normally
     *    (but without DEP) on older versions of Windows. 
     *  - Retain the result of the invocation to log any messages after
     *    the logging will be initialized. */
    if (_tcscmp(wrapperBits, TEXT("32")) == 0) {
        hk = GetModuleHandle(TEXT("KERNEL32.DLL"));
        if (hk) {
            pfnSetDEP = GetProcAddress(hk, "SetProcessDEPPolicy");
            if (pfnSetDEP) {
                DEPApiAvailable = TRUE;
                DEPStatus = (BOOL)pfnSetDEP(0x00000001); /* 0x00000001 = PROCESS_DEP_ENABLE */
                if (!DEPStatus) {
                    DEPError = getLastError();
                }
            }
            FreeLibrary(hk);
        }
    }
    
    if (!localeSet) {
        /* No need to log anything. Set the default locale so any startup error messages will have a chance of working.
         *  We will go back and try to set the actual locale again later once it is configured. */
        if (!_tsetlocale(LC_ALL, TEXT(""))) {
            defaultLocaleFailed = TRUE;
        }
    }

    if (buildSystemPath()) {
        appExit(1);
        return; /* For clarity. */
    }

    if (wrapperInitialize((argc > 1) && (argv[1][0] == TEXT('-')) && isPromptCallCommand(&argv[1][1]))) {
        appExit(1);
        return; /* For clarity. */
    }
    
    /* Store the DEP status to print it after the version banner. */
    wrapperData->DEPApiAvailable = DEPApiAvailable;
    wrapperData->DEPStatus       = DEPStatus;
    wrapperData->DEPError        = DEPError;

    /* Main thread initialized in wrapperInitialize. */

    /* Enclose the rest of the program in a try catch block so we can
     *  display and log useful information should the need arise.  This
     *  must be done after logging has been initialized as the catch
     *  block makes use of the logger. */
    __try {
#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Wrapper DEBUG build!"));
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Logging initialized."));
#endif
        /* Get the current process. */
        wrapperData->wrapperProcess = GetCurrentProcess();
        wrapperData->wrapperPID = GetCurrentProcessId();

        if (initializeWinSock()) {
            appExit(1);
            return; /* For clarity. */
        }

        if (setWorkingDir()) {
            appExit(1);
            return; /* For clarity. */
        }

        if (collectUserInfo()) {
            appExit(1);
            return; /* For clarity. */
        }
#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Working directory set."));
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Arguments:"));
        for (i = 0; i < argc; i++) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  argv[%d]=%s"), i, argv[i]);
        }
#endif

        /* Parse the command and configuration file from the command line. */
        if (!wrapperParseArguments(argc, argv)) {
            appExit(1);
            return; /* For clarity. */
        }
        
        /* Set launcher mode as soon as possible after the arguments are parsed. */
        if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("su")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-setup")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("td")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-teardown")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("i"))  || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-install")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("it")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-installstart")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("u"))  || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-update")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("r"))  || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-remove")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("t"))  || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-start")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("a"))  || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-pause")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("e"))  || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-resume")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("p"))  || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-stop")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("l"))  || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-controlcode")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("d"))  || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-dump")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("q"))  || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-query")) ||
           !strcmpIgnoreCase(wrapperData->argCommand, TEXT("qs")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-querysilent"))) {
            enterLauncherMode();
        }
        
        if (defaultLocaleFailed) {
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("Unable to set the locale."));
        }
        wrapperLoadHostName();

        /* At this point, we have a command, confFile, and possibly additional arguments. */
        if (!strcmpIgnoreCase(wrapperData->argCommand, TEXT("?")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-help"))) {
            /* User asked for the usage. */
            setSimpleLogLevels();
            wrapperUsage(argv[0]);
            appExit(0);
            return; /* For clarity. */
        } else if (!strcmpIgnoreCase(wrapperData->argCommand, TEXT("v")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-version"))) {
            /* User asked for version. */
            setSimpleLogLevels();
            wrapperVersionBanner();
            appExit(0);
            return; /* For clarity. */
        } else if (!strcmpIgnoreCase(wrapperData->argCommand, TEXT("h")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-hostid"))) {
            /* Print out a banner containing the HostId. */
            setSimpleLogLevels();
            wrapperVersionBanner();
            showHostIds(LEVEL_STATUS);
            appExit(0);
            return; /* For clarity. */
        }
        if (!strcmpIgnoreCase(wrapperData->argCommand, TEXT("s")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-service"))) {
            /* We are running as a Service, set a flag to remember it while loading the configuration. */
            wrapperData->isConsole = FALSE;
            if (!isVista()) {
                /* When running as a service on versions of Windows older than Vista,
                 *  the environment variables must first be loaded from the registry. */
                if (wrapperLoadEnvFromRegistry()) {
                    appExit(1);
                    return; /* For clarity. */
                }
            }
        }

        /* Load the properties. */
        /* To make the WRAPPER_LANG references in the configuration work correctly,
         *  it is necessary to load the configuration twice.
         * The first time, we want to ignore the return value.  Any errors will be
         *  suppressed and will get reported again the second time through. */
        /* From version 3.5.27, the community edition will also preload the configuration properties. */
        wrapperLoadConfigurationProperties(TRUE);
        if (wrapperLoadConfigurationProperties(FALSE)) {
            /* Unable to load the configuration.  Any errors will have already
             *  been reported. */
            if (wrapperData->argConfFileDefault && !wrapperData->argConfFileFound) {
                /* The config file that was being looked for was default and
                 *  it did not exist.  Show the usage. */
                wrapperUsage(argv[0]);
            }
            /* There might have been some queued messages logged on configuration load. Queue the following message to make it appear last. */
            log_printf_queue(TRUE, WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("  The Wrapper will stop."));
            appExit(wrapperData->errorExitCode);
            return; /* For clarity. */
        }

        /* Set the default umask of the Wrapper process. */
        _umask(wrapperData->umask);

        /* Perform the specified command */
        if (!strcmpIgnoreCase(wrapperData->argCommand, TEXT("-jvm_bits"))) {
            if (!wrapperInitChildPipe()) {
                /* Generate the command used to get the Java version but don't stop on failure. */
                if (!wrapperBuildJavaVersionCommand()) {
                    wrapperLaunchJavaVersion();
                }
            }
            appExit(wrapperData->jvmBits);
            return; /* For compiler. */
        } else if (!strcmpIgnoreCase(wrapperData->argCommand, TEXT("-request_delta_binary_bits"))) {
            /* Otherwise return the binary bits */
            appExit(_tcscmp(wrapperBits, TEXT("64")) == 0 ? 64 : 32);
            return; /* For compiler. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("su")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-setup"))) {
            /* Setup the Wrapper */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            /* are we elevated ? */
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                wrapperExit(wrapperSetup(FALSE));
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("td")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-teardown"))) {
            /* Teardown the Wrapper */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            /* are we elevated ? */
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                wrapperExit(wrapperTeardown(FALSE));
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("i")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-install"))) {
            /* Install an NT service */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            wrapperCheckForMappedDrives();
            /* are we elevated ? */
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                setupSyslogRegistration(TRUE);
                wrapperExit(wrapperInstall());
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("it")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-installstart"))) {
            /* Install and Start an NT service */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            wrapperCheckForMappedDrives();
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                setupSyslogRegistration(TRUE);
                result = wrapperInstall();
                if (!result) {
                    result = wrapperStartService();
                }
                wrapperExit(result);
            }
            return; /* For clarity. */
        } else if (!strcmpIgnoreCase(wrapperData->argCommand, TEXT("r")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-remove"))) {
            /* Remove an NT service */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                /* don't call teardown here because it may be confusing if the user still wants to use the Wrapper as a console. */
                wrapperExit(wrapperRemove());
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("t")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-start"))) {
            /* Start an NT service */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            wrapperCheckForMappedDrives();
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                wrapperExit(wrapperStartService());
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("a")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-pause"))) {
            /* Pause a started NT service */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                wrapperExit(wrapperPauseService());
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("e")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-resume"))) {
            /* Resume a paused NT service */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                wrapperExit(wrapperResumeService());
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("p")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-stop"))) {
            /* Stop an NT service */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                wrapperExit(wrapperStopService(TRUE));
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("l")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-controlcode"))) {
            /* Send a control code to an NT service */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                wrapperExit(wrapperSendServiceControlCode(argv, wrapperData->argCommandArg));
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("d")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-dump"))) {
            /* Request a thread dump */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                wrapperExit(wrapperRequestThreadDump(argv));
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("q")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-query"))) {
            /* Return service status with console output. */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                appExit(wrapperServiceStatus(wrapperData->serviceName, wrapperData->serviceDisplayName, TRUE));
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("qs")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-querysilent"))) {
            /* Return service status without console output. */
            
            /* Always auto close the log file to keep the output in synch. */
            setLogfileAutoClose(TRUE);
            if (!isElevated()) {
                appExit(elevateThis(argc, argv));
            } else {
                /* are we launched secondary? */
                if (isSecondary() && duplicateSTD() == FALSE) {
                    appExit(wrapperData->errorExitCode);
                    return;
                }
                appExit(wrapperServiceStatus(wrapperData->serviceName, wrapperData->serviceDisplayName, FALSE));
            }
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("c")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-console"))) {
            /* Run as a console application */
            /* Load any dynamic functions. */
            loadDLLProcs();

            /* Initialize the invocation mutex as necessary, exit if it already exists. */
            if (initInvocationMutex()) {
                appExit(wrapperData->exitCode);
                return; /* For clarity. */
            }

            /* See if the logs should be rolled on Wrapper startup. */
            if ((getLogfileRollMode() & ROLL_MODE_WRAPPER) ||
                (getLogfileRollMode() & ROLL_MODE_JVM)) {
                rollLogs(NULL);
            }

            if (checkPidFile()) {
                /* The pid file exists and we are strict, so exit (cleanUpPIDFilesOnExit has not been turned on yet, so we will exit without cleaning the pid files). */
                appExit(wrapperData->errorExitCode);
                return; /* For clarity. */
            }

            /* From now on:
             *  - all pid files will be cleaned when the Wrapper exits,
             *  - any existing file will be owerwritten. */
            cleanUpPIDFilesOnExit = TRUE;

            if (wrapperWriteStartupPidFiles()) {
                appExit(wrapperData->errorExitCode);
                return; /* For clarity. */
            }

            appExit(wrapperRunConsole());
            return; /* For clarity. */
        } else if(!strcmpIgnoreCase(wrapperData->argCommand, TEXT("s")) || !strcmpIgnoreCase(wrapperData->argCommand, TEXT("-service"))) {
            /* Run as a service */
            wrapperCheckForMappedDrives();
            /* Load any dynamic functions. */
            loadDLLProcs();

            /* Prepare the service table */
            serviceTable[0].lpServiceName = wrapperData->serviceName;
            serviceTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)wrapperServiceMain;
            serviceTable[1].lpServiceName = NULL;
            serviceTable[1].lpServiceProc = NULL;

            _tprintf(TEXT("Attempting to start %s as an NT service.\n"), wrapperData->serviceDisplayName);
            _tprintf(TEXT("\nCalling StartServiceCtrlDispatcher...please wait.\n"));

            /* Start the service control dispatcher. 
             *  The ServiceControlDispatcher will call the wrapperServiceMain method. */
            if (!StartServiceCtrlDispatcher(serviceTable)) {
                _tprintf(TEXT("\n"));
                _tprintf(TEXT("StartServiceControlDispatcher failed!\n"));
                _tprintf(TEXT("\n"));
                _tprintf(TEXT("The -s and --service commands should only be called by the Windows\n"));
                _tprintf(TEXT("ServiceManager to control the Wrapper as a service, and is not\n"));
                _tprintf(TEXT("designed to be run manually by the user.\n"));
                _tprintf(TEXT("\n"));
                _tprintf(TEXT("For help, type\n"));
                _tprintf(TEXT("%s -?\n"), argv[0]);
                _tprintf(TEXT("\n"));
                appExit(wrapperData->errorExitCode);
                return; /* For clarity. */
            }

            /* We will get here when the service starts to stop */
            /* As wrapperServiceMain should take care of shutdown, wait 10 sec to give some time for its shutdown 
             * but the process should exit before the sleep completes. */
            wrapperSleep(10000);
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("Timed out waiting for wrapperServiceMain"));
            appExit(wrapperData->errorExitCode);
            return; /* For clarity. */
        } else {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT(""));
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN, TEXT("Unrecognized option: -%s"), wrapperData->argCommand);
            wrapperUsage(argv[0]);
            appExit(wrapperData->errorExitCode);
            return; /* For clarity. */
        }
    } __except (exceptionFilterFunction(GetExceptionInformation())) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("<-- Wrapper Stopping due to error"));
        appExit(wrapperData->errorExitCode);
        return; /* For clarity. */
    }
}
#endif

#define IN_SIZE 1024

/*
 * This function will connect to the secondary/elevated wrapper process and
 * read all output from stdout and stderr. Furthermore it will handle input
 * being sent to stdin. This function won't return until the client closed all
 * named pipes connected.
 * @param in  - the File HANDLE for the outbound channel to write to the 2nd process
 * @param out, err - the File HANDLEs for the inbound channel to read from the 2nd process
 *
 * @return TRUE if everything worked well. FALSE otherwise.
 */
BOOL readAndWriteNamedPipes(HANDLE in, HANDLE out, HANDLE err) {
    TCHAR inbuf[IN_SIZE], *secret;
    TCHAR *buffer = NULL;
    DWORD bufferSize = 0;
    DWORD currentBlockAvail, bytesRead, inWritten, ret;
    BOOL fConnected, outClosed = FALSE, errClosed = FALSE;

    /* the named pipes are nonblocking, so loop until an connection could
     * have been established with the secondary process (or an error occurred) */
    do {
        /* ConnectNamedPipe does rather wait until a connection was established
           However, the inbound pipes are non-blocking, so ConnectNamedPipe immediately
           returns. So call it looped...*/
        fConnected = ConnectNamedPipe(out, NULL);
    } while((fConnected == 0) && (GetLastError() == ERROR_PIPE_LISTENING));
    /* check for error */
    /* if ERROR_PIPE_CONNECTED it just means that while ConnectNamedPipe(..) was
     * called again, in the meantime the client connected. So in fact that's what we want.
     * WIN-7: if ERROR_NO_DATA, it means that the process has already been gone, probably an error in the start (but not in the pipe)
     * it might even be very likely there is data in stderr to retrieve */
    if ((fConnected == 0) && (GetLastError() != ERROR_PIPE_CONNECTED) && (GetLastError() != ERROR_NO_DATA)) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("The connect to stdout of the elevated process failed: %s"), getLastErrorText());
        return FALSE;
    }
    /* Same as above */
    do {
        fConnected = ConnectNamedPipe(err, NULL);
    } while((fConnected == 0) && (GetLastError() == ERROR_PIPE_LISTENING));
    if ((fConnected == 0) && (GetLastError() != ERROR_PIPE_CONNECTED) && (GetLastError() != ERROR_NO_DATA)) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("The connect to stderr of the elevated process failed: %s"), getLastErrorText());
        return FALSE;
    }

    do {
        /* out */
        if (!outClosed) {
            currentBlockAvail = 0;
            /* Check how much data is available for reading... */
            ret = PeekNamedPipe(out, NULL, 0, NULL, &currentBlockAvail, NULL);
            if ((ret == 0) && (GetLastError() == ERROR_BROKEN_PIPE)) {
                /* ERROR_BROKEN_PIPE - the client has closed the pipe. So most likely it just exited */
                outClosed = TRUE;
            }
            /* currentBlockAvail is already in bytes! */
            if (ret && (currentBlockAvail > 0)) {
                if (bufferSize < currentBlockAvail + sizeof(TCHAR)) {
                    bufferSize = __max(512, currentBlockAvail + sizeof(TCHAR));
                    free(buffer);
                    buffer = malloc(bufferSize);
                    if (!buffer) {
                        outOfMemory(TEXT("RAWNP"), 1);
                        return FALSE;
                    }
                }
                /* Clean the buffer before each read, as we don't want old stuff */
                memset(buffer,0, bufferSize);
                if (ReadFile(out, buffer, bufferSize, &bytesRead, NULL) == TRUE) {
                    /* if the message we just read in, doesn't have a new line, it means, that we most likely
                       got the secondary process prompting sth. */
                    if (buffer[_tcslen(buffer) - 1] != TEXT('\n')) {
                        /* To make sure, check if in is indeed waiting for input */
                        if (WaitForSingleObject(in, 1000) == WAIT_OBJECT_0) {
                            /* Clean the input buffer before each read */
                            memset(inbuf, 0, sizeof(inbuf));
                            /* A prompt can have an "n" - normal at the end. So this means, that
                               we prompt with echo */
                            if (buffer[_tcslen(buffer) - 1] == TEXT('n')) {
                                /* clean the mark */
                                buffer[_tcslen(buffer) - 1] = TEXT('\0');
                                /* show the prompt */
                                printToConsole(buffer, stdout);
                                /* and prompt */
                                _fgetts(inbuf, IN_SIZE, stdin);
                                if (WriteFile(in, inbuf, (DWORD)(_tcslen(inbuf)) * sizeof(TCHAR), &inWritten, NULL) == FALSE) {
                                    /* something happened with the named pipe, get out */
                                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Writing to the elevated process failed (%d): %s"), GetLastError(), getLastErrorText());
                                    free(buffer);
                                    return FALSE;
                                }
                            } else if (buffer[_tcslen(buffer) - 1] == TEXT('p')) {
                              /* A prompt can have an "p" - password at the end. So this means, that
                               we prompt without showing the input on the screen */
                                buffer[_tcslen(buffer) - 1] = TEXT('\0');
                                /* show the prompt */
                                printToConsole(buffer, stdout);
                                /* now read secret, readPassword already works with allocating a buffer (max 64(+1) character supported) */
                                secret = readPassword();
                                if (!secret) {
                                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Failed to read input from the console. (%d): %s"), GetLastError(), getLastErrorText());
                                    free(buffer);
                                    return FALSE;
                                }
                                _tcsncpy(inbuf, secret, IN_SIZE);
                                wrapperSecureFreeStrW(secret);
                                /* "secret" does not have any delimiter we could use, so send the whole inbuf buffer */
                                /* this is the downside of using MESSAGE pipes */
                                if (WriteFile(in, inbuf, (DWORD)sizeof(inbuf), &inWritten, NULL) == FALSE) {
                                    wrapperSecureZero(inbuf, sizeof(inbuf));
                                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Writing to the elevated process failed (%d): %s"), GetLastError(), getLastErrorText());
                                    free(buffer);
                                    return FALSE;
                                }
                                wrapperSecureZero(inbuf, sizeof(inbuf));
                            } else {
                                /* no additional for the prompt was provided, but WaitForSingleObject indicates, that
                                   stdin is expecting input. Handle this as if "n" - normal was specified (without clearing the mark)
                                /* show the prompt */
                                printToConsole(buffer, stdout);
                                /* and prompt */
                                _fgetts(inbuf, IN_SIZE, stdin);
                                if (WriteFile(in, inbuf, (DWORD)(_tcslen(inbuf)) * sizeof(TCHAR), &inWritten, NULL) == FALSE) {
                                    /* something happened with the named pipe, get out */
                                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Writing to the elevated process failed (%d): %s"), GetLastError(), getLastErrorText());
                                    free(buffer);
                                    return FALSE;
                                }
                            }
                            /* this is important! for transparency writing to the elevated process works as without this layer,
                               however, not flushing the buffer will make _getts just keep blocking until the buffer over there
                               is full (what we don't want) */
                            FlushFileBuffers(in);
                        } else {
                            /* A timeout occurred! probably a print without a newline. */
                            printToConsoleLn(buffer, stdout);
                        }
                    } else {
                        /* This is the normal case - just output */
                        /* print the message on the screen */
                        printToConsole(buffer, stdout);
                    }
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Reading stdout of the elevated process failed (%d): %s"), GetLastError(), getLastErrorText());
                    free(buffer);
                    return FALSE;
                }
            }
        }
        /* err */
        /* it works almost exactly as reading stdout, except no input will be checked */
        if (!errClosed) {
            currentBlockAvail = 0;
            ret = PeekNamedPipe(err, NULL, 0, NULL, &currentBlockAvail, NULL);
            if ((ret == 0) && (GetLastError() == ERROR_BROKEN_PIPE)) {
                errClosed = TRUE;
            }
            if (ret && (currentBlockAvail > 0)) {
                if (bufferSize < currentBlockAvail + sizeof(TCHAR)) {
                    bufferSize = __max(512, currentBlockAvail + sizeof(TCHAR));
                    free(buffer);
                    buffer = malloc(bufferSize);
                    if (!buffer) {
                        outOfMemory(TEXT("RAWNP"), 2);
                        return FALSE;
                    }
                }
                /* Clean the buffer before each read, as we don't want old stuff */
                memset(buffer,0, bufferSize);
                if (ReadFile(err, buffer, bufferSize, &bytesRead, NULL) == TRUE) {
                    printToConsole(buffer, stderr);
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Reading stderr of the elevated process failed (%d): %s"), GetLastError(), getLastErrorText());
                    free(buffer);
                    return FALSE;
                }
            }
        }
    } while(!errClosed || !outClosed);

    free(buffer);
    return TRUE;
}

/* This function needs to get called from the elevated/secondary process.
 * It will open the named pipes, the caller has establishes and redirects stdin, stdout, stderr
 * to this named pipes.
 * All this call is doing should be transparent and (except the stdin prompting) not interfere
 * the secondary process (i.e. log files will still work and logging to logfile actually will be done here)
 *
 * @return If successful this function will return TRUE, FALSE otherwise
 */
BOOL duplicateSTD() {
    TCHAR* strNamedPipeNameIn, *strNamedPipeNameOut, *strNamedPipeNameErr;
    const TCHAR *pipeBaseName;
    HANDLE pipeIn, pipeOut, pipeErr;
    int ret, fdOut, fdIn, fdErr;
    size_t len;

    /* get the base name for the named pipe, each channel will append an additional extension */
    pipeBaseName = getStringProperty(properties, TEXT("wrapper.internal.namedpipe"), NULL);
    if (!pipeBaseName) {
        return FALSE;
    }
    len = _tcslen(pipeBaseName) + 13;
    strNamedPipeNameIn = malloc(sizeof(TCHAR) * len);
    if (!strNamedPipeNameIn) {
        outOfMemory(TEXT("MSE"), 1);
        return FALSE;
    }
    _sntprintf(strNamedPipeNameIn, len, TEXT("\\\\.\\pipe\\%sINN"), pipeBaseName);

    strNamedPipeNameOut = malloc(sizeof(TCHAR) * len);
    if (!strNamedPipeNameOut) {
        free(strNamedPipeNameIn);
        outOfMemory(TEXT("MSE"), 2);
        return FALSE;
    }
    _sntprintf(strNamedPipeNameOut, len, TEXT("\\\\.\\pipe\\%sOUT"), pipeBaseName);

    strNamedPipeNameErr = malloc(sizeof(TCHAR) * len);
    if (!strNamedPipeNameErr) {
        free(strNamedPipeNameIn);
        free(strNamedPipeNameOut);
        outOfMemory(TEXT("MSE"), 3);
        return FALSE;
    }
    _sntprintf(strNamedPipeNameErr, len, TEXT("\\\\.\\pipe\\%sERR"), pipeBaseName);

#ifdef _DEBUG
    _tprintf(TEXT("going to open %s, %s and %s\n"), strNamedPipeNameIn, strNamedPipeNameOut, strNamedPipeNameErr);
#endif
    /* Use CreateFile to connect to the Named Pipes. */
    if ((pipeIn = CreateFile(strNamedPipeNameIn, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Connect to stdin pipe failed (%d): %s"), GetLastError(), getLastErrorText());
        ret = FALSE;
    } else {
        if ((pipeOut = CreateFile(strNamedPipeNameOut, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Connect to stdout pipe failed (%d): %s"), GetLastError(), getLastErrorText());
            ret = FALSE;
        } else {
            if ((pipeErr = CreateFile(strNamedPipeNameErr, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Connect to stderr pipe failed (%d): %s"), GetLastError(), getLastErrorText());
                ret = FALSE;
            } else {
                /* This is magic */
                if (((fdIn = _open_osfhandle((long)pipeIn, 0)) != -1) &&
                   ((fdErr = _open_osfhandle((long)pipeErr, 0)) != -1) &&
                   ((fdOut = _open_osfhandle((long)pipeOut, 0)) != -1)) {

                    if ((_dup2(fdIn, 0) == 0) && (_dup2(fdOut, 1) == 0) && (_dup2(fdErr, 2) == 0)) {
                        ret = TRUE;
#ifdef _DEBUG
                        _ftprintf(stderr, TEXT("12345\n"));fflush(NULL);
                        _ftprintf(stderr, TEXT("1234567890\n"));fflush(NULL);
                        _ftprintf(stdout, TEXT("12345\n"));fflush(NULL);
                        _ftprintf(stdout, TEXT("1234567890\n"));fflush(NULL);
#endif
                    } else {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                            TEXT("ERROR: Could not redirect the file descriptors to the client sided named pipes."));
                    }
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                        TEXT("ERROR: Could not acquire the file descriptors for the client sided named pipes."));
                }
            }
        }
    }
    free(strNamedPipeNameErr);
    free(strNamedPipeNameOut);
    free(strNamedPipeNameIn);
    return ret;
}


/* This function first creates 3 named pipes (2 inbound & 1 outbound) for establishing the connection.
 * Then it will ask the user to allow elevation for a secondary process.
 * And finally (if elevation granted) wait and call readAndWriteNamedPipes until the elevated process finishes.
 *
 * @param hwnd - The current window handle.
 * @param pszVerb - the verb defining the action ShellExecuteEx will perform
 * @param pszPath - the path to the executable going to be called
 * @param pszParameters - the parameters for the executable
 * @param pszDirectory - the working directory the process will have (if NULL the working direcory context will be inherited)
 * @param namedPipeName - the base name for the named pipes for the IPC between us and the new process.
 * @return the exit code of the elevated process
 */
int myShellExec(HWND hwnd, LPCTSTR pszVerb, LPCTSTR pszPath, LPCTSTR pszParameters, LPCTSTR pszDirectory, TCHAR* namedPipeName) {
    DWORD returnValue;
    SHELLEXECUTEINFO shex;
    HANDLE hNamedPipeIn, hNamedPipeOut, hNamedPipeErr;
    TCHAR* strNamedPipeNameIn, *strNamedPipeNameOut, *strNamedPipeNameErr;
    int ret = wrapperData->errorExitCode;
    size_t len;

    /* first we generate the filenames for the named pipes based on namedPipeName */
    len = _tcslen(namedPipeName) + 4 + 9;
    strNamedPipeNameIn = malloc(sizeof(TCHAR) * len);
    if (!strNamedPipeNameIn) {
        outOfMemory(TEXT("MSE"), 1);
        return wrapperData->errorExitCode;
    }
    _sntprintf(strNamedPipeNameIn, len, TEXT("\\\\.\\pipe\\%sINN"), namedPipeName);

    strNamedPipeNameOut = malloc(sizeof(TCHAR) * len);
    if (!strNamedPipeNameOut) {
        free(strNamedPipeNameIn);
        outOfMemory(TEXT("MSE"), 2);
        return wrapperData->errorExitCode;
    }
    _sntprintf(strNamedPipeNameOut, len, TEXT("\\\\.\\pipe\\%sOUT"), namedPipeName);

    strNamedPipeNameErr = malloc(sizeof(TCHAR) * len);
    if (!strNamedPipeNameErr) {
        free(strNamedPipeNameIn);
        free(strNamedPipeNameOut);
        outOfMemory(TEXT("MSE"), 3);
        return wrapperData->errorExitCode;
    }
    _sntprintf(strNamedPipeNameErr, len, TEXT("\\\\.\\pipe\\%sERR"), namedPipeName);
    /* create the process information */
    memset(&shex, 0, sizeof(shex));
    shex.cbSize         = sizeof(SHELLEXECUTEINFO);
    shex.fMask          = SEE_MASK_NO_CONSOLE | SEE_MASK_NOCLOSEPROCESS;
    shex.hwnd           = hwnd;
    shex.lpVerb         = pszVerb;
    shex.lpFile         = pszPath;
    shex.lpParameters   = pszParameters;
    shex.lpDirectory    = pszDirectory;
#ifdef _DEBUG
    shex.nShow          = SW_SHOWNORMAL;
#else
    shex.nShow          = SW_HIDE;
#endif

    hNamedPipeIn = CreateNamedPipe(strNamedPipeNameIn, PIPE_ACCESS_OUTBOUND ,
                            PIPE_TYPE_BYTE |       // message type pipe
                            PIPE_READMODE_BYTE |   // message-read mode
                            PIPE_WAIT,             // blocking mode
                            1,                     // max. instances
                            1024 * sizeof(TCHAR),  // output buffer size
                            1024*sizeof(TCHAR),    // input buffer size
                            0,                     // client time-out
                            NULL);                 // default security attribute

    if (hNamedPipeIn == INVALID_HANDLE_VALUE) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Stdin CreateNamedPipe failed (%d): %s"), GetLastError(), getLastErrorText());
        ret = wrapperData->errorExitCode;
    } else {
            hNamedPipeOut = CreateNamedPipe(strNamedPipeNameOut, PIPE_ACCESS_INBOUND ,
                                PIPE_TYPE_MESSAGE |       // message type pipe
                                PIPE_READMODE_MESSAGE |   // message-read mode
                                PIPE_NOWAIT,              // nonblocking mode
                                1,                        // max. instances
                                512 * sizeof(TCHAR),      // output buffer size
                                512 * sizeof(TCHAR),      // input buffer size
                                0,                        // client time-out
                                NULL);                    // default security attribute

        if (hNamedPipeOut == INVALID_HANDLE_VALUE) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Stdout CreateNamedPipe failed (%d): %s"), GetLastError(), getLastErrorText());
            ret = wrapperData->errorExitCode;
        } else {
            hNamedPipeErr = CreateNamedPipe(strNamedPipeNameErr, PIPE_ACCESS_INBOUND ,
                                    PIPE_TYPE_MESSAGE |       // message type pipe
                                    PIPE_READMODE_MESSAGE |   // message-read mode
                                    PIPE_NOWAIT,              // nonblocking mode
                                    1,                        // max. instances
                                    512 * sizeof(TCHAR),      // output buffer size
                                    512 * sizeof(TCHAR),      // input buffer size
                                    0,                        // client time-out
                                    NULL);                    // default security attribute

            if (hNamedPipeErr == INVALID_HANDLE_VALUE) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Stderr CreateNamedPipe failed (%d): %s"), GetLastError(), getLastErrorText());
                ret = wrapperData->errorExitCode;
            } else {
                /* Now launch the process */
                if (ShellExecuteEx(&shex) == TRUE) {
                    if (shex.hProcess != NULL) {
                        /* now read and write the pipes */
                        if (readAndWriteNamedPipes(hNamedPipeIn, hNamedPipeOut, hNamedPipeErr) != TRUE) {
                            // the error should have already been reported.
                        }
                        /* Wait up to 1 sec to check if the elevated process really exited */
                        returnValue = WaitForSingleObject(shex.hProcess, 1000);
                        if (returnValue == WAIT_OBJECT_0) {
                            if (!GetExitCodeProcess(shex.hProcess, &ret)) {
                                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("WaitThread for Backend-Process: %s failed! (%d): %s"), TEXT("GetExitCodeProcess"), GetLastError(), getLastErrorText());
                                ret = wrapperData->errorExitCode;
                            }
                        } else {
                            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("The elevated Wrapper process is still alive. Trying to kill it. (%d): %s"), GetLastError(), getLastErrorText());
                            if (TerminateProcess(shex.hProcess, 1) == 0) {
                                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Failed to kill the elevated Wrapper process. (%d): %s"), GetLastError(), getLastErrorText());
                            }
                            ret = wrapperData->errorExitCode;
                        }
                    }
                } else {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Failed to obtain elevated status. (%d): %s"), GetLastError(), getLastErrorText());
                    ret = wrapperData->errorExitCode;
                }
                CloseHandle(hNamedPipeErr);
            }
            CloseHandle(hNamedPipeOut);
        }
        CloseHandle(hNamedPipeIn);
    }


    free(strNamedPipeNameIn);
    free(strNamedPipeNameOut);
    free(strNamedPipeNameErr);

    return ret;
}


/*
 * This is just a wrapper function between elevateThis and myShellExec filling in the verb
 * For more information please refer to myShellExec
 */
int runElevated(__in LPCTSTR pszPath, __in_opt LPCTSTR pszParameters, __in_opt LPCTSTR pszDirectory, TCHAR* namedPipeName) {
    return myShellExec(NULL, TEXT("runas"), pszPath, pszParameters, pszDirectory, namedPipeName);
}

/*
 * This is the entry point on the user side for creating an elevated process.
 * UAC does not allow to give a running process elevated privileges, so the
 * wrapper has to create a copy of the current process, arm it with elevated
 * privileges and take care of IPC.
 *
 * @return exit code of backend process
 */
int elevateThis(int argc, TCHAR **argv) {
    int i, ret = 0;
    size_t len = 0;
    TCHAR szPath[_MAX_PATH];
    TCHAR *parameter;
    TCHAR* strNamedPipeName;
    const TCHAR* commandProps = TEXT("wrapper.internal.namedpipe=");

    /* get the file name of the binary, we can't trust argv[0] as the working
     * directory might have been changed.
     */
    if (GetModuleFileName(NULL, szPath, _MAX_PATH)) {
        /* seed the pseudo-random generator */
        srand((unsigned)time(NULL));
        strNamedPipeName = malloc(sizeof(TCHAR) * 11);
        if (!strNamedPipeName) {
            outOfMemory(TEXT("MSE"), 1);
            return wrapperData->errorExitCode;
        }
        /* create a pseudo-random 10 digit string */
        _sntprintf(strNamedPipeName, 11, TEXT("%05d%05d"), rand() % 100000, rand() % 100000);
        /* ShellExecuteEx is expecting the parameter in a single string */
        for (i = 1; i < argc; i++) {
            /* if '--' was specified, wrapperParseArguments has replaced this parameter with NULL */
            if (argv[i] == NULL) {
                len += 3;
            } else {
                /* insert a space and quotes */
                len += _tcslen(argv[i]) + 3;
            }
        }
        if (wrapperData->argCommandArg) {
            len += _tcslen(wrapperData->argCommandArg) + 1;
        }
        len += _tcslen(commandProps) + _tcslen(strNamedPipeName) + 1;
        parameter = calloc(len, sizeof(TCHAR));
        if (!parameter) {
            outOfMemory(TEXT("ET"), 1);
            return wrapperData->errorExitCode;
        }
        
        /* append the command, conf file and any parameter before the command line properties and java additionals */
        for (i = 1; i < argc; i++) {
            if ((argv[i] == NULL) || ((wrapperData->argCount > 0) && (_tcscmp(wrapperData->argValues[0], argv[i]) == 0))) {
                /* hit '--' or a command line property */
                break;
            }
            _tcsncat(parameter, TEXT("\""), len);
            _tcsncat(parameter, argv[i], len);
            if ((i == 1) && wrapperData->argCommandArg) {
                _tcsncat(parameter, TEXT("="), len);
                _tcsncat(parameter, wrapperData->argCommandArg, len);
            }
            _tcsncat(parameter, TEXT("\""), len);
            _tcsncat(parameter, TEXT(" "), len);
        }
        
        /* the following properties must always be in first position as we will not allow to override them. */
        _tcsncat(parameter, commandProps, len);
        _tcsncat(parameter, strNamedPipeName, len);
        
        /* continue and fill the command line properties and java additionals */
        for (; i < argc; i++) {
            _tcsncat(parameter, TEXT(" "), len);
            if (argv[i] == NULL) {
                _tcsncat(parameter, TEXT("--"), len);
            } else {
                _tcsncat(parameter, TEXT("\""), len);
                _tcsncat(parameter, argv[i], len);
                _tcsncat(parameter, TEXT("\""), len);
            }
        }
        ret = runElevated(szPath, parameter, NULL, strNamedPipeName);
        free(strNamedPipeName);
        free(parameter);
        return ret;
    }
    return wrapperData->errorExitCode;

}
#endif /* ifdef WIN32 */
