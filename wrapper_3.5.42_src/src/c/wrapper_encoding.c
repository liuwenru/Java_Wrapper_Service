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
#include "wrapper.h"
#include "wrapper_hashmap.h"
#include "property.h"
#include "logger.h"
#include "wrapper_jvminfo.h"
#include "wrapper_encoding.h"

#ifdef WIN32
 #define WIN_FILL(a, i, v) a[i] = v
 #define NIX_FILL(a, i, v) /* nothing */
#else
 #define WIN_FILL(a, i, v) /* nothing */
 #define NIX_FILL(a, i, v) a[i] = v
#endif

#define K_ENCODING_V_ENCODING           1   /* map any encoding (io or nio) to its corresponding encoding (On Unix: io if key is nio, and nio if key is io, On Windows: code page). */
#define K_ENCODING_V_JVERSION           2   /* map any encoding (io or nio) to the Java version in which it was introduced. */
#ifndef WIN32
 #define K_ENCODING_V_ALIAS             3   /* map any encoding (io or nio) to its corresponding alias. */
 #define K_ALIAS_V_IOENCODING           4   /* map any iconv alias encoding (if it exists) to its corresponding io encoding. */
 #define K_ENCODING_V_IOENCODING        5   /* map any encoding (io or nio) to its corresponding io encoding (this also allows to normalize the case when the key is an io encoding). */
 #define K_SHORTENCODING_V_IOENCODING   6   /* map the short notation of any encoding (io or nio) to its corresponding io encoding (this also allows to normalize the case when the key is an io encoding). */
#else
 #define K_CODEPAGE_V_IOENCODING        7   /* map a code page to its corresponding io encoding. */
#endif

/**
 * Build a hashMap containing the encodings supported by Java.
 *  - On Windows, the keys are the canonical names for all APIs
 *    and the values are the corresponding code pages.
 *    If there there are no corresponding code page, 0 is set.
 *  - On UNIX, the keys are the canonical names for for java.io API and 
 *    java.lang API and the values are the canonical names for java.nio API. 
 *
 * @return The created hashmap or NULL on failure.
 */
PHashMap buildJvmEncodingsHashMap(int mode) {
    PHashMap hashMap;
    int i = 0;
    int          jv[163]; /* Java versions in which the encodings were introduced. */
    const TCHAR* e1[163]; /* Canonical Names for java.io API and java.lang API */
    const TCHAR* e2[163]; /* Canonical Names for java.nio API */
#ifdef WIN32
    int          cp[163]; /* Windows Code Pages */
    int          id[163]; /* Whether the code page is an ID to retrieve the encoding */
#else
    const TCHAR* al[163]; /* Alias used by iconv. */
    TCHAR  key1Buff[ENCODING_BUFFER_SIZE];
    TCHAR  key2Buff[ENCODING_BUFFER_SIZE];
#endif
    TCHAR* key1;
    TCHAR* key2;
    
         jv[i] = 5; e1[i] = TEXT("Cp858");                 e2[i] = TEXT("IBM00858");            NIX_FILL(al, i, TEXT("IBM-858"));         WIN_FILL(cp, i, 858);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp437");                 e2[i] = TEXT("IBM437");              NIX_FILL(al, i, TEXT("IBM-437"));         WIN_FILL(cp, i, 437);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp775");                 e2[i] = TEXT("IBM775");              NIX_FILL(al, i, TEXT("IBM-775"));         WIN_FILL(cp, i, 775);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp850");                 e2[i] = TEXT("IBM850");              NIX_FILL(al, i, TEXT("IBM-850"));         WIN_FILL(cp, i, 850);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp852");                 e2[i] = TEXT("IBM852");              NIX_FILL(al, i, TEXT("IBM-852"));         WIN_FILL(cp, i, 852);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp855");                 e2[i] = TEXT("IBM855");              NIX_FILL(al, i, TEXT("IBM-855"));         WIN_FILL(cp, i, 855);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp857");                 e2[i] = TEXT("IBM857");              NIX_FILL(al, i, TEXT("IBM-857"));         WIN_FILL(cp, i, 857);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp862");                 e2[i] = TEXT("IBM862");              NIX_FILL(al, i, TEXT("IBM-862"));         WIN_FILL(cp, i, 862);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp866");                 e2[i] = TEXT("IBM866");              NIX_FILL(al, i, TEXT("IBM-866"));         WIN_FILL(cp, i, 866);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO8859_1");             e2[i] = TEXT("ISO-8859-1");          NIX_FILL(al, i, TEXT("ISO8859-1"));       WIN_FILL(cp, i, 28591); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO8859_2");             e2[i] = TEXT("ISO-8859-2");          NIX_FILL(al, i, TEXT("ISO8859-2"));       WIN_FILL(cp, i, 28592); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO8859_4");             e2[i] = TEXT("ISO-8859-4");          NIX_FILL(al, i, TEXT("ISO8859-4"));       WIN_FILL(cp, i, 28594); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO8859_5");             e2[i] = TEXT("ISO-8859-5");          NIX_FILL(al, i, TEXT("ISO8859-5"));       WIN_FILL(cp, i, 28595); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO8859_7");             e2[i] = TEXT("ISO-8859-7");          NIX_FILL(al, i, TEXT("ISO8859-7"));       WIN_FILL(cp, i, 28597); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO8859_9");             e2[i] = TEXT("ISO-8859-9");          NIX_FILL(al, i, TEXT("ISO8859-9"));       WIN_FILL(cp, i, 28599); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO8859_13");            e2[i] = TEXT("ISO-8859-13");         NIX_FILL(al, i, TEXT("ISO8859-13"));      WIN_FILL(cp, i, 28603); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("ISO8859_15");            e2[i] = TEXT("ISO-8859-15");         NIX_FILL(al, i, TEXT("ISO8859-15"));      WIN_FILL(cp, i, 28605); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("KOI8_R");                e2[i] = TEXT("KOI8-R");              NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 20866); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 6; e1[i] = TEXT("KOI8_U");                e2[i] = TEXT("KOI8-U");              NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 21866); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ASCII");                 e2[i] = TEXT("US-ASCII");            NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 20127); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("UTF8");                  e2[i] = TEXT("UTF-8");               NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 65001); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("UTF-16");                e2[i] = TEXT("UTF-16");              NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 7 */
    i++; jv[i] = 5; e1[i] = TEXT("UnicodeBigUnmarked");    e2[i] = TEXT("UTF-16BE");            NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1201);  WIN_FILL(id, i, TRUE);  /* NOTE 7 */
    i++; jv[i] = 5; e1[i] = TEXT("UnicodeLittleUnmarked"); e2[i] = TEXT("UTF-16LE");            NIX_FILL(al, i, TEXT("UTF-16le"));        WIN_FILL(cp, i, 1200);  WIN_FILL(id, i, TRUE);  /* NOTE 7 */
    i++; jv[i] = 6; e1[i] = TEXT("UTF_32");                e2[i] = TEXT("UTF-32");              NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined */
    i++; jv[i] = 6; e1[i] = TEXT("UTF_32BE");              e2[i] = TEXT("UTF-32BE");            NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 12001); WIN_FILL(id, i, TRUE);  /* NOTE 7 */
    i++; jv[i] = 6; e1[i] = TEXT("UTF_32LE");              e2[i] = TEXT("UTF-32LE");            NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 12000); WIN_FILL(id, i, TRUE);  /* NOTE 7 */
    i++; jv[i] = 6; e1[i] = TEXT("UTF_32BE_BOM");          e2[i] = TEXT("x-UTF-32BE-BOM");      NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 12001); WIN_FILL(id, i, FALSE); /* NOTE 7 */
    i++; jv[i] = 6; e1[i] = TEXT("UTF_32LE_BOM");          e2[i] = TEXT("x-UTF-32LE-BOM");      NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 12000); WIN_FILL(id, i, FALSE); /* NOTE 7 */
    i++; jv[i] = 0; e1[i] = TEXT("Cp1250");                e2[i] = TEXT("windows-1250");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1250);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp1251");                e2[i] = TEXT("windows-1251");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1251);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp1252");                e2[i] = TEXT("windows-1252");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1252);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp1253");                e2[i] = TEXT("windows-1253");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1253);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp1254");                e2[i] = TEXT("windows-1254");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1254);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp1257");                e2[i] = TEXT("windows-1257");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1257);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 6; e1[i] = TEXT("UnicodeBig");            e2[i] = TEXT("Not available");       NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined */
    i++; jv[i] = 5; e1[i] = TEXT("Cp737");                 e2[i] = TEXT("x-IBM737");            NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 737);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp874");                 e2[i] = TEXT("x-IBM874");            NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 874);   WIN_FILL(id, i, FALSE);
    i++; jv[i] = 0; e1[i] = TEXT("UnicodeLittle");         e2[i] = TEXT("x-UTF-16LE-BOM");      NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1200);  WIN_FILL(id, i, FALSE); /* NOTE 7, NOTE 13 */
    i++; jv[i] = 0; e1[i] = TEXT("Big5");                  e2[i] = TEXT("Big5");                NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 950);   WIN_FILL(id, i, FALSE);
    i++; jv[i] = 0; e1[i] = TEXT("Big5_HKSCS");            e2[i] = TEXT("Big5-HKSCS");          NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 951);   WIN_FILL(id, i, FALSE); /* NOTE 1 */
    i++; jv[i] = 0; e1[i] = TEXT("EUC_JP");                e2[i] = TEXT("EUC-JP");              NIX_FILL(al, i, TEXT("eucJP"));           WIN_FILL(cp, i, 20932); WIN_FILL(id, i, TRUE);  /* NOTE 2, NOTE 16 */
    i++; jv[i] = 0; e1[i] = TEXT("EUC_KR");                e2[i] = TEXT("EUC-KR");              NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 51949); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("GB18030");               e2[i] = TEXT("GB18030");             NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 54936); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("EUC_CN");                e2[i] = TEXT("GB2312");              NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 51936); WIN_FILL(id, i, TRUE);  /* NOTE 3 */
    i++; jv[i] = 0; e1[i] = TEXT("GBK");                   e2[i] = TEXT("GBK");                 NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined */
    i++; jv[i] = 5; e1[i] = TEXT("Cp838");                 e2[i] = TEXT("IBM-Thai");            NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 20838); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1140");                e2[i] = TEXT("IBM01140");            NIX_FILL(al, i, TEXT("IBM-1140"));        WIN_FILL(cp, i, 1140);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1141");                e2[i] = TEXT("IBM01141");            NIX_FILL(al, i, TEXT("IBM-1141"));        WIN_FILL(cp, i, 1141);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1142");                e2[i] = TEXT("IBM01142");            NIX_FILL(al, i, TEXT("IBM-1142"));        WIN_FILL(cp, i, 1142);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1143");                e2[i] = TEXT("IBM01143");            NIX_FILL(al, i, TEXT("IBM-1143"));        WIN_FILL(cp, i, 1143);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1144");                e2[i] = TEXT("IBM01144");            NIX_FILL(al, i, TEXT("IBM-1144"));        WIN_FILL(cp, i, 1144);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1145");                e2[i] = TEXT("IBM01145");            NIX_FILL(al, i, TEXT("IBM-1145"));        WIN_FILL(cp, i, 1145);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1146");                e2[i] = TEXT("IBM01146");            NIX_FILL(al, i, TEXT("IBM-1146"));        WIN_FILL(cp, i, 1146);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1147");                e2[i] = TEXT("IBM01147");            NIX_FILL(al, i, TEXT("IBM-1147"));        WIN_FILL(cp, i, 1147);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1148");                e2[i] = TEXT("IBM01148");            NIX_FILL(al, i, TEXT("IBM-1148"));        WIN_FILL(cp, i, 1148);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1149");                e2[i] = TEXT("IBM01149");            NIX_FILL(al, i, TEXT("IBM-1149"));        WIN_FILL(cp, i, 1149);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp037");                 e2[i] = TEXT("IBM037");              NIX_FILL(al, i, TEXT("IBM-037"));         WIN_FILL(cp, i, 037);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp1026");                e2[i] = TEXT("IBM1026");             NIX_FILL(al, i, TEXT("IBM-1026"));        WIN_FILL(cp, i, 1026);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp1047");                e2[i] = TEXT("IBM1047");             NIX_FILL(al, i, TEXT("IBM-1047"));        WIN_FILL(cp, i, 1047);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp273");                 e2[i] = TEXT("IBM273");              NIX_FILL(al, i, TEXT("IBM-273"));         WIN_FILL(cp, i, 20273); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp277");                 e2[i] = TEXT("IBM277");              NIX_FILL(al, i, TEXT("IBM-277"));         WIN_FILL(cp, i, 20277); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp278");                 e2[i] = TEXT("IBM278");              NIX_FILL(al, i, TEXT("IBM-278"));         WIN_FILL(cp, i, 20278); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp280");                 e2[i] = TEXT("IBM280");              NIX_FILL(al, i, TEXT("IBM-280"));         WIN_FILL(cp, i, 20280); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp284");                 e2[i] = TEXT("IBM284");              NIX_FILL(al, i, TEXT("IBM-284"));         WIN_FILL(cp, i, 20284); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp285");                 e2[i] = TEXT("IBM285");              NIX_FILL(al, i, TEXT("IBM-285"));         WIN_FILL(cp, i, 20285); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 9; e1[i] = TEXT("Cp290");                 e2[i] = TEXT("IBM290");              NIX_FILL(al, i, TEXT("IBM-290"));         WIN_FILL(cp, i, 20290); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp297");                 e2[i] = TEXT("IBM297");              NIX_FILL(al, i, TEXT("IBM-297"));         WIN_FILL(cp, i, 20297); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 9; e1[i] = TEXT("Cp300");                 e2[i] = TEXT("IBM300");              NIX_FILL(al, i, TEXT("IBM-300"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined */
    i++; jv[i] = 0; e1[i] = TEXT("Cp420");                 e2[i] = TEXT("IBM420");              NIX_FILL(al, i, TEXT("IBM-420"));         WIN_FILL(cp, i, 20420); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp424");                 e2[i] = TEXT("IBM424");              NIX_FILL(al, i, TEXT("IBM-424"));         WIN_FILL(cp, i, 20424); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp500");                 e2[i] = TEXT("IBM500");              NIX_FILL(al, i, TEXT("IBM-500"));         WIN_FILL(cp, i, 500);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp860");                 e2[i] = TEXT("IBM860");              NIX_FILL(al, i, TEXT("IBM-860"));         WIN_FILL(cp, i, 860);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp861");                 e2[i] = TEXT("IBM861");              NIX_FILL(al, i, TEXT("IBM-861"));         WIN_FILL(cp, i, 861);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp863");                 e2[i] = TEXT("IBM863");              NIX_FILL(al, i, TEXT("IBM-863"));         WIN_FILL(cp, i, 863);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp864");                 e2[i] = TEXT("IBM864");              NIX_FILL(al, i, TEXT("IBM-864"));         WIN_FILL(cp, i, 864);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp865");                 e2[i] = TEXT("IBM865");              NIX_FILL(al, i, TEXT("IBM-865"));         WIN_FILL(cp, i, 865);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp868");                 e2[i] = TEXT("IBM868");              NIX_FILL(al, i, TEXT("IBM-868"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 0; e1[i] = TEXT("Cp869");                 e2[i] = TEXT("IBM869");              NIX_FILL(al, i, TEXT("IBM-869"));         WIN_FILL(cp, i, 869);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp870");                 e2[i] = TEXT("IBM870");              NIX_FILL(al, i, TEXT("IBM-870"));         WIN_FILL(cp, i, 870);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp871");                 e2[i] = TEXT("IBM871");              NIX_FILL(al, i, TEXT("IBM-871"));         WIN_FILL(cp, i, 20871); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp918");                 e2[i] = TEXT("IBM918");              NIX_FILL(al, i, TEXT("IBM-918"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("ISO2022CN");             e2[i] = TEXT("ISO-2022-CN");         NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 5, NOTE 14 */
    i++; jv[i] = 0; e1[i] = TEXT("ISO2022JP");             e2[i] = TEXT("ISO-2022-JP");         NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 50222); WIN_FILL(id, i, FALSE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO2022KR");             e2[i] = TEXT("ISO-2022-KR");         NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 50225); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO8859_3");             e2[i] = TEXT("ISO-8859-3");          NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 28593); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO8859_6");             e2[i] = TEXT("ISO-8859-6");          NIX_FILL(al, i, TEXT("ISO8859-6"));       WIN_FILL(cp, i, 28596); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("ISO8859_8");             e2[i] = TEXT("ISO-8859-8");          NIX_FILL(al, i, TEXT("ISO8859-8"));       WIN_FILL(cp, i, 28598); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("JIS_X0201");             e2[i] = TEXT("JIS_X0201");           NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 50221); WIN_FILL(id, i, FALSE); /* NOTE 4 */
    i++; jv[i] = 0; e1[i] = TEXT("JIS_X0212-1990");        e2[i] = TEXT("JIS_X0212-1990");      NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 20932); WIN_FILL(id, i, FALSE); /* NOTE 6 */
    i++; jv[i] = 0; e1[i] = TEXT("SJIS");                  e2[i] = TEXT("Shift_JIS");           NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 932);   WIN_FILL(id, i, FALSE);
    i++; jv[i] = 0; e1[i] = TEXT("TIS620");                e2[i] = TEXT("TIS-620");             NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 28601); WIN_FILL(id, i, TRUE);  /* NOTE 1 */
    i++; jv[i] = 0; e1[i] = TEXT("Cp1255");                e2[i] = TEXT("windows-1255");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1255);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp1256");                e2[i] = TEXT("windows-1256");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1256);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp1258");                e2[i] = TEXT("windows-1258");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1258);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("MS932");                 e2[i] = TEXT("windows-31j");         NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 932);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Big5_Solaris");          e2[i] = TEXT("x-Big5-Solaris");      NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined */
    i++; jv[i] = 0; e1[i] = TEXT("EUC_JP_LINUX");          e2[i] = TEXT("x-euc-jp-linux");      NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined */
    i++; jv[i] = 0; e1[i] = TEXT("EUC_TW");                e2[i] = TEXT("x-EUC-TW");            NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined */
    i++; jv[i] = 0; e1[i] = TEXT("EUC_JP_Solaris");        e2[i] = TEXT("x-eucJP-Open");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined */
    i++; jv[i] = 5; e1[i] = TEXT("Cp1006");                e2[i] = TEXT("x-IBM1006");           NIX_FILL(al, i, TEXT("IBM-1006"));        WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp1025");                e2[i] = TEXT("x-IBM1025");           NIX_FILL(al, i, TEXT("IBM-1025"));        WIN_FILL(cp, i, 21025); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp1046");                e2[i] = TEXT("x-IBM1046");           NIX_FILL(al, i, TEXT("IBM-1046"));        WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp1097");                e2[i] = TEXT("x-IBM1097");           NIX_FILL(al, i, TEXT("IBM-1097"));        WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp1098");                e2[i] = TEXT("x-IBM1098");           NIX_FILL(al, i, TEXT("IBM-1098"));        WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp1112");                e2[i] = TEXT("x-IBM1112");           NIX_FILL(al, i, TEXT("IBM-1112"));        WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp1122");                e2[i] = TEXT("x-IBM1122");           NIX_FILL(al, i, TEXT("IBM-1122"));        WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp1123");                e2[i] = TEXT("x-IBM1123");           NIX_FILL(al, i, TEXT("IBM-1123"));        WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp1124");                e2[i] = TEXT("x-IBM1124");           NIX_FILL(al, i, TEXT("IBM-1124"));        WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp1381");                e2[i] = TEXT("x-IBM1381");           NIX_FILL(al, i, TEXT("IBM-1381"));        WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp1383");                e2[i] = TEXT("x-IBM1383");           NIX_FILL(al, i, TEXT("IBM-1383"));        WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp33722");               e2[i] = TEXT("x-IBM33722");          NIX_FILL(al, i, TEXT("IBM-eucJP"));       WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp834");                 e2[i] = TEXT("x-IBM834");            NIX_FILL(al, i, TEXT("IBM-834"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0, NOTE 14 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp856");                 e2[i] = TEXT("x-IBM856");            NIX_FILL(al, i, TEXT("IBM-856"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp875");                 e2[i] = TEXT("x-IBM875");            NIX_FILL(al, i, TEXT("IBM-875"));         WIN_FILL(cp, i, 875);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp921");                 e2[i] = TEXT("x-IBM921");            NIX_FILL(al, i, TEXT("IBM-921"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp922");                 e2[i] = TEXT("x-IBM922");            NIX_FILL(al, i, TEXT("IBM-922"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp930");                 e2[i] = TEXT("x-IBM930");            NIX_FILL(al, i, TEXT("IBM-930"));         WIN_FILL(cp, i, 50930); WIN_FILL(id, i, TRUE); 
    i++; jv[i] = 5; e1[i] = TEXT("Cp933");                 e2[i] = TEXT("x-IBM933");            NIX_FILL(al, i, TEXT("IBM-933"));         WIN_FILL(cp, i, 50933); WIN_FILL(id, i, TRUE); 
    i++; jv[i] = 5; e1[i] = TEXT("Cp935");                 e2[i] = TEXT("x-IBM935");            NIX_FILL(al, i, TEXT("IBM-935"));         WIN_FILL(cp, i, 50935); WIN_FILL(id, i, TRUE); 
    i++; jv[i] = 5; e1[i] = TEXT("Cp937");                 e2[i] = TEXT("x-IBM937");            NIX_FILL(al, i, TEXT("IBM-937"));         WIN_FILL(cp, i, 50937); WIN_FILL(id, i, TRUE); 
    i++; jv[i] = 5; e1[i] = TEXT("Cp939");                 e2[i] = TEXT("x-IBM939");            NIX_FILL(al, i, TEXT("IBM-939"));         WIN_FILL(cp, i, 50939); WIN_FILL(id, i, TRUE); 
    i++; jv[i] = 5; e1[i] = TEXT("Cp942");                 e2[i] = TEXT("x-IBM942");            NIX_FILL(al, i, TEXT("IBM-942"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp942C");                e2[i] = TEXT("x-IBM942C");           NIX_FILL(al, i, TEXT("IBM-942"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp943");                 e2[i] = TEXT("x-IBM943");            NIX_FILL(al, i, TEXT("IBM-943"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp943C");                e2[i] = TEXT("x-IBM943C");           NIX_FILL(al, i, TEXT("IBM-943"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp948");                 e2[i] = TEXT("x-IBM948");            NIX_FILL(al, i, TEXT("IBM-948"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp949");                 e2[i] = TEXT("x-IBM949");            NIX_FILL(al, i, TEXT("IBM-949"));         WIN_FILL(cp, i, 949);   WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp949C");                e2[i] = TEXT("x-IBM949C");           NIX_FILL(al, i, TEXT("IBM-949"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp950");                 e2[i] = TEXT("x-IBM950");            NIX_FILL(al, i, TEXT("IBM-950"));         WIN_FILL(cp, i, 950);   WIN_FILL(id, i, FALSE);
    i++; jv[i] = 5; e1[i] = TEXT("Cp964");                 e2[i] = TEXT("x-IBM964");            NIX_FILL(al, i, TEXT("IBM-964"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 5; e1[i] = TEXT("Cp970");                 e2[i] = TEXT("x-IBM970");            NIX_FILL(al, i, TEXT("IBM-970"));         WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 0 */
    i++; jv[i] = 0; e1[i] = TEXT("ISCII91");               e2[i] = TEXT("x-ISCII91");           NIX_FILL(al, i, TEXT("ISCII.1991"));      WIN_FILL(cp, i, 57002); WIN_FILL(id, i, FALSE); /* undefined - NOTE 8 */
    i++; jv[i] = 6; e1[i] = TEXT("ISO2022_CN_CNS");        e2[i] = TEXT("x-ISO2022-CN-CNS");    NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 50229); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 6; e1[i] = TEXT("ISO2022_CN_GB");         e2[i] = TEXT("x-ISO2022-CN-GB");     NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 50227); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("x-iso-8859-11");         e2[i] = TEXT("x-iso-8859-11");       NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 874);   WIN_FILL(id, i, FALSE); /* NOTE 9 */
    i++; jv[i] = 0; e1[i] = TEXT("x-JIS0208");             e2[i] = TEXT("x-JIS0208");           NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 20932); WIN_FILL(id, i, FALSE);
    i++; jv[i] = 0; e1[i] = TEXT("JISAutoDetect");         e2[i] = TEXT("x-JISAutoDetect");     NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 12 */
    i++; jv[i] = 0; e1[i] = TEXT("x-Johab");               e2[i] = TEXT("x-Johab");             NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 1361);  WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacArabic");             e2[i] = TEXT("x-MacArabic");         NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10004); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacCentralEurope");      e2[i] = TEXT("x-MacCentralEurope");  NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10029); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacCroatian");           e2[i] = TEXT("x-MacCroatian");       NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10082); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacCyrillic");           e2[i] = TEXT("x-MacCyrillic");       NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10007); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacDingbat");            e2[i] = TEXT("x-MacDingbat");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined */
    i++; jv[i] = 5; e1[i] = TEXT("MacGreek");              e2[i] = TEXT("x-MacGreek");          NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10006); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacHebrew");             e2[i] = TEXT("x-MacHebrew");         NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10005); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacIceland");            e2[i] = TEXT("x-MacIceland");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10079); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacRoman");              e2[i] = TEXT("x-MacRoman");          NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10000); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacRomania");            e2[i] = TEXT("x-MacRomania");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10010); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacSymbol");             e2[i] = TEXT("x-MacSymbol");         NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined */
    i++; jv[i] = 5; e1[i] = TEXT("MacThai");               e2[i] = TEXT("x-MacThai");           NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10021); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacTurkish");            e2[i] = TEXT("x-MacTurkish");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10081); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MacUkraine");            e2[i] = TEXT("x-MacUkraine");        NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 10017); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("MS950_HKSCS");           e2[i] = TEXT("x-MS950-HKSCS");       NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 951);   WIN_FILL(id, i, TRUE);  /* NOTE 1 */
    i++; jv[i] = 0; e1[i] = TEXT("MS936");                 e2[i] = TEXT("x-mswin-936");         NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 936);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("PCK");                   e2[i] = TEXT("x-PCK");               NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 932);   WIN_FILL(id, i, FALSE); /* NOTE 10 */
    i++; jv[i] = 5; e1[i] = TEXT("x-SJIS_0213");           e2[i] = TEXT("x-SJIS_0213");         NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, -1);    WIN_FILL(id, i, FALSE); /* undefined - NOTE 11, NOTE 15 */
    i++; jv[i] = 0; e1[i] = TEXT("Cp50220");               e2[i] = TEXT("x-windows-50220");     NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 50220); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("Cp50221");               e2[i] = TEXT("x-windows-50221");     NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 50221); WIN_FILL(id, i, TRUE);
    i++; jv[i] = 5; e1[i] = TEXT("MS874");                 e2[i] = TEXT("x-windows-874");       NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 874);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("MS949");                 e2[i] = TEXT("x-windows-949");       NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 949);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("MS950");                 e2[i] = TEXT("x-windows-950");       NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 950);   WIN_FILL(id, i, TRUE);
    i++; jv[i] = 0; e1[i] = TEXT("x-windows-iso2022jp");   e2[i] = TEXT("x-windows-iso2022jp"); NIX_FILL(al, i, TEXT(""));                WIN_FILL(cp, i, 50220); WIN_FILL(id, i, FALSE);

    hashMap = newHashMap(16);
    if (!hashMap) {
        return NULL;
    }
    
    if (mode == K_ENCODING_V_ENCODING) {
        for (; i >= 0; i--) {
            key1 = toLower(e1[i]);
            if (!key1) {
                freeHashMap(hashMap);
                return NULL;
            }
            key2 = toLower(e2[i]);
            if (!key2) {
                free(key1);
                freeHashMap(hashMap);
                return NULL;
            }
#ifdef WIN32
            hashMapPutKWVI(hashMap, key1, cp[i]);
#else
            hashMapPutKWVW(hashMap, key1, e2[i]);
#endif
            if (_tcscmp(key1, key2) != 0) {
#ifdef WIN32
                hashMapPutKWVI(hashMap, key2, cp[i]);
#else
                hashMapPutKWVW(hashMap, key2, e1[i]);
#endif
            }
            free(key1);
            free(key2);
        }
    } else if (mode == K_ENCODING_V_JVERSION) {
        for (; i >= 0; i--) {
            key1 = toLower(e1[i]);
            if (!key1) {
                freeHashMap(hashMap);
                return NULL;
            }
            key2 = toLower(e2[i]);
            if (!key2) {
                free(key1);
                freeHashMap(hashMap);
                return NULL;
            }
            hashMapPutKWVI(hashMap, key1, jv[i]);
            if (_tcscmp(key1, key2) != 0) {
                hashMapPutKWVI(hashMap, key2, jv[i]);
            }
            free(key1);
            free(key2);
        }
#ifndef WIN32
    } else if ((mode == K_ENCODING_V_IOENCODING) || (mode == K_SHORTENCODING_V_IOENCODING) || (mode == K_ENCODING_V_ALIAS)) {
        for (; i >= 0; i--) {
            key1 = toLower(e1[i]);
            if (!key1) {
                freeHashMap(hashMap);
                return NULL;
            }
            if (mode == K_SHORTENCODING_V_IOENCODING) {
                clearNonAlphanumeric(key1, key1Buff);
                free(key1);
                key1 = key1Buff;
            }
            key2 = toLower(e2[i]);
            if (!key2) {
                free(key1);
                freeHashMap(hashMap);
                return NULL;
            }
            if (mode == K_SHORTENCODING_V_IOENCODING) {
                clearNonAlphanumeric(key2, key2Buff);
                free(key2);
                key2 = key2Buff;
            }
            if (mode == K_ENCODING_V_ALIAS) {
                hashMapPutKWVW(hashMap, key1, al[i]);
            } else {
                hashMapPutKWVW(hashMap, key1, e1[i]);
            }
            if (_tcscmp(key1, key2) != 0) {
                if (mode == K_ENCODING_V_ALIAS) {
                    hashMapPutKWVW(hashMap, key2, al[i]);
                } else {
                    hashMapPutKWVW(hashMap, key2, e1[i]);
                }
            }
            if (mode != K_SHORTENCODING_V_IOENCODING) {
                free(key1);
                free(key2);
            }
        }
    } else if (mode == K_ALIAS_V_IOENCODING) {
        for (; i >= 0; i--) {
            if (_tcslen(al[i]) > 0) {
                key1 = toLower(al[i]);
                if (!key1) {
                    freeHashMap(hashMap);
                    return NULL;
                }
                hashMapPutKWVW(hashMap, key1, e1[i]);
            }
        }
#else
    } else if (mode == K_CODEPAGE_V_IOENCODING) {
        for (; i >= 0; i--) {
            if (id[i] && cp[i] > 0) {
                if (wrapperData->isDebugging) {
                    if (hashMapGetKIVW(hashMap, cp[i])) {
                        /* This should not happen if the above tables are well maintened. */
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG, TEXT("Don't know if code page %d should be mapped to '%s' or '%s'."), cp[i], hashMapGetKIVW(hashMap, cp[i]), e1[i]);
                    }
                }
                hashMapPutKIVW(hashMap, cp[i], e1[i]);
            }
        }
#endif
    }
    return hashMap;
}

static PHashMap hashMapJvmEncoding = NULL;
static PHashMap hashMapJavaVersions = NULL;

/**
 * Should be called on exit to release the hashmap containing the JVM encodings.
 */
void disposeHashMapJvmEncoding() {
    if (hashMapJvmEncoding) {
        freeHashMap(hashMapJvmEncoding);
        hashMapJvmEncoding = NULL;
    }
    if (hashMapJavaVersions) {
        freeHashMap(hashMapJavaVersions);
        hashMapJavaVersions = NULL;
    }
}

/**
 * Check if the given encoding is supported for a specific version of Java.
 *
 * @encoding the encoding to search
 * @javaVersion current java version
 *
 * @return TRUE if the encoding exists, FALSE otherwise.
 */
int checkEncodingJavaVersion(const TCHAR* encoding, int javaVersion, int *pRequiredJavaVersion) {
    TCHAR* encLower;
    int outRequiredJavaVersion;
    int result = FALSE;
    
    if (encoding) {
        encLower = toLower(encoding);
        if (encLower) {
            if (!hashMapJavaVersions) {
                /* Create a hashmap containing the Java versions in which each encoding was introduced. Keep it as a static global variable. */
                hashMapJavaVersions = buildJvmEncodingsHashMap(K_ENCODING_V_JVERSION);
            }
            if (hashMapJavaVersions) {
                /* Return TRUE if the Java version is greater than or equal to the required Java version for the encoding. */
                outRequiredJavaVersion = hashMapGetKWVI(hashMapJavaVersions, encLower);
                result = (javaVersion >= outRequiredJavaVersion);
                if (pRequiredJavaVersion) {
                    *pRequiredJavaVersion = outRequiredJavaVersion;
                }
            }
            free(encLower);
        }
    }
    return result;
}

#ifndef WIN32
/**
 * On most systems, Iconv uses uppercase notations for the encodings, sometimes being case-sensitive.
 *  On HPUX, the lowercase notation is often used (e.g. 'utf8').
 *  This function will check Iconv support of the given encoding and its uppercase/lowercase notations
 *  to offer more flexibility. It's not a problem to try many times, because we would stop anyway if
 *  no supported encoding can be found.
 *
 * @encoding the encoding to check
 * @buffer   buffer which will be filled with the supported encoding:
 *           - encoding if it is supported by iconv without transformation.
 *           - the uppercase/lowercase version of encoding, if supported.
 *           - empty if none of the above are supported.
 *
 * @return the buffer or NULL if the encoding is not supported.
 */
TCHAR* getIconvEncodingVariousCases(const TCHAR* encoding, TCHAR* buffer) {
    TCHAR* altEncoding;
    
    buffer[0] = 0;
    if (encoding && (_tcslen(encoding) > 0)) {
        if (getIconvEncodingSupport(encoding) != ICONV_ENCODING_NOT_SUPPORTED) {
            _tcsncpy(buffer, encoding, ENCODING_BUFFER_SIZE);
        } else {
            altEncoding = toUpper(encoding);
            if (altEncoding) {
                if ((_tcscmp(altEncoding, encoding) != 0) && (getIconvEncodingSupport(altEncoding) != ICONV_ENCODING_NOT_SUPPORTED)) {
                    _tcsncpy(buffer, altEncoding, ENCODING_BUFFER_SIZE);
                    free(altEncoding);
                } else {
                    free(altEncoding);
                    altEncoding = toLower(encoding);
                    if (altEncoding) {
                        if ((_tcscmp(altEncoding, encoding) != 0) && (getIconvEncodingSupport(altEncoding) != ICONV_ENCODING_NOT_SUPPORTED)) {
                            _tcsncpy(buffer, altEncoding, ENCODING_BUFFER_SIZE);
                        }
                        free(altEncoding);
                    }
                }
            }
        }
    }
    if (_tcslen(buffer) == 0) {
        return NULL;
    }
    return buffer;
}

/**
 * Check if the encoding is supported by the JVM.
 *
 * @localeEncoding the locale encoding
 * @javaVersion current java version
 * @buffer      buffer where the output encoding should be copied
 *
 * @return a string representation of the JVM io encoding, or NULL if no value could be found.
 */
TCHAR* getJvmIoEncoding(TCHAR* localeEncoding, int javaVersion, TCHAR* buffer) {
    TCHAR* localeEncLower;
    TCHAR localeEncShort[ENCODING_BUFFER_SIZE];
    PHashMap hashMap;
    const TCHAR* encoding;
    TCHAR* result = NULL;
    
    buffer[0] = 0;
    if (localeEncoding && (_tcslen(localeEncoding) > 0)) {
        localeEncLower = toLower(localeEncoding);
        if (localeEncLower) {
            hashMap = buildJvmEncodingsHashMap(K_ENCODING_V_IOENCODING);
            if (hashMap) {
                encoding = hashMapGetKWVW(hashMap, localeEncLower);
                if (!encoding) {
                    /* No match found - search among aliases. */
                    freeHashMap(hashMap);
                    hashMap = buildJvmEncodingsHashMap(K_ALIAS_V_IOENCODING);
                    if (hashMap) {
                        encoding = hashMapGetKWVW(hashMap, localeEncLower);
                        if (!encoding) {
                            /* No match found - try without canonical dashes and punctuation (ex: EUC-JP -> eucjp).
                             *  No need to do this for aliases because they are already in the locale syntax. */
                            freeHashMap(hashMap);
                            hashMap = buildJvmEncodingsHashMap(K_SHORTENCODING_V_IOENCODING);
                            if (hashMap) {
                                clearNonAlphanumeric(localeEncLower, localeEncShort);
                                encoding = hashMapGetKWVW(hashMap, localeEncShort);
                            }
                        }
                    }
                }
                if (checkEncodingJavaVersion(encoding, javaVersion, NULL)) {
                    _tcsncpy(buffer, encoding, ENCODING_BUFFER_SIZE);
                    result = buffer;
                }
                freeHashMap(hashMap);
            }
            free(localeEncLower);
        }
    }
    return result;
}
#else
/**
 * Get the JVM encoding corresponding to a code page.
 *
 * @codePage    the Windows code page
 * @javaVersion current java version
 * @buffer      buffer where the output encoding should be copied.
 *
 * @return a string representation of the JVM io encoding, or NULL if no value could be found.
 */
TCHAR* getJvmIoEncodingFromCodePage(int codePage, int javaVersion, TCHAR* buffer) {
    PHashMap hashMap = buildJvmEncodingsHashMap(K_CODEPAGE_V_IOENCODING);
    const TCHAR* encoding;
    TCHAR* result = NULL;
    
    buffer[0] = 0;
    if (hashMap) {
        encoding = hashMapGetKIVW(hashMap, codePage);
        if (checkEncodingJavaVersion(encoding, javaVersion, NULL)) {
            _tcsncpy(buffer, encoding, ENCODING_BUFFER_SIZE);
            result = buffer;
        }
        freeHashMap(hashMap);
    }
    return result;
}
#endif

int checkEquivalentEncodings(TCHAR* encoding1, TCHAR* encoding2) {
#ifndef WIN32
    const TCHAR* value;
#endif
    int result = FALSE;
    TCHAR* enc1Lower;
    TCHAR* enc2Lower;
    
    enc1Lower = toLower(encoding1);
    if (!enc1Lower) {
        return FALSE;
    }
    enc2Lower = toLower(encoding2);
    if (!enc2Lower) {
        free(enc1Lower);
        return FALSE;
    }

    if (_tcscmp(enc1Lower, enc2Lower) == 0) {
        result = TRUE;
    } else {
        if (!hashMapJvmEncoding) {
            hashMapJvmEncoding = buildJvmEncodingsHashMap(K_ENCODING_V_ENCODING);
        }
        if (hashMapJvmEncoding &&
#ifdef WIN32
            (hashMapGetKWVI(hashMapJvmEncoding, enc1Lower) == hashMapGetKWVI(hashMapJvmEncoding, enc2Lower))
#else
            (((value = hashMapGetKWVW(hashMapJvmEncoding, enc1Lower)) != NULL) && strcmpIgnoreCase(value, enc2Lower) == 0)
#endif
        ) {
            result = TRUE;
        }
    }
    free(enc1Lower);
    free(enc2Lower);
    return result;
}

int getJvmSunEncodingSupport(int javaVersion, int jvmVendor) {
    int result = 0;
    
    if (jvmVendor == JVM_VENDOR_IBM) {
        result |= SUN_ENCODING_UNSUPPORTED_JVM_VENDOR;
    } else if ((jvmVendor != JVM_VENDOR_ORACLE) || (jvmVendor != JVM_VENDOR_OPENJDK)) {
        result |= SUN_ENCODING_SUPPORT_UNKNOWN;
    }
    if (javaVersion < 8) {
        result |= SUN_ENCODING_UNSUPPORTED_JAVA_VERSION;
    }
    if (result == 0) {
        result |= SUN_ENCODING_SUPPORTED;
    }
    return result;
}

void tryGetSunSystemProperty(const TCHAR* name, int javaVersion, int jvmVendor, TCHAR* propValue, TCHAR* buffer, int* result) {
    static int warnedPropsNotSupported[] = { FALSE, FALSE }; /* Remember if we logged warnings for each property, in the order: stdout, stderr */
    int propIndex;
    TCHAR argBase[23];
    int sunSupport;
    
#ifdef UNIT_TESTS
    if (resetStaticVariables) {
        warnedPropsNotSupported[0] = FALSE;
        warnedPropsNotSupported[1] = FALSE;
        resetStaticVariables = FALSE;
    }
#endif
    _sntprintf(argBase, 23, TEXT("-D%s="), name);
    if (_tcsstr(propValue, argBase) == propValue) {
        sunSupport = getJvmSunEncodingSupport(javaVersion, jvmVendor);
        if ((sunSupport & SUN_ENCODING_UNSUPPORTED_JVM_VENDOR) == SUN_ENCODING_UNSUPPORTED_JVM_VENDOR) {
            propIndex = (_tcscmp(name, TEXT("sun.stdout.encoding")) == 0) ? 0 : 1;
            if (!warnedPropsNotSupported[propIndex]) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                    TEXT("Found %s among the JVM parameters but this system property is not\n  supported on this implementation of Java."),
                    name);
                warnedPropsNotSupported[propIndex] = TRUE;
            }
        } else if ((sunSupport & SUN_ENCODING_UNSUPPORTED_JAVA_VERSION) == SUN_ENCODING_UNSUPPORTED_JAVA_VERSION) {
            propIndex = (_tcscmp(name, TEXT("sun.stdout.encoding")) == 0) ? 0 : 1;
            if (!warnedPropsNotSupported[propIndex]) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_WARN,
                    TEXT("Found %s among the JVM parameters but this system property is not\n  supported by this version of Java.\n  Requires Java 8 or above, using Java %d."),
                    name,
                    javaVersion);
                warnedPropsNotSupported[propIndex] = TRUE;
            }
        } else {
            if (buffer[0] == 0) {
                /* This is the first time we found this property. */
                _tcsncpy(buffer, propValue + 22, ENCODING_BUFFER_SIZE);
                buffer[ENCODING_BUFFER_SIZE - 1] = 0;
                /* Set the result to SUN_ENCODING to avoid searching for file.encoding. We may change it later if the sun properties are not configured correctly. */
                *result = SUN_ENCODING;
            } else if (!checkEquivalentEncodings(buffer, propValue + 22)) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                    TEXT("Found multiple occurrences of %s set with different values\n  among the JVM parameters. Cannot resolve the JVM output encoding."),
                    name);
                *result = UNRESOLVED_ENCODING;
            }   /* The remaining case is when the value of the current system property is different than one of a previously defined,
                 *  but both values point to the same encoding. If this happen, leave the result unchanged and continue. */
        } 
    }
}

/**
 * Retrieved the value of file.encoding (or sun.std*.encoding) if defined in the java additional properties.
 *  The buffer is set to an empty string if the value could not be found.
 *  disposeHashMapJvmEncoding() should be called before calling this function.
 *
 * @buffer buffer in which the encoding should be copied
 * @javaVersion current java version
 * @jvmVendor   current java implementation (Oracle, IBM, etc.)
 *
 * @return LOCALE_ENCODING if no encoding was specified in the JVM arguments
 *         FILE_ENCODING if the encoding was resolved to the value of file.encoding
 *         SUN_ENCODING if the encoding was resolved to the value of the sun.std*.encoding properties
 *         UNRESOLVED_ENCODING if there was any error. A FATAL message will be printed before returning
 */
int getJvmArgumentsEncoding(TCHAR* buffer, int javaVersion, int jvmVendor) {
    TCHAR** propNames;
    TCHAR** propValues;
    TCHAR bufferSunOut[ENCODING_BUFFER_SIZE];
    TCHAR bufferSunErr[ENCODING_BUFFER_SIZE];
    long unsigned int* propIndices;
    TCHAR* propValue;
    int i;
    int result = LOCALE_ENCODING; /* We can move the result to a higher value but never decrease it. The order is LOCALE_ENCODING -> FILE_ENCODING -> SUN_ENCODING -> UNRESOLVED_ENCODING. */
    int foundMultipleFileEncoding = FALSE;

    buffer[0] = 0;
    if (getStringProperties(properties, TEXT("wrapper.java.additional."), TEXT(""), wrapperData->ignoreSequenceGaps, FALSE, &propNames, &propValues, &propIndices)) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Failed to retrieve the values of the wrapper.java.additional.* properties."));
        return UNRESOLVED_ENCODING;
    }

    bufferSunOut[0] = 0;
    bufferSunErr[0] = 0;
    for (i = 0; propValues[i]; i++){
        propValue = propValues[i];
        tryGetSunSystemProperty(TEXT("sun.stdout.encoding"), javaVersion, jvmVendor, propValue, bufferSunOut, &result);
        if (result == UNRESOLVED_ENCODING) {
            break;
        }
        tryGetSunSystemProperty(TEXT("sun.stderr.encoding"), javaVersion, jvmVendor, propValue, bufferSunErr, &result);
        if (result == UNRESOLVED_ENCODING) {
            break;
        }
        if ((result != SUN_ENCODING) && (_tcsstr(propValue, TEXT("-Dfile.encoding=")) == propValue)) {
            if (buffer[0] == 0) {
                /* This is the first time we found this property. */
                _tcsncpy(buffer, propValue + 16, ENCODING_BUFFER_SIZE);
                buffer[ENCODING_BUFFER_SIZE - 1] = 0;
                result = FILE_ENCODING;
            } else if (!checkEquivalentEncodings(buffer, propValue + 16)) {
                /* Keep a flag and log later. We will ignore this case if the sun properties are set. */
                foundMultipleFileEncoding = TRUE;
            }   /* The remaining case is when the value of the current system property is different than one of a previously defined,
                 *  but both values point to the same encoding. If this happen, leave the result unchanged and continue. */
        }
    }
    
    if ((result == FILE_ENCODING) && foundMultipleFileEncoding) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
            TEXT("Found multiple occurrences of %s set with different values\n  among the JVM parameters. Cannot resolve the JVM output encoding."),
            TEXT("file.encoding"));
        result = UNRESOLVED_ENCODING;
    } else if (result == SUN_ENCODING) {
        /* For clarity, the sun.*.encoding propeties, when defined, should be both present. We don't even accept cases
         *  where only one would be defined along with file.encoding also set to the same value (although this is valid). */
        if (bufferSunOut[0] == 0) {
            if (bufferSunErr[0] != 0) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                    TEXT("Found sun.stderr.encoding but sun.stdout.encoding was not specified.\n  Please add sun.stdout.encoding to the list of JVM parameters and set its encoding to the same value as sun.stderr.encoding (%s)."), bufferSunErr);
                result = UNRESOLVED_ENCODING;
            }
        } else { /* bufferSunOut[0] != 0 */
            if (bufferSunErr[0] == 0) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                    TEXT("Found sun.stdout.encoding but sun.stderr.encoding was not specified.\n  Please add sun.stderr.encoding to the list of JVM parameters and set its encoding to the same value as sun.stdout.encoding (%s)."), bufferSunOut);
                result = UNRESOLVED_ENCODING;
            } else if (!checkEquivalentEncodings(bufferSunOut, bufferSunErr)) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                    TEXT("The encodings of sun.stdout.encoding (%s) and sun.stderr.encoding (%s) don't match.\n  Please set both system properties to the same value."), bufferSunOut, bufferSunErr);
                result = UNRESOLVED_ENCODING;
            } else {
                /* Both sun properties are defined and set to the same value (or equivalent values pointing to the same encoding). */
                _tcsncpy(buffer, bufferSunOut, ENCODING_BUFFER_SIZE);
            }
        }
    }
    
    if (result == UNRESOLVED_ENCODING) {
        buffer[0] = 0;
    }

    freeStringProperties(propNames, propValues, propIndices);
    return result;
}

#ifdef WIN32
static UINT jvmOutputCodePage;
/**
 * Get the code page used to encode the current JVM outputs.
 *  resolveJvmEncoding() should be called before using this function.
 *
 * @return UINT value of the code page (the code page of the current locale is returned by default)
 */
UINT getJvmOutputCodePage() {
    return jvmOutputCodePage;
}
#else
static TCHAR jvmOutputEncoding[ENCODING_BUFFER_SIZE];
static char jvmOutputEncodingMB[ENCODING_BUFFER_SIZE];
/**
 * Get the encoding used for the current JVM outputs.
 *  resolveJvmEncoding() should be called before using this function.
 *
 * @return String representation of the encoding if the value found in file.encoding is supported by iconv, NULL otherwise.
 *         The returned value doesn't need to be freed.
 */
const char* getJvmOutputEncodingMB() {
    if (jvmOutputEncodingMB[0] == 0) {
        return NULL;
    }
    return jvmOutputEncodingMB;
}
#endif

/**
 * Clear the Jvm encoding previously cached.
 *  This function can be called before getJvmOutputEncodingMB() to force using the encoding of the current locale.
 *  A call to resolveJvmEncoding() may then be necessary to restore the encoding.
 *
 * @debug TRUE to print a debug message, FALSE otherwise.
 */
void resetJvmOutputEncoding(int debug) {
    TCHAR buffer[ENCODING_BUFFER_SIZE];

    buffer[0] = 0;
#ifdef WIN32
    jvmOutputCodePage = wrapperData->jvm_stdout_codepage;
    _sntprintf(buffer, ENCODING_BUFFER_SIZE, TEXT("%d"), jvmOutputCodePage);
#else
    jvmOutputEncoding[0] = 0;
    jvmOutputEncodingMB[0] = 0;
    if (debug) {
        getCurrentLocaleEncoding(buffer);
    }
#endif
    if (debug) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
            TEXT("Reading the JVM output using the encoding of the current locale (%s)."), buffer);
    }
}

#define GET_ENCODING_SYSPROP(o)    o == FILE_ENCODING ? TEXT("file.encoding") : TEXT("sun.stdout.encoding and sun.stderr.encoding")

/**
 * Resolve the Java output encoding using system properties and an internal hashMap containing the supported encoding.
 *  This function should be called prior to using getJvmOutputCodePage() and getJvmOutputEncodingMB()
 *
 * @javaVersion current java version
 * @jvmVendor   current java implementation (Oracle, IBM, etc.)
 *
 * @return TRUE if there is any error (misconfiguration of system properties or unsuported encoding), FALSE otherwise.
 */
int resolveJvmEncoding(int javaVersion, int jvmVendor) {
    TCHAR buffer[ENCODING_BUFFER_SIZE];
    int jvmEncodingOrigin;
#ifndef WIN32
    PHashMap aliasHashMap;
    const TCHAR* altEncoding;
#endif
    int requiredJavaVersion = 0;
    TCHAR* encLower;
    
    /* Initiate use_sun_encoding to FALSE. We will use this flag when building the Java command line. */
    wrapperData->use_sun_encoding = FALSE;
    
    jvmEncodingOrigin = getJvmArgumentsEncoding(buffer, javaVersion, jvmVendor);
    if (jvmEncodingOrigin == UNRESOLVED_ENCODING) {
        /* Unresolved encoding - any error has already been logged */
        return TRUE;
    } else if (jvmEncodingOrigin != LOCALE_ENCODING) {
        /* The encoding was specified in a system property passed to the Java command line. */
        if (!hashMapJvmEncoding) {
            hashMapJvmEncoding = buildJvmEncodingsHashMap(K_ENCODING_V_ENCODING);
            if (!hashMapJvmEncoding) {
                return TRUE;
            }
        }
        encLower = toLower(buffer);
        if (!encLower) {
            return TRUE;
        }
#ifdef WIN32
        jvmOutputCodePage = (UINT)hashMapGetKWVI(hashMapJvmEncoding, encLower);
        free(encLower);
        if (jvmOutputCodePage == 0) {
            /* The value was not found in the hasmap. We have no way to know if the encoding is invalid or if it was
             *  added after this version of the Wrapper was released, so log a message to indicate both possibilities. */
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                TEXT("'%s' is not a valid value for %s\n  or is not supported by this version of the Wrapper."),
                buffer,
                GET_ENCODING_SYSPROP(jvmEncodingOrigin));
            return TRUE;
        } else if (jvmOutputCodePage == -1) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                TEXT("The value '%s' of %s is not supported on Windows."),
                buffer,
                GET_ENCODING_SYSPROP(jvmEncodingOrigin));
            jvmOutputCodePage = 0;
            return TRUE;
        } else if (!IsValidCodePage(jvmOutputCodePage)) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                TEXT("The value '%s' of %s is not a valid code page."),
                buffer,
                GET_ENCODING_SYSPROP(jvmEncodingOrigin));
            jvmOutputCodePage = 0;
            return TRUE;
        }
        
        if (!checkEncodingJavaVersion(buffer, javaVersion, &requiredJavaVersion)) {
            /* The value exist for a more recent version of Java. */
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                TEXT("The value '%s' of %s is supported from Java %d. The current version of Java is %d."),
                buffer,
                GET_ENCODING_SYSPROP(jvmEncodingOrigin),
                requiredJavaVersion,
                javaVersion);
            return TRUE;
        }

        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
            TEXT("Reading the JVM output using the value of %s: %s (resolved to code page %d)."),
            GET_ENCODING_SYSPROP(jvmEncodingOrigin),
            buffer,
            jvmOutputCodePage);
#else
        altEncoding = hashMapGetKWVW(hashMapJvmEncoding, encLower);
        if (!altEncoding || (_tcscmp(buffer, TEXT("Not available")) == 0)) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                TEXT("'%s' is not a valid value for %s\n  or is not supported by this version of the Wrapper."),
                buffer,
                GET_ENCODING_SYSPROP(jvmEncodingOrigin));
            free(encLower);
            return TRUE;
        } else if (!getIconvEncodingVariousCases(buffer, jvmOutputEncoding)) {
            /* Check if the alternative encoding is supported by Iconv. */
            if ((_tcscmp(altEncoding, TEXT("Not available")) == 0) || (!getIconvEncodingVariousCases(altEncoding, jvmOutputEncoding))) {
                /* Check if an alias is supported by Iconv. */
                altEncoding = NULL;
                aliasHashMap = buildJvmEncodingsHashMap(K_ENCODING_V_ALIAS);
                if (aliasHashMap) {
                    altEncoding = hashMapGetKWVW(aliasHashMap, encLower);
                }
                if (!altEncoding || (_tcslen(altEncoding) == 0) || (!getIconvEncodingVariousCases(altEncoding, jvmOutputEncoding))) {
                    /* Possible improvement: list all locale encodings, build a hashmap (key=<SHORT_LOCALE_ENCODING>, value=<LOCALE_ENCODING>), and search a match in it. */
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                        TEXT("The value '%s' of %s is not supported by iconv."),
                        buffer,
                        GET_ENCODING_SYSPROP(jvmEncodingOrigin));
                    freeHashMap(aliasHashMap);
                    free(encLower);
                    return TRUE;
                }
                freeHashMap(aliasHashMap);
            }
        }
        free(encLower);
        
        if (!checkEncodingJavaVersion(buffer, javaVersion, &requiredJavaVersion)) {
            /* The value exists for a more recent version of Java. */
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
                TEXT("The value '%s' of %s is supported from Java %d. The current version of Java is %d."),
                buffer,
                GET_ENCODING_SYSPROP(jvmEncodingOrigin),
                requiredJavaVersion,
                javaVersion);
            return TRUE;
        }

        if (wcstombs(jvmOutputEncodingMB, jvmOutputEncoding, ENCODING_BUFFER_SIZE) == (size_t)-1) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Error when converting the JVM output encoding '%s'."), jvmOutputEncoding);
            return TRUE;
        }
        if (_tcscmp(buffer, jvmOutputEncoding) == 0) {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("Reading the JVM output using the value of %s: %s."),
                GET_ENCODING_SYSPROP(jvmEncodingOrigin),
                buffer);
        } else {
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_DEBUG,
                TEXT("Reading the JVM output using the value of %s: %s (resolved to %s)."),
                GET_ENCODING_SYSPROP(jvmEncodingOrigin),
                buffer,
                jvmOutputEncoding);
        }
#endif
        if (jvmEncodingOrigin == SUN_ENCODING) {
            wrapperData->use_sun_encoding = TRUE;
        }
    } else {
        /* The encoding of the current locale should be used. */
        resetJvmOutputEncoding(wrapperData->isDebugging);
    }

    return FALSE;
}
