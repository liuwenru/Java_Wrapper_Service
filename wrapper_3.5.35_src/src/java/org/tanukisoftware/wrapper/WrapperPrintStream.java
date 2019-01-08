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

import java.io.PrintStream;
import java.io.UnsupportedEncodingException;

/**
 * Not all methods are currently overridden as this is an internal class.
 */
final class WrapperPrintStream
    extends PrintStream
{
    private String m_header;
    
    /**
     * Creates a new WrapperPrintStream wrapped around another PrintStream.
     *
     * @param out The PrintStream which will be wrapped by this new stream.
     * @param header The header to append at the beginning of any printed messages.
     */
    WrapperPrintStream( PrintStream parent, String header )
    {
        super( parent );
        
        m_header = header;
    }
    
    /**
     * Creates a new WrapperPrintStream wrapped around another PrintStream.
     *
     * @param out The PrintStream which will be wrapped by this new stream.
     * @param autoFlush Whether the output buffer should be automatically flushed or not. The value will be passed to the parent stream.
     * @param encoding The name of a supported character encoding. The value will be passed to the parent stream.
     * @param header The header to append at the beginning of any printed messages.
     *
     * @throws UnsupportedEncodingException If the named encoding is not supported.
     */
    WrapperPrintStream( PrintStream parent, boolean autoFlush, String encoding, String header ) throws UnsupportedEncodingException
    {
        super( parent, autoFlush, encoding );
        
        m_header = header;
    }
    
    /**
     * Terminate the current line by writing the line separator string.  The
     * line separator string is defined by the system property
     * <code>line.separator</code>, and is not necessarily a single newline
     * character (<code>'\n'</code>).
     */
    public void println()
    {
        super.println( m_header );
    }
    
    /**
     * Print a String and then terminate the line.  This method behaves as
     * though it invokes <code>{@link #print(String)}</code> and then
     * <code>{@link #println()}</code>.
     *
     * @param x  The <code>String</code> to be printed.
     */
    public void println( String x )
    {
        if ( x.indexOf( "\n" ) >= 0 )
        {
            String[] lines = x.split( "[\n]", -1 );
            StringBuffer sb = new StringBuffer();
            for ( int i = 0; i < lines.length; i++ )
            {
                if ( i > 0 )
                {
                    sb.append( "\n" );
                }
                sb.append( m_header );
                sb.append( lines[i] );
            }
            super.println( sb.toString() );
        }
        else
        {
            super.println( m_header + x );
        }
    }
    
    /**
     * Print an Object and then terminate the line.  This method behaves as
     * though it invokes <code>{@link #print(Object)}</code> and then
     * <code>{@link #println()}</code>.
     *
     * @param x  The <code>Object</code> to be printed.
     */
    public void println( Object x )
    {
        if ( x == null )
        {
            println( "null" );
        }
        else
        {
            println( x.toString() );
        }
    }
}
