package org.tanukisoftware.wrapper.test;

import javax.swing.*;
import java.awt.*;

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

public class OpenVariousSizeWindows
{
    static class MyRunnable implements Runnable {
        public int width;
        public int height;
        public int top = 0;
        public int left = 0;
        public boolean createInvisible;
        public int maximizedState = 0;
        public int totNumWindows;
        public int curWindowIndex;
        
        public void run() {
            System.out.println( Main.getRes().getString( "Creating new {0} (width={1}, height={2}, top={3}, left={4}, createInvisible={5})...",
                new Object[] { 
                    ( maximizedState > 0 ? "JFrame" : "JDialog" ),
                    new Integer( width ),
                    new Integer( height ),
                    new Integer( top ),
                    new Integer( left ),
                    ( createInvisible ? "true" : "false" )
                } ) );
            
            Window window;
            if ( maximizedState > 0 ) {
                window = new JFrame();
                ((JFrame)window).setExtendedState( maximizedState );
                ((JFrame)window).setTitle( "A window (JFrame) - " + curWindowIndex + " / " + totNumWindows );
            } else {
                // JDialog causes more problem because the second instance may also cause the bug (only the first instance of JFrame will matter)
                window = new JDialog();
                ((JDialog)window).setModal( true );
                ((JDialog)window).setDefaultCloseOperation( WindowConstants.DISPOSE_ON_CLOSE );
                ((JDialog)window).setTitle( "A window (JDialog) - " + curWindowIndex + " / " + totNumWindows );
            }
            window.setBounds( top, left, width, height );

            try {
                if ( createInvisible ) {
                    window.setVisible( false );
                    Thread.sleep( 1000 );
                }
                window.setVisible( true );
            } catch ( Exception e ) {
                System.out.println( e );
            }
        }
    };

    public static void main( String[] args )
    {
        System.out.println( "Opening several windows of different size every 4 secs." );
        System.out.println( " Make sure that the hidden console doesn't reappear (wrapperw)." );
        System.out.println( " You can maximize the windows or move them to see if something happens..." );
        System.out.println( " You can use wrapper.app.parameter.2=<index> & wrapper.app.parameter.3=<index> to specify start and end indexes." );        
        System.out.println( "" );
        
        Dimension screenSize = Toolkit.getDefaultToolkit().getScreenSize();

        // the screen height
        int screenW = (int)screenSize.getWidth();

        // the screen width
        int screenH = (int)screenSize.getHeight();
        
                    /* left, top, width, height, createInvisible */
        int[][] coords = {
                    /* first windows will be maximized (first both, then horizontally, then vertically). */
        /* 00 */    { 0, 0, screenW    , screenH,     0},
        /* 01 */    { 0, 0, screenW    , screenH,     0},
        /* 02 */    { 0, 0, screenW    , screenH,     0},

        /* 03 */    { 0, 0, screenW    , screenH,     0},
        /* 04 */    { 0, 0, screenW / 2, screenH,     0},
        /* 05 */    { 0, 0, screenW    , screenH / 2, 0},
        /* 06 */    { 0, 0, screenW / 2, screenH / 2, 0},
        /* 07 */    { 0, 0, screenW * 2, screenH,     0},
        /* 08 */    { 0, 0, screenW    , screenH * 2, 0},
        /* 09 */    { 0, 0, screenW * 2, screenH * 2, 0},
        
        /* 10 */    { 0, 0, screenW    , screenH,     1},
        /* 11 */    { 0, 0, screenW / 2, screenH,     1},
        /* 12 */    { 0, 0, screenW    , screenH / 2, 1},
        /* 13 */    { 0, 0, screenW / 2, screenH / 2, 1},
        /* 14 */    { 0, 0, screenW * 2, screenH,     1},
        /* 15 */    { 0, 0, screenW    , screenH * 2, 1},
        /* 16 */    { 0, 0, screenW * 2, screenH * 2, 1},
        
        /* 17 */    { screenW / 3, screenH / 3, screenW    , screenH,     0},
        /* 18 */    { screenW / 3, screenH / 3, screenW / 2, screenH,     0},
        /* 19 */    { screenW / 3, screenH / 3, screenW    , screenH / 2, 0},
        /* 20 */    { screenW / 3, screenH / 3, screenW / 2, screenH / 2, 0},
        /* 21 */    { screenW / 3, screenH / 3, screenW * 2, screenH,     0},
        /* 22 */    { screenW / 3, screenH / 3, screenW    , screenH * 2, 0},
        /* 23 */    { screenW / 3, screenH / 3, screenW * 2, screenH * 2, 0},
        
        /* 24 */    { screenW / 3, screenH / 3, screenW    , screenH,     1},
        /* 25 */    { screenW / 3, screenH / 3, screenW / 2, screenH,     1},
        /* 26 */    { screenW / 3, screenH / 3, screenW    , screenH / 2, 1},
        /* 27 */    { screenW / 3, screenH / 3, screenW / 2, screenH / 2, 1},
        /* 28 */    { screenW / 3, screenH / 3, screenW * 2, screenH,     1},
        /* 29 */    { screenW / 3, screenH / 3, screenW    , screenH * 2, 1},
        /* 30 */    { screenW / 3, screenH / 3, screenW * 2, screenH * 2, 1},
        
        /* 31 */    { screenW / 2 - 50, screenH / 2 - 50, screenW    , screenH,     0},
        /* 32 */    { screenW / 2 - 50, screenH / 2 - 50, screenW / 2, screenH,     0},
        /* 33 */    { screenW / 2 - 50, screenH / 2 - 50, screenW    , screenH / 2, 0},
        /* 34 */    { screenW / 2 - 50, screenH / 2 - 50, screenW / 2, screenH / 2, 0},
        /* 35 */    { screenW / 2 - 50, screenH / 2 - 50, screenW * 2, screenH,     0},
        /* 36 */    { screenW / 2 - 50, screenH / 2 - 50, screenW    , screenH * 2, 0},
        /* 37 */    { screenW / 2 - 50, screenH / 2 - 50, screenW * 2, screenH * 2, 0},
        
        /* 38 */    { screenW / 2 - 50, screenH / 2 - 50, screenW    , screenH,     1},
        /* 39 */    { screenW / 2 - 50, screenH / 2 - 50, screenW / 2, screenH,     1},
        /* 40 */    { screenW / 2 - 50, screenH / 2 - 50, screenW    , screenH / 2, 1},
        /* 41 */    { screenW / 2 - 50, screenH / 2 - 50, screenW / 2, screenH / 2, 1},
        /* 42 */    { screenW / 2 - 50, screenH / 2 - 50, screenW * 2, screenH,     1},
        /* 43 */    { screenW / 2 - 50, screenH / 2 - 50, screenW    , screenH * 2, 1},
        /* 44 */    { screenW / 2 - 50, screenH / 2 - 50, screenW * 2, screenH * 2, 1},
        };

        int start;
        try {
            start = args.length > 0 ? Integer.parseInt( args[0] ) : 0;
        } catch ( NumberFormatException e ) {
            start = 0;
        }
        
        int end;
        try {
            end = args.length > 1 ? Integer.parseInt( args[1] ) : coords.length;
        } catch ( NumberFormatException e ) {
            end = coords.length;
        }
        
        MyRunnable r = new OpenVariousSizeWindows.MyRunnable();
        r.totNumWindows = end - start + 1;

        if ( ( start == 0 && end == 0 ) || ( start == 1 && end == 1 ) || ( start == 1 && end == 1 ) ) {
            System.out.println( Main.getRes().getString( " Create 1 Jframe (it was observed that the first create JFrame can cause the hidden console to reappear).") );
        } else if ( start > 3 ) {
            System.out.println( Main.getRes().getString( " Create {0} JDialog(s) (it was observed that any instance created - not only the first one - can cause the hidden console to reappear).",
                new Integer( r.totNumWindows ) ) );
        } else {
            System.out.println( Main.getRes().getString( " Create {0} Window(s).",
                new Integer( r.totNumWindows ) ) );
        }
        System.out.println( "" );
        
        for ( int i = start; i <= end; i++ )
        {
            try {
                r.createInvisible = ( coords[i][4] != 0 );
                
                if ( i > start ) {
                    Thread.sleep( r.createInvisible ? 3000 : 4000 );
                }
                
                if ( i == 0 ) {
                    r.maximizedState = JFrame.MAXIMIZED_BOTH;
                } else if ( i == 1 ) {
                    r.maximizedState = JFrame.MAXIMIZED_HORIZ;
                } else if ( i == 2 ) {
                    r.maximizedState = JFrame.MAXIMIZED_VERT;
                }
                
                r.top = coords[i][0];
                r.left = coords[i][1];
                r.width = coords[i][2];
                r.height = coords[i][3];
                r.curWindowIndex = i - start + 1;
                
                SwingUtilities.invokeLater( r );
            } catch ( Exception e ) {
                System.out.println( e );
            }
        }
        
        try {
            Thread.sleep( 4000 );
        } catch ( Exception e ) {
        }
        System.exit( 0 );
    }
}
