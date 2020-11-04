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

#ifndef _WRAPPER_JVMINFO_H
#define _WRAPPER_JVMINFO_H
#include "wrapper_i18n.h"

#define JVM_VENDOR_UNKNOWN  0
#define JVM_VENDOR_ORACLE   1
#define JVM_VENDOR_OPENJDK  2
#define JVM_VENDOR_IBM      3

#define JVM_BITS_UNKNOWN    0
#define JVM_BITS_32         32
#define JVM_BITS_64         64

typedef struct JavaVersion JavaVersion;
struct JavaVersion {
    TCHAR       *displayName;   /* The name that will be used to display the version in the log output. When it comes from the configuration, it is the value as set by the user. */
    unsigned int major;         /* The major component of the version. */
    unsigned int minor;         /* The minor component of the version. */
    unsigned int revision;      /* The revision component of the version. */
    int          isUnknown;     /* Flag that indicates whether the Java version was parsed successfully or not. If not, it will be resolved to the lowest supported JVM version. */
};

/**
 * Dispose a JavaVersion structure.
 *
 * @param javaVersion A pointer to JavaVersion.
 */
void disposeJavaVersion(JavaVersion *javaVersion);

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
JavaVersion* getJavaVersionProperty(TCHAR *propName, TCHAR *defaultValue, JavaVersion *minVersion, const TCHAR *minVersionName, unsigned int defaultComponentValue);

/**
 * Get the minimum required version of Java for the Wrapper to run.
 *
 * @param javaVersion A pointer to JavaVersion.
 */
JavaVersion* getMinRequiredJavaVersion();

/**
 * Get the maximum required version of Java for the Wrapper to run.
 *
 * @param javaVersion A pointer to JavaVersion.
 */
JavaVersion* getMaxRequiredJavaVersion();

/**
 * Parse the output of 'java -version' and retrieve the major, minor and revision components of the version.
 *  Note: The output will modified by the function.
 *
 * @param javaOutput the output returned by 'java -version'.
 *
 * @return a pointer to the newly created JavaVersion structure, or NULL on error.
 */
JavaVersion* parseOutputJavaVersion(TCHAR *javaOutput);

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
int compareJavaVersion(JavaVersion *version1, JavaVersion* version2);

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
int parseOutputJvmVendor(TCHAR* output);

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
const TCHAR* getJvmVendorName(int jvmVendor);

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
int parseOutputJvmBits(TCHAR* output, JavaVersion *optionalJavaVersion);

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
const TCHAR* getJvmBitsName(int jvmBits);
#endif
