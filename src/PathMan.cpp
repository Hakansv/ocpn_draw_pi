/***************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Path Manager
 * Author:   Jon Gough
 *
 ***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 **************************************************************************/

#include "pathman.h"

#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
#endif //precompiled headers

#include "wx/image.h"
#include "wx/tokenzr.h"
#include <wx/progdlg.h>

#include "dychart.h"

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include <wx/listimpl.cpp>

#include "styles.h"
//#include "concanv.h"
//#include "navutil.h"
#include "georef.h"
//#include "routeprop.h"
//#include "routemanagerdialog.h"
#include "pluginmanager.h"
#include "multiplexer.h"
#include "MarkIcon.h"
#include "cutil.h"
#include "AIS_Decoder.h"
#include "OCPNSelect.h"
#include "OCPNDrawConfig.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/apptrait.h>

//    List definitions for Point Manager Icons
WX_DECLARE_LIST(wxBitmap, markicon_bitmap_list_type);
WX_DECLARE_LIST(wxString, markicon_key_list_type);
WX_DECLARE_LIST(wxString, markicon_description_list_type);

//    List implementation for Point Manager Icons
#include <wx/listimpl.cpp>
WX_DEFINE_LIST(markicon_bitmap_list_type);
WX_DEFINE_LIST(markicon_key_list_type);
WX_DEFINE_LIST(markicon_description_list_type);

extern PathList     *pPathList;
extern OCPNDrawConfig  *pConfig;
extern OCPNSelect      *pSelect;
extern PlugInManager  *g_pi_manager;
extern int             g_path_line_width;

//--------------------------------------------------------------------------------
//      Pathman   "Path Manager"
//--------------------------------------------------------------------------------

PathMan::PathMan()
{
    m_pparent_app = NULL;
    pActivePath = NULL;
    pActivePoint = NULL;
    pPathActivatePoint = NULL;
}


PathMan::PathMan(MyApp *parent)
{
    m_pparent_app = parent;
    pActivePath = NULL;
    pActivePoint = NULL;
    pPathActivatePoint = NULL;
}

PathMan::~PathMan()
{
    if( pPathActivatePoint ) delete pPathActivatePoint;
}

bool PathMan::IsPathValid( Path *pPath )
{
    wxPathListNode *node = pPathList->GetFirst();
    while( node ) {
        if( pPath == node->GetData() ) return true;
        node = node->GetNext();
    }
    return false;
}

bool PathMan::ActivatePath(Path *pPathToActivate )
{
    wxJSONValue v;
    v[_T("Name")] = pPathToActivate->m_PathNameString;
    v[_T("GUID")] = pPathToActivate->m_GUID;
    wxString msg_id( _T("OCPN_PATH_ACTIVATED") );
    g_pi_manager->SendJSONMessageToAllPlugins( msg_id, v );

    pPathToActivate->m_bPathIsActive = true;

    return true;
}

bool PathMan::DeactivatePath( Path *pPathToDeactivate )
{
    wxJSONValue v;
    v[_T("Path_deactivated")] = pPathToDeactivate->m_PathNameString;
    v[_T("GUID")] = pPathToDeactivate->m_GUID;
    wxString msg_id( _T("OCPN_PATH_DEACTIVATED") );
    g_pi_manager->SendJSONMessageToAllPlugins( msg_id, v );

    pPathToDeactivate->m_bPathIsActive = false;
//    console->pCDI->ClearBackground();

//    m_bDataValid = false;

    return true;
}

bool PathMan::DeletePath( Path *pPath )
{
    if( pPath ) {
        ::wxBeginBusyCursor();

        if( pPath->m_bIsInLayer )
            return false;
            
        pConfig->DeleteConfigPath( pPath );

        //    Remove the path from associated lists
        pSelect->DeleteAllSelectablePathSegments( pPath );
        pPathList->DeleteObject( pPath );

        // walk the path, tentatively deleting/marking points used only by this route
        wxOCPNPointListNode *pnode = ( pPath->pOCPNPointList )->GetFirst();
        while( pnode ) {
            OCPNPoint *prp = pnode->GetData();

            // check all other paths to see if this point appears in any other route
            Path *pcontainer_path = FindPathContainingOCPNPoint( prp );

            if( pcontainer_path == NULL && prp->m_bIsInPath ) {
                prp->m_bIsInPath = false;          // Take this point out of this (and only) path
                if( !prp->m_bKeepXPath ) {
//    This does not need to be done with navobj.xml storage, since the waypoints are stored with the route
//                              pConfig->DeleteWayPoint(prp);

                    pSelect->DeleteSelectablePoint( prp, SELTYPE_OCPNPOINT );

                    // Remove all instances of this point from the list.
                    wxOCPNPointListNode *pdnode = pnode;
                    while( pdnode ) {
                        pPath->pOCPNPointList->DeleteNode( pdnode );
                        pdnode = pPath->pOCPNPointList->Find( prp );
                    }

                    pnode = NULL;
                    delete prp;
                } else {
                    prp->m_bDynamicName = false;
                    prp->m_bIsolatedMark = true;        // This has become an isolated mark
                    prp->m_bKeepXPath = false;         // and is no longer part of a Boundary
                }

            }
            if( pnode ) pnode = pnode->GetNext();
            else
                pnode = pPath->pOCPNPointList->GetFirst();                // restart the list
        }

        delete pPath;

        ::wxEndBusyCursor();

    }
    return true;
}

bool PathMan::DoesPathContainSharedPoints( Path *pPath )
{
    if( pPath ) {
        // walk the route, looking at each point to see if it is used by another route
        // or is isolated
        wxOCPNPointListNode *pnode = ( pPath->pOCPNPointList )->GetFirst();
        while( pnode ) {
            OCPNPoint *prp = pnode->GetData();

            // check all other paths to see if this point appears in any other route
            wxArrayPtrVoid *pRA = GetPathArrayContaining( prp );
            
             if( pRA ) {
                 for( unsigned int ir = 0; ir < pRA->GetCount(); ir++ ) {
                    Path *pb = (Path *) pRA->Item( ir );
                    if( pb == pPath)
                        continue;               // self
                    else 
                        return true;
                }
            }
                
            if( pnode ) pnode = pnode->GetNext();
        }
        
        //      Now walk the path again, looking for isolated type shared waypoints
        pnode = ( pPath->pOCPNPointList )->GetFirst();
        while( pnode ) {
            OCPNPoint *prp = pnode->GetData();
            if( prp->m_bKeepXPath == true )
                return true;
            
           if( pnode ) pnode = pnode->GetNext();
        }
    }
    
    return false;
}
  
wxArrayPtrVoid *PathMan::GetPathArrayContaining( OCPNPoint *pWP )
{
    wxArrayPtrVoid *pArray = new wxArrayPtrVoid;

    wxPathListNode *path_node = pPathList->GetFirst();
    while( path_node ) {
        Path *ppath = path_node->GetData();

        wxOCPNPointListNode *OCPNpoint_node = ( ppath->pOCPNPointList )->GetFirst();
        while( OCPNpoint_node ) {
            OCPNPoint *prp = OCPNpoint_node->GetData();
            if( prp == pWP )                // success
            pArray->Add( (void *) ppath );

            OCPNpoint_node = OCPNpoint_node->GetNext();           // next waypoint
        }

        path_node = path_node->GetNext();                         // next route
    }

    if( pArray->GetCount() ) return pArray;

    else {
        delete pArray;
        return NULL;
    }
}

void PathMan::DeleteAllPaths( void )
{
    ::wxBeginBusyCursor();

    //    Iterate on the RouteList
    wxPathListNode *node = pPathList->GetFirst();
    while( node ) {
        Path *ppath = node->GetData();
        if( ppath->m_bIsInLayer ) {
            node = node->GetNext();
            continue;
        }

        pConfig->m_bSkipChangeSetUpdate = true;
        pConfig->DeleteConfigPath( ppath );
        DeletePath( ppath );
        node = pPathList->GetFirst();                   // Path
        pConfig->m_bSkipChangeSetUpdate = false;
    }

    ::wxEndBusyCursor();

}

Path *PathMan::FindPathContainingOCPNPoint( OCPNPoint *pWP )
{
    wxPathListNode *node = pPathList->GetFirst();
    while( node ) {
        Path *ppath = node->GetData();

        wxOCPNPointListNode *pnode = ( ppath->pOCPNPointList )->GetFirst();
        while( pnode ) {
            OCPNPoint *prp = pnode->GetData();
            if( prp == pWP )  return ppath;
            pnode = pnode->GetNext();
        }

        node = node->GetNext();
    }

    return NULL;                              // not found
}

void PathMan::SetColorScheme( ColorScheme cs )
{
    // Re-Create the pens and colors

//      m_pRoutePen =             wxThePenList->FindOrCreatePen(wxColour(0,0,255), 2, wxSOLID);
//      m_pSelectedRoutePen =     wxThePenList->FindOrCreatePen(wxColour(255,0,0), 2, wxSOLID);
//      m_pActiveRoutePen =       wxThePenList->FindOrCreatePen(wxColour(255,0,255), 2, wxSOLID);
    m_pActiveOCPNPointPen = wxThePenList->FindOrCreatePen( wxColour( 0, 0, 255 ),
            g_path_line_width, wxSOLID );
    m_pOCPNPointPen = wxThePenList->FindOrCreatePen( wxColour( 0, 0, 255 ), g_path_line_width,
            wxSOLID );

//    Or in something like S-52 compliance

    m_pPathPen = wxThePenList->FindOrCreatePen( GetGlobalColor( _T("UINFB") ), g_path_line_width,
            wxSOLID );
    m_pSelectedPathPen = wxThePenList->FindOrCreatePen( GetGlobalColor( _T("UINFO") ),
            g_path_line_width, wxSOLID );
//      m_pActiveRoutePen =       wxThePenList->FindOrCreatePen(GetGlobalColor(_T("PLRTE")), g_route_line_width, wxSOLID);
    m_pActivePathPen = wxThePenList->FindOrCreatePen( GetGlobalColor( _T("UARTE") ),
            g_path_line_width, wxSOLID );
//      m_pActiveRoutePointPen =  wxThePenList->FindOrCreatePen(GetGlobalColor(_T("PLRTE")), 2, wxSOLID);
//      m_pRoutePointPen =        wxThePenList->FindOrCreatePen(GetGlobalColor(_T("CHBLK")), 2, wxSOLID);

    m_pPathBrush = wxTheBrushList->FindOrCreateBrush( GetGlobalColor( _T("UINFB") ), wxSOLID );
    m_pSelectedPathBrush = wxTheBrushList->FindOrCreateBrush( GetGlobalColor( _T("UINFO") ),
            wxSOLID );
    m_pActivePathBrush = wxTheBrushList->FindOrCreateBrush( GetGlobalColor( _T("PLRTE") ),
            wxSOLID );
//      m_pActiveRoutePointBrush =  wxTheBrushList->FindOrCreatePen(GetGlobalColor(_T("PLRTE")), wxSOLID);
//      m_pRoutePointBrush =        wxTheBrushList->FindOrCreatePen(GetGlobalColor(_T("CHBLK")), wxSOLID);

}

