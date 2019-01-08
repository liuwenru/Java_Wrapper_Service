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

/**
 * A WrapperGroup contains information about a group which a user
 *  belongs to.  A WrapperGroup is obtained via a WrapperUser.
 *
 * @author Tanuki Software Development Team &lt;support@tanukisoftware.com&gt;
 */
public class WrapperUNIXGroup
    extends WrapperGroup
{
    /** The GID of the Group. */
    private int m_gid;
    
    /*---------------------------------------------------------------
     * Constructors
     *-------------------------------------------------------------*/
    WrapperUNIXGroup( int gid, String name )
    {
        super( name );

        m_gid = gid;
    }
    
    /*---------------------------------------------------------------
     * Methods
     *-------------------------------------------------------------*/
    /**
     * Returns the GID of the group.
     *
     * @return The GID of the group.
     */
    public int getGID()
    {
        return m_gid;
    }
    
    public String toString()
    {
        StringBuffer sb = new StringBuffer();
        sb.append( "WrapperUNIXGroup[" );
        sb.append( getGID() );
        sb.append( getGroup() );
        sb.append( "]" );
        return sb.toString();
    }
}

