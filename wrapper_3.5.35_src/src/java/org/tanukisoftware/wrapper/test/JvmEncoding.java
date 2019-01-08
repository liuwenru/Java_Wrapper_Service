package org.tanukisoftware.wrapper.test;

/*
 * Copyright (c) 1999, 2018 Tanuki Software, Ltd.
 * http://www.tanukisoftware.com
 * All rights reserved.
 *
 * This software is the proprietary information of Tanuki Software.
 * You shall use it only in accordance with the terms of the
 * license agreement you entered into with Tanuki Software.
 * http://wrapper.tanukisoftware.com/doc/english/licenseOverview.html
 */

import org.tanukisoftware.wrapper.WrapperManager;
import java.io.UnsupportedEncodingException;

/**
 *
 *
 * @author Tanuki Software Development Team &lt;support@tanukisoftware.com&gt;
 */
public class JvmEncoding {
    /*---------------------------------------------------------------
     * Main Method
     *-------------------------------------------------------------*/
    public static void main(String[] args) {
        System.out.println( Main.getRes().getString( "Test the handling of different charsets." ) );
        System.out.println();
        
        String encodings[][] = {{"Cp858", "IBM00858"},
                                {"Cp437", "IBM437"},
                                {"Cp775", "IBM775"},
                                {"Cp850", "IBM850"},
                                {"Cp852", "IBM852"},
                                {"Cp855", "IBM855"},
                                {"Cp857", "IBM857"},
                                {"Cp862", "IBM862"},
                                {"Cp866", "IBM866"},
                                {"ISO8859_1", "ISO-8859-1"},
                                {"ISO8859_2", "ISO-8859-2"},
                                {"ISO8859_4", "ISO-8859-4"},
                                {"ISO8859_5", "ISO-8859-5"},
                                {"ISO8859_7", "ISO-8859-7"},
                                {"ISO8859_9", "ISO-8859-9"},
                                {"ISO8859_13", "ISO-8859-13"},
                                {"ISO8859_15", "ISO-8859-15"},
                                {"KOI8_R", "KOI8-R"},
                                {"KOI8_U", "KOI8-U"},
                                {"ASCII", "US-ASCII"},
                                {"UTF8", "UTF-8"},
                                {"UTF-16", "UTF-16"},
                                {"UnicodeBigUnmarked", "UTF-16BE"},
                                {"UnicodeLittleUnmarked", "UTF-16LE"},
                                {"UTF_32", "UTF-32"},
                                {"UTF_32BE", "UTF-32BE"},
                                {"UTF_32LE", "UTF-32LE"},
                                {"UTF_32BE_BOM", "x-UTF-32BE-BOM"},
                                {"UTF_32LE_BOM", "x-UTF-32LE-BOM"},
                                {"Cp1250", "windows-1250"},
                                {"Cp1251", "windows-1251"},
                                {"Cp1252", "windows-1252"},
                                {"Cp1253", "windows-1253"},
                                {"Cp1254", "windows-1254"},
                                {"Cp1257", "windows-1257"},
                                {"UnicodeBig", "Not available"},
                                {"Cp737", "x-IBM737"},
                                {"Cp874", "x-IBM874"},
                                {"UnicodeLittle", "x-UTF-16LE-BOM"},
                                {"Big5", "Big5"},
                                {"Big5_HKSCS", "Big5-HKSCS"},
                                {"EUC_JP", "EUC-JP"},
                                {"EUC_KR", "EUC-KR"},
                                {"GB18030", "GB18030"},
                                {"EUC_CN", "GB2312"},
                                {"GBK", "GBK"},
                                {"Cp838", "IBM-Thai"},
                                {"Cp1140", "IBM01140"},
                                {"Cp1141", "IBM01141"},
                                {"Cp1142", "IBM01142"},
                                {"Cp1143", "IBM01143"},
                                {"Cp1144", "IBM01144"},
                                {"Cp1145", "IBM01145"},
                                {"Cp1146", "IBM01146"},
                                {"Cp1147", "IBM01147"},
                                {"Cp1148", "IBM01148"},
                                {"Cp1149", "IBM01149"},
                                {"Cp037", "IBM037"},
                                {"Cp1026", "IBM1026"},
                                {"Cp1047", "IBM1047"},
                                {"Cp273", "IBM273"},
                                {"Cp277", "IBM277"},
                                {"Cp278", "IBM278"},
                                {"Cp280", "IBM280"},
                                {"Cp284", "IBM284"},
                                {"Cp285", "IBM285"},
                                {"Cp290", "IBM290"},
                                {"Cp297", "IBM297"},
                                {"Cp300", "IBM300"},
                                {"Cp420", "IBM420"},
                                {"Cp424", "IBM424"},
                                {"Cp500", "IBM500"},
                                {"Cp860", "IBM860"},
                                {"Cp861", "IBM861"},
                                {"Cp863", "IBM863"},
                                {"Cp864", "IBM864"},
                                {"Cp865", "IBM865"},
                                {"Cp868", "IBM868"},
                                {"Cp869", "IBM869"},
                                {"Cp870", "IBM870"},
                                {"Cp871", "IBM871"},
                                {"Cp918", "IBM918"},
                                {"ISO2022CN", "ISO-2022-CN"},
                                {"ISO2022JP", "ISO-2022-JP"},
                                {"ISO2022KR", "ISO-2022-KR"},
                                {"ISO8859_3", "ISO-8859-3"},
                                {"ISO8859_6", "ISO-8859-6"},
                                {"ISO8859_8", "ISO-8859-8"},
                                {"JIS_X0201", "JIS_X0201"},
                                {"JIS_X0212-1990", "JIS_X0212-1990"},
                                {"SJIS", "Shift_JIS"},
                                {"TIS620", "TIS-620"},
                                {"Cp1255", "windows-1255"},
                                {"Cp1256", "windows-1256"},
                                {"Cp1258", "windows-1258"},
                                {"MS932", "windows-31j"},
                                {"Big5_Solaris", "x-Big5-Solaris"},
                                {"EUC_JP_LINUX", "x-euc-jp-linux"},
                                {"EUC_TW", "x-EUC-TW"},
                                {"EUC_JP_Solaris", "x-eucJP-Open"},
                                {"Cp1006", "x-IBM1006"},
                                {"Cp1025", "x-IBM1025"},
                                {"Cp1046", "x-IBM1046"},
                                {"Cp1097", "x-IBM1097"},
                                {"Cp1098", "x-IBM1098"},
                                {"Cp1112", "x-IBM1112"},
                                {"Cp1122", "x-IBM1122"},
                                {"Cp1123", "x-IBM1123"},
                                {"Cp1124", "x-IBM1124"},
                                {"Cp1381", "x-IBM1381"},
                                {"Cp1383", "x-IBM1383"},
                                {"Cp33722", "x-IBM33722"},
                                {"Cp834", "x-IBM834"},
                                {"Cp856", "x-IBM856"},
                                {"Cp875", "x-IBM875"},
                                {"Cp921", "x-IBM921"},
                                {"Cp922", "x-IBM922"},
                                {"Cp930", "x-IBM930"},
                                {"Cp933", "x-IBM933"},
                                {"Cp935", "x-IBM935"},
                                {"Cp937", "x-IBM937"},
                                {"Cp939", "x-IBM939"},
                                {"Cp942", "x-IBM942"},
                                {"Cp942C", "x-IBM942C"},
                                {"Cp943", "x-IBM943"},
                                {"Cp943C", "x-IBM943C"},
                                {"Cp948", "x-IBM948"},
                                {"Cp949", "x-IBM949"},
                                {"Cp949C", "x-IBM949C"},
                                {"Cp950", "x-IBM950"},
                                {"Cp964", "x-IBM964"},
                                {"Cp970", "x-IBM970"},
                                {"ISCII91", "x-ISCII91"},
                                {"ISO2022_CN_CNS", "x-ISO2022-CN-CNS"},
                                {"ISO2022_CN_GB", "x-ISO2022-CN-GB"},
                                {"x-iso-8859-11", "x-iso-8859-11"},
                                {"x-JIS0208", "x-JIS0208"},
                                {"JISAutoDetect", "x-JISAutoDetect"},
                                {"x-Johab", "x-Johab"},
                                {"MacArabic", "x-MacArabic"},
                                {"MacCentralEurope", "x-MacCentralEurope"},
                                {"MacCroatian", "x-MacCroatian"},
                                {"MacCyrillic", "x-MacCyrillic"},
                                {"MacDingbat", "x-MacDingbat"},
                                {"MacGreek", "x-MacGreek"},
                                {"MacHebrew", "x-MacHebrew"},
                                {"MacIceland", "x-MacIceland"},
                                {"MacRoman", "x-MacRoman"},
                                {"MacRomania", "x-MacRomania"},
                                {"MacSymbol", "x-MacSymbol"},
                                {"MacThai", "x-MacThai"},
                                {"MacTurkish", "x-MacTurkish"},
                                {"MacUkraine", "x-MacUkraine"},
                                {"MS950_HKSCS", "x-MS950-HKSCS"},
                                {"MS936", "x-mswin-936"},
                                {"PCK", "x-PCK"},
                                {"x-SJIS_0213", "x-SJIS_0213"},
                                {"Cp50220", "x-windows-50220"},
                                {"Cp50221", "x-windows-50221"},
                                {"MS874", "x-windows-874"},
                                {"MS949", "x-windows-949"},
                                {"MS950", "x-windows-950"},
                                {"x-windows-iso2022jp", "x-windows-iso2022jp"}};
        
        String str = "test sentence 123";
        
        int passed = 0;
        int failed = 0;
        boolean atLeastOneFailed = false;
        
        for ( int i = 0; i < encodings.length; i++ ) {
            if (checkEncoding(str, encodings[i][0])) {
                passed++;
            } else {
                atLeastOneFailed = true;
                failed++;
            }
            if (checkEncoding(str, encodings[i][0].toUpperCase())) {
                passed++;
            } else {
                atLeastOneFailed = true;
                failed++;
            }
            if (checkEncoding(str, encodings[i][0].toLowerCase())) {
                passed++;
            } else {
                atLeastOneFailed = true;
                failed++;
            }
            if (checkEncoding(str, encodings[i][1])) {
                passed++;
            } else {
                atLeastOneFailed = true;
                failed++;
            }
            if (checkEncoding(str, encodings[i][1].toUpperCase())) {
                passed++;
            } else {
                atLeastOneFailed = true;
                failed++;
            }
            if (checkEncoding(str, encodings[i][1].toLowerCase())) {
                passed++;
            } else {
                atLeastOneFailed = true;
                failed++;
            }
            if (atLeastOneFailed) {
                System.out.println();
                atLeastOneFailed = false;
            }
        }

        if (passed > 0) {
            System.out.println( Main.getRes().getString( "JVM encoding {0} test(s) passed.", new Integer(passed) ));
        }
        if (failed > 0) {
            System.out.println( Main.getRes().getString( "JVM encoding {0} test(s) FAILED.", new Integer(failed) ));
        }
    }
    
    private static boolean checkEncoding(String inStr, String encoding) {
        boolean result;
        try {
            byte[] bytes = inStr.getBytes(encoding);
            String outStr = new String(bytes, encoding);
            result = inStr.equals(outStr);
            if (!result) {
                System.err.println( Main.getRes().getString( "{0} supported but conversion error.", encoding ) );
            } else {
                /*System.out.println( Main.getRes().getString( "{0} supported", encoding ) );*/
            }
        } catch ( UnsupportedEncodingException e ) { 
            System.err.println( Main.getRes().getString( "{0} not supported.", encoding ) );
            result = false;
        } catch ( Exception e ) {
            System.err.println( Main.getRes().getString( "{0} not supported - {1}", encoding, e ) );
            result = false;
        }
        return result;
    }
}
