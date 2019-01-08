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

import java.awt.BorderLayout;
import java.awt.Button;
import java.awt.Checkbox;
import java.awt.Component;
import java.awt.Container;
import java.awt.Font;
import java.awt.Frame;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Label;
import java.awt.List;
import java.awt.Panel;
import java.awt.ScrollPane;
import java.awt.TextField;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.WindowEvent;
import java.awt.event.WindowListener;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JDialog;
import javax.swing.BoxLayout;
import javax.swing.border.Border;
import javax.swing.border.EmptyBorder;
import javax.swing.border.CompoundBorder;
import org.tanukisoftware.wrapper.WrapperActionServer;
import org.tanukisoftware.wrapper.WrapperListener;
import org.tanukisoftware.wrapper.WrapperManager;
import org.tanukisoftware.wrapper.WrapperResources;
import org.tanukisoftware.wrapper.WrapperSystemPropertyUtil;
import org.tanukisoftware.wrapper.event.WrapperEventListener;
import org.tanukisoftware.wrapper.event.WrapperSecondInvocationEvent;
import org.tanukisoftware.wrapper.event.WrapperServicePauseEvent;
import org.tanukisoftware.wrapper.event.WrapperServiceResumeEvent;
import org.tanukisoftware.wrapper.event.WrapperEvent;

/**
 * This is a Test / Example program which can be used to test the
 *  main features of the Wrapper.
 * <p>
 * It is also an example of Integration Method #3, where you implement
 *  the WrapperListener interface manually.
 * <p>
 * <b>NOTE</b> that in most cases you will want to use Method #1, using the
 *  WrapperSimpleApp helper class to integrate your application.  Please
 *  see the <a href="http://wrapper.tanukisoftware.com/doc/english/integrate.html">integration</a>
 *  section of the documentation for more details.
 *
 * @author Tanuki Software Development Team &lt;support@tanukisoftware.com&gt;
 */
public class Main
    extends AbstractActionApp
    implements WrapperListener
{
    private WrapperActionServer m_actionServer;
    
    private MainFrame m_frame;
    
    private ActionRunner m_actionRunner;
    private static WrapperResources m_res;
    private List m_listenerFlags;
    private TextField m_slowSeconds;
    private TextField m_serviceName;
    private TextField m_consoleTitle;
    private TextField m_childCommand;
    private Checkbox m_childDetached;
    
    private JDialog m_pauseDialog;
    
    /*---------------------------------------------------------------
     * Constructors
     *-------------------------------------------------------------*/
    private Main() {
    }
    
    /*---------------------------------------------------------------
     * Inner Classes
     *-------------------------------------------------------------*/
    private class MainFrame extends Frame implements ActionListener, WindowListener
    {
        /**
         * Serial Version UID.
         */
        private static final long serialVersionUID = -3847376282833547574L;
        private float dpiScaleFactor = 1;

        MainFrame()
        {
            super( getRes().getString( "TestWrapper Example Application" ) );
            
            try
            {
                dpiScaleFactor = (float)WrapperManager.nativeGetDpiScale() / 96;
            }
            catch ( UnsatisfiedLinkError e )
            {
                // This can happen if an old native library is used.  Fall through and make sure the scale factor is 1.
                dpiScaleFactor = 1;
            }
            
            init();
            
            setLocation( 10, 10 );
            setSize( (int)(750*dpiScaleFactor), (int)(480*dpiScaleFactor));
            
            setResizable( true );
        }
        
        private void init()
        {
            GridBagLayout gridBag = new GridBagLayout();
            GridBagConstraints c = new GridBagConstraints();
            
            Panel panel = new Panel();
            panel.setLayout( gridBag );
            
            ScrollPane scrollPane = new ScrollPane();
            scrollPane.add( panel );
            scrollPane.getHAdjustable().setUnitIncrement( 20 );
            scrollPane.getVAdjustable().setUnitIncrement( 20 );
            
            setLayout( new BorderLayout() );
            add( scrollPane, BorderLayout.CENTER );
            
            buildCommand( panel, gridBag, c, "Stop(0)", "stop0",
                    getRes().getString( "Calls WrapperManager.stop( 0 ) to shutdown the JVM and Wrapper with a success exit code." ) );
            
            buildCommand( panel, gridBag, c, "Stop(1)", "stop1",
                    getRes().getString( "Calls WrapperManager.stop( 1 ) to shutdown the JVM and Wrapper with a failure exit code." ) );
            
            buildCommand( panel, gridBag, c, "Exit(0)", "exit0",
                    getRes().getString( "Calls System.exit( 0 ) to shutdown the JVM and Wrapper with a success exit code." ) );
            
            buildCommand( panel, gridBag, c, "Exit(1)", "exit1",
                    getRes().getString( "Calls System.exit( 1 ) to shutdown the JVM and Wrapper with a failure exit code." ) );
            
            buildCommand( panel, gridBag, c, "StopImmediate(0)", "stopimmediate0",
                    getRes().getString( "Calls WrapperManager.stopImmediate( 0 ) to immediately shutdown the JVM and Wrapper with a success exit code." ) );
            
            buildCommand( panel, gridBag, c, "StopImmediate(1)", "stopimmediate1",
                    getRes().getString(" Calls WrapperManager.stopImmediate( 1 ) to immediately shutdown the JVM and Wrapper with a failure exit code." ) );
            
            buildCommand( panel, gridBag, c, "StopAndReturn(0)", "stopandreturn0",
                    getRes().getString( "Calls WrapperManager.stopAndReturn( 0 ) to shutdown the JVM and Wrapper with a success exit code." ) );
            
            buildCommand( panel, gridBag, c, "Nested Exit(1)", "nestedexit1",
                    getRes().getString( "Calls System.exit(1) within WrapperListener.stop(1) callback." ) );
            
            buildCommand( panel, gridBag, c, "Halt(0)", "halt0",
                    getRes().getString( "Calls Runtime.getRuntime().halt(0) to kill the JVM, the Wrapper will restart it." ) );
            
            buildCommand( panel, gridBag, c, "Halt(1)", "halt1",
                    getRes().getString( "Calls Runtime.getRuntime().halt(1) to kill the JVM, the Wrapper will restart it." ) );
            
            buildCommand( panel, gridBag, c, "Restart()", "restart",
                    getRes().getString( "Calls WrapperManager.restart() to shutdown the current JVM and start a new one." ) );
            
            buildCommand( panel, gridBag, c, "RestartAndReturn()", "restartandreturn",
                    getRes().getString( "Calls WrapperManager.restartAndReturn() to shutdown the current JVM and start a new one." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Native Access Violation" ), "access_violation_native",
                    getRes().getString( "Causes an access violation using native code, the JVM will crash and be restarted." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Simulate JVM Hang" ), "appear_hung",
                    getRes().getString( "Makes the JVM appear to be hung as viewed from the Wrapper, it will be killed and restarted." ) );
            
            m_slowSeconds = new TextField( "0" );
            Panel slowPanel = new Panel();
            slowPanel.setLayout( new BorderLayout() );
            slowPanel.add( new Label( getRes().getString( "Delay Seconds: " ) ), BorderLayout.WEST );
            slowPanel.add( m_slowSeconds, BorderLayout.CENTER );
            Panel slowPanel2 = new Panel();
            slowPanel2.setLayout( new BorderLayout() );
            slowPanel2.add( slowPanel, BorderLayout.WEST );
            slowPanel2.add( new Label( getRes().getString( "Makes the JVM appear sluggish by being slow to respond to all packet requests from the Wrapper." ) ), BorderLayout.CENTER );
            buildCommand( panel, gridBag, c, getRes().getString( "Simulate Slow JVM" ), "appear_slow", slowPanel2 );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Create Deadlock" ), "deadlock",
                    getRes().getString( "Creates two new threads which intentionally go into a deadlock situation.  (Standard, Professional)" ) );
            
            buildCommand( panel, gridBag, c, getRes().getString("Simulate Out Of Memory" ), "outofmemory",
                    getRes().getString( "Throws an OutOfMemoryError to demonstrate the Trigger feature." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Ignore Control Events" ), "ignore_events",
                    getRes().getString( "Makes this application ignore control events.  It will not shutdown in response to CTRL-C.  The Wrapper will still respond." ) );
            
            buildCommand( panel, gridBag, c,getRes().getString( "Request Thread Dump" ), "dump",
                    getRes().getString( "Calls WrapperManager.requestThreadDump() to cause the JVM to dump its current thread state." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "System.out Deadlock" ), "deadlock_out",
                    getRes().getString( "Simulates a failure mode where the System.out object has become deadlocked." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Poll Users" ), "users",
                    getRes().getString( "Begins calling WrapperManager.getUser() and getInteractiveUser() to monitor the current and interactive users." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Poll Users with Groups" ), "groups",
                    getRes().getString( "Same as above, but includes information about the user''s groups." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Console" ), "console", getRes().getString( "Prompt for Actions in the console." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Idle" ), "idle", getRes().getString( "Run idly." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Dump Properties" ), "properties",
                    getRes().getString( "Dumps all System Properties to the console." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Dump Configuration" ), "configuration",
                    getRes().getString( "Dumps all Wrapper Configuration Properties to the console." ) );
            
            
            m_listenerFlags = new List( 2, true );
            m_listenerFlags.add( "Service" );
            m_listenerFlags.add( "Control" );
            m_listenerFlags.add( "Logging" );
            m_listenerFlags.add( "Core" );
            m_listenerFlags.add( "RemoteControl" );
            
            Panel flagPanel = new Panel();
            flagPanel.setLayout( new BorderLayout() );
            flagPanel.add( new Label( "Event Flags: " ), BorderLayout.WEST );
            flagPanel.add( m_listenerFlags, BorderLayout.CENTER );
            flagPanel.setSize( 100, 10 );
            
            Panel flagPanel2 = new Panel();
            flagPanel2.setLayout( new BorderLayout() );
            flagPanel2.add( flagPanel, BorderLayout.WEST );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Update Event Listener" ), "listener", flagPanel2 );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Service List" ), "service_list", getRes().getString( "Displays a list of registered services on Windows." ) );
            
            m_serviceName = new TextField( "testwrapper" );
            Panel servicePanel = new Panel();
            servicePanel.setLayout( new BorderLayout() );
            servicePanel.add( new Label( getRes().getString( "Interrogate Service.  Service name: " ) ), BorderLayout.WEST );
            servicePanel.add( m_serviceName, BorderLayout.CENTER );
            Panel servicePanel2 = new Panel();
            servicePanel2.setLayout( new BorderLayout() );
            servicePanel2.add( servicePanel, BorderLayout.WEST );
            buildCommand( panel, gridBag, c, getRes().getString( "Service Interrogate" ), "service_interrogate", servicePanel2 );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Service Start" ), "service_start", getRes().getString( "Starts the above service." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Service Stop" ), "service_stop", getRes().getString( "Stops the above service." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Service User Code" ), "service_user", getRes().getString( "Sends a series of user codes to the above service." ) );
            
            m_consoleTitle = new TextField( getRes().getString( "Java Service Wrapper" ) );
            Panel titlePanel = new Panel();
            titlePanel.setLayout( new BorderLayout() );
            titlePanel.add( new Label( getRes().getString( "Console Title: " ) ), BorderLayout.WEST );
            titlePanel.add( m_consoleTitle, BorderLayout.CENTER );
            Panel titlePanel2 = new Panel();
            titlePanel2.setLayout( new BorderLayout() );
            titlePanel2.add( titlePanel, BorderLayout.WEST );
            buildCommand( panel, gridBag, c, getRes().getString( "Set Console Title" ), "console_title", titlePanel2 );
            
            m_childCommand = new TextField( getRes().getString( "(Please enter command)" ) );
            m_childDetached = new Checkbox( getRes().getString( "Detached  (Professional)" ), false);
            Panel childPanel = new Panel();
            childPanel.setLayout( new BorderLayout() );
            childPanel.add( new Label( getRes().getString( "Command: " ) ), BorderLayout.WEST );
            childPanel.add( m_childCommand, BorderLayout.CENTER );
            childPanel.add( m_childDetached, BorderLayout.EAST );
            Panel childPanel2 = new Panel();
            childPanel2.setLayout( new BorderLayout() );
            childPanel2.add( childPanel, BorderLayout.WEST );
            buildCommand( panel, gridBag, c, getRes().getString( "Execute Child Process" ), "child_exec", childPanel2 );
            
            buildCommand( panel, gridBag, c, getRes().getString( "GC" ), "gc", getRes().getString( "Performs a GC sweep." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Is Professional?" ), "is_professional", getRes().getString( "Prints true if this is a Professional Edition." ) );
            
            buildCommand( panel, gridBag, c, getRes().getString( "Is Standard?" ), "is_standard", getRes().getString( "Prints true if this is a Standard Edition." ) );
            
            addWindowListener( this );
            
            // By default listen to Service and RemoteControl events.
            buildEventMask( new String[] { "Service", "RemoteControl" } );
            updateEventListener();
        }
        
        private void buildCommand( Container container,
                                   GridBagLayout gridBag,
                                   GridBagConstraints c,
                                   String label,
                                   String command,
                                   Object description )
        {
            Button button = new Button( label );
            button.setActionCommand( command );
            if (dpiScaleFactor != 1) {
                button.setFont(new Font("Arial", Font.PLAIN, (int)(12 * dpiScaleFactor)));
            }
            
            c.fill = GridBagConstraints.BOTH;
            c.gridwidth = 1;
            gridBag.setConstraints( button, c );
            container.add( button );
            button.addActionListener(this);
            
            c.gridwidth = GridBagConstraints.REMAINDER;
            Component desc;
            if ( description instanceof String )
            {
                desc = new Label( (String)description );
            }
            else if ( description instanceof Component )
            {
                desc = (Component)description;
            }
            else
            {
                desc = new Label( description.toString() );
            }
            if (dpiScaleFactor != 1) {
                desc.setFont(new Font("Arial", Font.PLAIN, (int)(12 * dpiScaleFactor)));
            }
            
            gridBag.setConstraints( desc, c );
            container.add( desc );
        }
        
        private void buildEventMask(String[] flags) 
        {
            // Create the mask.
            long mask = 0;
            for ( int i = 0; i < flags.length; i++ )
            {
                String flag = flags[i];
                if ( flag.equals( "Service" ) )
                {
                    mask |= WrapperEventListener.EVENT_FLAG_SERVICE;
                }
                else if ( flag.equals( "Control" ) )
                {
                    mask |= WrapperEventListener.EVENT_FLAG_CONTROL;
                }
                else if ( flag.equals( "Logging" ) )
                {
                    mask |= WrapperEventListener.EVENT_FLAG_LOGGING;
                }
                else if ( flag.equals( "Core" ) )
                {
                    mask |= WrapperEventListener.EVENT_FLAG_CORE;
                }
                else if ( flag.equals( "RemoteControl" ) )
                {
                    mask |= WrapperEventListener.EVENT_FLAG_REMOTE_CONTROL;
                }
            }
            
            setEventMask( mask );
        }
        
        /**************************************************************************
         * ActionListener Methods
         *************************************************************************/
        public void actionPerformed( ActionEvent event )
        {
            String action = event.getActionCommand();
            if ( action.equals( "listener" ) )
            {
                String[] flags = m_listenerFlags.getSelectedItems();
                buildEventMask(flags);
            }
            
            int slowSeconds;
            try
            {
                slowSeconds = Integer.parseInt( m_slowSeconds.getText() );
            }
            catch ( NumberFormatException e )
            {
                slowSeconds = 0;
            }
            m_slowSeconds.setText( Integer.toString( slowSeconds ) );
            setSlowSeconds( slowSeconds );
            setServiceName( m_serviceName.getText() );
            setConsoleTitle( m_consoleTitle.getText() );
            setChildParams( m_childCommand.getText(), m_childDetached.getState() );
            
            Main.this.doAction( action );
        }
        
        /**************************************************************************
         * WindowListener Methods
         *************************************************************************/
        public void windowOpened( WindowEvent e )
        {
        }
        
        public void windowClosing( WindowEvent e )
        {
            WrapperManager.stopAndReturn( 0 );
        }
        
        public void windowClosed( WindowEvent e )
        {
        }
        
        public void windowIconified( WindowEvent e )
        {
        }
        
        public void windowDeiconified( WindowEvent e )
        {
        }
        
        public void windowActivated( WindowEvent e )
        {
        }
        
        public void windowDeactivated( WindowEvent e )
        {
        }
    }

    private class ActionRunner implements Runnable {
        private String m_action;
        private boolean m_alive;
        
        public ActionRunner(String action) {
            m_action = action;
            m_alive = true;
        }
    
        public void run() {
            // Wait for a second so that the startup will complete.
            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {}
            
            if (!Main.this.doAction(m_action)) {
                printHelp("\"" + m_action + getRes().getString( "\" is an unknown action." ) );
                WrapperManager.stop( 0 );
                return;
            }
    
            while (m_alive) {
                // Idle some
                try {
                    Thread.sleep(500);
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }
    
        public void endThread( ) {
            m_alive = false;
        }
    }
    
    /*---------------------------------------------------------------
     * WrapperEventListener Methods
     *-------------------------------------------------------------*/
    /**
     * Called whenever a WrapperEvent is fired.  The exact set of events that a
     *  listener will receive will depend on the mask supplied when
     *  WrapperManager.addWrapperEventListener was called to register the
     *  listener.
     *
     * Listener implementations should never assume that they will only receive
     *  events of a particular type.   To assure that events added to future
     *  versions of the Wrapper do not cause problems with user code, events
     *  should always be tested with "if ( event instanceof {EventClass} )"
     *  before casting it to a specific event type.
     *
     * @param event WrapperEvent which was fired.
     */
    public void fired( WrapperEvent event )
    {
        if (event instanceof WrapperSecondInvocationEvent )
        {
            // bring the window to front
            bringToFront(m_frame);
        }
        if (event instanceof WrapperServicePauseEvent )
        {
            // pause the demo application
            pause();
        }
        if (event instanceof WrapperServiceResumeEvent )
        {
            // resume the demo application
            resume();
        }
    }

    /*---------------------------------------------------------------
     * WrapperListener Methods
     *-------------------------------------------------------------*/
    public Integer start( String[] args )
    {
        String command;
        System.out.println( getRes().getString( "TestWrapper: start()" ) );

        prepareSystemOutErr();
        
        if ( args.length <= 0 )
        {
            System.out.println( getRes().getString( "TestWrapper: An action was not specified.  Default to \"dialog\".  Use \"help\" for list of actions." ) );
            command = "dialog";
        }
        else
        {
            command = args[0];
        }
        
        if ( command.equals( "help" ) ) {
            printHelp( null );
            return null;
        }
        
        try
        {
            int port = 9999;
            m_actionServer = new WrapperActionServer( port );
            m_actionServer.enableShutdownAction( true );
            m_actionServer.enableHaltExpectedAction( true );
            m_actionServer.enableRestartAction( true );
            m_actionServer.enableThreadDumpAction( true );
            m_actionServer.enableHaltUnexpectedAction( true );
            m_actionServer.enableAccessViolationAction( true );
            m_actionServer.enableAppearHungAction( true );
            m_actionServer.start();
            
            System.out.println( getRes().getString( "TestWrapper: ActionServer Enabled. " ) );
            System.out.println( getRes().getString( "TestWrapper:   Telnet localhost 9999" ) );
            System.out.println( getRes().getString( "TestWrapper:   Commands: " ) );
            System.out.println( getRes().getString( "TestWrapper:     S: Shutdown" ) );
            System.out.println( getRes().getString( "TestWrapper:     H: Expected Halt" ) );
            System.out.println( getRes().getString( "TestWrapper:     R: Restart" ) );
            System.out.println( getRes().getString( "TestWrapper:     D: Thread Dump" ) );
            System.out.println( getRes().getString( "TestWrapper:     U: Unexpected Halt (Simulate crash)" ) );
            System.out.println( getRes().getString( "TestWrapper:     V: Access Violation (Actual crash)" ) );
            System.out.println( getRes().getString( "TestWrapper:     G: Make the JVM appear to be hung." ) );
        }
        catch ( java.io.IOException e )
        {
            System.out.println( getRes().getString( "TestWrapper: Unable to open the action server socket: {0}", e.getMessage() ) );
            m_actionServer = null;
        }
        
        if ( command.equals( "dialog" ) )
        {
            System.out.println( getRes().getString( "TestWrapper: Showing dialog..." ) );
            
            try
            {
                m_frame = new MainFrame();
                m_frame.setVisible( true );
            }
            catch ( java.lang.InternalError e )
            {
                System.out.println( "TestWrapper: " );
                System.out.println( getRes().getString( "TestWrapper: ERROR - Unable to display the GUI:" ) );
                System.out.println( "TestWrapper:           "  + e.toString() );
                System.out.println( "TestWrapper: " );
                System.out.println( getRes().getString( "TestWrapper: Fall back to the \"console\" action." ) );
                command = "console";
            }
            catch ( java.awt.AWTError e )
            {
                System.out.println( "TestWrapper: " );
                System.out.println( getRes().getString( "TestWrapper: ERROR - Unable to display the GUI:" ) );
                System.out.println( "TestWrapper:           " + e.toString() );
                System.out.println( "TestWrapper: " );
                System.out.println( getRes().getString( "TestWrapper: Fall back to the \"console\" action." ) );
                command = "console";
            }
           catch ( java.lang.UnsupportedOperationException e )
            {
                // java.awt.HeadlessException does not exist in Java versions prior to 1.4
                if ( e.getClass().getName().equals( "java.awt.HeadlessException" ) ) 
                {
                    System.out.println( "TestWrapper: " );
                    System.out.println( getRes().getString( "TestWrapper: ERROR - Unable to display the GUI:" ) );
                    System.out.println( "TestWrapper:           " + e.toString() );
                    System.out.println( "TestWrapper: " );
                    System.out.println( getRes().getString( "TestWrapper: Fall back to the \"console\" action." ) );
                    command = "console";
                }
                else 
                {
                    throw e;
                }
            }
        }
        
        if ( !command.equals( "dialog" ) )
        {
            // * * Start the action thread
            m_actionRunner = new ActionRunner( command );
            Thread actionThread = new Thread( m_actionRunner );
            actionThread.start();
        }
        
        return null;
    }
    
    public int stop( int exitCode )
    {
        System.out.println( getRes().getString( "TestWrapper: stop({0})", new Integer( exitCode ) ) );
        
        if ( m_actionServer != null )
        {
            try
            {
                m_actionServer.stop();
            }
            catch ( Exception e )
            {
                System.out.println( getRes().getString( "TestWrapper: Unable to stop the action server: {0}", e.getMessage() ) );
            }
        }
        
        if ( m_frame != null )
        {
            if ( !WrapperManager.hasShutdownHookBeenTriggered() )
            {
                m_frame.setVisible( false );
                m_frame.dispose();
            }
            m_frame = null;
        }
        
        if ( isNestedExit() )
        {
            System.out.println( getRes().getString( "TestWrapper: calling System.exit({0}) within stop.", String.valueOf(exitCode) ) );
            System.exit( exitCode );
        }
        
        return exitCode;
    }
    
    /**
     * Pause the demo application. 
     */
    private void pause() {
        // Update the title with the "paused" state.
        m_frame.setTitle( m_frame.getTitle() + getRes().getString( " (paused)" ) );
        
        // Disable all components of the frame.
        enableComponents( m_frame, false );
        
        // Show a dialog centered over the frame to indicate that the application was paused and is waiting the resume event.
        JLabel label1 = new JLabel();
        label1.setHorizontalAlignment( javax.swing.SwingConstants.CENTER );
        label1.setAlignmentX(Component.CENTER_ALIGNMENT);
        label1.setText( getRes().getString( "The demo application is paused." ) );
        
        JLabel label2 = new JLabel();
        label2.setHorizontalAlignment( javax.swing.SwingConstants.CENTER );
        label2.setAlignmentX(Component.CENTER_ALIGNMENT);
        label2.setText( getRes().getString( "Resume it to make this dialog box disappear." ) );
      
        Border border1 = label1.getBorder();
        Border margin1 = new EmptyBorder( 25, 25, 0, 25 );
        label1.setBorder( new CompoundBorder(border1, margin1) );
      
        Border border2 = label2.getBorder();
        Border margin2 = new EmptyBorder( 0, 25, 30, 25 );
        label2.setBorder( new CompoundBorder(border2, margin2) );
        
        JPanel panel = new JPanel();
        panel.setLayout(new BoxLayout(panel, BoxLayout.PAGE_AXIS));
        panel.add( label1 );
        panel.add( label2 );

        m_pauseDialog = new JDialog( m_frame, m_frame.getTitle(), false );
        m_pauseDialog.getContentPane().add( panel );
        m_pauseDialog.setResizable( false );
        m_pauseDialog.setDefaultCloseOperation( javax.swing.WindowConstants.DO_NOTHING_ON_CLOSE );
        m_pauseDialog.pack();
        m_pauseDialog.setLocationRelativeTo( m_frame );
        m_pauseDialog.setVisible( true );
    }

    /**
     * Resume the application. 
     */
    private void resume() {
        String title = m_frame.getTitle();
        // Update the title to remove the "paused" state.
        m_frame.setTitle( title.substring( 0, title.indexOf( getRes().getString( " (paused)" ) ) ) );

        // Enable all components of the frame.
        enableComponents( m_frame, true );
        
        // Close the dialog.
        m_pauseDialog.setDefaultCloseOperation( javax.swing.WindowConstants.DISPOSE_ON_CLOSE );
        m_pauseDialog.setVisible(false);
        m_pauseDialog.dispatchEvent(new WindowEvent(m_pauseDialog, WindowEvent.WINDOW_CLOSING));
    }
    
    public void controlEvent( int event )
    {
        System.out.println( getRes().getString( "TestWrapper: controlEvent({0})",  new Integer( event ) ) );
        
        if ( event == WrapperManager.WRAPPER_CTRL_LOGOFF_EVENT )
        {
            if ( WrapperManager.isLaunchedAsService() || WrapperManager.isIgnoreUserLogoffs() )
            {
            System.out.println( getRes().getString( "TestWrapper:   Ignoring logoff event" ) );
            // Ignore
            }
            else if ( !ignoreControlEvents() )
            {
                WrapperManager.stop( 0 );
            }
        }
        else if ( event == WrapperManager.WRAPPER_CTRL_C_EVENT )
        {
            if ( !ignoreControlEvents() ) {
                //WrapperManager.stop(0);
                
                // May be called before the runner is started.
                if (m_actionRunner != null) {
                    m_actionRunner.endThread();
                }
            }
        }
        else
        {
            if ( !ignoreControlEvents() )
            {
                WrapperManager.stop( 0 );
            }
        }
    }
    
    /*---------------------------------------------------------------
     * Static Methods
     *-------------------------------------------------------------*/
    /**
     * Prints the usage text.
     *
     * @param error_msg Error message to write with usage text
     */
    private static void printHelp( String errorMsg )
    {
        System.err.println( getRes().getString( "USAGE" ) );
        System.err.println( "" );
        System.err.println( getRes().getString( "TestWrapper <action>" ) );
        printActions();
        System.err.println( getRes().getString( "  Interactive:" ) );
        System.err.println( getRes().getString( "   dialog                   : Shows the dialog interface" ) );
        System.err.println( getRes().getString( "[EXAMPLE]" ) );
        System.err.println( getRes().getString( "   TestAction access_violation_native " ) );
        System.err.println( "" );
        if ( errorMsg != null )
        {
            System.err.println( getRes().getString( "ERROR: " ) + errorMsg );
            System.err.println( "" );
        }
        
        System.exit( 1 );
    }
    
    /**
     * Request the Resources object for this application.
     */
    public static WrapperResources getRes()
    {
        if ( m_res == null )
        {
            // Synchronize and then recheck to avoid this method always synchronizing.
            synchronized( Main.class )
            {
                if ( m_res == null )
                {
                    m_res = WrapperManager.loadWrapperResources( "wrapperTestApp",
                        WrapperSystemPropertyUtil.getStringProperty( "wrapper.lang.folder", "../lang" ) );
                }
            }
        }
        return m_res;
    }
    
    /**
     * Helper method to bring a window to the front
     */
    private boolean bringToFront(MainFrame frame) {
        try {
            // use reflection as setAlwaysOnTop() requires java 1.5
            java.lang.reflect.Method m = MainFrame.class.getMethod("setAlwaysOnTop", new Class[] { boolean.class } );
            m.invoke(frame, new Object[] { new Boolean(true) });
            m.invoke(frame, new Object[] { new Boolean(false) });
            return true;
        } 
        catch (Exception e)
        {
            return false;
        }
    }
    
    /**
     * Helper method to disable or enable all components of a container
     */
    private void enableComponents(Container container, boolean enable) {
        Component[] components = container.getComponents();
        for (int a = 0; a < components.length; a++) {
            components[a].setEnabled( enable );
            if (components[a] instanceof Container) {
                enableComponents((Container)components[a], enable);
            }
        }
    }
    
    /*---------------------------------------------------------------
     * Main Method
     *-------------------------------------------------------------*/
    /**
     * IMPORTANT: Please read the Javadocs for this class at the top of the
     *  page before you start to use this class as a template for integrating
     *  your own application.  This will save you a lot of time.
     */
    public static void main( String[] args )
    {
       System.out.println( getRes().getString( "TestWrapper: Initializing..." ));
        
       // System.out.println(wrm.getString("test 0 = {0} String = {1} double = {2} object = {3}", new Object[]{ String.valueOf(99), "test",String.valueOf(34.33), "testob"}) );
        // Start the application.  If the JVM was launched from the native
        //  Wrapper then the application will wait for the native Wrapper to
        //  call the application's start method.  Otherwise the start method
        //  will be called immediately.
        WrapperManager.start( new Main(), args );
        
    }
}

