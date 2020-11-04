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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
 #include <limits.h>
#endif
#include "logger.h"
#include "wrapper.h"
#include "wrapper_jvminfo.h"
#include "property.h"

/**
 * Dispose a JavaVersion structure.
 *
 * @param javaVersion A pointer to JavaVersion.
 */
JavaVersion* createJavaVersion() {
    JavaVersion *javaVersion = malloc(sizeof(JavaVersion));
    if (!javaVersion) {
        outOfMemory(TEXT("CJV"), 1);
        return NULL;
    }
    javaVersion->displayName = NULL;
    javaVersion->isUnknown = FALSE;
    return javaVersion;
}

/**
 * Dispose a JavaVersion structure.
 *
 * @param javaVersion A pointer to JavaVersion.
 */
void disposeJavaVersion(JavaVersion *javaVersion) {
    if (javaVersion) {
        if (javaVersion->displayName) {
            free(javaVersion->displayName);
            javaVersion->displayName = NULL;
        }
        free(javaVersion);
    }
}

/**
 * Parse the Java version (at the format returned by 'java -version') and retrieve the major, minor and revision components.
 *
 * @param javaVersionStr        the string reprentation of the Java version as returned by 'java -version'.
 * @param major                 a pointer to the major component of the Java version.
 * @param minor                 a pointer to the minor component of the Java version.
 * @param revision              a pointer to the revision component of the Java version.
 * @param defaultComponentValue value to use if minor or revision are not specified.
 *
 * @return TRUE if there is any error, FALSE otherwise.
 */
int parseJavaVersionInner(const TCHAR *javaVersionStr, unsigned int *major, unsigned int *minor, unsigned int *revision, unsigned int defaultComponentValue) {
    TCHAR c[16]; /* This size should be enough to get the version on the first line of the output. */
    TCHAR *ptr;
    TCHAR *endptr;
    TCHAR *token;
#if defined(UNICODE) && !defined(WIN32)
    TCHAR *state = NULL;
#endif

    if (!javaVersionStr || (_tcslen(javaVersionStr) == 0)) {
        return TRUE;
    }

    /* Work on a copy. */
    _tcsncpy(c, javaVersionStr, 15);
    c[15] = 0;
    
    /* JVM < 9 are at the format '1.x' - skip the first 2 characters. */
    if ((*c == TEXT('1')) && (*(c+1) == TEXT('.'))) {
        ptr = c + 2;
    } else {
        ptr = c;
    }

    /* Get the major version. */
    if (ptr[0] == TEXT('.')) {
        /* Make sure there is no leading delimiters that _tcstok() would skip. */
        return TRUE;
    }
    token = _tcstok(ptr, TEXT(".")
#if defined(UNICODE) && !defined(WIN32)
        , &state
#endif
    );
    errno = 0;
    *major = (unsigned int)_tcstoul(token, &endptr, 10);
    if ((errno != 0) || (token == endptr) || (*endptr)) {
        /* invalid || no digits were read || additional characters remain */
        return TRUE;
    }
    if (major == 0) {
        return TRUE;
    }

    /* Get the minor version (the next delimiter can be a '_' or a '.'). */
    token = _tcstok(NULL, TEXT("_.")
#if defined(UNICODE) && !defined(WIN32)
        , &state
#endif
    );
    if (token) {
        if (*(token - 2) == 0) {
            /* Make sure _tcstok() didn't skip an empty token (processed delimiters are replaced by 0). */
            return TRUE;
        }
        errno = 0;
        *minor = (unsigned int)_tcstoul(token, &endptr, 10);
        if ((errno != 0) || (token == endptr) || (*endptr)) {
            /* invalid || no digits were read || additional characters remain */
            return TRUE;
        }
        /* Get the revision. */
        token = _tcstok(NULL, TEXT("_.")
#if defined(UNICODE) && !defined(WIN32)
            , &state
#endif
        );
        if (token) {
            if (*(token - 2) == 0) {
                /* Make sure _tcstok() didn't skip an empty token (processed delimiters are replaced by 0). */
                return TRUE;
            }
            errno = 0;
            *revision = (unsigned int)_tcstoul(token, &endptr, 10);
            if ((errno != 0) || (token == endptr) || (*endptr)) {
                /* invalid || no digits were read || additional characters remain */
                return TRUE;
            }
        } else {
            *revision = defaultComponentValue;
        }
    } else {
        *minor = defaultComponentValue;
        *revision = defaultComponentValue;
    }
    return FALSE;
}

/**
 * Parse the output of 'java -version' and retrieve the major, minor and revision components of the version.
 *  Note: The output will modified by the function.
 *
 * @param javaVersionStr the string reprentation of the Java version as returned by 'java -version'.
 * @param defaultComponentValue value to use if minor or revision are not specified.
 *
 * @return a pointer to the newly created JavaVersion structure, or NULL on error.
 */
JavaVersion* parseJavaVersion(const TCHAR *javaVersionStr, unsigned int defaultComponentValue) {
    JavaVersion* result = NULL;
    
    if (javaVersionStr) {
        result = createJavaVersion();
        if (result) {
            if (strcmpIgnoreCase(javaVersionStr, TEXT("UNLIMITED")) == 0) {
                result->major    = UINT_MAX;
                result->minor    = UINT_MAX;
                result->revision = UINT_MAX;
            } else if (parseJavaVersionInner(javaVersionStr, &result->major, &result->minor, &result->revision, defaultComponentValue)) {
                disposeJavaVersion(result);
                return NULL;
            }
            updateStringValue(&result->displayName, javaVersionStr);
        }
    }
    return result;
}

/**
 * Get the Java version from a configuration property.
 *
 * @param propName       name of the configuration property.
 * @param defaultValue   default value.
 * @param minVersion     the minimum version allowed.
 * @param minVersionName if specified will be printed when the value
 *                       of the property is less than minVersion.
 * @param defaultComponentValue value to use if minor or revision are not specified.
 *
 * @param javaVersion A pointer to JavaVersion.
 */
JavaVersion* getJavaVersionProperty(TCHAR *propName, TCHAR *defaultValue, JavaVersion *minVersion, const TCHAR *minVersionName, unsigned int defaultComponentValue) {
    JavaVersion* result = NULL;
    const TCHAR* propValue;
    
    propValue = getNotEmptyStringProperty(properties, propName, defaultValue);
    if (propValue) {
        result = parseJavaVersion(propValue, defaultComponentValue);
        if (!result) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                TEXT("Encountered an invalid value for configuration property %s=%s."),
                propName, propValue);
        } else if (minVersion && compareJavaVersion(result, minVersion) < 0) {
            if (minVersionName) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                    TEXT("Encountered an invalid value for configuration property %s=%s.\n  The target version must be greater than or equal to the value of %s (%s)."),
                    propName, result->displayName, minVersionName, minVersion->displayName);
            } else {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                    TEXT("Encountered an invalid value for configuration property %s=%s.\n  The target version must be greater than or equal to %s."),
                    propName, result->displayName, minVersion->displayName);
            }
            disposeJavaVersion(result);
            result = NULL;
        }
    }
    return result;
}

/**
 * Get the minimum required version of Java for the Wrapper to run.
 *
 * @return javaVersion A pointer to JavaVersion.
 */
JavaVersion* getMinRequiredJavaVersion() {
    JavaVersion* result = createJavaVersion();
    
    if (result) {
        result->major = 4;
        result->minor = 0;
        result->revision = 0;
        updateStringValue(&result->displayName, TEXT("1.4"));
    }
    return result;
}

/**
 * Get the maximum required version of Java for the Wrapper to run.
 *
 * @return javaVersion A pointer to JavaVersion.
 */
JavaVersion* getMaxRequiredJavaVersion() {
    JavaVersion* result = createJavaVersion();
    
    if (result) {
        result->major = UINT_MAX;
        result->minor = UINT_MAX;
        result->revision = UINT_MAX;
        updateStringValue(&result->displayName, TEXT("UNLIMITED"));
    }
    return result;
}

/**
 * Parse the output of 'java -version' and retrieve the major, minor and revision components of the version.
 *  Note: The output will modified by the function.
 *
 * @param javaOutput      the output returned by 'java -version'.
 * @param pJavaVersionStr a pointer to the string reprentation of the Java version contained in javaOutput.
 * @param major           a pointer to the major component of the Java version.
 * @param minor           a pointer to the minor component of the Java version.
 * @param revision        a pointer to the revision component of the Java version.
 *
 * @return TRUE if there is any error, FALSE otherwise.
 */
int parseOutputJavaVersionInner(TCHAR *javaOutput, TCHAR **pJavaVersionStr, unsigned int *major, unsigned int *minor, unsigned int *revision) {
    TCHAR *ptr;

    if (!javaOutput) {
        return TRUE;
    }

    /* Start after the first double quote. */
    *pJavaVersionStr = _tcschr(javaOutput, TEXT('\"'));
    if (!*pJavaVersionStr) {
        /* Not found - fail. */
        return TRUE;
    }
    (*pJavaVersionStr)++;

    /* Crop at the closing double quote. */
    ptr = _tcschr(*pJavaVersionStr, TEXT('\"'));
    if (!ptr) {
        /* Not found - fail. */
        return TRUE;
    }
    *ptr = 0;
    ptr = *pJavaVersionStr;
    while (*ptr) {
        if (!_istdigit(*ptr) && (*ptr != '.') && (*ptr != '_')) {
            /* Some JVM implementations (or customized JVMs) append a label after the version.
             *  Examples: "1.8.0.14-hp-ux", "9-Raspbian". */
            *ptr = 0;
            break;
        }
        ptr++;
    }

    return parseJavaVersionInner(*pJavaVersionStr, major, minor, revision, 0);
}

/**
 * Parse the output of 'java -version' and retrieve the major, minor and revision components of the version.
 *  Note: The output will modified by the function.
 *
 * @param javaOutput the output returned by 'java -version'.
 *
 * @return a pointer to the newly created JavaVersion structure, or NULL on error.
 */
JavaVersion* parseOutputJavaVersion(TCHAR *javaOutput) {
    TCHAR* displayName;
    JavaVersion* result = NULL;
    
    if (javaOutput) {
        result = createJavaVersion();
        if (result) {
            if (parseOutputJavaVersionInner(javaOutput, &displayName, &result->major, &result->minor, &result->revision)) {
                disposeJavaVersion(result);
                return NULL;
            }
            updateStringValue(&result->displayName, displayName);
        }
    }
    return result;
}

/**
 * Compare two versions of Java.
 *
 * @param version1 version of Java
 * @param version2 version of Java
 *
 * @return <0 if version1 < version2
 *         0 if version1 == version2
 *         >0 if version1 > version2
 */
int compareJavaVersion(JavaVersion *version1, JavaVersion* version2) {
    if (version1 && version2) {
        if (version1->major < version2->major) {
            return -1;
        } else if (version1->major > version2->major) {
            return 1;
        } else {
            if (version1->minor < version2->minor) {
                return -1;
            } else if (version1->minor > version2->minor) {
                return 1;
            } else {
                if (version1->revision < version2->revision) {
                    return -1;
                } else if (version1->revision > version2->revision) {
                    return 1;
                }
            }
        }
    } else if (version1) {
        return 1;
    } else if (version2) {
        return -1;
    }
    return 0;
}

/**
 * Parse the output of 'java -version' and retrieve the maker (implementation) of the JVM.
 *
 * @param output   the output returned by 'java -version' (or only the line of the output containing the maker).
 *
 * @return an integer representing the JVM implementation:
 *              JVM_VENDOR_UNKNOWN
 *              JVM_VENDOR_ORACLE
 *              JVM_VENDOR_OPENJDK
 *              JVM_VENDOR_IBM
 */
int parseOutputJvmVendor(TCHAR* output) {
    if (output) {
        if (_tcsstr(output, TEXT("IBM"))) {
            return JVM_VENDOR_IBM;
        } else if (_tcsstr(output, TEXT("OpenJDK"))) {
            return JVM_VENDOR_OPENJDK;
        } else if (_tcsstr(output, TEXT("Java HotSpot"))) {
            return JVM_VENDOR_ORACLE;
        }
    }
    return JVM_VENDOR_UNKNOWN;
}

/**
 * Get the name of a JVM maker.
 *
 * @param maker an integer representing the JVM implementation:
 *              JVM_VENDOR_UNKNOWN
 *              JVM_VENDOR_ORACLE
 *              JVM_VENDOR_OPENJDK
 *              JVM_VENDOR_IBM
 *
 * @return the name of the JVM maker.
 */
const TCHAR* getJvmVendorName(int jvmVendor) {
    TCHAR *name;
    switch (jvmVendor) {
    case JVM_VENDOR_ORACLE:
        name = TEXT("Oracle");
        break;

    case JVM_VENDOR_OPENJDK:
        name = TEXT("OpenJDK");
        break;

    case JVM_VENDOR_IBM:
        name = TEXT("IBM");
        break;

    default:
        name = TEXT("Unknown");
        break;
    }
    return name;
}

/**
 * Parse the output of 'java -version' and retrieve the bits of the JVM.
 *
 * @param output   the output returned by 'java -version' (or only the line of the output containing the bits).
 *
 * @return an integer which indicates the bits of the JVM:
 *              JVM_BITS_64
 *              JVM_BITS_32
 *              JVM_BITS_UNKNOWN
 */
int parseOutputJvmBits(TCHAR* output, JavaVersion *optionalJavaVersion) {
    if (output) {
        /* This is experimental and may not work for all JVMs.
         * For example a IBM JVM will return the following output:
         *  IBM J9 VM (build 2.3, J2RE 1.5.0 IBM J9 2.3 Linux ppc64-64 j9vmxp6423-20130203 (JIT enabled).
         * Should we add a check on the Maker? */
        if (_tcsstr(output, TEXT("64-Bit")) ||
            _tcsstr(output, TEXT("64-bit")) ||
            _tcsstr(output, TEXT("64 Bit")) ||
            _tcsstr(output, TEXT("64 bit")) ||
            _tcsstr(output, TEXT("-64 ")) ||
            _tcsstr(output, TEXT("ppc64"))) {
            return JVM_BITS_64;
        } else {
#if defined(HPUX) || defined(MACOSX) || defined(SOLARIS) || defined(FREEBSD)
            /* On these systems, the JVM (for version < 9) can operate both in 32-bit and 64-bits.
             *  To confirm the bits, we would need to first run 'java -version', then if and only if
             *  the version is less than 9, run 'java -d64 -version' (-d64 is invalid for Java 9+). */
            if ((optionalJavaVersion == NULL) || (optionalJavaVersion->major < 9)) {
                return JVM_BITS_UNKNOWN;
            }
#else
            return JVM_BITS_32;
#endif
        }
    }
    return JVM_BITS_UNKNOWN;
}

/**
 * Get a string representing the bits of the JVM.
 *
 * @param bits an integer which indicates the bits of the JVM:
 *              JVM_BITS_64
 *              JVM_BITS_32
 *              JVM_BITS_UNKNOWN
 *
 * @return the string representing the bits of the JVM.
 */
const TCHAR* getJvmBitsName(int jvmBits) {
    TCHAR *name;
    switch (jvmBits) {
    case JVM_BITS_64:
        name = TEXT("64-Bit");
        break;

    case JVM_BITS_32:
        name = TEXT("32-Bit");
        break;

    default:
        name = TEXT("Unknown");
        break;
    }
    return name;
}
