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

import org.tanukisoftware.wrapper.WrapperJNIError;
import org.tanukisoftware.wrapper.WrapperLicenseError;
import org.tanukisoftware.wrapper.WrapperManager;
import org.tanukisoftware.wrapper.WrapperProcess;
import org.tanukisoftware.wrapper.WrapperProcessConfig;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.Random;

/**
 *
 *
 * @author Tanuki Software Development Team &lt;support@tanukisoftware.com&gt;
 */
public class ChildWrapper
{
    private static String c_encoding;
    
    /*---------------------------------------------------------------
     * Static Methods
     *-------------------------------------------------------------*/
    static
    {
        // In order to read the output from some processes correctly we need to get the correct encoding.
        //  On some systems, the underlying system encoding is different than the file encoding.
        c_encoding = System.getProperty( "sun.jnu.encoding" );
        if ( c_encoding == null )
        {
            c_encoding = System.getProperty( "file.encoding" );
            if ( c_encoding == null )
            {
                // Default to Latin1
                c_encoding = "Cp1252";
            }
        }
    }
    
    private static Thread outputThread( final String context, final Object process, final InputStream is, final String name  )
        throws IOException
    {
        final BufferedReader br = new BufferedReader( new InputStreamReader( is, c_encoding ) );
        Thread thread = new Thread( context + "-" + name )
        {
            public void run()
            {
                try
                {
                    try
                    {
                        try
                        {
                            String line;
                            while ( ( line = br.readLine() ) != null )
                            {
                                System.out.println( name + ": " + line );
                            }
                        }
                        finally
                        {
                            br.close();
                        }
                    }
                    catch ( IOException e )
                    {
                        System.out.println( name + ": Error" );
                        e.printStackTrace();
                    }
                }
                finally
                {
                    System.out.println( name + ": End" );
                }
            }
        };
        thread.start();
        
        return thread;
    }
    
    private static void joinThread( Thread thread )
    {
        if ( thread != null )
        {
            try
            {
                thread.join();
            }
            catch ( InterruptedException e )
            {
            }
        }
    }
    
    private static void handleJavaProcess( String command )
        throws IOException, InterruptedException
    {
        System.out.println( "Test Begin: " + command );
        
        Thread stderrThread;
        Thread stdoutThread;
        Process process = Runtime.getRuntime().exec( command );
        try
        {
            // Dump all stdout
            stdoutThread = outputThread( "handleJavaProcess", process, process.getInputStream(), "stdout" );
            
            // Dump all stderr
            stderrThread = outputThread( "handleJavaProcess", process, process.getErrorStream(), "stderr" );
        }
        finally
        {
            int exitCode = process.waitFor();
            System.out.println( "exitCode: " + exitCode );
        }
        
        // Wait for the stdout, stderr threads to complete.
        joinThread( stdoutThread );
        joinThread( stderrThread );
        
        System.out.println( "Test End" );
        System.out.println();
        System.out.println();
    }

    private static void handleWrapperProcess( String command )
        throws IOException, InterruptedException
    {
        System.out.println( "Test Begin: " + command );
        
        Thread stderrThread;
        Thread stdoutThread;
        final WrapperProcess process = WrapperManager.exec( command );
        try
        {
            // Dump all stdout
            stdoutThread = outputThread( "handleJavaProcess", process, process.getInputStream(), "stdout" );
            
            // Dump all stderr
            stderrThread = outputThread( "handleJavaProcess", process, process.getErrorStream(), "stderr" );
        }
        finally
        {
            int exitCode = process.waitFor();
            System.out.println( "exitCode: " + exitCode );
        }
        
        // Wait for the stdout, stderr threads to complete.
        joinThread( stdoutThread );
        joinThread( stderrThread );
        
        System.out.println( "Test End" );
        System.out.println();
        System.out.println();
    }
    
    /*---------------------------------------------------------------
     * Main Method
     *-------------------------------------------------------------*/
    public static void main( String[] args )
    {
        System.out.println( "Communicate with child processes using encoding: " + c_encoding );
        
        try
        {
            String wrapperCmdVersion;
            String wrapperCmdTestWrapper;
            if ( WrapperManager.isWindows() )
            {
                wrapperCmdVersion = "..\\bin\\wrapper.exe -v";
                wrapperCmdTestWrapper = "..\\bin\\wrapper.exe -c ..\\conf\\wrapper.conf -- exit0";
            }
            else
            {
                wrapperCmdVersion = "../bin/wrapper -v";
                wrapperCmdTestWrapper = "../bin/wrapper -c ../conf/wrapper.conf -- exit0";
            }
            String batCmd = "cmd /c ..\\bin\\TestWrapper.bat exit0";
            String batDirect = "..\\bin\\TestWrapper.bat exit0";
            
            System.out.println( Main.getRes().getString( "Runtime.exec test (Version)." ) );
            handleJavaProcess( wrapperCmdVersion );
            
            System.out.println( Main.getRes().getString( "Runtime.exec test (TestWrapper)." ) );
            handleJavaProcess( wrapperCmdTestWrapper );
            
            System.out.println( Main.getRes().getString( "Runtime.exec test (SimpleWaiter)." ) );
            handleJavaProcess( "../test/simplewaiter 99 3" );
            
            System.out.println( Main.getRes().getString( "Runtime.exec test (SimpleWaiter Crash)." ) );
            handleJavaProcess( "../test/simplewaiter -crash 99 3" );
            
            if ( WrapperManager.isStandardEdition() )
            {
                System.out.println( Main.getRes().getString( "WrapperManager.exec test (Version)." ) );
                handleWrapperProcess( wrapperCmdVersion );
                
                System.out.println( Main.getRes().getString( "WrapperManager.exec test (TestWrapper)." ) );
                handleWrapperProcess( wrapperCmdTestWrapper );
                
                System.out.println( Main.getRes().getString( "WrapperManager.exec test (SimpleWaiter)." ) );
                handleWrapperProcess( "../test/simplewaiter 99 3" );
                
                System.out.println( Main.getRes().getString( "WrapperManager.exec test (SimpleWaiter Crash)." ) );
                handleWrapperProcess( "../test/simplewaiter -crash 99 3" );
            }
            
            if ( WrapperManager.isWindows() )
            {
                System.out.println( Main.getRes().getString( "Runtime.exec test (Bat with cmd)." ) );
                handleJavaProcess( batCmd );
                
                System.out.println( Main.getRes().getString( "Runtime.exec test (Bat direct)." ) );
                handleJavaProcess( batDirect );
            }
        }
        catch ( Exception e )
        {
            e.printStackTrace();
        }
    }
}
