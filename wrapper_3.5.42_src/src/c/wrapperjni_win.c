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

#ifndef WIN32
/* For some reason this is not defined sometimes when I build on MFVC 6.0 $%$%$@@!!
 * This causes a compiler error to let me know about the problem.  Anyone with any
 * ideas as to why this sometimes happens or how to fix it, please let me know. */
barf
#endif

#ifdef WIN32

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntsecapi.h>
#include <ntstatus.h>
#include <io.h>
#include <time.h>
#include <tlhelp32.h>
#include <winnt.h>
#include <Sddl.h>
#include <Iphlpapi.h>
#include "loggerjni.h"
#include "wrapperjni.h"

/* The largest possible command line length on Windows. */
#define MAX_COMMAND_LINE_LEN 32766

/* MS Visual Studio 8 went and deprecated the POXIX names for functions.
 *  Fixing them all would be a big headache for UNIX versions. */
#pragma warning(disable : 4996)

/* Reference to HINSTANCE of this DLL */
EXTERN_C IMAGE_DOS_HEADER __ImageBase;

static DWORD javaProcessId = 0;

HANDLE controlEventQueueMutexHandle = NULL;

FARPROC OptionalProcess32First = NULL;
FARPROC OptionalProcess32Next = NULL;
FARPROC OptionalThread32First = NULL;
FARPROC OptionalThread32Next = NULL;
FARPROC OptionalCreateToolhelp32Snapshot = NULL;
FARPROC OptionalRaiseFailFastException = NULL;

int wrapperLockControlEventQueue() {
#ifdef _DEBUG
        _tprintf(TEXT(" wrapperLockControlEventQueue()\n"));
        fflush(NULL);
#endif
    if (!controlEventQueueMutexHandle) {
        /* Not initialized so fail quietly.  A message was shown on startup. */
        return -1;
    }

    /* Only wait for up to 30 seconds to make sure we don't get into a deadlock situation.
     *  This could happen if a signal is encountered while locked. */
    switch (WaitForSingleObject(controlEventQueueMutexHandle, 30000)) {
    case WAIT_ABANDONED:
        log_printf(TEXT("WrapperJNI Error: Control Event mutex was abandoned."));
        return -1;
    case WAIT_FAILED:
        log_printf(TEXT("WrapperJNI Error: Control Event mutex wait failed."));
        return -1;
    case WAIT_TIMEOUT:
        log_printf(TEXT("WrapperJNI Error: Control Event mutex wait timed out."));
        return -1;
    default:
        /* Ok */
        break;
    }
    return 0;
}

int wrapperReleaseControlEventQueue() {
    #ifdef _DEBUG
        _tprintf(TEXT(" wrapperReleaseControlEventQueue()\n"));
        fflush(NULL);
    #endif
    if (!ReleaseMutex(controlEventQueueMutexHandle)) {
        log_printf(TEXT( "WrapperJNI Error: Failed to release Control Event mutex. %s"), getLastErrorText());
        return -1;
    }

    return 0;
}

/**
 * Handler to take care of the case where the user hits CTRL-C when the wrapper
 *  is being run as a console.  If this is not done, then the Java process
 *  would exit due to a CTRL_LOGOFF_EVENT when a user logs off even if the
 *  application is installed as a service.
 *
 * Handlers are called in the reverse order that they are registered until one
 *  returns TRUE.  So last registered is called first until the default handler
 *  is called.  This means that if we return FALSE, the JVM'S handler will then
 *  be called.
 */
int wrapperConsoleHandler(int key) {
    int event;

    /* Call the control callback in the java code */
    switch(key) {
    case CTRL_C_EVENT:
        event = org_tanukisoftware_wrapper_WrapperManager_WRAPPER_CTRL_C_EVENT;
        break;
    case CTRL_BREAK_EVENT:
        /* This is a request to do a thread dump. Let the JVM handle this. */
        return FALSE;
    case CTRL_CLOSE_EVENT:
        event = org_tanukisoftware_wrapper_WrapperManager_WRAPPER_CTRL_CLOSE_EVENT;
        break;
    case CTRL_LOGOFF_EVENT:
        event = org_tanukisoftware_wrapper_WrapperManager_WRAPPER_CTRL_LOGOFF_EVENT;
        break;
    case CTRL_SHUTDOWN_EVENT:
        event = org_tanukisoftware_wrapper_WrapperManager_WRAPPER_CTRL_SHUTDOWN_EVENT;
        break;
    default:
        event = key;
    }
    if (wrapperJNIDebugging) {
        log_printf(TEXT("WrapperJNI Debug: Got Control Signal %d->%d"), key, event);
    }

    wrapperJNIHandleSignal(event);

    if (wrapperJNIDebugging) {
        log_printf(TEXT("WrapperJNI Debug: Handled signal"));
    }

    return TRUE; /* We handled the event. */
}

/**
 * Looks up the name of the explorer.exe file in the registry.  It may change
 *  in a future version of windows, so this is the safe thing to do.
 */
TCHAR explorerExe[1024];
void
initExplorerExeName() {
    /* Location: "\\HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\Shell" */
    _sntprintf(explorerExe, 1024, TEXT("Explorer.exe"));
}

void throwException(JNIEnv *env, const char *className, int jErrorCode, const TCHAR *message) {
    jclass exceptionClass;
    jmethodID constructor;
    jbyteArray jMessage;
    jobject exception;

    if (exceptionClass = (*env)->FindClass(env, className)) {
        /* Look for the constructor. Ignore failures. */
        if (constructor = (*env)->GetMethodID(env, exceptionClass, "<init>", "(I[B)V")) {
            jMessage = (*env)->NewByteArray(env, (jsize)_tcslen(message) * sizeof(TCHAR));
            /* The 1.3.1 jni.h file does not specify the message as const.  The cast is to
             *  avoid compiler warnings trying to pass a (const TCHAR *) as a (TCHAR *). */
            JNU_SetByteArrayRegion(env, &jMessage, 0, (jsize)_tcslen(message) * sizeof(TCHAR), message);

            exception = (*env)->NewObject(env, exceptionClass, constructor, jErrorCode, jMessage);

            if ((*env)->Throw(env, exception)) {
                log_printf(TEXT("WrapperJNI Error: Unable to throw exception of class '%s' with message: %s"),
                    className, message);
            }

            (*env)->DeleteLocalRef(env, jMessage);
            (*env)->DeleteLocalRef(env, exception);
        }

        (*env)->DeleteLocalRef(env, exceptionClass);
    } else {
        log_printf(TEXT("WrapperJNI Error: Unable to load class, '%s' to report exception: %s"),
            className, message);
    }
}

void throwServiceException(JNIEnv *env, int errorCode, const TCHAR *message) {
    throwException(env, "org/tanukisoftware/wrapper/WrapperServiceException", errorCode, message);
}

/**
 * Converts a FILETIME to a time_t structure.
 */
time_t fileTimeToTimeT(FILETIME *filetime) {
    SYSTEMTIME utc;
    SYSTEMTIME local;
    TIME_ZONE_INFORMATION timeZoneInfo;
    struct tm tm;

    FileTimeToSystemTime(filetime, &utc);
    GetTimeZoneInformation(&timeZoneInfo);
    SystemTimeToTzSpecificLocalTime(&timeZoneInfo, &utc, &local);

    tm.tm_sec = local.wSecond;
    tm.tm_min = local.wMinute;
    tm.tm_hour = local.wHour;
    tm.tm_mday = local.wDay;
    tm.tm_mon = local.wMonth - 1;
    tm.tm_year = local.wYear - 1900;
    tm.tm_wday = local.wDayOfWeek;
    tm.tm_yday = -1;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

/**
 * Looks for the login time given a user SID.  The login time is found by looking
 *  up the SID in the registry.
 */
time_t getUserLoginTime(TCHAR *sidText) {
    LONG     result;
    LPSTR    pBuffer = NULL;
    HKEY     userKey;
    int      i;
    TCHAR    userKeyName[MAX_PATH];
    DWORD    userKeyNameSize;
    FILETIME lastTime;
    time_t   loginTime;

    loginTime = 0;

    /* Open a key to the HKRY_USERS registry. */
    result = RegOpenKey(HKEY_USERS, NULL, &userKey);
    if (result != ERROR_SUCCESS) {
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, result, 0, (LPTSTR)&pBuffer, 0, NULL);
        log_printf(TEXT("WrapperJNI Error: Error opening registry for HKEY_USERS: %s"), getLastErrorText());
        LocalFree(pBuffer);
        return loginTime;
    }

    /* Loop over the users */
    i = 0;
    userKeyNameSize = sizeof(userKeyName);
    while ((result = RegEnumKeyEx(userKey, i, userKeyName, &userKeyNameSize, NULL, NULL, NULL, &lastTime)) == ERROR_SUCCESS) {
        if (_tcsicmp(sidText, userKeyName) == 0) {
            /* We found the SID! */
            /* Convert the FILETIME to UNIX time. */
            loginTime = fileTimeToTimeT(&lastTime);
            break;
        }

        userKeyNameSize = sizeof(userKeyName);
        i++;
    }
    if (result != ERROR_SUCCESS) {
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, result, 0, (LPTSTR)&pBuffer, 0, NULL);
        log_printf(TEXT("WrapperJNI Error: Unable to enumerate the registry: %d : %s"), result, pBuffer);
        LocalFree(pBuffer);
    }

    /* Always close the userKey. */
    result = RegCloseKey(userKey);
    if (result != ERROR_SUCCESS) {
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, result, 0, (LPTSTR)&pBuffer, 0, NULL);
        log_printf(TEXT("WrapperJNI Error: Unable to close the registry: %d : %s"), result, pBuffer);
        LocalFree(pBuffer);
    }
    return loginTime;
}

/**
 * Sets group information in a user object.
 *
 * Returns TRUE if there were any problems.
 */
int setUserGroups(JNIEnv *env, jclass wrapperUserClass, jobject wrapperUser, HANDLE hProcessToken) {
    jmethodID addGroup;

    TOKEN_GROUPS *tokenGroups;
    DWORD tokenGroupsSize;
    DWORD i;

    TCHAR *sidText;
    TCHAR *groupName;
    DWORD groupNameSize;
    TCHAR *domainName;
    DWORD domainNameSize;
    SID_NAME_USE sidType;

    jstring jstringSID;
    jstring jstringGroupName;
    jstring jstringDomainName;

    int result = FALSE;

    /* Look for the method used to add groups to the user. */
    if (addGroup = (*env)->GetMethodID(env, wrapperUserClass, "addGroup", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V")) {
        /* Get the TokenGroups info from the token. */
        GetTokenInformation(hProcessToken, TokenGroups, NULL, 0, &tokenGroupsSize);
        tokenGroups = (TOKEN_GROUPS *)malloc(tokenGroupsSize);
        if (!tokenGroups) {
            throwOutOfMemoryError(env, TEXT("SUG1"));
            result = TRUE;
        } else {
            if (GetTokenInformation(hProcessToken, TokenGroups, tokenGroups, tokenGroupsSize, &tokenGroupsSize)) {
                /* Loop over each of the groups and add each one to the user. */
                for (i = 0; i < tokenGroups->GroupCount; i++) {
                    /* Get the text representation of the sid. */
                    if (ConvertSidToStringSid(tokenGroups->Groups[i].Sid, &sidText) == 0) {
                        log_printf(TEXT("WrapperJNI Error: Failed to convert SId to String: %s"), getLastErrorText());
                        result = TRUE;
                    } else {
                        /* We now have an SID, use it to lookup the account. */
                        groupNameSize = 0;
                        domainNameSize = 0;
                        LookupAccountSid(NULL, tokenGroups->Groups[i].Sid, NULL, &groupNameSize, NULL, &domainNameSize, &sidType);
                        groupName = (TCHAR*)malloc(sizeof(TCHAR) * groupNameSize);
                        if (!groupName) {
                            throwOutOfMemoryError(env, TEXT("SUG3"));
                            result = TRUE;
                        } else {
                            domainName = (TCHAR*)malloc(sizeof(TCHAR) * domainNameSize);
                            if (!domainName) {
                                throwOutOfMemoryError(env, TEXT("SUG4"));
                                result = TRUE;
                            } else {
                                if (LookupAccountSid(NULL, tokenGroups->Groups[i].Sid, groupName, &groupNameSize, domainName, &domainNameSize, &sidType)) {
                                    /* Create the arguments to the constructor as java objects */
                                    /* SID byte array */
                                    jstringSID = JNU_NewStringFromNativeW(env, sidText);
                                    if (jstringSID) {
                                        /* GroupName byte array */
                                        jstringGroupName = JNU_NewStringFromNativeW(env, groupName);
                                        if (jstringGroupName) {
                                            /* DomainName byte array */
                                            jstringDomainName = JNU_NewStringFromNativeW(env, domainName);
                                            if (jstringDomainName) {
                                                /* Now actually add the group to the user. */
                                                (*env)->CallVoidMethod(env, wrapperUser, addGroup, jstringSID, jstringGroupName, jstringDomainName);

                                                (*env)->DeleteLocalRef(env, jstringDomainName);
                                            } else {
                                                /* Exception Thrown */
                                                break;
                                            }

                                            (*env)->DeleteLocalRef(env, jstringGroupName);
                                        } else {
                                            /* Exception Thrown */
                                            break;
                                        }

                                        (*env)->DeleteLocalRef(env, jstringSID);
                                    } else {
                                        /* Exception Thrown */
                                        break;
                                    }
                                } else {
                                    /* This is normal as some accounts do not seem to be mappable. */
                                    /*
                                    log_printf(TEXT("WrapperJNI Debug: Unable to locate account for Sid, %s: %s"), sidText, getLastErrorText());
                                    */
                                }
                                free(domainName);
                            }

                            free(groupName);
                        }

                        LocalFree(sidText);
                    }
                }
            } else {
                log_printf(TEXT("WrapperJNI Error: Unable to get token information: %s"), getLastErrorText());
            }

            free(tokenGroups);
        }
    } else {
        /* Exception Thrown */
    }

    return result;
}

/**
 * Creates and returns a WrapperUser instance to represent the user who owns
 *  the specified process Id.
 */
jobject createWrapperUserForProcess(JNIEnv *env, DWORD processId, jboolean groups) {
    HANDLE hProcess;
    HANDLE hProcessToken;
    TOKEN_USER *tokenUser;
    DWORD tokenUserSize;

    TCHAR *sidText;
    TCHAR *userName;
    DWORD userNameSize;
    TCHAR *domainName;
    DWORD domainNameSize;
    SID_NAME_USE sidType;
    time_t loginTime;

    jclass wrapperUserClass;
    jmethodID constructor;
    jstring jstringSID;
    jstring jstringUserName;
    jstring jstringDomainName;
    jobject wrapperUser = NULL;

    if (hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processId)) {
        if (OpenProcessToken(hProcess, TOKEN_QUERY, &hProcessToken)) {
            GetTokenInformation(hProcessToken, TokenUser, NULL, 0, &tokenUserSize);
            tokenUser = (TOKEN_USER *)malloc(tokenUserSize);
            if (!tokenUser) {
                throwOutOfMemoryError(env, TEXT("CWUFP1"));
            } else {
                if (GetTokenInformation(hProcessToken, TokenUser, tokenUser, tokenUserSize, &tokenUserSize)) {
                    /* Get the text representation of the sid. */
                    if (ConvertSidToStringSid(tokenUser->User.Sid, &sidText) == 0) {
                        log_printf(TEXT("Failed to convert SId to String: %s"), getLastErrorText());
                    } else {
                        /* We now have an SID, use it to lookup the account. */
                        userNameSize = 0;
                        domainNameSize = 0;
                        LookupAccountSid(NULL, tokenUser->User.Sid, NULL, &userNameSize, NULL, &domainNameSize, &sidType);
                        userName = (TCHAR*)malloc(sizeof(TCHAR) * userNameSize);
                        if (!userName) {
                            throwOutOfMemoryError(env, TEXT("CWUFP3"));
                        } else {
                            domainName = (TCHAR*)malloc(sizeof(TCHAR) * domainNameSize);
                            if (!domainName) {
                                throwOutOfMemoryError(env, TEXT("CWUFP4"));
                            } else {
                                if (LookupAccountSid(NULL, tokenUser->User.Sid, userName, &userNameSize, domainName, &domainNameSize, &sidType)) {
                                    /* Get the time that this user logged in. */
                                    loginTime = getUserLoginTime(sidText);

                                    /* Look for the WrapperUser class. Ignore failures as JNI throws an exception. */
                                    if (wrapperUserClass = (*env)->FindClass(env, "org/tanukisoftware/wrapper/WrapperWin32User")) {

                                        /* Look for the constructor. Ignore failures. */
                                        if (constructor = (*env)->GetMethodID(env, wrapperUserClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V")) {

                                            /* Create the arguments to the constructor as java objects */
                                            /* SID */
                                            jstringSID = JNU_NewStringFromNativeW(env, sidText);
                                            if (jstringSID) {
                                                /* UserName */
                                                jstringUserName = JNU_NewStringFromNativeW(env, userName);
                                                if (jstringUserName) {
                                                    /* DomainName */
                                                    jstringDomainName = JNU_NewStringFromNativeW(env, domainName);
                                                    if (jstringDomainName) {
                                                        /* Now create the new wrapperUser using the constructor arguments collected above. */
                                                        wrapperUser = (*env)->NewObject(env, wrapperUserClass, constructor, jstringSID, jstringUserName, jstringDomainName, loginTime);

                                                        /* If the caller requested the user's groups then look them up. */
                                                        if (groups) {
                                                            if (setUserGroups(env, wrapperUserClass, wrapperUser, hProcessToken)) {
                                                                /* Failed. Just continue without groups. */
                                                            }
                                                        }

                                                        (*env)->DeleteLocalRef(env, jstringDomainName);
                                                    } else {
                                                        /* Exception Thrown */
                                                    }

                                                    (*env)->DeleteLocalRef(env, jstringUserName);
                                                } else {
                                                    /* Exception Thrown */
                                                }

                                                (*env)->DeleteLocalRef(env, jstringSID);
                                            } else {
                                                /* Exception Thrown */
                                            }
                                        } else {
                                            /* Exception Thrown */
                                        }

                                        (*env)->DeleteLocalRef(env, wrapperUserClass);
                                    } else {
                                        /* Exception Thrown */
                                    }
                                } else {
                                    /* This is normal as some accounts do not seem to be mappable. */
                                }
                                free(domainName);
                            }

                            free(userName);
                        }

                        LocalFree(sidText);
                    }
                } else {
                    log_printf(TEXT("WrapperJNI Error: Unable to get token information: %s"), getLastErrorText());
                }

                free(tokenUser);
            }

            CloseHandle(hProcessToken);
        } else {
            log_printf(TEXT("WrapperJNI Error: Unable to open process token: %s"), getLastErrorText());
        }

        CloseHandle(hProcess);
    } else {
        log_printf(TEXT("WrapperJNI Error: Unable to open process: %s"), getLastErrorText());
    }

    return wrapperUser;
}

HMODULE kernel32Mod;
void loadDLLProcs() {
    if ((kernel32Mod = GetModuleHandle(TEXT("KERNEL32.DLL"))) == NULL) {
        log_printf(TEXT("WrapperJNI Error: Unable to load %s: %s"), TEXT("KERNEL32.DLL"), getLastErrorText());
        return;
    }
#ifdef UNICODE
    if ((OptionalProcess32First = GetProcAddress(kernel32Mod, "Process32FirstW")) == NULL) {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: The %s function is not available on this version of Windows."), TEXT("Process32FirstW"));
        }
    }
#else
    if ((OptionalProcess32First = GetProcAddress(kernel32Mod, "Process32First")) == NULL) {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: The %s function is not available on this version of Windows."), TEXT("Process32First"));
        }
    }
#endif
#ifdef UNICODE
    if ((OptionalProcess32Next = GetProcAddress(kernel32Mod, "Process32NextW")) == NULL) {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: The %s function is not available on this version of Windows."), TEXT("Process32NextW"));
        }
    }
#else
    if ((OptionalProcess32Next = GetProcAddress(kernel32Mod, "Process32Next")) == NULL) {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: The %s function is not available on this version of Windows."), TEXT("Process32Next"));
        }
    }
#endif
    if ((OptionalThread32First = GetProcAddress(kernel32Mod, "Thread32First")) == NULL) {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: The %s function is not available on this version of Windows."), TEXT("Thread32First"));
        }
    }
    if ((OptionalThread32Next = GetProcAddress(kernel32Mod, "Thread32Next")) == NULL) {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: The %s function is not available on this version of Windows."), TEXT("Thread32Next"));
        }
    }
    if ((OptionalCreateToolhelp32Snapshot = GetProcAddress(kernel32Mod, "CreateToolhelp32Snapshot")) == NULL) {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: The %s function is not available on this version of Windows."), TEXT("CreateToolhelp32Snapshot"));
        }
    }
    if ((OptionalRaiseFailFastException = GetProcAddress(kernel32Mod, "RaiseFailFastException")) == NULL) {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: The %s function is not available on this version of Windows."), TEXT("RaiseFailFastException"));
        }
    }
}

const TCHAR* getExceptionName(DWORD exCode) {
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
        exName = TEXT("EXCEPTION_UNKNOWN");
        break;
    }

    return exName;
}

void commonExceptionInfo(PEXCEPTION_POINTERS pExceptionInfo) {
    int i;
    
    log_printf(TEXT("WrapperJNI Error: ExceptionCode: %s (0x%x)"), getExceptionName(pExceptionInfo->ExceptionRecord->ExceptionCode), pExceptionInfo->ExceptionRecord->ExceptionCode);
    
    switch (pExceptionInfo->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
        if (pExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
            switch (pExceptionInfo->ExceptionRecord->ExceptionInformation[0]) {
            case 0:
                log_printf(TEXT("WrapperJNI Error:   Read access exception from 0x%p"), pExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
                break;
            case 1:
                log_printf(TEXT("WrapperJNI Error:   Write access exception to 0x%p"), pExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
                break;
            case 8:
                log_printf(TEXT("WrapperJNI Error:   DEP access exception to 0x%p"), pExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
                break;
            default:
                log_printf(TEXT("WrapperJNI Error:   Unexpected access exception to 0x%p (%d)"), pExceptionInfo->ExceptionRecord->ExceptionInformation[1], pExceptionInfo->ExceptionRecord->ExceptionInformation[0]);
                break;
            }
        }
        if ((pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && (pExceptionInfo->ExceptionRecord->NumberParameters >= 3)) {
            log_printf(TEXT("WrapperJNI Error:   Status Code: %d"), pExceptionInfo->ExceptionRecord->ExceptionInformation[2]);
        }
        break;
        
    default:
        for (i = 0; i < (int)pExceptionInfo->ExceptionRecord->NumberParameters; i++) {
            log_printf(TEXT("WrapperJNI Error:   ExceptionInformation[%d] = %ld"), i, pExceptionInfo->ExceptionRecord->ExceptionInformation[i]);
        }
        break;
    }
    
    log_printf(TEXT("WrapperJNI Error: ExceptionFlags: %s (0x%x)"), (pExceptionInfo->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE_EXCEPTION ? TEXT("EXCEPTION_NONCONTINUABLE_EXCEPTION") : TEXT("")), pExceptionInfo->ExceptionRecord->ExceptionFlags);
}
LONG WINAPI wrapperVectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo) {
    log_printf(TEXT("WrapperJNI Error: ============================================================"));
    log_printf(TEXT("WrapperJNI Error: Detected an exception in the Java process."));
    commonExceptionInfo(pExceptionInfo);
    log_printf(TEXT("WrapperJNI Error: ============================================================"));
    
    return EXCEPTION_CONTINUE_SEARCH;
}
        
/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeInit
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeInit(JNIEnv *env, jclass jClassWrapperManager, jboolean debugging) {
    TCHAR szPath[_MAX_PATH];
    DWORD usedLen;
    OSVERSIONINFO osVer;
    TCHAR *registerNativeHandlerValue;
    DWORD shutdownlevel;
    DWORD shutdownflags;
    
    wrapperJNIDebugging = debugging;

    /* Set the locale so we can display MultiByte characters. */
    _tsetlocale(LC_ALL, TEXT(""));
    initLog(env);

    if (wrapperJNIDebugging) {
        /* This is useful for making sure that the JNI call is working. */
        log_printf(TEXT("WrapperJNI Debug: Initializing WrapperManager native library."));

        /* Important : For win XP getLastError() is unchanged if the buffer is too small, so if we don't reset the last error first, we may actually test an old pending error. */
        SetLastError(ERROR_SUCCESS);
        usedLen = GetModuleFileName(NULL, szPath, _MAX_PATH);
        if (usedLen == 0) {
            log_printf(TEXT("WrapperJNI Debug: Unable to retrieve the Java process file name. %s"), getLastErrorText());
        } else if ((usedLen == _MAX_PATH) || (getLastError() == ERROR_INSUFFICIENT_BUFFER)) {
            log_printf(TEXT("WrapperJNI Debug: Unable to retrieve the Java process file name. %s"), TEXT("Path too long."));
        } else {
            log_printf(TEXT("WrapperJNI Debug: Java Executable: %s"), szPath);
        }

        /* Important : For win XP getLastError() is unchanged if the buffer is too small, so if we don't reset the last error first, we may actually test an old pending error. */
        SetLastError(ERROR_SUCCESS);
        usedLen = GetModuleFileName((HINSTANCE)&__ImageBase, szPath, _MAX_PATH);
        if (usedLen == 0) {
            log_printf(TEXT("WrapperJNI Debug: Unable to retrieve the native library file name. %s"), getLastErrorText());
        } else if ((usedLen == _MAX_PATH) || (getLastError() == ERROR_INSUFFICIENT_BUFFER)) {
            log_printf(TEXT("WrapperJNI Debug: Unable to retrieve the native library file name. %s"), TEXT("Path too long."));
        } else {
            log_printf(TEXT("WrapperJNI Debug: Native Library: %s"), szPath);
        }
    }

    /* Lets define the shutdown priority lower than wrapper.exe, meaning the system will wait that wrapper.exe shuts down completely before killing the Java process.
     *  By default, the shutdown level inherits from the parent process. */
    if (!GetProcessShutdownParameters(&shutdownlevel, &shutdownflags)) {
        log_printf(TEXT("WrapperJNI Error: Failed to retrieve the shutdown parameters."));
        log_printf(TEXT("WrapperJNI Error:  If the system shuts down while the application is still running, the Java process will be forcibly killed."));
    } else if (!SetProcessShutdownParameters(shutdownlevel-1, SHUTDOWN_NORETRY)) {
        log_printf(TEXT("WrapperJNI Error: Failed to set the shutdown parameters."));
        log_printf(TEXT("WrapperJNI Error:  If the system shuts down while the application is still running, the Java process will be forcibly killed."));
    }

    if (initCommon(env, jClassWrapperManager)) {
        /* Failed.  An exception will have been thrown. */
        return;
    }
    
    /* Register exception handlers. */
    if (!getSystemProperty(env, TEXT("wrapper.register_native_handler"), &registerNativeHandlerValue, FALSE)) {
        /* Default to no registered handlers. */
        if ((registerNativeHandlerValue != NULL) && (strcmpIgnoreCase(registerNativeHandlerValue, TEXT("TRUE")) == 0)) {
            /*  VectoredExceptionHander is called whenever any Exception is thrown, regardless of whether or not it is caught by its surounding code. */
            AddVectoredExceptionHandler(1, wrapperVectoredExceptionHandler);
        }
        if (registerNativeHandlerValue != NULL) {
            free(registerNativeHandlerValue);
            registerNativeHandlerValue = NULL;
        }
    }

    osVer.dwOSVersionInfoSize = sizeof(osVer);
#pragma warning(push)
#pragma warning(disable : 4996) /* Visual Studio 2013 deprecates GetVersionEx but we still want to use it. */    
    if (GetVersionEx(&osVer)) {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: Windows version: %ld.%ld.%ld"),
                osVer.dwMajorVersion, osVer.dwMinorVersion, osVer.dwBuildNumber);
        }
    } else {
        log_printf(TEXT("WrapperJNI Error: Unable to retrieve the Windows version information."));
    }
#pragma warning(pop)
    loadDLLProcs();
    if (!(controlEventQueueMutexHandle = CreateMutex(NULL, FALSE, NULL))) {
        log_printf(TEXT("WrapperJNI Error: Failed to create control event queue mutex. Signals will be ignored. %s"), getLastErrorText());
        controlEventQueueMutexHandle = NULL;
    }

    /* Make sure that the handling of CTRL-C signals is enabled for this process. */
    if (!SetConsoleCtrlHandler(NULL, FALSE)) {
        log_printf(TEXT("WrapperJNI Error: Attempt to reset control signal handlers failed. %s"), getLastErrorText());
    }

    /* Initialize the CTRL-C handler */
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)wrapperConsoleHandler, TRUE)) {
        log_printf(TEXT("WrapperJNI Error: Attempt to register a control signal handler failed. %s"), getLastErrorText());
    }

    /* Store the current process Id */
    javaProcessId = GetCurrentProcessId();

    /* Initialize the explorer.exe name. */
    initExplorerExeName();
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeRaiseExceptionInner
 * Signature: (I)V
 */
JNIEXPORT void JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeRaiseExceptionInner(JNIEnv *env, jclass clazz, jint code) {
    log_printf(TEXT("WrapperJNI Warn: Raising Exception 0x%08x..."), code);
    
    RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeRaiseFailFastExceptionInner
 * Signature: ()V
 */
JNIEXPORT void JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeRaiseFailFastExceptionInner(JNIEnv *env, jclass clazz) {
    if (OptionalRaiseFailFastException != NULL) {
        log_printf(TEXT("WrapperJNI Warn: Raising FailFastException..."));
        
        OptionalRaiseFailFastException(NULL, NULL, 0x1/*FAIL_FAST_GENERATE_EXCEPTION_ADDRESS*/);
    } else {
        log_printf(TEXT("WrapperJNI: FailFastException not available on this version of Windows."));
    }
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeRedirectPipes
 * Signature: ()I
 */
JNIEXPORT jint JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeRedirectPipes(JNIEnv *evn, jclass clazz) {
    /* We don't need to do anything on Windows. */
    return 0;
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeGetJavaPID
 * Signature: ()I
 */
JNIEXPORT jint JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeGetJavaPID(JNIEnv *env, jclass clazz) {
    return GetCurrentProcessId();
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeRequestThreadDump
 * Signature: ()V
 */
JNIEXPORT void JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeRequestThreadDump(JNIEnv *env, jclass clazz) {
    if (wrapperJNIDebugging) {
        log_printf(TEXT("WrapperJNI Debug: Sending BREAK event to process group %ld."), javaProcessId);
    }
    if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, javaProcessId) == 0) {
        if (getLastError() == 6) {
            log_printf(TEXT("WrapperJNI Error: Unable to send BREAK event to JVM process because it does not have a console."));
        } else {
            log_printf(TEXT("WrapperJNI Error: Unable to send BREAK event to JVM process: %s"),
                getLastErrorText());
        }
    }
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeSetConsoleTitle
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeSetConsoleTitle(JNIEnv *env, jclass clazz, jstring jstringTitle) {
    TCHAR *title;

    title = JNU_GetNativeWFromString(env, jstringTitle);
    if (!title) {
        throwOutOfMemoryError(env, TEXT("NSCT1"));
    } else {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: Setting the console title to: %s"), title);
        }

        SetConsoleTitle(title);

        free(title);
    }
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeGetUser
 * Signature: (Z)Lorg/tanukisoftware/wrapper/WrapperUser;
 */
/*#define UVERBOSE*/
JNIEXPORT jobject JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeGetUser(JNIEnv *env, jclass clazz, jboolean groups) {
    DWORD processId;

#ifdef UVERBOSE
    log_printf(TEXT("WrapperJNI Debug: nativeGetUser()"));
#endif

    /* Get the current processId. */
    processId = GetCurrentProcessId();

    return createWrapperUserForProcess(env, processId, groups);
}


/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeGetInteractiveUser
 * Signature: (Z)Lorg/tanukisoftware/wrapper/WrapperUser;
 */
/*#define IUVERBOSE*/
JNIEXPORT jobject JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeGetInteractiveUser(JNIEnv *env, jclass clazz, jboolean groups) {
    HANDLE snapshot;
    PROCESSENTRY32 processEntry;
    THREADENTRY32 threadEntry;
    BOOL foundThread;
    HDESK desktop;
    jobject wrapperUser = NULL;

#ifdef IUVERBOSE
    log_printf(TEXT("WrapperJNI Debug: nativeGetInteractiveUser()"));
#endif

    /* This function will only work if all required optional functions existed. */
    if ((OptionalProcess32First == NULL) || (OptionalProcess32Next == NULL) ||
            (OptionalThread32First == NULL) || (OptionalThread32Next == NULL) ||
            (OptionalCreateToolhelp32Snapshot == NULL)) {
        if (wrapperJNIDebugging) {
            log_printf(TEXT("WrapperJNI Debug: getInteractiveUser not supported on this platform."));
        }
        return NULL;
    }

    /* In order to be able to return the interactive user, we first need to locate the
     *  logged on user whose desktop we are able to open.  On XP systems, there will be
     *  more than one user with a desktop, but only the first one to log on will allow
     *  us to open its desktop.  On all NT systems, there will be additional logged on
     *  users if there are other services running. */
    if ((snapshot = (HANDLE)OptionalCreateToolhelp32Snapshot(TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD, 0)) >= 0) {
        processEntry.dwSize = sizeof(processEntry);
        if (OptionalProcess32First(snapshot, &processEntry)) {
            do {
                /* We are only interrested in the Explorer processes. */
                if (_tcsicmp(explorerExe, processEntry.szExeFile) == 0) {
#ifdef IUVERBOSE
                    log_printf(TEXT("WrapperJNI Debug: Process size=%ld, cnt=%ld, id=%ld, parentId=%ld, moduleId=%ld, threads=%ld, exe=%s"),
                        processEntry.dwSize, processEntry.cntUsage, processEntry.th32ProcessID,
                        processEntry.th32ParentProcessID, processEntry.th32ModuleID, processEntry.cntThreads,
                        processEntry.szExeFile);
#endif

                    /* Now look for a thread which is owned by the explorer process. */
                    threadEntry.dwSize = sizeof(threadEntry);
                    if (OptionalThread32First(snapshot, &threadEntry)) {
                        foundThread = FALSE;
                        do {
                            /* We are only interrested in threads that belong to the current Explorer process. */
                            if (threadEntry.th32OwnerProcessID == processEntry.th32ProcessID) {
#ifdef IUVERBOSE
                                log_printf(TEXT("WrapperJNI Debug:   Thread Id=%ld"), threadEntry.th32ThreadID);
#endif

                                /* We have a thread, now see if we can gain access to its desktop */
                                if (desktop = GetThreadDesktop(threadEntry.th32ThreadID)) {
                                    /* We got the desktop!   We now know that this is the thread and thus
                                     *  process that we have been looking for.   Unfortunately it does not
                                     *  appear that we can get the Sid of the account directly from this
                                     *  desktop.  I tried using GetUserObjectInformation, but the Sid
                                     *  returned does not seem to map to a valid account. */

                                    wrapperUser = createWrapperUserForProcess(env, processEntry.th32ProcessID, groups);
                                } else {
#ifdef IUVERBOSE
                                    log_printf(TEXT("WrapperJNI Debug: GetThreadDesktop failed: %s"), getLastErrorText());
#endif
                                }

                                /* We only need the first thread, so break */
                                foundThread = TRUE;
                                break;
                            }
                        } while (OptionalThread32Next(snapshot, &threadEntry));

                        if (!foundThread && (GetLastError() != ERROR_NO_MORE_FILES)) {
#ifdef IUVERBOSE
                            log_printf(TEXT("WrapperJNI Debug: Unable to get next thread entry: %s"), getLastErrorText());
#endif
                        }
                    } else if (GetLastError() != ERROR_NO_MORE_FILES) {
                        log_printf(TEXT("WrapperJNI Debug: Unable to get first thread entry: %s"), getLastErrorText());
                    }
                }
            } while (OptionalProcess32Next(snapshot, &processEntry));

#ifdef IUVERBOSE
            if (GetLastError() != ERROR_NO_MORE_FILES) {
                log_printf(TEXT("WrapperJNI Debug: Unable to get next process entry: %s"), getLastErrorText());
            }
#endif
        } else if (GetLastError() != ERROR_NO_MORE_FILES) {
            log_printf(TEXT("WrapperJNI Error: Unable to get first process entry: %s"), getLastErrorText());
        }

        CloseHandle(snapshot);
    } else {
        log_printf(TEXT("WrapperJNI Error: Toolhelp snapshot failed: %s"), getLastErrorText());
    }
    return wrapperUser;
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeListServices
 * Signature: ()[Lorg/tanukisoftware/wrapper/WrapperWin32Service;
 */
JNIEXPORT jobjectArray JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeListServices(JNIEnv *env, jclass clazz) {
    TCHAR buffer[512];
    SC_HANDLE hSCManager;
    DWORD size, sizeNeeded, servicesReturned, resumeHandle;
    DWORD err;
    ENUM_SERVICE_STATUS *services = NULL;
    BOOL threwError = FALSE;
    DWORD i;

    jobjectArray serviceArray = NULL;
    jclass serviceClass;
    jmethodID constructor;
    jstring jStringName;
    jstring jStringDisplayName;
    DWORD state;
    DWORD exitCode;
    jobject service;

    hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (hSCManager) {
        /* Before we can get the list of services, we need to know how much memory it will take. */
        resumeHandle = 0;
        if (!EnumServicesStatus(hSCManager, SERVICE_WIN32, SERVICE_STATE_ALL, NULL, 0, &sizeNeeded, &servicesReturned, &resumeHandle)) {
            err = GetLastError();
            if ((err == ERROR_MORE_DATA) || (err == ERROR_INSUFFICIENT_BUFFER)) {
                /* Allocate the needed memory and call again. */
                size = sizeNeeded;
                services = malloc(size);
                if (!services) {
                    throwOutOfMemoryError(env, TEXT("NLS1"));
                } else {
                    if (!EnumServicesStatus(hSCManager, SERVICE_WIN32, SERVICE_STATE_ALL, services, size, &sizeNeeded, &servicesReturned, &resumeHandle)) {
                        /* Failed to get the services. */
                        _sntprintf(buffer, 512, TEXT("Unable to enumerate the system services: %s"),
                            getLastErrorText());
                        throwServiceException(env, GetLastError(), buffer);
                        threwError = TRUE;
                    } else {
                        /* Success. */
                    }

                    /* free(services) is done below. */
                }
            } else {
                _sntprintf(buffer, 512, TEXT("Unable to enumerate the system services: %s"),
                    getLastErrorText());
                throwServiceException(env, GetLastError(), buffer);
                threwError = TRUE;
            }
        } else {
            /* Success which means that no services were found. */
        }

        if (!threwError) {
            if (serviceClass = (*env)->FindClass(env, "org/tanukisoftware/wrapper/WrapperWin32Service")) {
                /* Look for the constructor. Ignore failures. */
                if (constructor = (*env)->GetMethodID(env, serviceClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;II)V")) {
                    serviceArray = (*env)->NewObjectArray(env, servicesReturned, serviceClass, NULL);

                    for (i = 0; i < servicesReturned; i++) {
                        jStringName = JNU_NewStringFromNativeW(env, services[i].lpServiceName);
                        if (jStringName) {
                            jStringDisplayName = JNU_NewStringFromNativeW(env, services[i].lpDisplayName);
                            if (jStringDisplayName) {
                                state = services[i].ServiceStatus.dwCurrentState;

                                exitCode = services[i].ServiceStatus.dwWin32ExitCode;
                                if (exitCode == ERROR_SERVICE_SPECIFIC_ERROR) {
                                    exitCode = services[i].ServiceStatus.dwServiceSpecificExitCode;
                                }

                                service = (*env)->NewObject(env, serviceClass, constructor, jStringName, jStringDisplayName, state, exitCode);
                                (*env)->SetObjectArrayElement(env, serviceArray, i, service);
                                (*env)->DeleteLocalRef(env, service);

                                (*env)->DeleteLocalRef(env, jStringDisplayName);
                            } else {
                                /* Exception Thrown */
                                break;
                            }
                            (*env)->DeleteLocalRef(env, jStringName);
                        } else {
                            /* Exception Thrown */
                            break;
                        }
                    }
                }

                (*env)->DeleteLocalRef(env, serviceClass);
            } else {
                /* Unable to load the service class. */
                _sntprintf(buffer, 512, TEXT("Unable to locate class org.tanukisoftware.wrapper.WrapperWin32Service"));
                throwServiceException(env, 1, buffer);
            }
        }

        if (services != NULL) {
            free(services);
        }

        /* Close the handle to the service control manager database */
        CloseServiceHandle(hSCManager);
    } else {
        /* Unable to open the service manager. */
        _sntprintf(buffer, 512, TEXT("Unable to open the Windows service control manager database: %s"),
            getLastErrorText());
        throwServiceException(env, GetLastError(), buffer);
    }

    return serviceArray;
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeSendServiceControlCode
 * Signature: (Ljava/lang/String;I)Lorg/tanukisoftware/wrapper/WrapperWin32Service;
 */
JNIEXPORT jobject JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeSendServiceControlCode(JNIEnv *env, jclass clazz, jstring jStringServiceName, jint controlCode) {
    jobject service = NULL;
    TCHAR *serviceName;
    size_t bufferSize = 2048;
    TCHAR buffer[2048];
    SC_HANDLE hSCManager;
    SC_HANDLE hService;
    int serviceAccess;
    DWORD wControlCode;
    BOOL threwError = FALSE;
    SERVICE_STATUS serviceStatus;
    jclass serviceClass;
    jmethodID constructor;
    DWORD displayNameSize;
    TCHAR *displayName;
    jstring jStringDisplayName;
    DWORD state;
    DWORD exitCode;

    if ((serviceName = JNU_GetNativeWFromString(env, jStringServiceName))) {
        hSCManager = OpenSCManager(NULL, NULL, GENERIC_READ);
        if (hSCManager) {
            /* Decide on the access needed when opening the service. */
            if (controlCode == org_tanukisoftware_wrapper_WrapperManager_SERVICE_CONTROL_CODE_START) {
                serviceAccess = SERVICE_START | SERVICE_INTERROGATE | SERVICE_QUERY_STATUS;
                wControlCode = SERVICE_CONTROL_INTERROGATE;
            } else if (controlCode == org_tanukisoftware_wrapper_WrapperManager_SERVICE_CONTROL_CODE_STOP) {
                serviceAccess = SERVICE_STOP | SERVICE_QUERY_STATUS;
                wControlCode = SERVICE_CONTROL_STOP;
            } else if (controlCode == org_tanukisoftware_wrapper_WrapperManager_SERVICE_CONTROL_CODE_INTERROGATE) {
                serviceAccess = SERVICE_INTERROGATE | SERVICE_QUERY_STATUS;
                wControlCode = SERVICE_CONTROL_INTERROGATE;
            } else if (controlCode == org_tanukisoftware_wrapper_WrapperManager_SERVICE_CONTROL_CODE_PAUSE) {
                serviceAccess = SERVICE_PAUSE_CONTINUE | SERVICE_QUERY_STATUS;
                wControlCode = SERVICE_CONTROL_PAUSE;
            } else if (controlCode == org_tanukisoftware_wrapper_WrapperManager_SERVICE_CONTROL_CODE_CONTINUE) {
                serviceAccess = SERVICE_PAUSE_CONTINUE | SERVICE_QUERY_STATUS;
                wControlCode = SERVICE_CONTROL_CONTINUE;
            } else if ((controlCode >= 128) || (controlCode <= 255)) {
                serviceAccess = SERVICE_USER_DEFINED_CONTROL | SERVICE_QUERY_STATUS;
                wControlCode = controlCode;
            } else {
                /* Illegal control code. */
                _sntprintf(buffer, 512, TEXT("Illegal Control code specified: %d"), controlCode);
                throwServiceException(env, 1, buffer);
                threwError = TRUE;
            }

            if (!threwError) {
                hService = OpenService(hSCManager, serviceName, serviceAccess);
                if (hService) {
                    /* If we are trying to start a service, it needs to be handled specially. */
                    if (controlCode == org_tanukisoftware_wrapper_WrapperManager_SERVICE_CONTROL_CODE_START) {
                        if (StartService(hService, 0, NULL)) {
                            /* Started the service. Continue on and interrogate the service. */
                        } else {
                           /* Failed. */
                            _sntprintf(buffer, bufferSize, TEXT("Unable to start service \"%s\": %s"), serviceName, getLastErrorText());
                            throwServiceException(env, GetLastError(), buffer);
                            threwError = TRUE;
                        }
                    }

                    if (!threwError) {
                        if (ControlService(hService, wControlCode, &serviceStatus)) {
                            /* Success.  fall through. */
                        } else {
                            /* Failed to send the control code.   See if the service is running. */
                            if (GetLastError() == ERROR_SERVICE_NOT_ACTIVE) {
                                /* Service is not running, so get its status information. */
                                if (QueryServiceStatus(hService, &serviceStatus)) {
                                    /* We got the status.  fall through. */
                                } else {
                                    /* Actual failure. */
                                    _sntprintf(buffer, bufferSize, TEXT("Unable to query status of service \"%s\": %s"), serviceName, getLastErrorText());
                                    throwServiceException(env, GetLastError(), buffer);
                                    threwError = TRUE;
                                }
                            } else {
                                /* Actual failure. */
                                _sntprintf(buffer, bufferSize, TEXT("Unable to query status of service \"%s\": %s"), serviceName, getLastErrorText());
                                throwServiceException(env, GetLastError(), buffer);
                                threwError = TRUE;
                            }
                        }

                        if (!threwError) {
                            /* Build up a service object to return. */
                            if (serviceClass = (*env)->FindClass(env, "org/tanukisoftware/wrapper/WrapperWin32Service")) {
                                /* Look for the constructor. Ignore failures. */
                                if (constructor = (*env)->GetMethodID(env, serviceClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;II)V")) {
                                    /* Look up the display name of the service. First need to figure out how big it is. */
                                    displayNameSize = 0;
                                    GetServiceDisplayName(hSCManager, serviceName, NULL, &displayNameSize);
                                    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                                        _sntprintf(buffer, bufferSize, TEXT("Unable to obtain the display name of service \"%s\": %s"), serviceName, getLastErrorText());
                                        throwServiceException(env, GetLastError(), buffer);
                                        threwError = TRUE;
                                    } else {
                                        displayNameSize++; /* Add room for the '\0' . */
                                        displayName = malloc(sizeof(TCHAR) * displayNameSize);
                                        if (!displayName) {
                                            throwOutOfMemoryError(env, TEXT("NSSCC1"));
                                            threwError = TRUE;
                                        } else {
                                            /* Now get the display name for real. */
                                            if ((GetServiceDisplayName(hSCManager, serviceName, displayName, &displayNameSize) == 0) && GetLastError()) {
                                                _sntprintf(buffer, bufferSize, TEXT("Unable to obtain the display name of service \"%s\": %s"), serviceName, getLastErrorText());
                                                throwServiceException(env, GetLastError(), buffer);
                                                threwError = TRUE;
                                            } else {
                                                /* Convert the display name to a jstring. */
                                                jStringDisplayName = JNU_NewStringFromNativeW(env, displayName);
                                                if (jStringDisplayName) {
                                                    state = serviceStatus.dwCurrentState;

                                                    exitCode = serviceStatus.dwWin32ExitCode;
                                                    if (exitCode == ERROR_SERVICE_SPECIFIC_ERROR) {
                                                        exitCode = serviceStatus.dwServiceSpecificExitCode;
                                                    }

                                                    service = (*env)->NewObject(env, serviceClass, constructor, jStringServiceName, jStringDisplayName, state, exitCode);

                                                    (*env)->DeleteLocalRef(env, jStringDisplayName);
                                                }
                                            }

                                            free(displayName);
                                        }
                                    }
                                } else {
                                    /* Exception Thrown */
                                    threwError = TRUE;
                                }

                                (*env)->DeleteLocalRef(env, serviceClass);
                            } else {
                                /* Exception Thrown */
                                threwError = TRUE;
                            }
                        }
                    }

                    CloseServiceHandle(hService);
                } else {
                    /* Unable to open service. */
                    _sntprintf(buffer, bufferSize, TEXT("Unable to open the service '%s': %s"), serviceName, getLastErrorText());
                    throwServiceException(env, GetLastError(), buffer);
                    threwError = TRUE;
                }
            }

            /* Close the handle to the service control manager database */
            CloseServiceHandle(hSCManager);
        } else {
            /* Unable to open the service manager. */
            _sntprintf(buffer, bufferSize, TEXT("Unable to open the Windows service control manager database: %s"), getLastErrorText());
            throwServiceException(env, GetLastError(), buffer);
            threwError = TRUE;
        }

        free(serviceName);
    } else {
        /* Exception Thrown */
    }

    return service;
}

/*#define GETPORTSTATUS_DEBUG*/
int getPortStatusTCPv4(JNIEnv *env, int port, jstring jAddress) {
    PMIB_TCPTABLE tcpTable;
    DWORD tcpTableSize;
    DWORD tcpTableSizeNew;
    DWORD retVal;
    int tries;
    int i;
    int result;
    int haveTable;
    const char *nativeAddress;
    struct in_addr addr;
    char localAddr[128]; /* This will only be IPV4 address, so the maximum possible length should be 15+1 */
    int localPort;
#ifdef GETPORTSTATUS_DEBUG
    char remoteAddr[128];
    int remotePort;
    TCHAR *stateName;
#endif
    
    /* We don't know how big the table needs to be, and it can change with timing, so loop. */
    tcpTableSize = 0;
    tcpTable = NULL;
    tries = 0;
    result = 0;
    haveTable = FALSE;
    while (!haveTable) {
        tries++;
        if (tries > 10) {
            /* Too many attempts, may happen if the TCP table is changing a lot? */
            free(tcpTable);
            return -101;
        }
        
        /* When we retry after errors other than 'ERROR_INSUFFICIENT_BUFFER', not sure that the table size variable is unchanged.
         *  Keep track of the last known tcpTable size as we reuse it to be safe. */
        tcpTableSizeNew = tcpTableSize;
        retVal = GetTcpTable(tcpTable, &tcpTableSizeNew, TRUE);
        switch (retVal) {
        case NO_ERROR:
            /* Got our table. */
            haveTable = TRUE;
            break;
            
        case ERROR_NOT_SUPPORTED:
            /* No need to retry */
            if (wrapperJNIDebugging) {
                log_printf(TEXT("WrapperJNI Error: Cannot check port status (GetTcpTable not supported on this system)."));
            }
            free(tcpTable);
            return -100;
            
        case ERROR_INSUFFICIENT_BUFFER:
            /* Initial call or too small, need to retry. */
#ifdef GETPORTSTATUS_DEBUG
            log_printf(TEXT("WrapperJNI Debug: GetTcpTable buffer too small < %d"), tcpTableSizeNew);
#endif
            if (tcpTable) {
                free(tcpTable);
            }
            
            tcpTableSize = tcpTableSizeNew;
            tcpTable = malloc(tcpTableSize);
            if (!tcpTable) {
                throwOutOfMemoryError(env, TEXT("GPSVF1"));
                return -100;
            }
            break;
            
        case ERROR_INVALID_PARAMETER:
            /* Should not happen, but retry. */
#ifdef GETPORTSTATUS_DEBUG
            log_printf(TEXT("WrapperJNI Debug: GetTcpTable invalid parameter.  Try again."));
#endif
            break;
            
        case STATUS_UNSUCCESSFUL:
            /* Can happen when the table is receiving lots of changes and is normal. */
#ifdef GETPORTSTATUS_DEBUG
            log_printf(TEXT("WrapperJNI Debug: GetTcpTable busy.  Try again."));
#endif
            break;
            
        default:
            /* Unexpected. */
#ifdef GETPORTSTATUS_DEBUG
            log_printf(TEXT("WrapperJNI Debug: GetTcpTable error(%d)"), retVal);
#endif
            break;
        }
    }
    
    nativeAddress = (*env)->GetStringUTFChars(env, jAddress, 0);
    if (nativeAddress) {
#ifdef GETPORTSTATUS_DEBUG
        log_printf(TEXT("WrapperJNI Debug: nativeAddress=%S port=%d"), nativeAddress, port);
#endif
        
        for (i = 0; i < (int)tcpTable->dwNumEntries; i++) {
            addr.S_un.S_addr = (u_long)tcpTable->table[i].dwLocalAddr;
            strncpy(localAddr, inet_ntoa(addr), sizeof(localAddr));
            localPort = ntohs((u_short)tcpTable->table[i].dwLocalPort);
            
#ifdef GETPORTSTATUS_DEBUG
            addr.S_un.S_addr = (u_long)tcpTable->table[i].dwRemoteAddr;
            strncpy(remoteAddr, inet_ntoa(addr), sizeof(remoteAddr));
            remotePort = ntohs((u_short)tcpTable->table[i].dwRemotePort);
            
            switch (tcpTable->table[i].dwState) {
            case MIB_TCP_STATE_CLOSED:
                stateName = TEXT("CLOSED");
                break;
            case MIB_TCP_STATE_LISTEN:
                stateName = TEXT("LISTEN");
                break;
            case MIB_TCP_STATE_SYN_SENT:
                stateName = TEXT("SYN-SENT");
                break;
            case MIB_TCP_STATE_SYN_RCVD:
                stateName = TEXT("SYN-RECEIVED");
                break;
            case MIB_TCP_STATE_ESTAB:
                stateName = TEXT("ESTABLISHED");
                break;
            case MIB_TCP_STATE_FIN_WAIT1:
                stateName = TEXT("FIN-WAIT-1");
                break;
            case MIB_TCP_STATE_FIN_WAIT2:
                stateName = TEXT("FIN-WAIT-2");
                break;
            case MIB_TCP_STATE_CLOSE_WAIT:
                stateName = TEXT("CLOSE-WAIT");
                break;
            case MIB_TCP_STATE_CLOSING:
                stateName = TEXT("CLOSING");
                break;
            case MIB_TCP_STATE_LAST_ACK:
                stateName = TEXT("LAST-ACK");
                break;
            case MIB_TCP_STATE_TIME_WAIT:
                stateName = TEXT("TIME-WAIT");
                break;
            case MIB_TCP_STATE_DELETE_TCB:
                stateName = TEXT("DELETE-TCB");
                break;
            default:
                stateName = TEXT("UNKNOWN");
                break;
            }
            
            log_printf(TEXT("WrapperJNI Debug:   TcpTable[%d] State: %d %s  Local: %S:%d  Remote: %S:%d"),
                    i, tcpTable->table[i].dwState, stateName,
                    localAddr, localPort,
                    remoteAddr, remotePort);
#endif
            
            if ((port == localPort) && (strcmp(nativeAddress, localAddr) == 0)) {
                /* Matched. */
                result = tcpTable->table[i].dwState;
#ifdef GETPORTSTATUS_DEBUG
                log_printf(TEXT("WrapperJNI Debug:   MATCHED!  Port In Use. state=%d"), result);
#endif
                /* No need to keep looping. */
                break;
            }
        }
        
        (*env)->ReleaseStringUTFChars(env, jAddress, nativeAddress);
    } else {
        free(tcpTable);
        throwOutOfMemoryError(env, TEXT("GPSVF2"));
        return -100;
    }
    
    free(tcpTable);
    
    return result;
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeGetPortStatus
 * Signature: (ILjava/lang/String;I)I
 *
 * @param port Port is the port whose status is requested.
 * @param protocol The protocol of the port, 0=tcpv4, 1=tcpv6
 *
 * @return The status, -1=error, 0=closed, >0=in use.
 */
JNIEXPORT jint JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeGetPortStatus(JNIEnv *env, jclass clazz, jint port, jstring jAddress, jint protocol) {
    switch (protocol) {
    case 0:
        return getPortStatusTCPv4(env, port, jAddress);
    case 1:
        /* GetTcp6Table required Windows Vista/2008 Server.  Is it needed now? */
        /*return getPortStatusTCPv6(env, port, jAddress);*/
        return 0;
    default:
        return -99;
    }
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeGetDpiScale
 * Signature: ()I
 *
 * @return The dpi scale (should be devided by 96 to get the scale factor).
 */
JNIEXPORT jint JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeGetDpiScale(JNIEnv *env, jclass clazz) {
    const POINT ptZero = { 0, 0 };
    int dpiX;
    int dpiY;
    HMONITOR  hMonitor;
    int awareness; /* PROCESS_DPI_AWARENESS */
    HMODULE scalingAPI;
    FARPROC DynGetProcessDpiAwareness = NULL;
    FARPROC DynSetProcessDpiAwareness = NULL;
    FARPROC DynGetDpiForMonitor = NULL;
    jint result;
    
    /* The ShellScalingAPI was added in windows 8.1 */
    if ((scalingAPI = LoadLibrary(TEXT("Shcore.dll"))) == NULL) {
        return 96; /* no scaling */
    } else if ((DynGetProcessDpiAwareness = GetProcAddress(scalingAPI, "GetProcessDpiAwareness")) == NULL ||
               (DynSetProcessDpiAwareness = GetProcAddress(scalingAPI, "SetProcessDpiAwareness")) == NULL ||
               (DynGetDpiForMonitor = GetProcAddress(scalingAPI, "GetDpiForMonitor")) == NULL) {
        return 96; /* no scaling */
    }
    
    /* get DPI awareness */
    DynGetProcessDpiAwareness(NULL, &awareness);
    
    /* if awareness is PROCESS_SYSTEM_DPI_AWARE or PROCESS_PER_MONITOR_DPI_AWARE, 
     *  we can get the scale factor, else it will return 96 anyway so stop here. */
    if (awareness == 0) {
        /* try to set the dpi awarness to PROCESS_PER_MONITOR_DPI_AWARE.
         *  (this may be moved before the function call if we create a 
         *  configuration property to control dpi awareness.) */
        if (DynSetProcessDpiAwareness(2) != S_OK) {
            return 96;
        }
    }
    
    /* get the primary monitor */
    hMonitor = MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
    
    /* get DPI scale of the monitor */
    DynGetDpiForMonitor(hMonitor, 0, &dpiX, &dpiY);
    
    result = (dpiX + dpiY) / 2;
    
    /* do some control on the returned value */
    if (result > 4 * 96) {
        result = 4 * 96;
    } else if (result < 96) {
        result = 96;
    }
    
    return result;
}

#endif
