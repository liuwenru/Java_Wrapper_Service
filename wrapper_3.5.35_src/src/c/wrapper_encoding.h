/*
 * Copyright (c) 1999, 2018 Tanuki Software, Ltd.
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

#ifndef _WRAPPER_ENCODING_H
#define _WRAPPER_ENCODING_H
#include "wrapper_i18n.h"

/* Constants that define which system property was used to determine the JVM output encoding.  */
#define LOCALE_ENCODING     1
#define FILE_ENCODING       2
#define SUN_ENCODING        3
#define UNRESOLVED_ENCODING 4

#define SUN_ENCODING_SUPPORTED                  0x0001                              /* sun.std*.encoding System Properties are supported */
#define SUN_ENCODING_SUPPORT_UNKNOWN            0x0002                              /* support is unknown */
#define SUN_ENCODING_UNSUPPORTED                0x0004                              /* not supported */
#define SUN_ENCODING_UNSUPPORTED_JAVA_VERSION  (0x0008 | SUN_ENCODING_UNSUPPORTED)  /* not supported by this version of Java */
#define SUN_ENCODING_UNSUPPORTED_JVM_MAKER     (0x0010 | SUN_ENCODING_UNSUPPORTED)  /* not supported by the JVM implementation */

#ifdef WIN32
/**
 * Get the JVM encoding corresponding to a code page.
 *
 * @codePage    the Windows code page
 * @javaVersion current java version
 * @buffer      buffer where the output encoding should be copied.
 *
 * @return a string representation of the JVM io encoding, or NULL if no value could be found.
 */
TCHAR* getJvmIoEncodingFromCodePage(int codePage, int javaVersion, TCHAR* buffer);

/**
 * Retrieved the value of file.encoding (or sun.std*.encoding) if defined in the java additional properties.
 *  The buffer is set to an empty string if the value could not be found.
 *  disposeHashMapJvmEncoding() should be called before calling this function.
 *
 * @buffer buffer in which the encoding should be copied
 * @javaVersion current java version
 * @jvmMaker    current java implementation (Oracle, IBM, etc.)
 *
 * @return LOCALE_ENCODING if no encoding was specified in the JVM arguements
 *         FILE_ENCODING if the encoding was resolved to the value of file.encoding
 *         SUN_ENCODING if the encoding was resolved to the value of the sun.std*.encoding properties
 *         UNRESOLVED_ENCODING if there was any error. A FATAL message will be printed before returning
 */
int getJvmArgumentsEncoding(TCHAR* buffer, int javaVersion, int jvmMaker);

/**
 * Get the code page used to encode the current JVM outputs.
 *  resolveJvmEncoding() should be called before using this function.
 *
 * @return UINT value of the code page (the code page of the current locale is returned by default)
 */
UINT getJvmOutputCodePage();
#else
/**
 * Check if the encoding is supported by the JVM.
 *
 * @localeEncoding the locale encoding
 * @javaVersion current java version
 * @buffer      buffer where the output encoding should be copied
 *
 * @return a string representation of the JVM io encoding, or NULL if no value could be found.
 */
TCHAR* getJvmIoEncoding(TCHAR* localeEncoding, int javaVersion, TCHAR* buffer);

/**
 * Get the encoding used for the current JVM outputs.
 *  resolveJvmEncoding() should be called before using this function.
 *
 * @return String representation of the encoding if the value found in file.encoding is supported by iconv, NULL otherwise.
 *         The returned value doesn't need to be freed.
 */
const char* getJvmOutputEncodingMB();
#endif

/**
 * Should be called on exit to release the hashmap containing the JVM encodings.
 */
void disposeHashMapJvmEncoding();

/**
 * Clear the Jvm encoding previously cached.
 *  This function can be called before getJvmOutputEncodingMB() to force using the encoding of the current locale.
 *  A call to resolveJvmEncoding() may then be necessary to restore the encoding.
 *
 * @debug TRUE to print a debug message, FALSE otherwise.
 */
void resetJvmOutputEncoding(int debug);

/**
 * Resolve the Java output encoding using system properties and an internal hashMap containing the supported encoding.
 *  This function should be called prior to using getJvmOutputCodePage() and getJvmOutputEncodingMB()
 *
 * @javaVersion current java version
 * @jvmMaker    current java implementation (Oracle, IBM, etc.)
 *
 * @return TRUE if there is any error (misconfiguration of system properties or unsuported encoding), FALSE otherwise.
 */
int resolveJvmEncoding(int javaVersion, int jvmMaker);

#endif
