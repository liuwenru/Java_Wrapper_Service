package org.tanukisoftware.wrapper;

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

import java.io.File;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.StringTokenizer;
import java.util.jar.Attributes;
import java.util.jar.JarFile;
import java.util.jar.Manifest;

/**
 * By default the WrapperJarApp will only wait for 2 seconds for the main
 *  method of the start class to complete.  This was done because the main
 *  methods of many applications never return.  It is possible to force the
 *  class to wait for the startup main method to complete by defining the
 *  following system property when launching the JVM (defaults to FALSE):
 *  -Dorg.tanukisoftware.wrapper.WrapperJarApp.waitForStartMain=TRUE
 * <p>
 * Using the waitForStartMain property will cause the startup to wait
 *  indefinitely.  This is fine if the main method will always return
 *  within a predefined period of time.  But if there is any chance that
 *  it could hang, then the maxStartMainWait property may be a better
 *  option.  It allows the 2 second wait time to be overridden. To wait
 *  for up to 5 minutes for the startup main method to complete, set
 *  the property to 300 as follows (defaults to 2 seconds):
 *  -Dorg.tanukisoftware.wrapper.WrapperJarApp.maxStartMainWait=300
 * <p>
 * By default, the WrapperJarApp will tell the Wrapper to exit with an
 *  exit code of 1 if any uncaught exceptions are thrown in the configured
 *  main method.  This is good in most cases, but is a little different than
 *  the way Java works on its own.  Java will stay up and running if it has
 *  launched any other non-daemon threads even if the main method ends because
 *  of an uncaught exception.  To get this same behavior, it is possible to
 *  specify the following system property when launching the JVM (defaults to
 *  FALSE):
 *  -Dorg.tanukisoftware.wrapper.WrapperJarApp.ignoreMainExceptions=TRUE
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
public class WrapperJarApp
    implements WrapperListener, Runnable
{
    /** Info level log channel */
    private static WrapperPrintStream m_outInfo;
    
    /** Error level log channel */
    private static WrapperPrintStream m_outError;
    
    /** Debug level log channel */
    private static WrapperPrintStream m_outDebug;
    
    /**
     * Application's main method
     */
    private Method m_mainMethod;
    
    /**
     * Command line arguments to be passed on to the application
     */
    private String[] m_appArgs;
    
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
    
    /*---------------------------------------------------------------
     * Constructors
     *-------------------------------------------------------------*/
    /**
     * Creates an instance of a WrapperJarApp.
     *
     * @param args The full list of arguments passed to the JVM.
     */
    protected WrapperJarApp( String args[] )
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
                    m_outInfo = new WrapperPrintStream( System.out, false, sunStdoutEncoding, "WrapperJarApp: " );
                    m_outError = new WrapperPrintStream( System.out, false, sunStdoutEncoding, "WrapperJarApp Error: " );
                    m_outDebug = new WrapperPrintStream( System.out, false, sunStdoutEncoding, "WrapperJarApp Debug: " );
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
            m_outInfo = new WrapperPrintStream( System.out, "WrapperJarApp: " );
            m_outError = new WrapperPrintStream( System.out, "WrapperJarApp Error: " );
            m_outDebug = new WrapperPrintStream( System.out, "WrapperJarApp Debug: " );
        }
        
        // Do all of our initialization here so the modified array list which is passed
        //  to the WrapperListener.start method can remain unchanged.  Ideally we would
        //  want to handle this within the start method, but that would be an API change
        //  that could effect users.
        
        // appArgs will be an args array with the jar file name stripped off.
        String[] appArgs;
        
        // Get the jar file name of the application
        if ( args.length < 1 )
        {
            m_initFailed = true;
            m_initError = WrapperManager.getRes().getString( "Not enough arguments.  Minimum {0} required.", "1" );
            m_initShowUsage = true;
            
            // No jar file name, do the best we can for now.
            appArgs = new String[0];
        }
        else
        {
            // Look for the specified jar file.
            File file = new File( args[0] );
            if ( !file.exists() )
            {
                m_initFailed = true;
                m_initError = WrapperManager.getRes().getString( "Unable to locate the jar file {0}", args[0] );
                m_initShowUsage = true;
            }
            else
            {
                File parent = file.getParentFile();
        
                JarFile jarFile;
                try
                {
                    jarFile = new JarFile( file );
                }
                catch ( IOException e )
                {
                    m_initFailed = true;
                    m_initError = WrapperManager.getRes().getString( "Unable to open the jar file {0} : {1}", args[0], e );
                    jarFile = null;
                }
                
                if ( !m_initFailed )
                {
                    Manifest manifest;
                    try
                    {
                        manifest = jarFile.getManifest();
                    }
                    catch ( IOException e )
                    {
                        m_initFailed = true;
                        m_initError = WrapperManager.getRes().getString( "Unable to access the jar''s manifest file {0} : {1}", args[0], e );
                        manifest = null;
                    }
                    
                    if ( !m_initFailed )
                    {
                        Attributes attributes = manifest.getMainAttributes();
                        String mainClassName = attributes.getValue( "Main-Class" );
                        if ( mainClassName == null ) 
                        {
                            m_initFailed = true;
                            m_initError = WrapperManager.getRes().getString( "The Main-Class was not specified correctly in the jar file''s manifest file.  Please make sure all required meta information is being set." );
                            /* no main class, no chance this will ever do something sensible, so stop here now */
                        }
                        else
                        {
                            String classPath = attributes.getValue( "Class-Path" );
                    
                            if ( WrapperManager.isDebugEnabled() )
                            {
                                m_outDebug.println( "Jar Main-Class: " + mainClassName );
                            }
                            
                            URL[] classURLs;
                            if ( ( classPath != null ) && ( !classPath.equals( "" ) ) )
                            {
                                if ( WrapperManager.isDebugEnabled() )
                                {
                                    m_outDebug.println( WrapperManager.getRes().getString("Jar Classpath: {0}", classPath ) );
                                }
                                
                                StringTokenizer st = new StringTokenizer( classPath, " \n\r" );
                                classURLs = new URL[st.countTokens() + 1];
                                
                                // Store the main jar in the classpath.
                                try
                                {
                                    classURLs[0] = new URL( "file:" + file.getAbsolutePath() );
                                }
                                catch ( MalformedURLException e )
                                {
                                    m_initFailed = true;
                                    m_initError = WrapperManager.getRes().getString( "Unable to add jar to classpath: {0}", e );
                                }
                                if ( !m_initFailed )
                                {
                                    if ( WrapperManager.isDebugEnabled() )
                                    {
                                        m_outDebug.println( WrapperManager.getRes().getString("    Classpath[0]=") + classURLs[0] );
                                    }
                                    
                                    // Add any other jars in the manifest classpath relative to the location of the main jar.
                                    for ( int i = 1; st.hasMoreTokens(); i++ )
                                    {
                                        String classEntry = st.nextToken();
                                        try
                                        {
                                            classURLs[i] = new URL( "file:" + new File( parent, classEntry).getAbsolutePath() );
                                        }
                                        catch ( MalformedURLException e )
                                        {
                                            m_initFailed = true;
                                            m_initError = WrapperManager.getRes().getString( "Malformed classpath in the jar''s manifest file {0} : {1}", args[0], e );
                                        }
                                        if ( !m_initFailed )
                                        {
                                            if ( WrapperManager.isDebugEnabled() )
                                            {
                                                m_outDebug.println( WrapperManager.getRes().getString("    Classpath[{0}]=", new Integer( i ) ) + classURLs[i] );
                                            }
                                        }
                                    }
                                }
                            }
                            else
                            {
                                if ( WrapperManager.isDebugEnabled() )
                                {
                                    m_outDebug.println( WrapperManager.getRes().getString( "Jar Classpath: Not specified." ) );
                                }
                                
                                classURLs = new URL[1];
                                
                                // Store the main jar in the classpath.
                                try
                                {
                                    classURLs[0] = new URL( "file:" + file.getAbsolutePath() );
                                }
                                catch ( MalformedURLException e )
                                {
                                    m_initFailed = true;
                                    m_initError = WrapperManager.getRes().getString( "Unable to add jar to classpath: {0}", e );
                                }
                                if ( !m_initFailed )
                                {
                                    if ( WrapperManager.isDebugEnabled() )
                                    {
                                        m_outDebug.println( WrapperManager.getRes().getString( "    Classpath[0]=" ) + classURLs[0] );
                                    }
                                }
                            }
                            
                            if ( !m_initFailed )
                            {
                                URLClassLoader cl = URLClassLoader.newInstance( classURLs, this.getClass().getClassLoader() );
                                
                                // Look for the specified class by name
                                Class mainClass;
                                try
                                {
                                    mainClass = Class.forName( mainClassName, true, cl );
                                }
                                catch ( ClassNotFoundException e )
                                {
                                    m_initFailed = true;
                                    m_initError = WrapperManager.getRes().getString( "Unable to locate the class {0} : {1}", mainClassName, e );
                                    mainClass = null;
                                }
                                catch ( ExceptionInInitializerError e )
                                {
                                    m_initFailed = true;
                                    m_initError = WrapperManager.getRes().getString( "Class {0} found but could not be initialized due to:", mainClassName );
                                    m_initException = e;
                                    mainClass = null;
                                }
                                catch ( LinkageError e )
                                {
                                    m_initFailed = true;
                                    m_initError = WrapperManager.getRes().getString( "Class {0} found but could not be initialized: {1}", mainClassName,  e );
                                    mainClass = null;
                                }
                                
                                if ( !m_initFailed )
                                {
                                    // Look for the main method
                                    try
                                    {
                                        // getDeclaredMethod will return any method named main in the specified class,
                                        //  while getMethod will only return public methods, but it will search up the
                                        //  inheritance path.
                                        m_mainMethod = mainClass.getMethod( "main", new Class[] { String[].class } );
                                    }
                                    catch ( NoSuchMethodException e )
                                    {
                                        m_initFailed = true;
                                        m_initError = WrapperManager.getRes().getString( "Unable to locate a public static main method in class {0} : {1}", args[0] , e );
                                    }
                                    catch ( SecurityException e )
                                    {
                                        m_initFailed = true;
                                        m_initError = WrapperManager.getRes().getString( "Unable to locate a public static main method in class {0} : {1}", args[0], e );
                                    }
                                    
                                    if ( !m_initFailed )
                                    {
                                        // Make sure that the method is public and static
                                        int modifiers = m_mainMethod.getModifiers();
                                        if ( !( Modifier.isPublic( modifiers ) && Modifier.isStatic( modifiers ) ) )
                                        {
                                            m_initFailed = true;
                                            m_initError = WrapperManager.getRes().getString( "The main method in class {0} must be declared public and static.", args[0] );
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // Strip the jar file name off of the args list.
            //  This is assuming the jar file name was valid for now.
            appArgs = new String[args.length - 1];
            System.arraycopy( args, 1, appArgs, 0, appArgs.length );
        }
        
        // Start the application.  If the JVM was launched from the native
        //  Wrapper then the application will wait for the native Wrapper to
        //  call the application's start method.  Otherwise the start method
        //  will be called immediately.
        WrapperManager.start( this, appArgs );
        
        // This thread ends, the WrapperManager will start the application after the Wrapper has
        //  been properly initialized by calling the start method above.
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
                m_outDebug.println( WrapperManager.getRes().getString("invoking main method" ) );
            }
            
            try
            {
                m_mainMethod.invoke( null, new Object[] { m_appArgs } );
            }
            finally
            {
                // Make sure the rest of this thread does not fall behind the application.
                Thread.currentThread().setPriority( Thread.MAX_PRIORITY );
            }
            
            if ( WrapperManager.isDebugEnabled() )
            {
                m_outDebug.println(  WrapperManager.getRes().getString("main method completed" ) );
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
        m_outError.println( WrapperManager.getRes().getString( "Encountered an error running main:" ) );

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
     * native wrapper code that it can start its application.  This
     * method call is expected to return, so a new thread should be launched
     * if necessary.
     * If there are any problems, then an Integer should be returned, set to
     * the desired exit code.  If the application should continue,
     * return null.
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
            WrapperJarApp.class.getName() + ".waitForStartMain", false );
        m_ignoreMainExceptions = WrapperSystemPropertyUtil.getBooleanProperty(
            WrapperJarApp.class.getName() + ".ignoreMainExceptions", false );
        int maxStartMainWait = WrapperSystemPropertyUtil.getIntProperty(
            WrapperJarApp.class.getName() + ".maxStartMainWait", 2 );
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
                        "start(args) Will wait up to {0} seconds for the main method to complete.", new Integer( maxLoops ) ) );
            }
        }
        
        Thread mainThread = new Thread( this, "WrapperJarAppMain" );
        synchronized(this)
        {
            m_appArgs = args;
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
                        new Boolean( m_mainComplete ),  m_mainExitCode ) );
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
            m_outDebug.println( "stop(" + exitCode + ")" );
        }
        
        // Normally an application will be asked to shutdown here.  Standard Java applications do
        //  not have shutdown hooks, so do nothing here.  It will be as if the user hit CTRL-C to
        //  kill the application.
        return exitCode;
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
            m_outInfo.println( WrapperManager.getRes().getString("User logged out.  Ignored." ) );
        }
        else
        {
            if ( WrapperManager.isDebugEnabled() )
            {
                m_outDebug.println(WrapperManager.getRes().getString(
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
     * Displays application usage
     */
    protected void showUsage()
    {
        // Show this output without headers.
        System.out.println();
        System.out.println( WrapperManager.getRes().getString(
            "WrapperJarApp Usage:" ) );
        System.out.println( WrapperManager.getRes().getString(
            "  java org.tanukisoftware.wrapper.WrapperJarApp {jar_file} [app_arguments]" ) );
        System.out.println();
        System.out.println( WrapperManager.getRes().getString(
            "Where:" ) );
        System.out.println( WrapperManager.getRes().getString(
            "  jar_file:       The jar file to run." ) );
        System.out.println( WrapperManager.getRes().getString(
            "  app_arguments:  The arguments that would normally be passed to the" ) );
        System.out.println( WrapperManager.getRes().getString(
            "                  application." ) );
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
        new WrapperJarApp( args );
    }
}

