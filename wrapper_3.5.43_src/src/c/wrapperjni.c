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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#ifdef WIN32
 #include <windows.h>
 #include <tchar.h>
 #include <io.h>
 #define dup2 _dup2
#endif
#include "wrapper_i18n.h"
#ifndef WIN32
 #include <langinfo.h>
#endif

#include "loggerjni.h"
#include "wrapperjni.h"
#include "wrapperinfo.h"

/* The largest possible "name+'='+value" property pair length on Windows. */
#define MAX_ENV_PAIR_LEN 32767

int wrapperJNIDebugging = JNI_FALSE;

#define CONTROL_EVENT_QUEUE_SIZE 10
int controlEventQueue[CONTROL_EVENT_QUEUE_SIZE];
int controlEventQueueLastReadIndex = 0;
int controlEventQueueLastWriteIndex = 0;

/** Flag to keep track of whether StdOut has been redirected. */
int redirectedStdOut = FALSE;

/** Flag to keep track of whether StdErr has been redirected. */
int redirectedStdErr = FALSE;

/* Special symbols that need to be defined manually as part of the bootstrap process. */
const char utf8ClassJavaLangString[] = {106, 97,118, 97, 47, /* java/ */
                                        108, 97,110,103, 47, /* lang/ */
                                        83,116,114,105,110,103, 0}; /* "java/lang/String" */
const char utf8MethodInit[] = {60,105,110,105,116, 62, 0}; /* "<init>" */
const char utf8Sig_BrV[] = {40, 91, 66, 41, 86, 0}; /* "([B)V" */
const char utf8ClassJavaLangOutOfMemoryError[] = {106, 97,118, 97, 47, /* java/ */
                                                  108, 97,110, 103, 47, /* lang/ */
                                                  79, 117, 116, 79, 102, 77, 101, 109, 111, 114, 121, 69, 114, 114, 111, 114, 0}; /* OutOfMemoryError */
const char utf8ClassOrgTanukisoftwareWrapperWrapperJNIError[] = {111, 114, 103, 47, /* org/ */
                                                                116, 97, 110, 117, 107, 105, 115, 111, 102, 116, 119, 97, 114, 101, 47, /* tanukisoftware/ */
                                                                119, 114, 97, 112, 112, 101, 114, 47, /* wrapper/ */
                                                                87,  114, 97, 112, 112, 101, 114, 74, 78, 73, 69, 114, 114, 111, 114, 0}; /* "WrapperJNIError" */

/*
 * For UTF8 constants, '_' in the name means an array, 'r' preceeds the return
 *  portion of a method declaration, 'V' is Void.  The rest is like the
 *  Java format.
 */
char *utf8SigLjavaLangStringrV;
char *utf8ClassJavaLangSystem;
char *utf8MethodGetProperties;
char *utf8SigVrLjavaUtilProperties;
char *utf8MethodGetProperty;
char *utf8SigLjavaLangStringrLjavaLangString;
char *utf8javaIOIOException; /* "java/io/IOException" */

#ifdef WIN32
#else
char *utf8ClassOrgTanukisoftwareWrapperWrapperUNIXUser;
char *utf8MethodSetGroup;
char *utf8MethodAddGroup;
char *utf8SigIIStringStringStringStringrV;
char *utf8SigIStringrV;
#endif



/**
 * Cause the current thread to sleep for the specified number of milliseconds.
 *  Sleeps over one second are not allowed.
 *
 * @param ms Number of milliseconds to wait for.
 *
 * @return TRUE if the was interrupted, FALSE otherwise.  Neither is an error.
 */
int wrapperSleep(int ms) {
#ifdef WIN32
    Sleep(ms);
#else
    /* We want to use nanosleep if it is available, but make it possible for the
       user to build a version that uses usleep if they want.
       usleep does not behave nicely with signals thrown while sleeping.  This
       was the believed cause of a hang experienced on one Solaris system. */
#ifdef USE_USLEEP
    usleep(ms * 1000); /* microseconds */
#else
    struct timespec ts;

    if (ms >= 1000) {
        ts.tv_sec = (ms * 1000000) / 1000000000;
        ts.tv_nsec = (ms * 1000000) % 1000000000; /* nanoseconds */
    } else {
        ts.tv_sec = 0;
        ts.tv_nsec = ms * 1000000; /* nanoseconds */
    }
    if (nanosleep(&ts, NULL)) {
        if (errno == EINTR) {
            return TRUE;
        } else if (errno == EAGAIN) {
            return TRUE;
        }
    }
#endif
#endif
    return FALSE;
}

/**
 * Create a jstring from a MultiBytes Char string.  The jstring must be freed up by caller.
 *
 * @param env The current JNIEnv.
 * @param str The MultiBytes string to convert.
 *
 * @return The new jstring or NULL if there were any exceptions thrown.
 */
jstring JNU_NewStringFromNativeMB(JNIEnv *env, const char *str) {
    jstring result;
    size_t len;
    char *strOut;
    TCHAR* errorW;
#ifdef WIN32
    int encodingFrom = CP_OEMCP;
#else
    char* encodingFrom = nl_langinfo(CODESET);
 #ifdef MACOSX
    if (strlen(encodingFrom) == 0) {
        encodingFrom = "UTF-8";
    }
 #endif
#endif
    
    len = strlen(str);
    if (len > 0) {
        if (converterMBToMB(str, encodingFrom, &strOut, __UTF8) < 0) {
            if (strOut) {
                /* An error message is stored in strOut (we need to convert it to wide chars to display it). */
#ifdef WIN32
                if (multiByteToWideChar(strOut, __UTF8, &errorW, FALSE)) {
#else
                if (converterMBToWide(strOut, __UTF8, &errorW, FALSE)) {
#endif
                    _tprintf(TEXT("WrapperJNI Warn: Unexpected conversion error: %s\n"), getLastErrorText()); fflush(NULL);
                } else {
                    _tprintf(TEXT("%s\n"), errorW); fflush(NULL);
                }
                if (errorW) {
                    free(errorW);
                }
                free(strOut);
            } else {
                throwOutOfMemoryError(env, TEXT("JNSFNC1"));
            }
            return NULL;
        }
        result = (*env)->NewStringUTF(env, strOut);
        free(strOut);
    } else {
        result = (*env)->NewStringUTF(env, str);
    }
    
    return result;
}

/**
 * Create a jstring from a Wide Char string.  The jstring must be freed up by caller.
 *
 * @param env The current JNIEnv.
 * @param strW The Wide string to convert.
 *
 * @return The new jstring or NULL if there were any exceptions thrown.
 */
jstring JNU_NewStringFromNativeW(JNIEnv *env, const TCHAR *strW) {
    jstring result;
    size_t len;
    char* msgMB;
#ifdef WIN32
    int size;
#else
    TCHAR* errorW;
#endif

    len = _tcslen(strW);
    if (len > 0) {
#ifdef WIN32
        size = WideCharToMultiByte(CP_UTF8, 0, strW, -1, NULL, 0, NULL, NULL);
        if (size <= 0) {
            _tprintf(TEXT("WrapperJNI Warn: Failed to convert string \"%s\": %s\n"), strW, getLastErrorText()); fflush(NULL);
            return NULL;
        }
        msgMB = malloc(sizeof(char) * size);
        if (!msgMB) {
            throwOutOfMemoryError(env, TEXT("JNSN1"));
            return NULL;
        }
        WideCharToMultiByte(CP_UTF8, 0, strW, -1, msgMB, size, NULL, NULL);
#else
        if (converterWideToMB(strW, &msgMB, MB_UTF8) < 0) {
            if (msgMB) {
                /* An error message is stored in msgMB (we need to convert it to wide chars to display it). */
                if (converterMBToWide(msgMB, NULL, &errorW, FALSE)) {
                    _tprintf(TEXT("WrapperJNI Warn: Failed to convert string \"%s\": %s\n"), strW, getLastErrorText()); fflush(NULL);
                } else {
                    _tprintf(TEXT("%s\n"), errorW); fflush(NULL);
                }
                if (errorW) {
                    free(errorW);
                }
                free(msgMB);
            } else {
                throwOutOfMemoryError(env, TEXT("JNSN2"));
            }
            return NULL;
        }
#endif
    } else {
        /* We need to special case empty strings as some of the functions don't work correctly for them. */
        msgMB = malloc(sizeof(char) * 1);
        if (!msgMB) {
            throwOutOfMemoryError(env, TEXT("JNSN3"));
            return NULL;
        }
        msgMB[0] = '\0';
    }
    result = (*env)->NewStringUTF(env, msgMB);
    free(msgMB);
    return result;
}

/**
 * Converts a jstring into a newly malloced TCHAR array.
 *
 * @param end The JNIEnv.
 * @param jstr The jstring.
 *
 * @return The requested Wide String, or NULL if there was a problem.  It is
 *         the responsibility of the caller to free up the returned string.
 */
TCHAR *JNU_GetNativeWFromString(JNIEnv *env, jstring jstr) {
    TCHAR* tresult = NULL;
    const char *result;
#ifdef WIN32
    int size;
#endif

    result = (*env)->GetStringUTFChars(env, jstr, NULL);
#ifdef WIN32
    size = MultiByteToWideChar(CP_UTF8, 0, result, -1, NULL, 0);
    if (size <= 0) {
        _tprintf(TEXT("WrapperJNI Warn: Unexpected conversion error: %s\n"), getLastErrorText()); fflush(NULL);
    } else {
        tresult = malloc(sizeof(TCHAR) * (size + 1));
        if (!tresult) {
            throwOutOfMemoryError(env, TEXT("GSNC1"));
        } else {
            MultiByteToWideChar(CP_UTF8, 0, result,-1, tresult, size + 1);
        }
    }
#else
    if (converterMBToWide(result, MB_UTF8, &tresult, TRUE)) {
        if (tresult) {
            _tprintf(tresult); fflush(NULL);
            free(tresult);
            tresult = NULL;
        } else {
            throwOutOfMemoryError(env, TEXT("GSNC2"));
        }
    }
#endif
    (*env)->ReleaseStringUTFChars(env, jstr, (const char *)result);
    return tresult;
}

#ifdef WIN32
/* So far this function is only used by windows. if we want to use it for unix as well, first
   provide correct wchar handling... */
void JNU_SetByteArrayRegion(JNIEnv *env, jbyteArray* jarray, jsize start, jsize len, const TCHAR *buffer) {
    char* msg;
 #if defined(UNICODE) && defined(WIN32)
    int size;

    size = WideCharToMultiByte(CP_OEMCP, 0, buffer, -1, NULL, 0, NULL, NULL);
    msg = malloc(size);
    if (!msg) {
        throwOutOfMemoryError(env, TEXT("JSBAR1"));
        return;
    }
    WideCharToMultiByte(CP_OEMCP,0, buffer,-1, msg, size, NULL, NULL);
 #else
     msg = (TCHAR*) buffer;
 #endif
    (*env)->SetByteArrayRegion(env, *jarray, start, len, (jbyte*) msg);
 #if defined(UNICODE) && defined(WIN32)
    free(msg);
 #endif
}
#endif

/**
 * Returns a new buffer containing the UTF8 characters for the specified native string.
 *  Note: this function is ok. Just not sure we need to need to pass through a jstring.
 *  We could handle the conversion in native code only.
 *
 * It is the responsibility of the caller to free the returned buffer.
 */
char *getUTF8Chars(JNIEnv *env, const char *nativeChars) {
    jstring js;
    jsize jlen;
    const char *stringChars;
    jboolean isCopy;
    char *utf8Chars = NULL;

    js = JNU_NewStringFromNativeMB(env, nativeChars);
    if (js != NULL) {
        jlen = (*env)->GetStringUTFLength(env, js);
        utf8Chars = malloc(jlen + 1);
        if (!utf8Chars) {
            throwOutOfMemoryError(env, TEXT("GUTFC1"));
        } else {
            stringChars = ((*env)->GetStringUTFChars(env, js, &isCopy));
            if (stringChars != NULL) {
                memcpy(utf8Chars, stringChars, jlen);
                utf8Chars[jlen] = '\0';
    
                (*env)->ReleaseStringUTFChars(env, js, stringChars);
            } else {
                throwOutOfMemoryError(env, TEXT("GUTFC2"));
                free(utf8Chars);
                utf8Chars = NULL;
            }
        }
        
        (*env)->DeleteLocalRef(env, js);
    }
    return utf8Chars;
}

void initUTF8Strings(JNIEnv *env) {
    /* Now initialize all of the strings using our helper function. */
    utf8SigLjavaLangStringrV = getUTF8Chars(env, "(Ljava/lang/String;)V");
    utf8ClassJavaLangSystem = getUTF8Chars(env, "java/lang/System");
    utf8MethodGetProperties = getUTF8Chars(env, "getProperties");
    utf8SigVrLjavaUtilProperties = getUTF8Chars(env, "()Ljava/util/Properties;");
    utf8MethodGetProperty = getUTF8Chars(env, "getProperty");
    utf8SigLjavaLangStringrLjavaLangString = getUTF8Chars(env, "(Ljava/lang/String;)Ljava/lang/String;");
    utf8javaIOIOException = getUTF8Chars(env, "java/io/IOException");
    
#ifdef WIN32
#else
    utf8ClassOrgTanukisoftwareWrapperWrapperUNIXUser = getUTF8Chars(env, "org/tanukisoftware/wrapper/WrapperUNIXUser");
    utf8MethodSetGroup = getUTF8Chars(env, "setGroup");
    utf8MethodAddGroup = getUTF8Chars(env, "addGroup");
    utf8SigIIStringStringStringStringrV = getUTF8Chars(env, "(IILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    utf8SigIStringrV = getUTF8Chars(env, "(ILjava/lang/String;)V");
#endif
}

/**
 * Looks up a System property and sets its value in the propertyValue parameter.
 *
 * It is the responsibility of the caller to free up the propertyValue buffer if it is non-NULL.
 *
 * @param env Current JNIEnv.
 * @param propertyName Name of the property.
 * @param propertyValue Value of the property, or NULL if it was not set.
 *
 * @return TRUE if there were any problems, FALSE if successful.  NULL values will still be successful.
 */
int getSystemProperty(JNIEnv *env, const TCHAR *propertyName, TCHAR **propertyValue, int encodeNative) {
    int result;
    jclass jClassSystem;
    jmethodID jMethodIdGetProperty;
    jstring jStringKeyPropName;
    jstring jStringKeyValue;
    TCHAR *keyChars;

    /* Initialize the propertyValue to point to NULL in case we fail. */
    *propertyValue = NULL;

    if ((jClassSystem = (*env)->FindClass(env, utf8ClassJavaLangSystem)) != NULL) {
        if ((jMethodIdGetProperty = (*env)->GetStaticMethodID(env, jClassSystem, utf8MethodGetProperty, utf8SigLjavaLangStringrLjavaLangString)) != NULL) {
            if ((jStringKeyPropName = JNU_NewStringFromNativeW(env, propertyName)) != NULL) {
                if ((jStringKeyValue = (jstring)(*env)->CallStaticObjectMethod(env, jClassSystem, jMethodIdGetProperty, jStringKeyPropName)) != NULL) {
                    /* Collect the value. */
                    if (!encodeNative) {
                        if ((keyChars = JNU_GetNativeWFromString(env, jStringKeyValue)) != NULL) {
                            *propertyValue = malloc(sizeof(TCHAR) * (_tcslen(keyChars) + 1));
                            if (!*propertyValue) {
                                throwOutOfMemoryError(env, TEXT("GSP1"));
                                result = TRUE;
                            } else {
                                _tcsncpy(*propertyValue, keyChars, _tcslen(keyChars) + 1);
                                result = FALSE;
                            }
                            
                            free(keyChars);
                        } else {
                            /* Exception Thrown */
                            result = TRUE;
                        }
                    } else {
                        if ((keyChars = (TCHAR*)(*env)->GetStringUTFChars(env, jStringKeyValue, NULL)) != NULL) {
                            *propertyValue = malloc(strlen((char*)keyChars) + 1);
                            if (!*propertyValue) {
                                throwOutOfMemoryError(env, TEXT("GSP2"));
                                result = TRUE;
                            } else {
                                strncpy((char*)*propertyValue, (char*)keyChars, strlen((char*)keyChars) + 1);
                                result = FALSE;
                            }
                            
                            (*env)->ReleaseStringUTFChars(env, jStringKeyValue, (const char *)keyChars);
                        } else {
                            /* Exception Thrown */
                            result = TRUE;
                        }
                    }
                    
                    (*env)->DeleteLocalRef(env, jStringKeyValue);
                } else {
                    /* Property was not set. */
                    result = FALSE;
                }

                (*env)->DeleteLocalRef(env, jStringKeyPropName);
            } else {
                result = TRUE;
            }
        } else {
            result = TRUE;
        }

        (*env)->DeleteLocalRef(env, jClassSystem);
    } else {
        result = TRUE;
    }

    return result;
}

static JavaVM *jvm = NULL;
static jobject outGlobalRef = NULL;
static jmethodID printlnMethodId = NULL;

int printMessageCallback(const TCHAR* message) {
    JNIEnv** envPtr;
    JNIEnv* env = NULL;
    jstring jMsg;

    /* Do not print directly to the standard output because the JVM will interpret it with its own encoding (file.encoding)
     *  which may differ from the locale encoding (especially on Windows or when setting file.encoding from the java additionals).
     *  Instead we will create a JString and let the WrapperResources print it.
     *
     *  Another possibility would be to change the locale so that its encoding matches with the JVM encoding, or to convert the log
     *  messages to MB using the JVM encoding (we should however use the equivalent code page or iconv syntax). To do this, we would
     *  need to either include wrapper_jvminfo.c to the native libary (but it would make it heavier), or pass the equivalent encoding
     *  in a system property (the conversion would be the opposite as when the Wrapper catches JVM outputs). */
    if (jvm && outGlobalRef && printlnMethodId) {
        /* envPtr is to avoid a warning "dereferencing type-punned pointer will break strict-aliasing rules" happening on certain platforms. */
        envPtr = &env;
        if ((*jvm)->AttachCurrentThread(jvm, (void**)envPtr, NULL) == 0) {
            if ((jMsg = JNU_NewStringFromNativeW(env, message)) != NULL) {
                (*env)->CallVoidMethod(env, outGlobalRef, printlnMethodId, jMsg);
                return FALSE;
            }
        }
    }
    return TRUE;
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeDispose
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeDispose(JNIEnv *env, jclass jClassWrapperManager, jboolean debugging) {
    if (wrapperJNIDebugging) {
        /* This is useful for making sure that this is the last JNI call. */
        log_printf(TEXT("WrapperJNI Debug: Disposing WrapperManager native library."));
    }
    (*env)->DeleteGlobalRef(env, outGlobalRef);
}

/**
 * Get a pointer to a Java method used to print native messages.
 *
 * @return TRUE if there were any problems.
 */
int initLog(JNIEnv *env) {
    jobject outField;
    jclass systemClass, printStreamClass;
    jfieldID outFieldId;

    /* Get system class */
    if ((systemClass = (*env)->FindClass(env, getUTF8Chars(env, "java/lang/System"))) != NULL) {
        /* Lookup the "out" field */
        if ((outFieldId = (*env)->GetStaticFieldID(env, systemClass, getUTF8Chars(env, "out"), getUTF8Chars(env, "Ljava/io/PrintStream;"))) != NULL) {
            /* Get "out" PrintStream instance */
            if ((outField = (*env)->GetStaticObjectField(env, systemClass, outFieldId)) != NULL) {
                /* Get PrintStream class */
                if ((printStreamClass = (*env)->FindClass(env, getUTF8Chars(env, "java/io/PrintStream"))) != NULL) {
                    /* Lookup println() */
                    if ((printlnMethodId = (*env)->GetMethodID(env, printStreamClass, getUTF8Chars(env, "println"), getUTF8Chars(env, "(Ljava/lang/String;)V"))) != NULL) {
                        /* Save a JavaVM* instance to get an environment in our callback method */
                        if ((*env)->GetJavaVM(env, &jvm) == 0) {
                            /* Keep a global reference to the out stream for faster reuse. */
                            if ((outGlobalRef = (*env)->NewGlobalRef(env, outField)) != NULL) {
                                /* Register a callback method to print our messages */
                                setPrintMessageCallback(printMessageCallback);
                                return FALSE;
                            }
                        }
                    }
                    (*env)->DeleteLocalRef(env, printStreamClass);
                }
                (*env)->DeleteLocalRef(env, outField);
            }
        }
        (*env)->DeleteLocalRef(env, systemClass);
    }
    return TRUE;
}

/**
 * Do common initializaion.
 *
 * @return TRUE if there were any problems.
 */
int initCommon(JNIEnv *env, jclass jClassWrapperManager) {
    TCHAR* outfile;
    TCHAR* errfile;
    int outfd;
    int errfd;
    int mode;
    int options;
#ifdef HPUX
    TCHAR* fixIconvHpuxPropValue = NULL;
#endif

#ifdef WIN32
    mode = _S_IWRITE;
    options = _O_WRONLY | _O_APPEND | _O_CREAT;
#else
    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    options = O_WRONLY | O_APPEND | O_CREAT;
#endif
    initUTF8Strings(env);

#ifdef HPUX
    if (!getSystemProperty(env, TEXT("wrapper.fix_iconv_hpux"), &fixIconvHpuxPropValue, FALSE)) {
        if (fixIconvHpuxPropValue && (strcmpIgnoreCase(fixIconvHpuxPropValue, TEXT("ALWAYS")) == 0)) {
            toggleIconvHpuxFix(TRUE);
        }
    }
#endif

    if (getSystemProperty(env, TEXT("wrapper.java.errfile"), &errfile, FALSE)) {
        /* Failed */
        return TRUE;
    }
    if (errfile) {
        _ftprintf(stderr, TEXT("WrapperJNI: Redirecting %s to file %s...\n"), TEXT("StdErr"), errfile); fflush(NULL);
        if (((errfd = _topen(errfile, options, mode)) == -1) || (dup2(errfd, STDERR_FILENO) == -1)) {
            throwThrowable(env, utf8javaIOIOException, TEXT("Failed to redirect %s to file %s  (Err: %s)"), TEXT("StdErr"), errfile, getLastErrorText());
            return TRUE;
        } else {
            redirectedStdErr = TRUE;
        }
    }
    if (getSystemProperty(env, TEXT("wrapper.java.outfile"), &outfile, FALSE)) {
        /* Failed */
        return TRUE;
    }
    if (outfile) {
        log_printf(TEXT("WrapperJNI: Redirecting %s to file %s..."), TEXT("StdOut"), outfile);
        if (((outfd = _topen(outfile, options, mode)) == -1) || (dup2(outfd, STDOUT_FILENO) == -1)) {
            throwThrowable(env, utf8javaIOIOException, TEXT("Failed to redirect %s to file %s  (Err: %s)"), TEXT("StdOut"), outfile, getLastErrorText());
            return TRUE;
        } else {
            redirectedStdOut = TRUE;
        }
    }
    
    return FALSE;
}

void throwThrowable(JNIEnv *env, char *throwableClassName, const TCHAR *lpszFmt, ...) {
    va_list vargs;
    int messageBufferSize = 0;
    TCHAR *messageBuffer = NULL;
    int count;
    jclass jThrowableClass;
    jmethodID constructor;
    jstring jMessageBuffer;
    jobject jThrowable;
#if defined(UNICODE) && !defined(WIN32)
    TCHAR *msg = NULL;
    int i;
    int flag;
#endif

#if defined(UNICODE) && !defined(WIN32)
    if (wcsstr(lpszFmt, TEXT("%s")) != NULL) {
        msg = malloc(sizeof(wchar_t) * (wcslen(lpszFmt) + 1));
        if (msg) {
            /* Loop over the format and convert all '%s' patterns to %S' so the UNICODE displays correctly. */
            if (wcslen(lpszFmt) > 0) {
                for (i = 0; i < _tcslen(lpszFmt); i++){
                    msg[i] = lpszFmt[i];
                    if ((lpszFmt[i] == TEXT('%')) && (i  < _tcslen(lpszFmt)) && (lpszFmt[i + 1] == TEXT('s')) && ((i == 0) || (lpszFmt[i - 1] != TEXT('%')))){
                        msg[i+1] = TEXT('S'); i++;
                    }
                }
            }
            msg[wcslen(lpszFmt)] = TEXT('\0');
        } else {
            throwOutOfMemoryError(env, TEXT("TT0"));
            return;
        }
        flag = TRUE;
    } else {
        msg = (TCHAR*) lpszFmt;
        flag = FALSE;
    }
#endif

    do {
        if (messageBufferSize == 0) {
            /* No buffer yet. Allocate one to get started. */
            messageBufferSize = 100;
#if defined(HPUX)
            /* Due to a bug in the HPUX libc (version < 1403), the length of the buffer passed to _vsntprintf must have a length of 1 + N, where N is a multiple of 8.  Adjust it as necessary. */
            messageBufferSize = messageBufferSize + (((messageBufferSize - 1) % 8) == 0 ? 0 : 8 - ((messageBufferSize - 1) % 8)); 
#endif
            messageBuffer = (TCHAR*)malloc( messageBufferSize * sizeof(TCHAR));
            if (!messageBuffer) {
                throwOutOfMemoryError(env, TEXT("TT1"));
#if defined(UNICODE) && !defined(WIN32)
                if (flag == TRUE) {
                    free(msg);
                }
#endif
                return;
            }
        }

        /* Try writing to the buffer. */
        va_start(vargs, lpszFmt);

#if defined(UNICODE) && !defined(WIN32)
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
            if (count + 1 <= (int)messageBufferSize + 50) {
                messageBufferSize += 50;
            } else {
                messageBufferSize = count + 1;
            }
#if defined(HPUX)
            /* Due to a bug in the HPUX libc (version < 1403), the length of the buffer passed to _vsntprintf must have a length of 1 + N, where N is a multiple of 8.  Adjust it as necessary. */
            messageBufferSize = messageBufferSize + (((messageBufferSize - 1) % 8) == 0 ? 0 : 8 - ((messageBufferSize - 1) % 8)); 
#endif

            messageBuffer = (TCHAR*)malloc(messageBufferSize * sizeof(TCHAR));
            if (!messageBuffer) {
                throwOutOfMemoryError(env, TEXT("TT2"));
#if defined(UNICODE) && !defined(WIN32)
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

#if defined(UNICODE) && !defined(WIN32)
    if (flag == TRUE) {
        free(msg);
    }
#endif

    /* We have the messageBuffer */
    if ((jThrowableClass = (*env)->FindClass(env, throwableClassName)) != NULL) {
        if ((constructor = (*env)->GetMethodID(env, jThrowableClass, utf8MethodInit, utf8SigLjavaLangStringrV)) != NULL) {
            if ((jMessageBuffer = JNU_NewStringFromNativeW(env, messageBuffer)) != NULL) {
                if ((jThrowable = (*env)->NewObject(env, jThrowableClass, constructor, jMessageBuffer)) != NULL) {
                    if ((*env)->Throw(env, jThrowable)) {
                        log_printf(TEXT("WrapperJNI Error: Unable to throw %s with message: %s"), throwableClassName, messageBuffer);
                    }
                    (*env)->DeleteLocalRef(env, jThrowable);
                }
                (*env)->DeleteLocalRef(env, jMessageBuffer);
            }
        }
        (*env)->DeleteLocalRef(env, jThrowableClass);
    }
    free(messageBuffer);
}

/**
 * Throws an OutOfMemoryError.
 *
 * @param env The current JNIEnv.
 * @param locationCode The locationCode to help tell where the error happened.
 */
void throwOutOfMemoryError(JNIEnv *env, const TCHAR* locationCode) {
    throwThrowable(env, (char*)utf8ClassJavaLangOutOfMemoryError, TEXT("Out of memory (%s)"), locationCode);

    log_printf(TEXT("WrapperJNI Error: Out of memory (%s)"), locationCode);
}

void throwJNIError(JNIEnv *env, const TCHAR *message) {
    jclass exceptionClass;
    jmethodID constructor;
    jstring jMessage;
    jobject exception;

    if ((exceptionClass = (*env)->FindClass(env, utf8ClassOrgTanukisoftwareWrapperWrapperJNIError)) != NULL) {
        /* Look for the constructor. Ignore failures. */
        if ((constructor = (*env)->GetMethodID(env, exceptionClass, utf8MethodInit, utf8Sig_BrV)) != NULL) {
            if ((jMessage = JNU_NewStringFromNativeW(env, message)) != NULL) {
                if ((exception = (*env)->NewObject(env, exceptionClass, constructor, jMessage)) != NULL) {
                    if ((*env)->Throw(env, exception)) {
                        log_printf(TEXT("WrapperJNI Error: Unable to throw WrapperJNIError with message: %s"), message);
                    }
                    (*env)->DeleteLocalRef(env, exception);
                }
    
                (*env)->DeleteLocalRef(env, jMessage);
            }
        }

        (*env)->DeleteLocalRef(env, exceptionClass);
    }
}

void wrapperJNIHandleSignal(int signal) {
    if (wrapperLockControlEventQueue()) {
        /* Failed.  Should have been reported. */
        log_printf(TEXT("WrapperJNI Error: Signal %d trapped, but ignored."), signal);
        return;
    }
#ifdef _DEBUG
    log_printf(TEXT(" Queue Write 1 R:%d W:%d E:%d"), controlEventQueueLastReadIndex, controlEventQueueLastWriteIndex, signal);
#endif
    controlEventQueueLastWriteIndex++;
    if (controlEventQueueLastWriteIndex >= CONTROL_EVENT_QUEUE_SIZE) {
        controlEventQueueLastWriteIndex = 0;
    }
    controlEventQueue[controlEventQueueLastWriteIndex] = signal;
#ifdef _DEBUG
    log_printf(TEXT(" Queue Write 2 R:%d W:%d"), controlEventQueueLastReadIndex, controlEventQueueLastWriteIndex);
#endif

    if (wrapperReleaseControlEventQueue()) {
        /* Failed.  Should have been reported. */
        return;
    }
}


/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeGetLibraryVersion
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeGetLibraryVersion(JNIEnv *env, jclass clazz) {
    jstring version;
    version = JNU_NewStringFromNativeW(env, wrapperVersion);
    return version;
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeIsProfessionalEdition
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeIsProfessionalEdition(JNIEnv *env, jclass clazz) {
    return JNI_FALSE;
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeIsStandardEdition
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeIsStandardEdition(JNIEnv *env, jclass clazz) {
    return JNI_FALSE;
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    nativeGetControlEvent
 * Signature: (V)I
 */
JNIEXPORT jint JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeGetControlEvent(JNIEnv *env, jclass clazz) {
    int event = 0;

    if (wrapperLockControlEventQueue()) {
        /* Failed.  Should have been reported. */
        return 0;
    }

    if (controlEventQueueLastWriteIndex != controlEventQueueLastReadIndex) {
#ifdef _DEBUG
        _tprintf(TEXT(" Queue Read 1 R:%d W:%d\n"), controlEventQueueLastReadIndex, controlEventQueueLastWriteIndex);
        fflush(NULL);
#endif
        controlEventQueueLastReadIndex++;
        if (controlEventQueueLastReadIndex >= CONTROL_EVENT_QUEUE_SIZE) {
            controlEventQueueLastReadIndex = 0;
        }
        event = controlEventQueue[controlEventQueueLastReadIndex];
#ifdef _DEBUG
        _tprintf(TEXT(" Queue Read 2 R:%d W:%d E:%d\n"), controlEventQueueLastReadIndex, controlEventQueueLastWriteIndex, event);
        fflush(NULL);
#endif
    }

    if (wrapperReleaseControlEventQueue()) {
        /* Failed.  Should have been reported. */
        return event;
    }
    return event;
}

/*
 * Class:     org_tanukisoftware_wrapper_WrapperManager
 * Method:    accessViolationInner
 * Signature: (V)V
 */
JNIEXPORT void JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_accessViolationInner(JNIEnv *env, jclass clazz) {
    TCHAR *ptr;

    log_printf(TEXT("WrapperJNI Warn: Causing access violation..."));
    
    /* Cause access violation */
    ptr = NULL;
    ptr[0] = L'\n';

}



JNIEXPORT jobject JNICALL
Java_org_tanukisoftware_wrapper_WrapperManager_nativeExec(JNIEnv *env, jclass jWrapperManagerClass, jobjectArray jCmdArray, jstring jCmdLine, jobject jWrapperProcessConfig, jboolean spawnChDir) {

    throwThrowable(env, "org/tanukisoftware/wrapper/WrapperLicenseError", TEXT("This function is only available in the Professional Edition of the Java Service Wrapper."));
    return NULL;

}
