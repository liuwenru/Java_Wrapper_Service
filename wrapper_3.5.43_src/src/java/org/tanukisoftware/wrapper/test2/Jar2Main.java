package org.tanukisoftware.wrapper.test2;

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

import org.tanukisoftware.wrapper.test.JarMain;

/**
 *
 *
 * @author Tanuki Software Development Team &lt;support@tanukisoftware.com&gt;
 */
public class Jar2Main
{
    /*---------------------------------------------------------------
     * Main Method
     *-------------------------------------------------------------*/
    public static void main( String[] args )
    {
        System.out.println( "Calling JarMain.main." );
        
        JarMain.main( args );
        
        System.out.println( "Returned from JarMain.main." );
    }
}

