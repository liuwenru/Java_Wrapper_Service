/*
 * Copyright (c) 1999, 2020 Tanuki Software, Ltd.
 * http://www.tanukisoftware.com
 * All rights reserved.
 *
 * This software is the proprietary information of Tanuki Software.
 * You shall use it only in accordance with the terms of the
 * license agreement you entered into with Tanuki Software.
 * http://wrapper.tanukisoftware.com/doc/english/licenseOverview.html
 */

#include <stdio.h>
#include <string.h>
#ifdef WIN32
 #include <tchar.h>
 #include <windows.h>
#else
 #include <pthread.h>
#endif
#include <errno.h>
#include "loggerjni.h"

void outOfMemory(const TCHAR *context, int id) {
    log_printf(TEXT("WrapperJNI Error: Out of memory (%s%02d). %s"), context, id, getLastErrorText());
}

void invalidMultiByteSequence(const TCHAR *context, int id) {
    log_printf(TEXT("WrapperJNI Error: Invalid multibyte Sequence found in (%s%02d). %s"), context, id, getLastErrorText());
}

#define LAST_ERROR_TEXT_BUFFER_SIZE 1024
/** Buffer holding the last error message.
 *  TODO: This needs to be made thread safe, meaning that we need a buffer for each thread. */
TCHAR lastErrorTextBufferW[LAST_ERROR_TEXT_BUFFER_SIZE];

/**
 * Returns a textual error message of the last error encountered.
 *
 * @return The last error message.
 */
const TCHAR* getLastErrorText() {
    int errorNum;
#ifdef WIN32
    DWORD dwRet;
    TCHAR* lpszTemp = NULL;
#else
    char* lastErrorTextMB;
    size_t req;
#endif

#ifdef WIN32
    errorNum = GetLastError();
    dwRet = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                          NULL,
                          GetLastError(),
                          LANG_NEUTRAL,
                          (TCHAR*)&lpszTemp,
                          0,
                          NULL);

    /* supplied buffer is not long enough */
    if (!dwRet) {
        /* There was an error calling FormatMessage. */
        _sntprintf(lastErrorTextBufferW, LAST_ERROR_TEXT_BUFFER_SIZE, TEXT("Failed to format system error message (Error: %d) (Original Error: 0x%x)"), GetLastError(), errorNum);
    } else if ((long)LAST_ERROR_TEXT_BUFFER_SIZE - 1 < (long)dwRet + 14) {
        _sntprintf(lastErrorTextBufferW, LAST_ERROR_TEXT_BUFFER_SIZE, TEXT("System error message is too large to convert (Required size: %d) (Original Error: 0x%x)"), dwRet, errorNum);
    } else {
        lpszTemp[lstrlen(lpszTemp)-2] = TEXT('\0');  /*remove cr and newline character */
        _sntprintf(lastErrorTextBufferW, LAST_ERROR_TEXT_BUFFER_SIZE, TEXT("%s (0x%x)"), lpszTemp, errorNum);
    }

    /* following the documentation of FormatMessage, LocalFree should be called to free the output buffer. */
    if (lpszTemp) {
        LocalFree(lpszTemp);
    }
#else
    errorNum = errno;
    lastErrorTextMB = strerror(errorNum);
    req = mbstowcs(NULL, lastErrorTextMB, MBSTOWCS_QUERY_LENGTH);
    if (req == (size_t)-1) {
        invalidMultiByteSequence(TEXT("GLET"), 1);
        _sntprintf(lastErrorTextBufferW, LAST_ERROR_TEXT_BUFFER_SIZE, TEXT("System error message could not be decoded (Error 0x%x)"), errorNum);
    } else if (req >= LAST_ERROR_TEXT_BUFFER_SIZE) {
        _sntprintf(lastErrorTextBufferW, LAST_ERROR_TEXT_BUFFER_SIZE, TEXT("System error message too large to convert (Require size: %d) (Original Error: 0x%x)"), req, errorNum);
    } else {
        mbstowcs(lastErrorTextBufferW, lastErrorTextMB, LAST_ERROR_TEXT_BUFFER_SIZE);
    }
#endif
    /* Always reterminate the buffer just to be sure it is safe because badly encoded characters can cause issues. */
    lastErrorTextBufferW[LAST_ERROR_TEXT_BUFFER_SIZE - 1] = TEXT('\0');

    return lastErrorTextBufferW;
}

/**
 * Returns the last error number.
 *
 * @return The last error number.
 */
int getLastError() {
#ifdef WIN32
    return GetLastError();
#else
    return errno;
#endif
}

static int (*printMessageCallback)(const TCHAR* message) = NULL;

/**
 * Set a method to print log messages.
 *
 * @param callback the method to call to print the message.
 */
void setPrintMessageCallback(int (*callback)(const TCHAR* message)) {
    printMessageCallback = callback;
}

/**
 * Prints the contents of a buffer to all configured targets.
 *
 * Must be called while locked.
 */
void log_printf_message(TCHAR *message) {
    TCHAR       *subMessage;
    TCHAR       *nextLF;
    FILE        *target;

    /* If the message contains line feeds then break up the line into substrings and recurse. */
    subMessage = message;
    nextLF = _tcschr(subMessage, TEXT('\n'));
    if (nextLF) {
        /* This string contains more than one line.   Loop over the strings.  It is Ok to corrupt this string because it is only used once. */
        while (nextLF) {
            nextLF[0] = TEXT('\0');
            log_printf_message(subMessage);
            
            /* Locate the next one. */
            subMessage = &(nextLF[1]);
            nextLF = _tcschr(subMessage, TEXT('\n'));
        }
        
        /* The rest of the buffer will be the final line. */
        log_printf_message(subMessage);
        
        return;
    }
    
    if (!printMessageCallback || printMessageCallback(message)) {
        /* We failed at some point. Print the message even if the encoding may be wrong. The string is already localized. Can we get the original string? */
        target = stdout;
        _ftprintf(target, TEXT("%s\n"), message);
        /* As this is JNI, we always need to flush the output. */
        fflush(target);
    }
}

/**
 * The tLog_printf function logs a message to the configured log targets.
 *
 * This method can be used safely in most cases.  See the tLog_printf_queue
 *  funtion for the exceptions.
 */
void log_printf(const TCHAR *lpszFmt, ...) {
    va_list     vargs;
    int         count;
#ifndef WIN32
    TCHAR       *msg = NULL;
    int         i, flag;
#endif
    TCHAR*      messageBuffer = NULL;
    size_t      messageBufferSize = 1024;
   
#ifndef WIN32
    if (wcsstr(lpszFmt, TEXT("%s")) != NULL) {
        msg = malloc(sizeof(wchar_t) * (wcslen(lpszFmt) + 1));
        if (msg) {
            /* Loop over the format and convert all '%s' patterns to %S' so the UNICODE displays correctly. */
            if (wcslen(lpszFmt) > 0) {
                for (i = 0; i < _tcslen(lpszFmt); i++) {
                    msg[i] = lpszFmt[i];
                    if ((lpszFmt[i] == TEXT('%')) && (i  < _tcslen(lpszFmt)) && (lpszFmt[i + 1] == TEXT('s')) && ((i == 0) || (lpszFmt[i - 1] != TEXT('%')))) {
                        msg[i+1] = TEXT('S'); i++;
                    }
                }
            }
            msg[wcslen(lpszFmt)] = TEXT('\0');
        } else {
            _tprintf(TEXT("Out of memory (P1)\n"));
            return;
        }
        flag = TRUE;
    } else {
        msg = (TCHAR*) lpszFmt;
        flag = FALSE;
    }
#endif
    
    /* Loop until the buffer is large enough that we are able to successfully
     *  print into it. Once the buffer has grown to the largest message size,
     *  smaller messages will pass through this code without looping. */
    do {
        if (messageBuffer == 0) {
            /* No buffer yet. Allocate one to get started. */
            messageBuffer = malloc(sizeof(TCHAR) * messageBufferSize);
            if (!messageBuffer) {
                _tprintf(TEXT("Out of memory (P2)\n"));
                messageBufferSize = 0;
#ifndef WIN32
                if (flag == TRUE) {
                    free(msg);
                }
#endif
                return;
            }
        }

        /* Try writing to the buffer. */
        va_start(vargs, lpszFmt);
#ifndef WIN32
        count = _vsntprintf(messageBuffer, messageBufferSize, msg, vargs);
#else
        count = _vsntprintf(messageBuffer, messageBufferSize, lpszFmt, vargs);
#endif
        va_end(vargs);

        if ((count < 0) || (count >= (int)messageBufferSize)) {
            /* If the count is exactly equal to the buffer size then a null TCHAR was not written.
             *  It must be larger.
             * Windows will return -1 if the buffer is too small. If the number is
             *  exact however, we still need to expand it to have room for the null.
             * UNIX will return the required size. */

            /* Free the old buffer for starters. */
            free(messageBuffer);

            /* Decide on a new buffer size. */
            if (count <= (int)messageBufferSize) {
                messageBufferSize += 100;
            } else {
                messageBufferSize = count + 1;
            }

            messageBuffer = malloc(sizeof(TCHAR) * messageBufferSize);
            if (!messageBuffer) {
                _tprintf(TEXT("Out of memory (P3)\n"));
                messageBufferSize = 0;
#ifndef WIN32
                if (flag == TRUE) {
                    free(msg);
                }
#endif
                return;
            }

            /* Always set the count to -1 so we will loop again. */
            count = -1;
        }
    } while (count < 0);
#ifndef WIN32
    if (flag == TRUE) {
        free(msg);
    }
#endif
    
    /* Actually log the message. */
    log_printf_message(messageBuffer);
    free(messageBuffer);
}
