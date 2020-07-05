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
 */

import org.tanukisoftware.wrapper.WrapperManager;
import java.io.UnsupportedEncodingException;
import java.io.IOException;
import java.io.File;

/**
 *
 *
 * @author Tanuki Software Development Team &lt;support@tanukisoftware.com&gt;
 */
public class JvmEncoding2 {
    /*---------------------------------------------------------------
     * Main Method
     *-------------------------------------------------------------*/
    public static void main(String[] args) {
        System.out.println( Main.getRes().getString( "Test the handling of different charsets (file creation and if output is correctly decoded by the Wrapper)." ) );
        System.out.println();
        
        String currentEncoding = System.getProperty("file.encoding");
        System.out.println(Main.getRes().getString("Current file.encoding: {0}", currentEncoding));
        
        String currentSunEncoding = System.getProperty("sun.stdout.encoding");
        System.out.println(Main.getRes().getString("Current sun.stdout.encoding: {0}", currentSunEncoding));
        
        boolean noTest = true;
        
        if (WrapperManager.isWindows()) {
            if (currentEncoding.equals("MS949")) {
                /* Do not translate this message! Korean characters would corrupt the mo file... */
                System.out.println("Test output korean characters: \uD55C\uAD6D\uC5B4");
                CreateAndDeleteFile("\uD55C\uAD6D\uC5B4");
                noTest = false;
            }
            /* Add more languages here */
        }
        
        if (noTest) {
            System.out.println(Main.getRes().getString("No test for the current platform and language. Try changing the OS language."));
        }
    }
    
    private static boolean CreateAndDeleteFile(String filename) {
        File file = new File(filename);
        boolean result = false;
        try
        {
            result = file.createNewFile();
            System.out.println(Main.getRes().getString("Successfully created a new file ''{0}'' in the working directory.\nPress any key to delete it.", filename));
            
            System.in.read();
            
            if (!file.delete()) {
                System.out.println(Main.getRes().getString("Error while deleting the file"));
            }
        }
        catch(IOException ioe)
        {
            System.out.println(Main.getRes().getString("Error while creating a new empty file :") + ioe);
            result = false;
        }
        return result;
    }
}
