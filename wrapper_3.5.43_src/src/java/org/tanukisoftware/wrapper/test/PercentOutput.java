package org.tanukisoftware.wrapper.test;

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
 * Tests Bug #531880 where % characters in the text output were causing
 *  the wrapper to crash.
 *
 * @author Tanuki Software Development Team &lt;support@tanukisoftware.com&gt;
 */
public class PercentOutput {
    /*---------------------------------------------------------------
     * Main Method
     *-------------------------------------------------------------*/
    public static void main(String[] args) {
        System.out.println( Main.getRes().getString( "Starting Test..." ) );
        System.out.println( "%");
        System.out.println("%%");
        System.out.println("%s");
        System.out.println("%S");
        System.out.println("%d");
        System.out.println("\\%s%%");
        System.out.println("\\%S%%");
        
        // This is a case from a user crash
        System.out.println("         and vg.foobar like '%SEARCHKEY=vendorid%'");
        
        // Lots more output mixed with various quotes.
        //  This will test the code that causes the internal buffer to be expanded.
        for ( int i = 0; i < 100; i++ )
        {
            StringBuffer sb = new StringBuffer();
            sb.append( i );
            sb.append( ": " );
            for ( int j = 0; j < i; j++ )
            {
                sb.append( ":01234567890123456%s90123456789012345%d890123456789012345%S8901234%%789012%S5678901234%p789012345678" );
            }
            sb.append( ":END" );
            System.out.println( sb.toString() );
        }
        
        System.out.println( Main.getRes().getString( "Test Complete..." ) );
    }
}

