package org.tanukisoftware.wrapper;

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

import java.io.UnsupportedEncodingException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

/**
 * By default the WrapperStartStopApp will only wait for 2 seconds for the main
 *  method of the start class to complete.  This was done because the main
 *  methods of many applications never return.  It is possible to force the
 *  class to wait for the startup main method to complete by defining the
 *  following system property when launching the JVM (defaults to FALSE):
 *  -Dorg.tanukisoftware.wrapper.WrapperStartStopApp.waitForStartMain=TRUE
 * <p>
 * Using the waitForStartMain property will cause the startup to wait
 *  indefinitely.  This is fine if the main method will always return
 *  within a predefined period of time.  But if there is any chance that
 *  it could hang, then the maxStartMainWait property may be a better
 *  option.  It allows the 2 second wait time to be overridden. To wait
 *  for up to 5 minutes for the startup main method to complete, set
 *  the property to 300 as follows (defaults to 2 seconds):
 *  -Dorg.tanukisoftware.wrapper.WrapperStartStopApp.maxStartMainWait=300
 * <p>
 * By default, the WrapperStartStopApp will tell the Wrapper to exit with an
 *  exit code of 1 if any uncaught exceptions are thrown in the configured
 *  main method.  This is good in most cases, but is a little different than
 *  the way Java works on its own.  Java will stay up and running if it has
 *  launched any other non-daemon threads even if the main method ends because
 *  of an uncaught exception.  To get this same behavior, it is possible to
 *  specify the following system property when launching the JVM (defaults to
 *  FALSE):
 *  -Dorg.tanukisoftware.wrapper.WrapperStartStopApp.ignoreMainExceptions=TRUE
 * <p>
 * By default, passthrough parameters are ignored, however it is possible to 
 * specify a different behaviour using the following system property:
 *  -Dorg.tanukisoftware.wrapper.WrapperStartStopApp.passthroughMode=START<br>
 * Possible values are:
 * <ul>
 *  <li><b>START</b> : add passthrough parameters to the parameters list of the start method</li>
 *  <li><b>STOP</b> : add passthrough parameters to the parameters list of the stop method</li>
 *  <li><b>BOTH</b> : add passthrough parameters to both parameters lists of the start 
 *   and stop methods</li>
 *  <li><b>IGNORE</b> : (default) ignore the passthrough parameters</li>
 * </ul>
 * <p>
 * It is possible to extend this class but make absolutely sure that any
 *  overridden methods call their super method or the class will fail to
 *  function correctly.  Most users will have no need to override this
 *  class.  Remember that if overridden, the main method will also need to
 *  be recreated in the child class to make sure that the correct instance
 *  is created.
 * <p>
 * NOTE - The main methods of many applications are designed not to
 *  return.  In these cases, you must either stick with the default 2 second
 *  startup timeout or specify a slightly longer timeout, using the
 *  maxStartMainWait property, to simulate the amount of time your application
 *  takes to start up.
 * <p>
 * WARNING - If the waitForStartMain is specified for an application
 *  whose start method never returns, the Wrapper will appear at first to be
 *  functioning correctly.  However the Wrapper will never enter a running
 *  state, this means that the Windows Service Manager and several of the
 *  Wrapper's error recovery mechanisms will not function correctly.
 *
 * @author Tanuki Software Development Team &lt;support@tanukisoftware.com&gt;
 */
public class WrapperStartStopApp
    implements WrapperListener, Runnable
{
    /** Info level log channel */
    private static WrapperPrintStream m_outInfo;
    
    /** Error level log channel */
    private static WrapperPrintStream m_outError;
    
    /** Debug level log channel */
    private static WrapperPrintStream m_outDebug;
    
    /**
     * Application's start main method
     */
    private Method m_startMainMethod;
    
    /**
     * Command line arguments to be passed on to the start main method
     */
    private String[] m_startMainArgs;
    
    /**
     * Application's stop main method
     */
    private Method m_stopMainMethod;
    
    /**
     * Should the stop process force the JVM to exit, or wait for all threads
     *  to die on their own.
     */
    private boolean m_stopWait;
    
    /**
     * Command line arguments to be passed on to the stop main method
     */
    private String[] m_stopMainArgs;
    
    /**
     * Gets set to true when the thread used to launch the application
     *  actuially starts.
     */
    private boolean m_mainStarted;
    
    /**
     * Gets set to true when the thread used to launch the application
     *  completes.
     */
    private boolean m_mainComplete;
    
    /**
     * Exit code to be returned if the application fails to start.
     */
    private Integer m_mainExitCode;
    
    /**
     * True if uncaught exceptions in the user app's main method should be ignored.
     */
    private boolean m_ignoreMainExceptions;
    
    /**
     * Flag used to signify that the start method has completed.
     */
    private boolean m_startComplete;
    
    /**
     * Flag that is set if there were any initialization problems.
     */
    private boolean m_initFailed;
    
    /**
     * Error message which should be shown if initialization Failed.
     */
    private String m_initError;
    
    /**
     * True if usage should be shown as part of an initialization error.
     */
    private boolean m_initShowUsage;
    
    /**
     * The exception which caused the error.  Only needs to be set if the stacktrace is required.
     */
    private Throwable m_initException;
    
    /**
     * Params passed by passthrough are ignored.
     */
    private final String PASSTHROUGH_MODE_IGNORE = "ignore";
    
    /**
     * Params passed by passthrough are added in the start arguments list and stop arguments list. 
     */
    private final String PASSTHROUGH_MODE_BOTH   = "both";
    
    /**
     * Params passed by passthrough are added in the start arguments list.
     */
    private final String PASSTHROUGH_MODE_START  = "start";
    
    /**
     * Params passed by passthrough are added in the stop arguments list. 
     */
    private final String PASSTHROUGH_MODE_STOP   = "stop";
    
    /*---------------------------------------------------------------
     * Constructors
     *-------------------------------------------------------------*/
    /**
     * Creates an instance of a WrapperStartStopApp.
     *
     * @param args The full list of arguments passed to the JVM.
     */
    protected WrapperStartStopApp( String args[] )
    {
        // Initialize the WrapperManager class on startup by referencing it.
        Class wmClass = WrapperManager.class;
        
        // Set up some log channels
        boolean streamsSet = false;
        if ( "true".equals( System.getProperty( "wrapper.use_sun_encoding" ) ) ) {
            String sunStdoutEncoding = System.getProperty( "sun.stdout.encoding" );
            if ( ( sunStdoutEncoding != null ) && !sunStdoutEncoding.equals( System.getProperty( "file.encoding" ) ) ) {
                /* We need to create the stream using the same encoding as the one used for stdout, else this will lead to encoding issues. */
                try
                {
                    m_outInfo = new WrapperPrintStream( System.out, false, sunStdoutEncoding, "WrapperStartStopApp: " );
                    m_outError = new WrapperPrintStream( System.out, false, sunStdoutEncoding, "WrapperStartStopApp Error: " );
                    m_outDebug = new WrapperPrintStream( System.out, false, sunStdoutEncoding, "WrapperStartStopApp Debug: " );
                    streamsSet = true;
                }
                catch ( UnsupportedEncodingException e )
                {
                    /* This should not happen because we always make sure the encoding exists before launching a JVM.
                     *  If any of the above streams failed, we want to fall back to streams that use the same encoding. */
                    System.out.println( WrapperManager.getRes().getString( "Failed to set the encoding '{0}' when creating a WrapperPrintStream.\n Make sure the value of sun.stdout.encoding is correct.", sunStdoutEncoding ) );
                }
            }
        }
        if ( !streamsSet )
        {
            m_outInfo = new WrapperPrintStream( System.out, "WrapperStartStopApp: " );
            m_outError = new WrapperPrintStream( System.out, "WrapperStartStopApp Error: " );
            m_outDebug = new WrapperPrintStream( System.out, "WrapperStartStopApp Debug: " );
        }
        
        // Do all of our initialization here so the modified array lists which are passed
        //  to the WrapperListener.start method can remain unchanged.  Ideally we would
        //  want to handle this within the start method, but that would be an API change
        //  that could effect users.
        
        // startArgs will be an args array with the main class name and stop args stripped off.
        String[] startArgs;
     
        // Get the class name of the application
        if ( args.length < 5 )
        {
            m_initFailed = true;
            m_initError = WrapperManager.getRes().getString( "Not enough arguments.  Minimum {0} required.", "5" );
            m_initShowUsage = true;
            
            // No main class, do the best we can for now.
            startArgs = new String[0];
        }
        else
        {
            // Look for the start main method.
            m_startMainMethod = getMainMethod( args[0] );
            
            int argCount = getArgCount( args, 1 );
            if ( argCount < 0 ) 
            {
                // Failed, but we need an empty array for the start method below.
                startArgs = new String[0];
                    
                // m_initFailed and m_initError already set.
            }
            else
            {
                // Get the start arguments
                startArgs = getArgs( args, 1, argCount );
                if ( startArgs == null )
                {
                    // Failed, but we need an empty array for the start method below.
                    startArgs = new String[0];
                    
                    // m_initFailed and m_initError already set.
                }
                else
                {
                    // Where do the stop arguments start
                    int stopArgBase = 2 + startArgs.length;
                    if ( args.length < stopArgBase + 3 )
                    {
                        m_initFailed = true;
                        m_initError = WrapperManager.getRes().getString( "Not enough arguments. Minimum 3 after start arguments." );
                        m_initShowUsage = true;
                    }
                    else
                    {
                        // Look for the stop main method.
                        m_stopMainMethod = getMainMethod( args[stopArgBase] );
                        // Get the stopWait flag
                        if ( args[stopArgBase + 1].equalsIgnoreCase( "true" ) )
                        {
                            m_stopWait = true;
                        }
                        else if ( args[stopArgBase + 1].equalsIgnoreCase( "false" ) )
                        {
                            m_stopWait = false;
                        }
                        else
                        {
                            m_initFailed = true;
                            m_initError = WrapperManager.getRes().getString( "The stop_wait argument must be either true or false." );
                            m_initShowUsage = true;
                        }
                        
                        if ( !m_initFailed )
                        {
                            argCount = getArgCount( args, stopArgBase + 2 );
                            if ( argCount < 0 ) 
                            {
                                // m_initFailed and m_initError already set.
                            }
                            else
                            {    
                                // Get the stop arguments
                                m_stopMainArgs = getArgs( args, stopArgBase + 2, argCount );
                                if ( m_stopMainArgs == null )
                                {
                                    // m_initFailed and m_initError already set.
                                }
                                else
                                {
                                    // Let's see if there is any passthrough params.
                                    // Calculate the expected size of args if there is no passthrough params
                                    int expectedSize = stopArgBase + 2 + argCount + 1;
                                    
                                    if ( expectedSize < args.length ) {
                                        // Passthrough parameters are present. Read passthrough mode, this will tell us
                                        // to which array we should add these params.
                                        String passthroughMode = WrapperSystemPropertyUtil.getStringProperty(
                                            WrapperStartStopApp.class.getName() + ".passthroughMode", PASSTHROUGH_MODE_IGNORE );
                                        
                                        if ( passthroughMode.equalsIgnoreCase( PASSTHROUGH_MODE_BOTH ) ) 
                                        {
                                            startArgs      = addPassthroughParams( startArgs,      args, expectedSize );
                                            m_stopMainArgs = addPassthroughParams( m_stopMainArgs, args, expectedSize );  
                                        } 
                                        else if ( passthroughMode.equalsIgnoreCase( PASSTHROUGH_MODE_START ) ) 
                                        {
                                            startArgs = addPassthroughParams( startArgs, args, expectedSize );
                                        } 
                                        else if ( passthroughMode.equalsIgnoreCase( PASSTHROUGH_MODE_STOP ) ) 
                                        {
                                            m_stopMainArgs = addPassthroughParams( m_stopMainArgs, args, expectedSize );
                                        } 
                                        else 
                                        { 
                                            // By default, ignore the extra parameters. Nothing to do.
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        
        // Start the application.  If the JVM was launched from the native
        //  Wrapper then the application will wait for the native Wrapper to
        //  call the application's start method.  Otherwise the start method
        //  will be called immediately.
        WrapperManager.start( this, startArgs );
        
        // This thread ends, the WrapperManager will start the application after the Wrapper has
        //  been propperly initialized by calling the start method above.
    }
    
    /**
     * Helper method to make it easier for user classes extending this class to have their
     *  own methods of parsing the command line.
     * 
     * @param startMainMethod Name of the start method.
     * @param stopMainMethod Name of the stop method.
     * @param stopWait Should the stop process force the JVM to exit, or wait for all threads to die on their own.
     * @param stopMainArgs Arguments for the stop method.
     */
    protected WrapperStartStopApp( Method startMainMethod,
                                   Method stopMainMethod,
                                   boolean stopWait,
                                   String[] stopMainArgs )
    {
        m_startMainMethod = startMainMethod;
        m_stopMainMethod = stopMainMethod;
        m_stopWait = stopWait;
        m_stopMainArgs = stopMainArgs;
        
        // NOTE - The call to WrapperManager.start() appears to be missing here, but it can't be added
        //        as doing so would break how existing users are making use of this constructor.
    }
    
    /*---------------------------------------------------------------
     * Runnable Methods
     *-------------------------------------------------------------*/
    /**
     * Used to launch the application in a separate thread.
     */
    public void run()
    {
        // Notify the start method that the thread has been started by the JVM.
        synchronized( this )
        {
            m_mainStarted = true;
            notifyAll();
        }
        
        Throwable t = null;
        try
        {
            if ( WrapperManager.isDebugEnabled() )
            {
                m_outDebug.println( WrapperManager.getRes().getString( "invoking start main method" ) );
            }
            
            try
            {
                try
                {
                    m_startMainMethod.invoke( null, new Object[] { m_startMainArgs } );
                }
                catch ( IllegalArgumentException iae )
                {
                    m_startMainMethod.invoke( null, new Object[] { } ); 
                }
            }
            finally
            {
                // Make sure the rest of this thread does not fall behind the application.
                Thread.currentThread().setPriority( Thread.MAX_PRIORITY );
            }
            
            if ( WrapperManager.isDebugEnabled() )
            {
                m_outDebug.println( WrapperManager.getRes().getString( "start main method completed" ) );
            }
            
            synchronized(this)
            {
                // Let the start() method know that the main method returned, in case it is 
                //  still waiting.
                m_mainComplete = true;
                this.notifyAll();
            }
            
            return;
        }
        catch ( IllegalAccessException e )
        {
            t = e;
        }
        catch ( IllegalArgumentException e )
        {
            t = e;
        }
        catch ( InvocationTargetException e )
        {
            t = e.getTargetException();
            if ( t == null )
            {
                t = e;
            }
        }
        
        // If we get here, then an error was thrown.  If this happened quickly 
        // enough, the start method should be allowed to shut things down.
        m_outInfo.println();
        m_outError.println( WrapperManager.getRes().getString(
                "Encountered an error running start main: {0}", t ) );

        // We should print a stack trace here, because in the case of an 
        // InvocationTargetException, the user needs to know what exception
        // their app threw.
        t.printStackTrace( m_outError );

        synchronized( this )
        {
            if ( m_ignoreMainExceptions )
            {
                if ( !m_startComplete )
                {
                    // An exception was thrown, but we want to let the application continue.
                    m_mainComplete = true;
                    this.notifyAll();
                }
                return;
            }
            else
            {
                if ( m_startComplete )
                {
                    // Shut down here.
                    WrapperManager.stop( 1 );
                    return; // Will not get here.
                }
                else
                {
                    // Let start method handle shutdown.
                    m_mainComplete = true;
                    m_mainExitCode = new Integer( 1 );
                    this.notifyAll();
                    return;
                }
            }
        }
    }
    
    /*---------------------------------------------------------------
     * WrapperListener Methods
     *-------------------------------------------------------------*/
    /**
     * The start method is called when the WrapperManager is signaled by the 
     *    native wrapper code that it can start its application.  This
     *    method call is expected to return, so a new thread should be launched
     *    if necessary.
     * If there are any problems, then an Integer should be returned, set to
     *    the desired exit code.  If the application should continue,
     *    return null.
     */
    public Integer start( String[] args )
    {
        // See if there were any startup problems.
        if ( m_initFailed )
        {
            if ( m_initError != null )
            {
                m_outError.println( m_initError );
            }
            if ( m_initException != null )
            {
                m_initException.printStackTrace( m_outError );
            }
            if ( m_initShowUsage )
            {
                showUsage();
            }
            return new Integer( 1 );
        }
        
        // Decide whether or not to wait for the start main method to complete before returning.
        boolean waitForStartMain = WrapperSystemPropertyUtil.getBooleanProperty(
            WrapperStartStopApp.class.getName() + ".waitForStartMain", false );
        m_ignoreMainExceptions = WrapperSystemPropertyUtil.getBooleanProperty(
            WrapperStartStopApp.class.getName() + ".ignoreMainExceptions", false );
        int maxStartMainWait = WrapperSystemPropertyUtil.getIntProperty(
            WrapperStartStopApp.class.getName() + ".maxStartMainWait", 2 );
        maxStartMainWait = Math.max( 1, maxStartMainWait ); 
        
        // Decide the maximum number of times to loop waiting for the main start method.
        int maxLoops;
        if ( waitForStartMain )
        {
            maxLoops = Integer.MAX_VALUE;
            if ( WrapperManager.isDebugEnabled() )
            {
                m_outDebug.println( WrapperManager.getRes().getString(
                    "start(args) Will wait indefinitely for the main method to complete." ) );
            }
        }
        else
        {
            maxLoops = maxStartMainWait; // 1s loops.
            if ( WrapperManager.isDebugEnabled() )
            {
                m_outDebug.println( WrapperManager.getRes().getString(
                "start(args) Will wait up to {0} seconds for the main method to complete.",
                    new Integer(maxLoops) ) );
            }
        }
        
        Thread mainThread = new Thread( this, "WrapperStartStopAppMain" );
        synchronized(this)
        {
            m_startMainArgs = args;
            mainThread.start();
            
            // Make sure the rest of this thread does not fall behind the application.
            Thread.currentThread().setPriority( Thread.MAX_PRIORITY );
            
            // To avoid problems with the main thread starting slowly on heavily loaded systems,
            //  do not continue until the thread has actually started.
            while ( !m_mainStarted )
            {
                try
                {
                    this.wait( 1000 );
                }
                catch ( InterruptedException e )
                {
                    // Continue.
                }
            }
            
            // Wait for startup main method to complete.
            int loops = 0;
            while ( ( loops < maxLoops ) && ( !m_mainComplete ) )
            {
                try
                {
                    this.wait( 1000 );
                }
                catch ( InterruptedException e )
                {
                    // Continue.
                }
                
                if ( !m_mainComplete )
                {
                    // If maxLoops is large then this could take a while.  Notify the
                    //  WrapperManager that we are still starting so it doesn't give up.
                    WrapperManager.signalStarting( 5000 );
                }
                
                loops++;
            }
            
            // Always set the flag stating that the start method completed.  This is needed
            //  so the run method can decide whether or not it needs to be responsible for
            //  shutting down the JVM in the event of an exception thrown by the start main
            //  method.
            m_startComplete = true;
            
            // The main exit code will be null unless an error was thrown by the start
            //  main method.
            if ( WrapperManager.isDebugEnabled() )
            {
                m_outDebug.println( WrapperManager.getRes().getString(
                        "start(args) end.  Main Completed={0}, exitCode={1}",
                        new Boolean( m_mainComplete ), m_mainExitCode ) );
            }
            return m_mainExitCode;
        }
    }
    
    /**
     * Called when the application is shutting down.
     */
    public int stop( int exitCode )
    {
        if ( WrapperManager.isDebugEnabled() )
        {
            m_outDebug.println(WrapperManager.getRes().getString( "stop({0})", new Integer( exitCode ) ) );
        }
        
        // Execute the main method in the stop class
        Throwable t = null;
        try
        {
            if ( WrapperManager.isDebugEnabled() )
            {
                m_outDebug.println( WrapperManager.getRes().getString( "invoking stop main method" ) );
            }

            try
            {
                m_stopMainMethod.invoke( null, new Object[] { m_stopMainArgs } );
            }
            catch ( IllegalArgumentException iae )
            {
                m_stopMainMethod.invoke( null, new Object[] { } ); 
            }
            
            if ( WrapperManager.isDebugEnabled() )
            {
                m_outDebug.println( WrapperManager.getRes().getString( "stop main method completed" ) );
            }
            
            if ( m_stopWait )
            {
                // This feature exists to make sure the stop process waits for the main
                //  application to fully shutdown.  This can only be done by looking for
                //  and counting the number of non-daemon threads still running in the
                //  system.
                
                int systemThreadCount = WrapperSystemPropertyUtil.getIntProperty(
                    WrapperStartStopApp.class.getName() + ".systemThreadCount", 1 );
                systemThreadCount = Math.max( 0, systemThreadCount ); 
                
                int threadCnt;
                while( ( threadCnt = getNonDaemonThreadCount() ) > systemThreadCount )
                {
                    if ( WrapperManager.isDebugEnabled() )
                    {
                        m_outDebug.println( WrapperManager.getRes().getString(
                                "stopping.  Waiting for {0} threads to complete.",
                            new Integer( threadCnt - systemThreadCount ) ) );
                    }
                    try
                    {
                        Thread.sleep( 1000 );
                    }
                    catch ( InterruptedException e )
                    {
                    }
                }
            }
            
            // Success
            return exitCode;
        }
        catch ( IllegalAccessException e )
        {
            t = e;
        }
        catch ( IllegalArgumentException e )
        {
            t = e;
        }
        catch ( InvocationTargetException e )
        {
            t = e;
        }
        
        // If we get here, then an error was thrown.
        m_outError.println( WrapperManager.getRes().getString(
                "Encountered an error running stop main: {0}", t ) );

        // We should print a stack trace here, because in the case of an 
        // InvocationTargetException, the user needs to know what exception
        // their app threw.
        t.printStackTrace( m_outError );
        
        // Return a failure exit code
        return 1;
    }
    
    /**
     * Called whenever the native wrapper code traps a system control signal
     *  against the Java process.  It is up to the callback to take any actions
     *  necessary.  Possible values are: WrapperManager.WRAPPER_CTRL_C_EVENT, 
     *    WRAPPER_CTRL_CLOSE_EVENT, WRAPPER_CTRL_LOGOFF_EVENT, or 
     *    WRAPPER_CTRL_SHUTDOWN_EVENT
     */
    public void controlEvent( int event )
    {
        if ( ( event == WrapperManager.WRAPPER_CTRL_LOGOFF_EVENT )
            && ( WrapperManager.isLaunchedAsService() || WrapperManager.isIgnoreUserLogoffs() ) )
        {
            // Ignore
            m_outInfo.println( WrapperManager.getRes().getString( "User logged out.  Ignored." ) );
        }
        else
        {
            if ( WrapperManager.isDebugEnabled() )
            {
                m_outDebug.println( WrapperManager.getRes().getString(
                        "controlEvent({0}) Stopping", new Integer( event ) ) );
            }
            WrapperManager.stop( 0 );
            // Will not get here.
        }
    }
    
    /*---------------------------------------------------------------
     * Methods
     *-------------------------------------------------------------*/
    /**
     * Returns a count of all non-daemon threads in the JVM, starting with the top
     *  thread group.
     *
     * @return Number of non-daemon threads.
     */
    private int getNonDaemonThreadCount()
    {
        // Locate the top thread group.
        ThreadGroup topGroup = Thread.currentThread().getThreadGroup();
        while ( topGroup.getParent() != null )
        {
            topGroup = topGroup.getParent();
        }
        
        // Get a list of all threads.  Use an array that is twice the total number of
        //  threads as the number of running threads may be increasing as this runs.
        Thread[] threads = new Thread[topGroup.activeCount() * 2];
        topGroup.enumerate( threads, true );
        
        // Only count any non daemon threads which are 
        //  still alive other than this thread.
        int liveCount = 0;
        for ( int i = 0; i < threads.length; i++ )
        {
            /*
            if ( threads[i] != null )
            {
                m_outDebug.println( "Check " + threads[i].getName() + " daemon="
                    + threads[i].isDaemon() + " alive=" + threads[i].isAlive() );
            }
            */
            if ( ( threads[i] != null ) && threads[i].isAlive() )
            {
                // Do not count this thread.
                if ( ( Thread.currentThread() != threads[i] ) && ( !threads[i].isDaemon() ) )
                {
                    // Non-Daemon living thread
                    liveCount++;
                    //m_outDebug.println( "  -> Non-Daemon" );
                }
            }
        }
        //m_outDebug.println( "  => liveCount = " + liveCount );
        
        return liveCount;
    }
    
    /**
     * Returns the main method of the specified class.  If there are any problems,
     *  an error message will be displayed and the Wrapper will be stopped.  This
     *  method will only return if it has a valid method.
     */
    private Method getMainMethod( String className )
    {
        // Look for the start class by name
        Class mainClass;
        String methodName = "main";
        String [] arr = className.split("/");
        if ( arr.length > 1 )
        {
            className = arr[0];
            methodName = arr[1];
        }
        try
        {
            mainClass = Class.forName( className );
        }
        catch ( ClassNotFoundException e )
        {
            m_initFailed = true;
            m_initError = WrapperManager.getRes().getString( "Unable to locate the class {0}: {1}", className, e );
            m_initShowUsage = true;
            return null;  // Will not get here
        }
        catch ( ExceptionInInitializerError e )
        {
            m_initFailed = true;
            m_initError = WrapperManager.getRes().getString( "Class {0} found but could not be initialized due to:", className );
            m_initException = e;
            return null;  // Will not get here
        }
        catch ( LinkageError e )
        {
            m_initFailed = true;
            m_initError = WrapperManager.getRes().getString( "Class {0} found but could not be initialized: {1}", className , e );
            return null;  // Will not get here
        }
        
        // Look for the start method
        Method mainMethod = null;
        try
        {
            // getDeclaredMethod will return any method named main in the specified class,
            //  while getMethod will only return public methods, but it will search up the
            //  inheritance path.
            mainMethod = mainClass.getMethod( methodName, new Class[] { String[].class } );
        }
        catch ( NoSuchMethodException e )
        {
            try
            {
                // getDeclaredMethod will return any method named <methodname> in the specified class,
                // while getMethod will only return public methods, but it will search up the
                // inheritance path.
                // try without parameters
                mainMethod = mainClass.getMethod( methodName, new Class[] { } );
            }
            catch ( NoSuchMethodException e2 )
            {
            }
            if ( mainMethod == null ) {
                m_initFailed = true;
                m_initError = WrapperManager.getRes().getString( "Unable to locate a public static {2} method in class {0}: {1}", className, e, methodName );
                return null;  // Will not get here
            }
        }
        catch ( SecurityException e )
        {
            m_initFailed = true;
            m_initError = WrapperManager.getRes().getString( "Unable to locate a public static {2} method in class {0}: {1}", className, e, methodName );
            return null;  // Will not get here
        }
        
        // Make sure that the method is public and static
        int modifiers = mainMethod.getModifiers();
        if ( !( Modifier.isPublic( modifiers ) && Modifier.isStatic( modifiers ) ) )
        {
            m_initFailed = true;
            m_initError = WrapperManager.getRes().getString( "The {1} method in class {0} must be declared public and static.", className, methodName );
            return null;  // Will not get here
        }
        
        return mainMethod;
    }
    
    /**
     * Parses a set of arguments starting with a count.
     *
     * @param args List of arguments.
     * @param argBase From which index we want to copy the args.
     * @param argCount Count of the number of available arguments.
     * @return the Argument list, or null if there was a problem.
     */
    private String[] getArgs( String[] args, int argBase, int argCount )
    {
        // Make sure argCount is a valid value.
        if ( argCount < 0 )
        {
            m_initFailed = true;
            m_initError = WrapperManager.getRes().getString( "Illegal argument count: {0}", args[argBase] );
            m_initShowUsage = true;
            return null;
        }
        
        // Make sure that there are enough arguments in the array.
        if ( args.length < argBase + 1 + argCount )
        {
            m_initFailed = true;
            m_initError = WrapperManager.getRes().getString( "Not enough arguments.  Argument count of {0} was specified.", new Integer( argCount) );
            m_initShowUsage = true;
            return null;
        }
        
        // Create the argument array
        String[] mainArgs = new String[argCount];
        System.arraycopy( args, argBase + 1, mainArgs, 0, argCount );
        
        return mainArgs;
    }
    
    /**
     * Convert a String to an int.
     * @param args List of Strings.
     * @param argBase Index at which we expect to find an int.
     * @return Arguments count or -1 if error.
     */
    private int getArgCount ( String[] args, int argBase )
    {
        int argCount;
        try
        {
            argCount = Integer.parseInt( args[argBase] );
        }
        catch ( NumberFormatException e )
        {
            m_initFailed = true;
            m_initError = WrapperManager.getRes().getString( "Illegal argument count: {0}", args[argBase] );
            m_initShowUsage = true;
            return -1;
        }
        return argCount;
    }
    
    /**
     * Add remaining elements from array 'extra' starting at position 'position'
     * to array 'source'.
     * Concat both arrays, but only use elements of second array from index position.
     * source = { "a", "b", "c" };
     * extra  = { "d", "e", "f", "g" };
     * position = 2;
     * result = { "a", "b", "c", "f", "g" };
     * 
     * @param source   The source array. All its elements are added to the
     *                 return value.
     * @param extra    Array of extra values to concact.
     * @param position Indicate from which index to concat the values of 'extra'.
     * @return Array containing values of 'source' and 'extra' from index 'position'.
     *         Return an empty array if error.
     */
    private String[] addPassthroughParams( String[] source, String[] extra, int position )
    {
        
        if ( (extra == null ) || ( extra.length == 0 ) || ( position >= extra.length ) ) 
        {
            if (source == null) 
            {
                return new String[0];
            }
            else
            {
                return source;
            }
        }
        
        // Count how many extra elements we need to add
        int numberExtraElements = extra.length - position;
        
        int sizeSource = 0;
        
        if ( source != null ) 
        {
            sizeSource = source.length;
        }
        
        String[] result = new String[sizeSource + numberExtraElements];
        
        // add all elements from 'source'
        for ( int i = 0; i < sizeSource; i++)
        {
            result[i] = source[i];
        }
                
        // add elements from 'extra' starting from index 'position'
        for ( int i = 0; i < numberExtraElements; i++ )
        {
            result[sizeSource + i] = extra[position + i];
        }
        
        return result;
    }
        
    
    /**
     * Displays application usage
     */
    protected void showUsage()
    {
        // Show this output without headers.
        System.out.println();
        System.out.println( WrapperManager.getRes().getString(
            "WrapperStartStopApp Usage:" ) );
        System.out.println( WrapperManager.getRes().getString(
            "  java org.tanukisoftware.wrapper.WrapperStartStopApp '{'start_class'{'/start_method'}}' '{'start_arg_count'}' [start_arguments] '{'stop_class'{'/stop_method'}}' '{'stop_wait'}' '{'stop_arg_count'}' [stop_arguments]" ) );
        System.out.println();
        System.out.println( WrapperManager.getRes().getString(
            "Where:" ) );
        System.out.println( WrapperManager.getRes().getString(
            "  start_class:     The fully qualified class name to run to start the " ) );
        System.out.println( WrapperManager.getRes().getString(
            "                   application." ) );
        System.out.println( WrapperManager.getRes().getString(
            "  start_arg_count: The number of arguments to be passed to the start class''s " ) );
        System.out.println( WrapperManager.getRes().getString(
            "                   main method." ) );
        System.out.println( WrapperManager.getRes().getString(
            "  start_arguments: The arguments that would normally be passed to the start " ) );
        System.out.println( WrapperManager.getRes().getString(
            "                   class application." ) );
        System.out.println( WrapperManager.getRes().getString(
            "  stop_class:      The fully qualified class name to run to stop the " ) );
        System.out.println( WrapperManager.getRes().getString(
            "                   application." ) );
        System.out.println( WrapperManager.getRes().getString(
            "  stop_wait:       When stopping, should the Wrapper wait for all threads to " ) );
        System.out.println( WrapperManager.getRes().getString(
            "                   complete before exiting (true/false)." ) );
        System.out.println( WrapperManager.getRes().getString(
            "  stop_arg_count:  The number of arguments to be passed to the stop class''s " ) );
        System.out.println( WrapperManager.getRes().getString(
            "                   main method." ) );
        System.out.println( WrapperManager.getRes().getString(
            "  stop_arguments:  The arguments that would normally be passed to the stop " ) );
        System.out.println( WrapperManager.getRes().getString(
            "                   class application." ) );
    }
    
    /*---------------------------------------------------------------
     * Main Method
     *-------------------------------------------------------------*/
    /**
     * Used to Wrapper enable a standard Java application.  This main
     *  expects the first argument to be the class name of the application
     *  to launch.  All remaining arguments will be wrapped into a new
     *  argument list and passed to the main method of the specified
     *  application.
     *
     * @param args Arguments passed to the application.
     */
    public static void main( String args[] )
    {
        new WrapperStartStopApp( args );
    }
}

