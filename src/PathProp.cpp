/**************************************************************************
*
* Project:  OpenCPN
* Purpose:  Path Properties Support
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

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/datetime.h>
#include <wx/clipbrd.h>
#include <wx/print.h>
#include <wx/printdlg.h>
#include <wx/stattext.h>

#include "styles.h"
#include "OCPNDrawConfig.h"
#include "georef.h"
#include "chart1.h"
#include "PathMan.h"
#include "pathmanagerdialog.h"
//#include "pathprintout.h"
#include "chcanv.h"
#include "chart1.h"
#include "tcmgr.h"        // pjotrc 2011.03.02
#include "PositionParser.h"
#include "pluginmanager.h"
#include "PathProp.h"
#include "PointMan.h"
#include "OCPNSelect.h"
#include "ocpn_draw_pi.h"


extern double             gLat, gLon, gSog, gCog;
extern double             g_PlanSpeed;
extern wxDateTime         g_StartTime;
extern int                g_StartTimeTZ;
extern IDX_entry          *gpIDX;
extern TCMgr              *ptcmgr;
extern long               gStart_LMT_Offset;
extern OCPNDrawConfig     *pOCPNDrawConfig;
extern PointMan        *pWayPointMan;
extern ChartCanvas        *cc1;
extern OCPNSelect        *pSelect;
extern PathMan           *g_pPathMan;
extern PathManagerDialog *pPathManagerDialog;
extern PathProp       *pPathPropDialog;
extern Track              *g_pActiveTrack;
extern PathList       *pPathList;
extern PlugInManager      *g_pi_manager;
extern bool                g_bShowMag;
extern wxString    m_ActiveLineColour;
extern wxString    m_InActiveLineColour;
extern wxString    m_ActiveFillColour;
extern wxString    m_InActiveLineColour;

extern MyFrame            *gFrame;

// Global print data, to remember settings during the session
extern wxPrintData               *g_printData;

// Global page setup data
extern wxPageSetupData*          g_pageSetupData;

/*!
* Helper stuff for calculating Path
*/

#define    pi        (4.*atan(1.0))
#define    tpi        (2.*pi)
#define    twopi    (2.*pi)
#define    degs    (180./pi)
#define    rads    (pi/180.)

#define    MOTWILIGHT    1    // in some languages there may be a distinction between morning/evening
#define    SUNRISE        2
#define    DAY            3
#define    SUNSET        4
#define    EVTWILIGHT    5
#define    NIGHT        6

/* Next high tide, low tide, transition of the mark level, or some
combination.
Bit      Meaning
0       low tide
1       high tide
2       falling transition
3       rising transition
*/

#define    LW    1
#define    HW    2
#define    FALLING    4
#define    RISING    8


static double sign( double x )
{
    if( x < 0. ) return -1.;
    else
        return 1.;
}

static double FNipart( double x )
{
    return ( sign( x ) * (int) ( fabs( x ) ) );
}

static double FNday( int y, int m, int d, int h )
{
    long fd = ( 367 * y - 7 * ( y + ( m + 9 ) / 12 ) / 4 + 275 * m / 9 + d );
    return ( (double) fd - 730531.5 + h / 24. );
}

static double FNrange( double x )
{
    double b = x / tpi;
    double a = tpi * ( b - FNipart( b ) );
    if( a < 0. ) a = tpi + a;
    return ( a );
}

static double getLMT( double ut, double lon )
{
    double t = ut + lon / 15.;
    if( t >= 0. ) if( t <= 24. ) return ( t );
    else
        return ( t - 24. );
    else
        return ( t + 24. );
}

#define    UTCINPUT         0
#define    LTINPUT          1    // i.e. this PC local time
#define    LMTINPUT         2    // i.e. the remote location LMT time
#define    INPUT_FORMAT     1
#define    DISPLAY_FORMAT   2
#define    TIMESTAMP_FORMAT 3

/*!
 * PathProp type definition
 */

IMPLEMENT_DYNAMIC_CLASS( PathProp, wxDialog )
/*!
 * PathProp event table definition
 */

BEGIN_EVENT_TABLE( PathProp, wxDialog )
    EVT_BUTTON( ID_PATHPROP_CANCEL, PathProp::OnPathPropCancelClick )
    EVT_BUTTON( ID_PATHPROP_OK, PathProp::OnPathPropOkClick )
END_EVENT_TABLE()

/*!
 * PathProp constructors
 */

PathProp::PathProp()
{
    //ctor
}

PathProp::PathProp( wxWindow* parent, wxWindowID id, const wxString& caption, const wxPoint& pos,
        const wxSize& size, long style )
{
    m_wpList = NULL;
    m_nSelected = 0;
    m_pEnroutePoint = NULL;
    m_bStartNow = false;

    m_pEnroutePoint = NULL;
    m_bStartNow = false;
    long wstyle = style;
#ifdef __WXOSX__
    wstyle |= wxSTAY_ON_TOP;
#endif

    SetExtraStyle( GetExtraStyle() | wxWS_EX_BLOCK_EVENTS );
    wxDialog::Create( parent, id, caption, pos, size, style );

    wxFont *qFont = GetOCPNScaledFont(_("Dialog"));
    SetFont( *qFont );
        
    CreateControls();

    //  Make an estimate of the dialog size, without scrollbars showing
    wxSize esize;
    esize.x = GetCharWidth() * 110;
    esize.y = GetCharHeight() * 40;
    SetSize( esize );
    Centre();
}

PathProp::~PathProp()
{
    //dtor
    delete m_PathNameCtl;
    delete m_wpList;

}

void PathProp::OnPathPropRightClick( wxListEvent &event )
{
    wxMenu menu;

    if( ! m_pPath->m_bIsInLayer ) {
        wxString sPropertiesType( wxS("") );
        if ( m_pPath->m_sTypeString.IsNull() || m_pPath->m_sTypeString.IsEmpty() )
            sPropertiesType.append( wxS("OCPN Draw Point") );
        else
            sPropertiesType.append( m_pPath->m_sTypeString );
        sPropertiesType.append( wxS(" &Properties...") );
        wxMenuItem* editItem = menu.Append( ID_PATHPROP_MENU_EDIT_WP, sPropertiesType );
        editItem->Enable( m_wpList->GetSelectedItemCount() == 1 );

        wxMenuItem* delItem = menu.Append( ID_PATHPROP_MENU_DELETE, wxS("&Remove Selected") );
        delItem->Enable( m_wpList->GetSelectedItemCount() > 0 && m_wpList->GetItemCount() > 2 );
    }

    wxMenuItem* copyItem = menu.Append( ID_PATHPROP_MENU_COPY_TEXT, wxS("&Copy all as text") );

    PopupMenu( &menu );
}

void PathProp::CreateControls()
{

    wxBoxSizer* itemBoxSizer1 = new wxBoxSizer( wxVERTICAL );
    SetSizer( itemBoxSizer1 );

    itemDialog1 = new wxScrolledWindow( this, wxID_ANY,
                                      wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    itemDialog1->SetScrollRate(5, 5);

    itemBoxSizer1->Add( itemDialog1, 1, wxEXPAND | wxALL, 0 );

    wxBoxSizer* itemBoxSizer2 = new wxBoxSizer( wxVERTICAL );
    itemDialog1->SetSizer( itemBoxSizer2 );

    wxStaticBox* itemStaticBoxSizer3Static = new wxStaticBox( itemDialog1, wxID_ANY,
            _("Properties") );
    wxStaticBoxSizer* itemStaticBoxSizer3 = new wxStaticBoxSizer( itemStaticBoxSizer3Static,
            wxVERTICAL );
    itemBoxSizer2->Add( itemStaticBoxSizer3, 0, wxEXPAND | wxALL, 5 );

    wxStaticText* itemStaticText4 = new wxStaticText( itemDialog1, wxID_STATIC, _("Name"),
            wxDefaultPosition, wxDefaultSize, 0 );
    itemStaticBoxSizer3->Add( itemStaticText4, 0,
            wxALIGN_LEFT | wxLEFT | wxRIGHT | wxTOP | wxADJUST_MINSIZE, 5 );

    m_PathNameCtl = new wxTextCtrl( itemDialog1, ID_PATHPROP_TEXTCTRL, _T(""), wxDefaultPosition,
            wxSize( 710, -1 ), 0 );
    itemStaticBoxSizer3->Add( m_PathNameCtl, 0,
            wxALIGN_LEFT | wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5 );

//    wxFlexGridSizer* itemFlexGridSizer6 = new wxFlexGridSizer( 2, 2, 0, 0 );
//    itemStaticBoxSizer3->Add( itemFlexGridSizer6, 1, wxALIGN_LEFT | wxALL, 5 );

    wxStaticText* itemStaticText7 = new wxStaticText( itemDialog1, wxID_STATIC, _("Description"),
            wxDefaultPosition, wxDefaultSize, 0 );
    itemStaticBoxSizer3->Add( itemStaticText7, 0,
            wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5 );

    m_textDescription = new wxTextCtrl( itemDialog1, wxID_ANY , wxEmptyString,
            wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
    m_textDescription->SetMinSize( wxSize( -1, 60 ) );
    itemStaticBoxSizer3->Add( m_textDescription, 1, wxALL | wxEXPAND, 5 );
    
    wxStaticText* itemStaticText8 = new wxStaticText( itemDialog1, wxID_STATIC,
            _("Total Length"), wxDefaultPosition, wxDefaultSize, 0 );
    itemStaticBoxSizer3->Add( itemStaticText8, 0,
            wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT | wxTOP | wxADJUST_MINSIZE,
            5 );
            
    m_TotalDistCtl = new wxTextCtrl( itemDialog1, ID_PATHPROP_TEXTCTRL3, _T(""), wxDefaultPosition,
            wxDefaultSize, wxTE_READONLY );
    itemStaticBoxSizer3->Add( m_TotalDistCtl, 0,
            wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT | wxBOTTOM, 5 );
            
    m_pPathActive = new wxCheckBox( itemDialog1, ID_PATHPROP_ACTIVE, _("Active") );
    itemStaticBoxSizer3->Add( m_pPathActive, 0 );

    //wxFlexGridSizer* itemFlexGridSizer6a = new wxFlexGridSizer( 2, 4, 0, 0 );
    //itemStaticBoxSizer3->Add( itemFlexGridSizer6a, 1, wxALIGN_LEFT | wxALL, 5 );

    wxBoxSizer* bSizer2;
    bSizer2 = new wxBoxSizer( wxHORIZONTAL );

    m_staticText1 = new wxStaticText( itemDialog1, wxID_ANY, _("Line Colour:"), wxDefaultPosition, wxDefaultSize,
            0 );
    m_staticText1->Wrap( -1 );
    bSizer2->Add( m_staticText1, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    wxString m_chColorChoices[] = { _("Default color"), _("Black"), _("Dark Red"), _("Dark Green"),
            _("Dark Yellow"), _("Dark Blue"), _("Dark Magenta"), _("Dark Cyan"),
            _("Light Gray"), _("Dark Gray"), _("Red"), _("Green"), _("Yellow"), _("Blue"),
            _("Magenta"), _("Cyan"), _("White") };
    int m_chColorNChoices = sizeof( m_chColorChoices ) / sizeof(wxString);
    m_chColor = new wxChoice( itemDialog1, wxID_ANY, wxDefaultPosition, wxDefaultSize, m_chColorNChoices,
            m_chColorChoices, 0 );
    m_chColor->SetSelection( 0 );
    bSizer2->Add( m_chColor, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    m_staticText2 = new wxStaticText( itemDialog1, wxID_ANY, _("Fill Colour:"), wxDefaultPosition, wxDefaultSize,
            0 );
    m_staticText2->Wrap( -1 );
    bSizer2->Add( m_staticText2, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );
    
    m_chLineColor = new wxChoice( itemDialog1, wxID_ANY, wxDefaultPosition, wxDefaultSize, m_chColorNChoices,
            m_chColorChoices, 0 );
    m_chLineColor->SetSelection( 0 );
    bSizer2->Add( m_chLineColor, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    m_staticText3 = new wxStaticText( itemDialog1, wxID_ANY, _("Style:"), wxDefaultPosition, wxDefaultSize,
            0 );
    m_staticText3->Wrap( -1 );
    bSizer2->Add( m_staticText3, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    wxString m_chStyleChoices[] = { _("Default"), _("Solid"), _("Dot"), _("Long dash"),
            _("Short dash"), _("Dot dash") };
    int m_chStyleNChoices = sizeof( m_chStyleChoices ) / sizeof(wxString);
    m_chStyle = new wxChoice( itemDialog1, wxID_ANY, wxDefaultPosition, wxDefaultSize, m_chStyleNChoices,
            m_chStyleChoices, 0 );
    m_chStyle->SetSelection( 0 );
    bSizer2->Add( m_chStyle, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    m_staticText4 = new wxStaticText( itemDialog1, wxID_ANY, _("Width:"), wxDefaultPosition, wxDefaultSize,
            0 );
    m_staticText4->Wrap( -1 );
    bSizer2->Add( m_staticText4, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    wxString m_chWidthChoices[] = { _("Default"), _("1 pixel"), _("2 pixels"), _("3 pixels"),
            _("4 pixels"), _("5 pixels"), _("6 pixels"), _("7 pixels"), _("8 pixels"),
            _("9 pixels"), _("10 pixels") };
    int m_chWidthNChoices = sizeof( m_chWidthChoices ) / sizeof(wxString);
    m_chWidth = new wxChoice( itemDialog1, wxID_ANY, wxDefaultPosition, wxDefaultSize, m_chWidthNChoices,
            m_chWidthChoices, 0 );
    m_chWidth->SetSelection( 0 );
    bSizer2->Add( m_chWidth, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    itemStaticBoxSizer3->Add( bSizer2, 1, wxEXPAND, 0 );

    wxStaticBox* itemStaticBoxSizer14Static = new wxStaticBox( itemDialog1, wxID_ANY,
            _("Waypoints") );
    m_pListSizer = new wxStaticBoxSizer( itemStaticBoxSizer14Static, wxVERTICAL );
    itemBoxSizer2->Add( m_pListSizer, 1, wxEXPAND | wxALL, 5 );

    wxBoxSizer* itemBoxSizerBottom = new wxBoxSizer( wxHORIZONTAL );
    itemBoxSizer1->Add( itemBoxSizerBottom, 0, wxALIGN_LEFT | wxALL | wxEXPAND, 5 );
    
    wxBoxSizer* itemBoxSizerAux = new wxBoxSizer( wxHORIZONTAL );
    itemBoxSizerBottom->Add( itemBoxSizerAux, 1, wxALIGN_LEFT | wxALL, 5 );

    wxBoxSizer* itemBoxSizer16 = new wxBoxSizer( wxHORIZONTAL );
    itemBoxSizerBottom->Add( itemBoxSizer16, 0, wxALIGN_RIGHT | wxALL, 5 );

    m_CancelButton = new wxButton( this, ID_PATHPROP_CANCEL, _("Cancel"), wxDefaultPosition,
            wxDefaultSize, 0 );
    itemBoxSizer16->Add( m_CancelButton, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    m_OKButton = new wxButton( this, ID_PATHPROP_OK, _("OK"), wxDefaultPosition,
            wxDefaultSize, 0 );
    itemBoxSizer16->Add( m_OKButton, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxALL, 5 );
    m_OKButton->SetDefault();

    //      To correct a bug in MSW commctl32, we need to catch column width drag events, and do a Refresh()
    //      Otherwise, the column heading disappear.....
    //      Does no harm for GTK builds, so no need for conditional
    Connect( wxEVT_COMMAND_LIST_COL_END_DRAG,
            (wxObjectEventFunction) (wxEventFunction) &PathProp::OnEvtColDragEnd );


    //      Create the list control
    m_wpList = new wxListCtrl( itemDialog1, ID_PATHPROP_LISTCTRL, wxDefaultPosition, wxSize( 800, 200 ),
            wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxLC_EDIT_LABELS );

    int char_size = GetCharWidth();

    m_wpList->InsertColumn( 0, _("Leg"), wxLIST_FORMAT_LEFT, char_size * 6 );
    m_wpList->InsertColumn( 1, _("To Waypoint"), wxLIST_FORMAT_LEFT, char_size * 14 );
    m_wpList->InsertColumn( 2, _("Distance"), wxLIST_FORMAT_RIGHT, char_size * 9 );

    if(g_bShowMag)
        m_wpList->InsertColumn( 3, _("Bearing (M)"), wxLIST_FORMAT_LEFT, char_size * 10 );
    else
        m_wpList->InsertColumn( 3, _("Bearing"), wxLIST_FORMAT_LEFT, char_size * 10 );

    m_wpList->InsertColumn( 4, _("Latitude"), wxLIST_FORMAT_LEFT, char_size * 11 );
    m_wpList->InsertColumn( 5, _("Longitude"), wxLIST_FORMAT_LEFT, char_size * 11 );
    m_wpList->InsertColumn( 6, _("Next tide event"), wxLIST_FORMAT_LEFT, char_size * 11 );
    if(g_bShowMag)
        m_wpList->InsertColumn( 7, _("Course (M)"), wxLIST_FORMAT_LEFT, char_size * 10 );
    else
        m_wpList->InsertColumn( 7, _("Course"), wxLIST_FORMAT_LEFT, char_size * 10 );
    m_wpList->InsertColumn( 8, _("Description"), wxLIST_FORMAT_LEFT, char_size * 11 );
    //    m_wpList->Hide();

    m_pListSizer->Add( m_wpList, 2, wxEXPAND | wxALL, 5 );

    //Set the maximum size of the entire  dialog
    int width, height;
    ::wxDisplaySize( &width, &height );
    SetSizeHints( -1, -1, width-100, height-100 );
    
    Connect( wxEVT_COMMAND_LIST_ITEM_RIGHT_CLICK,
            wxListEventHandler(PathProp::OnPathPropRightClick), NULL, this );
    Connect( wxEVT_COMMAND_MENU_SELECTED,
            wxCommandEventHandler(PathProp::OnPathPropMenuSelected), NULL, this );

    //  Fetch any config file values

/*    if( g_StartTimeTZ == 0 )
        m_prb_tzUTC->SetValue( true);
    else if( g_StartTimeTZ == 1 )
        m_prb_tzLocal->SetValue( true);
    else if( g_StartTimeTZ == 2 )
        m_prb_tzLMT->SetValue( true);
*/    

    SetColorScheme( (ColorScheme) 0 );

    
}

void PathProp::OnPathPropListClick( wxListEvent& event )
{
    long itemno = 0;
    m_nSelected = 0;

    //      We use different methods to determine the selected point,
    //      depending on whether this is a Route or a Track.
    int selected_no;
    const wxListItem &i = event.GetItem();
    i.GetText().ToLong( &itemno );
    selected_no = itemno;

    m_pPath->ClearHighlights();

    wxOCPNPointListNode *node = m_pPath->pOCPNPointList->GetFirst();
    while( node && itemno-- ) {
        node = node->GetNext();
    }
    if( node ) {
        OCPNPoint *prp = node->GetData();
        if( prp ) {
            prp->m_bPtIsSelected = true;                // highlight the routepoint

            gFrame->JumpToPosition( prp->m_lat, prp->m_lon, cc1->GetVPScale() );

        }
    }
}

void PathProp::OnPathPropMenuSelected( wxCommandEvent& event )
{
    switch( event.GetId() ) {
        case ID_PATHPROP_MENU_DELETE: {
            long item = -1;
            item = m_wpList->GetNextItem( item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
            if( item == -1 ) break;

            OCPNPoint *wp;
            wp = (OCPNPoint *) m_wpList->GetItemData( item );

            wxString sMessage( wxS("Are you sure you want to remove this ") );
            wxString sCaption( wxS("OCPN Draw Remove ") );
            wxString sType( wxS("") );
            if (!wp || wp->GetTypeString().IsNull() || wp->GetTypeString().IsEmpty() )
                sType.append( wxS("waypoint") );
            else
                sType.append( wp->GetTypeString() );
            sMessage.append( sType );
            sMessage.append( wxS("?") );
            sCaption.append( sType );
            
            int dlg_return = OCPNMessageBox( this, sMessage, sCaption, (long) wxYES_NO | wxCANCEL | wxYES_DEFAULT );

            if( dlg_return == wxID_YES ) {
                m_pPath->RemovePointFromPath( wp, m_pPath );
            }
            break;
        }
        case ID_PATHPROP_MENU_EDIT_WP: {
            long item = -1;

            item = m_wpList->GetNextItem( item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

            if( item == -1 ) break;

            OCPNPoint *wp = (OCPNPoint *) m_wpList->GetItemData( item );
            if( !wp ) break;

            PathManagerDialog::OCPNPointShowPropertiesDialog( wp, this );
            break;
        }
    }
}

void PathProp::SetColorScheme( ColorScheme cs )
{
    DimeControl( this );
}

/*
 * Should we show tooltips?
 */

bool PathProp::ShowToolTips()
{
    return TRUE;
}

void PathProp::SetDialogTitle(const wxString & title)
{
    SetTitle(title);
}

bool PathProp::UpdateProperties( Path *pPath )
{
    if( NULL == pPath ) return false;

    ::wxBeginBusyCursor();

    long LMT_Offset = 0;         // offset in seconds from UTC for given location (-1 hr / 15 deg W)
    
    m_PathNameCtl->SetValue( pPath->m_PathNameString );
    m_textDescription->SetValue( pPath->m_PathDescription);
    m_pPathActive->SetValue( pPath->IsActive() );

    double brg;
    double join_distance = 0.;
    OCPNPoint *first_point = pPath->GetPoint( 1 );
    if( first_point )
        DistanceBearingMercator( first_point->m_lat, first_point->m_lon, gLat, gLon, &brg, &join_distance );

    //    Update the "tides event" column header
    wxListItem column_info;
    if( m_wpList->GetColumn( 6, column_info ) ) {
        wxString c = _("Next tide event");
        if( gpIDX && m_starttime.IsValid() ) {
            c = _T("@~~");
            c.Append( wxString( gpIDX->IDX_station_name, wxConvUTF8 ) );
            int i = c.Find( ',' );
            if( i != wxNOT_FOUND ) c.Remove( i );

        }
        column_info.SetText( c );
        m_wpList->SetColumn( 6, column_info );
    }

    //  Total length
    double total_length = pPath->m_path_length;

    wxString slen;
    slen.Printf( wxT("%5.2f ") + getUsrDistanceUnit(), toUsrDistance( total_length ) );
    m_TotalDistCtl->SetValue( slen );

    wxString time_form;
    wxString tide_form;

    //  Iterate on Route Points
    wxOCPNPointListNode *node = pPath->pOCPNPointList->GetFirst();

    int i = 0;
    double slat = gLat;
    double slon = gLon;
    double tdis = 0.;
    double tsec = 0.;    // total time in seconds

    int stopover_count = 0;
    bool arrival = true; // marks which pass over the wpt we do - 1. arrival 2. departure
    bool enroute = true; // for active route, skip all points up to the active point

    wxString nullify = _T("----");

    int i_prev_point = -1;
    OCPNPoint *prev_OCPNPoint_point = NULL;

    while( node ) {
        OCPNPoint *prp = node->GetData();
        long item_line_index = i + stopover_count;

        //  Leg
        wxString t;
        t.Printf( _T("%d"), i );
        if( i == 0 ) t = _T("Boat");
        if( arrival ) m_wpList->SetItem( item_line_index, 0, t );

        //  Mark Name
        if( arrival ) m_wpList->SetItem( item_line_index, 1, prp->GetName() );
    // Store Dewcription
        if( arrival ) m_wpList->SetItem( item_line_index, 8, prp->GetDescription() );

        //  Distance
        //  Note that Distance/Bearing for Leg 000 is as from current position

        double brg, leg_dist;
        bool starting_point = false;

        starting_point = ( i == 0 ) && enroute;
        if( m_pEnroutePoint && !starting_point ) starting_point = ( prp->m_GUID
                == m_pEnroutePoint->m_GUID );

        DistanceBearingMercator( prp->m_lat, prp->m_lon, slat, slon, &brg, &leg_dist );

    // calculation of course at current WayPoint.
    double course=10, tmp_leg_dist=23;
    wxOCPNPointListNode *next_node = node->GetNext();
    OCPNPoint * _next_prp = (next_node)? next_node->GetData(): NULL;
    if (_next_prp )
    {
        DistanceBearingMercator( _next_prp->m_lat, _next_prp->m_lon, prp->m_lat, prp->m_lon, &course, &tmp_leg_dist );
    }else
    {
      course = 0.0;
      tmp_leg_dist = 0.0;
    }

    //prp->SetCourse(course); // save the course to the next waypoint for printing.
    // end of calculation


    t.Printf( _T("%6.2f ") + getUsrDistanceUnit(), toUsrDistance( leg_dist ) );
    if( arrival )
        m_wpList->SetItem( item_line_index, 2, t );
    if( !enroute )
        m_wpList->SetItem( item_line_index, 2, nullify );
    prp->SetDistance(leg_dist); // save the course to the next waypoint for printing.

        //  Bearing
    if( g_bShowMag )
        t.Printf( _T("%03.0f Deg. M"), gFrame->GetTrueOrMag( brg ) );
    else
        t.Printf( _T("%03.0f Deg. T"), gFrame->GetTrueOrMag( brg ) );

//    if( arrival )
//        m_wpList->SetItem( item_line_index, 3, t );
//    if( !enroute )
//        m_wpList->SetItem( item_line_index, 3, nullify );

    // Course (bearing of next )
    if (_next_prp){
        if( g_bShowMag )
            t.Printf( _T("%03.0f Deg. M"), gFrame->GetTrueOrMag( course ) );
        else
            t.Printf( _T("%03.0f Deg. T"), gFrame->GetTrueOrMag( course ) );
        if( arrival )
            m_wpList->SetItem( item_line_index, 7, t );
    }
    else
        m_wpList->SetItem( item_line_index, 7, nullify );

        //  Lat/Lon
        wxString tlat = toSDMM( 1, prp->m_lat, prp->m_bIsInTrack );  // low precision for routes
        if( arrival ) m_wpList->SetItem( item_line_index, 4, tlat );

        wxString tlon = toSDMM( 2, prp->m_lon, prp->m_bIsInTrack );
        if( arrival ) m_wpList->SetItem( item_line_index, 5, tlon );


        tide_form = _T("");

        LMT_Offset = long( ( prp->m_lon ) * 3600. / 15. );


        //  Save for iterating distance/bearing calculation
        slat = prp->m_lat;
        slon = prp->m_lon;

        // if stopover (ETD) found, loop for next output line for the same point
        //   with departure time & tide information

        if( arrival && ( prp->m_seg_etd.IsValid() ) ) {
            stopover_count++;
            arrival = false;
        } else {
            arrival = true;
            i++;
            node = node->GetNext();

            //    Record this point info for use as previous point in next iteration.
            i_prev_point = i - 1;
            prev_OCPNPoint_point = prp;
        }
    }

    if( pPath->m_LineColour == wxEmptyString ) m_chColor->Select( 0 );
    else {
        for( unsigned int i = 0; i < sizeof( ::GpxxColorNames ) / sizeof(wxString); i++ ) {
            if( pPath->m_LineColour == ::GpxxColorNames[i] ) {
                m_chColor->Select( i + 1 );
                break;
            }
        }
    }

    if( pPath->m_FillColour == wxEmptyString ) m_chLineColor->Select( 0 );
    else {
        for( unsigned int i = 0; i < sizeof( ::GpxxColorNames ) / sizeof(wxString); i++ ) {
            if( pPath->m_FillColour == ::GpxxColorNames[i] ) {
                m_chLineColor->Select( i + 1 );
                break;
            }
        }
    }

    for( unsigned int i = 0; i < sizeof( ::StyleValues ) / sizeof(int); i++ ) {
        if( pPath->m_style == ::StyleValues[i] ) {
            m_chStyle->Select( i );
            break;
        }
    }

    for( unsigned int i = 0; i < sizeof( ::WidthValues ) / sizeof(int); i++ ) {
        if( pPath->m_width == ::WidthValues[i] ) {
            m_chWidth->Select( i );
            break;
        }
    }

    ::wxEndBusyCursor();

    return true;
}

bool PathProp::UpdateProperties()
{
    if( NULL == m_pPath ) return false;
/*
    ::wxBeginBusyCursor();

    m_TotalDistCtl->SetValue( _T("") );
    m_TimeEnrouteCtl->SetValue( _T("") );
    int tz_selection = GetTZSelection();
    long LMT_Offset = 0;         // offset in seconds from UTC for given location (-1 hr / 15 deg W)

    m_SplitButton->Enable( false );
    m_ExtendButton->Enable( false );

    if( m_pRoute->m_bIsTrack ) {
        m_pRoute->UpdateSegmentDistances();           // get segment and total distance
        // but ignore leg speed calcs

        // Calculate AVG speed if we are showing a track and total time
        RoutePoint *last_point = m_pRoute->GetLastPoint();
        RoutePoint *first_point = m_pRoute->GetPoint( 1 );
        double total_seconds = 0.;

        if( last_point->GetCreateTime().IsValid() && first_point->GetCreateTime().IsValid() ) {
            total_seconds =
                    last_point->GetCreateTime().Subtract( first_point->GetCreateTime() ).GetSeconds().ToDouble();
            if( total_seconds != 0. ) {
                m_avgspeed = m_pRoute->m_route_length / total_seconds * 3600;
            } else {
                m_avgspeed = 0;
            }
            wxString s;
            s.Printf( _T("%5.2f"), toUsrSpeed( m_avgspeed ) );
            m_PlanSpeedCtl->SetValue( s );
        } else {
            wxString s( _T("--") );
            m_PlanSpeedCtl->SetValue( s );
        }

        //  Total length
        wxString slen;
        slen.Printf( wxT("%5.2f ") + getUsrDistanceUnit(), toUsrDistance( m_pRoute->m_route_length ) );

        m_TotalDistCtl->SetValue( slen );

        //  Time
        wxString time_form;
        wxTimeSpan time( 0, 0, (int) total_seconds, 0 );
        if( total_seconds > 3600. * 24. ) time_form = time.Format(
                _(" %D Days  %H Hours  %M Minutes") );
        else
            if( total_seconds > 0. ) time_form = time.Format( _(" %H Hours  %M Minutes") );
            else
                time_form = _T("--");
        m_TimeEnrouteCtl->SetValue( time_form );

    } else        // Route
    {
        double brg;
        double join_distance = 0.;
        RoutePoint *first_point = m_pRoute->GetPoint( 1 );
        if( first_point )
            DistanceBearingMercator( first_point->m_lat, first_point->m_lon, gLat, gLon, &brg, &join_distance );

        //    Update the "tides event" column header
        wxListItem column_info;
        if( m_wpList->GetColumn( 8, column_info ) ) {
            wxString c = _("Next tide event");
            if( gpIDX && m_starttime.IsValid() ) {
                c = _T("@~~");
                c.Append( wxString( gpIDX->IDX_station_name, wxConvUTF8 ) );
                int i = c.Find( ',' );
                if( i != wxNOT_FOUND ) c.Remove( i );

            }
            column_info.SetText( c );
            m_wpList->SetColumn( 8, column_info );
        }

        //Update the "ETE/Timestamp" column header

        if( m_wpList->GetColumn( 6, column_info ) ) {
            if( m_starttime.IsValid() ) column_info.SetText( _("ETA") );
            else
                column_info.SetText( _("ETE") );
            m_wpList->SetColumn( 6, column_info );
        }

        m_pRoute->UpdateSegmentDistances( m_planspeed );           // get segment and total distance
        double leg_speed = m_planspeed;
        wxTimeSpan stopover_time( 0 ); // time spent waiting for ETD
        wxTimeSpan joining_time( 0 );   // time spent before reaching first waypoint

        double total_seconds = 0.;

        if( m_pRoute ) {
            total_seconds = m_pRoute->m_route_time;
            if( m_bStartNow ) {
                if( m_pEnroutePoint ) gStart_LMT_Offset = long(
                        ( m_pEnroutePoint->m_lon ) * 3600. / 15. );
                else
                    gStart_LMT_Offset = long(
                            ( m_pRoute->pRoutePointList->GetFirst()->GetData()->m_lon ) * 3600.
                                    / 15. );
            }
        }

        if( m_bStartNow ) {
            joining_time = wxTimeSpan::Seconds((long) wxRound( ( join_distance * 3600. ) / m_planspeed ) );
            double join_seconds = joining_time.GetSeconds().ToDouble();
            total_seconds += join_seconds;
        }

        if( m_starttime.IsValid() ) {
            wxString s;
            if( m_bStartNow ) {
                wxDateTime d = wxDateTime::Now();
                if( tz_selection == 1 ) {
                    m_starttime = d.ToUTC();
                    s = _T(">");
                }
            }
            s += ts2s( m_starttime, tz_selection, (int) gStart_LMT_Offset, INPUT_FORMAT );
            m_StartTimeCtl->SetValue( s );
        } else
            m_StartTimeCtl->Clear();

        if( IsThisRouteExtendable() ) m_ExtendButton->Enable( true );

        //  Total length
        double total_length = m_pRoute->m_route_length;
        if( m_bStartNow ) {
            total_length += join_distance;
        }

        wxString slen;
        slen.Printf( wxT("%5.2f ") + getUsrDistanceUnit(), toUsrDistance( total_length ) );

        if( !m_pEnroutePoint ) m_TotalDistCtl->SetValue( slen );
        else
            m_TotalDistCtl->Clear();

        wxString time_form;
        wxString tide_form;

        //  Time

        wxTimeSpan time( 0, 0, (int) total_seconds, 0 );
        if( total_seconds > 3600. * 24. ) time_form = time.Format(
                _(" %D Days  %H Hours  %M Minutes") );
        else
            time_form = time.Format( _(" %H Hours  %M Minutes") );

        if( !m_pEnroutePoint ) m_TimeEnrouteCtl->SetValue( time_form );
        else
            m_TimeEnrouteCtl->Clear();

        //  Iterate on Route Points
        wxRoutePointListNode *node = m_pRoute->pRoutePointList->GetFirst();

        int i = 0;
        double slat = gLat;
        double slon = gLon;
        double tdis = 0.;
        double tsec = 0.;    // total time in seconds

        int stopover_count = 0;
        bool arrival = true; // marks which pass over the wpt we do - 1. arrival 2. departure
        bool enroute = true; // for active route, skip all points up to the active point

        if( m_pRoute->m_bRtIsActive ) {
            if( m_pEnroutePoint && m_bStartNow ) enroute = ( m_pRoute->GetPoint( 1 )->m_GUID
                    == m_pEnroutePoint->m_GUID );
        }

        wxString nullify = _T("----");

        int i_prev_point = -1;
        RoutePoint *prev_route_point = NULL;

        while( node ) {
            RoutePoint *prp = node->GetData();
            long item_line_index = i + stopover_count;

            //  Leg
            wxString t;
            t.Printf( _T("%d"), i );
            if( i == 0 ) t = _T("---");
            if( arrival ) m_wpList->SetItem( item_line_index, 0, t );

            //  Mark Name
            if( arrival ) m_wpList->SetItem( item_line_index, 1, prp->GetName() );
        // Store Dewcription
            if( arrival ) m_wpList->SetItem( item_line_index, 9, prp->GetDescription() );

            //  Distance
            //  Note that Distance/Bearing for Leg 000 is as from current position

            double brg, leg_dist;
            bool starting_point = false;

            starting_point = ( i == 0 ) && enroute;
            if( m_pEnroutePoint && !starting_point ) starting_point = ( prp->m_GUID
                    == m_pEnroutePoint->m_GUID );

            if( starting_point ) {
                slat = gLat;
                slon = gLon;
                if( gSog > 0.0 ) leg_speed = gSog; // should be VMG
                else
                    leg_speed = m_planspeed;
                if( m_bStartNow ) {
                    DistanceBearingMercator( prp->m_lat, prp->m_lon, slat, slon, &brg, &leg_dist );
                    if( i == 0 ) joining_time = wxTimeSpan::Seconds(
                            (long) wxRound( ( leg_dist * 3600. ) / leg_speed ) );
                }
                enroute = true;
            } else {
                if( prp->m_seg_vmg > 0. ) leg_speed = prp->m_seg_vmg;
                else
                    leg_speed = m_planspeed;
            }

            DistanceBearingMercator( prp->m_lat, prp->m_lon, slat, slon, &brg, &leg_dist );

        // calculation of course at current WayPoint.
        double course=10, tmp_leg_dist=23;
        wxRoutePointListNode *next_node = node->GetNext();
        RoutePoint * _next_prp = (next_node)? next_node->GetData(): NULL;
        if (_next_prp )
        {
        DistanceBearingMercator( _next_prp->m_lat, _next_prp->m_lon, prp->m_lat, prp->m_lon, &course, &tmp_leg_dist );
        }else
        {
          course = 0.0;
          tmp_leg_dist = 0.0;
        }

        prp->SetCourse(course); // save the course to the next waypoint for printing.
        // end of calculation


        t.Printf( _T("%6.2f ") + getUsrDistanceUnit(), toUsrDistance( leg_dist ) );
        if( arrival )
            m_wpList->SetItem( item_line_index, 2, t );
        if( !enroute )
            m_wpList->SetItem( item_line_index, 2, nullify );
        prp->SetDistance(leg_dist); // save the course to the next waypoint for printing.

            //  Bearing
        if( g_bShowMag )
            t.Printf( _T("%03.0f Deg. M"), gFrame->GetTrueOrMag( brg ) );
        else
            t.Printf( _T("%03.0f Deg. T"), gFrame->GetTrueOrMag( brg ) );

        if( arrival )
            m_wpList->SetItem( item_line_index, 3, t );
        if( !enroute )
            m_wpList->SetItem( item_line_index, 3, nullify );

        // Course (bearing of next )
        if (_next_prp){
            if( g_bShowMag )
                t.Printf( _T("%03.0f Deg. M"), gFrame->GetTrueOrMag( course ) );
            else
                t.Printf( _T("%03.0f Deg. T"), gFrame->GetTrueOrMag( course ) );
            if( arrival )
                m_wpList->SetItem( item_line_index, 10, t );
        }
        else
            m_wpList->SetItem( item_line_index, 10, nullify );

            //  Lat/Lon
            wxString tlat = toSDMM( 1, prp->m_lat, prp->m_bIsInTrack );  // low precision for routes
            if( arrival ) m_wpList->SetItem( item_line_index, 4, tlat );

            wxString tlon = toSDMM( 2, prp->m_lon, prp->m_bIsInTrack );
            if( arrival ) m_wpList->SetItem( item_line_index, 5, tlon );


            tide_form = _T("");

            LMT_Offset = long( ( prp->m_lon ) * 3600. / 15. );

            // Time to each waypoint or creation date for tracks
            if( i == 0 && enroute ) {
                time_form.Printf( _("Start") );
                if( m_starttime.IsValid() ) {
                    wxDateTime act_starttime = m_starttime + joining_time;
                    time_form.Append( _T(": ") );

                    if( !arrival ) {
                        wxDateTime etd = prp->m_seg_etd;
                        if( etd.IsValid() && etd.IsLaterThan( m_starttime ) ) {
                            stopover_time += etd.Subtract( m_starttime );
                            act_starttime = prp->m_seg_etd;
                        }
                    }

                    int ds = getDaylightStatus( prp->m_lat, prp->m_lon, act_starttime );
                    wxString s = ts2s( act_starttime, tz_selection, (int) LMT_Offset,
                            DISPLAY_FORMAT );
                    time_form.Append( s );
                    time_form.Append( _T("   (") );
                    time_form.Append( GetDaylightString(ds) );
                    time_form.Append( _T(")") );

                    if( ptcmgr ) {
                        int jx = 0;
                        if( prp->GetName().Find( _T("@~~") ) != wxNOT_FOUND ) {
                            tide_form = prp->GetName().Mid( prp->GetName().Find( _T("@~~") ) + 3 );
                            jx = ptcmgr->GetStationIDXbyName( tide_form, prp->m_lat, prp->m_lon );
                        }
                        if( gpIDX || jx ) {
                            time_t tm = act_starttime.GetTicks();
                            tide_form = MakeTideInfo( jx, tm, tz_selection, LMT_Offset );
                        }
                    }
                }
                tdis = 0;
                tsec = 0.;
            } // end of route point 0
            else {
                if( arrival && enroute ) tdis += leg_dist;
                if( leg_speed ) {
                    if( arrival && enroute ) tsec += 3600 * leg_dist / leg_speed; // time in seconds to arrive here
                    wxTimeSpan time( 0, 0, (int) tsec, 0 );

                    if( m_starttime.IsValid() ) {

                        wxDateTime ueta = m_starttime;
                        ueta.Add( time + stopover_time + joining_time );

                        if( !arrival ) {
                            wxDateTime etd = prp->m_seg_etd;
                            if( etd.IsValid() && etd.IsLaterThan( ueta ) ) {
                                stopover_time += etd.Subtract( ueta );
                                ueta = prp->m_seg_etd;
                            }
                        }

                        int ds = getDaylightStatus( prp->m_lat, prp->m_lon, ueta );
                        time_form = ts2s( ueta, tz_selection, LMT_Offset, DISPLAY_FORMAT );
                        time_form.Append( _T("   (") );
                        time_form.Append( GetDaylightString(ds) );
                        time_form.Append( _T(")") );


                        if( ptcmgr ) {
                            int jx = 0;
                            if( prp->GetName().Find( _T("@~~") ) != wxNOT_FOUND ) {
                                tide_form = prp->GetName().Mid(
                                        prp->GetName().Find( _T("@~~") ) + 3 );
                                jx = ptcmgr->GetStationIDXbyName( tide_form, prp->m_lat, prp->m_lon );
                            }
                            if( gpIDX || jx ) {
                                time_t tm = ueta.GetTicks();
                                tide_form = MakeTideInfo( jx, tm, tz_selection, LMT_Offset );
                            }
                        }
                    } else {
                        if( tsec > 3600. * 24. ) time_form = time.Format( _T(" %D D  %H H  %M M") );
                        else
                            time_form = time.Format( _T(" %H H  %M M") );
                    }
                } // end if legspeed
            }  // end of repeatable route point

            if( enroute && ( arrival || m_starttime.IsValid() ) ) m_wpList->SetItem(
                    item_line_index, 6, time_form );
            else
                m_wpList->SetItem( item_line_index, 6, _T("----") );

            //Leg speed
            wxString s;
            s.Printf( _T("%5.2f"), toUsrSpeed( leg_speed ) );
            if( arrival ) m_wpList->SetItem( item_line_index, 7, s );

            if( enroute ) m_wpList->SetItem( item_line_index, 8, tide_form );
            else {
                m_wpList->SetItem( item_line_index, 7, nullify );
                m_wpList->SetItem( item_line_index, 8, nullify );
            }

            //  Save for iterating distance/bearing calculation
            slat = prp->m_lat;
            slon = prp->m_lon;

            // if stopover (ETD) found, loop for next output line for the same point
            //   with departure time & tide information

            if( arrival && ( prp->m_seg_etd.IsValid() ) ) {
                stopover_count++;
                arrival = false;
            } else {
                arrival = true;
                i++;
                node = node->GetNext();

                //    Record this point info for use as previous point in next iteration.
                i_prev_point = i - 1;
                prev_route_point = prp;
            }
        }
    }

    if( m_pRoute->m_Colour == wxEmptyString ) m_chColor->Select( 0 );
    else {
        for( unsigned int i = 0; i < sizeof( ::GpxxColorNames ) / sizeof(wxString); i++ ) {
            if( m_pRoute->m_Colour == ::GpxxColorNames[i] ) {
                m_chColor->Select( i + 1 );
                break;
            }
        }
    }

    for( unsigned int i = 0; i < sizeof( ::StyleValues ) / sizeof(int); i++ ) {
        if( m_pRoute->m_style == ::StyleValues[i] ) {
            m_chStyle->Select( i );
            break;
        }
    }

    for( unsigned int i = 0; i < sizeof( ::WidthValues ) / sizeof(int); i++ ) {
        if( m_pRoute->m_width == ::WidthValues[i] ) {
            m_chWidth->Select( i );
            break;
        }
    }

    ::wxEndBusyCursor();
*/
    return true;
}

bool PathProp::SaveChanges( void )
{

    if( m_pPath && !m_pPath->m_bIsInLayer ) {
        //  Get User input Text Fields
        m_pPath->m_PathNameString = m_PathNameCtl->GetValue();
        m_pPath->m_PathDescription = m_textDescription->GetValue();
        m_pPath->m_bPathIsActive = m_pPathActive->GetValue();
        
        if( m_chColor->GetSelection() == 0 ) m_pPath->m_LineColour = wxEmptyString;
        else
            m_pPath->m_LineColour = ::GpxxColorNames[m_chColor->GetSelection() - 1];
        if( m_chLineColor->GetSelection() == 0 ) m_pPath->m_FillColour = wxEmptyString;
        else
            m_pPath->m_FillColour = ::GpxxColorNames[m_chLineColor->GetSelection() - 1];
        m_pPath->m_style = ::StyleValues[m_chStyle->GetSelection()];
        m_pPath->m_width = ::WidthValues[m_chWidth->GetSelection()];

        pOCPNDrawConfig->UpdatePath( m_pPath );
        pOCPNDrawConfig->UpdateSettings();
    }

    if( m_pPath->IsActive() )
    {
        wxJSONValue v;
        v[_T("Name")] =  m_pPath->m_PathNameString;
        v[_T("GUID")] =  m_pPath->m_GUID;
        wxString msg_id( _T("OCPN_BND_ACTIVATED") );
        g_pi_manager->SendJSONMessageToAllPlugins( msg_id, v );
    }

    return true;
}

void PathProp::OnPathPropCancelClick( wxCommandEvent& event )
{
    //    Look in the route list to be sure the raoute is still available
    //    (May have been deleted by RouteMangerDialog...)

    bool b_found_boundary = false;
    wxPathListNode *node = pPathList->GetFirst();
    while( node ) {
        Path *pPath = node->GetData();

        if( pPath == m_pPath ) {
            b_found_boundary = true;
            break;
        }
        node = node->GetNext();
    }

    if( b_found_boundary ) m_pPath->ClearHighlights();

    m_bStartNow = false;

    Hide();
    cc1->Refresh( false );

    event.Skip();
}

void PathProp::OnPathPropOkClick( wxCommandEvent& event )
{
    //    Look in the boundary list to be sure the boundary is still available
    //    (May have been deleted by RouteManagerDialog...)

    bool b_found_boundary = false;
    wxPathListNode *node = pPathList->GetFirst();
    while( node ) {
        Path *pPath = node->GetData();

        if( pPath == m_pPath ) {
            b_found_boundary = true;
            break;
        }
        node = node->GetNext();
    }

    if( b_found_boundary ) {
        SaveChanges();              // write changes to globals and update config
        m_pPath->ClearHighlights();
    }

    m_pEnroutePoint = NULL;
    m_bStartNow = false;

    if( pPathManagerDialog && pPathManagerDialog->IsShown() ) {
        pPathManagerDialog->UpdatePathListCtrl();
    }

    Hide();
    cc1->Refresh( false );

    event.Skip();

}

void PathProp::OnEvtColDragEnd( wxListEvent& event )
{
    m_wpList->Refresh();
}

void PathProp::SetPathAndUpdate( Path *pB, bool only_points )
{
    if( NULL == pB )
        return;

    //  Fetch any config file values
    if ( !only_points )
    {
        //      long LMT_Offset = 0;                    // offset in seconds from UTC for given location (-1 hr / 15 deg W)
        m_tz_selection = 1;

        m_pPath = pB;
        
        m_PathNameCtl->SetValue( m_pPath->m_PathNameString );

        m_PathNameCtl->SetFocus();
    }
    m_wpList->DeleteAllItems();

#if 0
    // Select the proper list control, and add it to List sizer
    m_pListSizer->Clear();

    if( m_pPath ) {
        m_wpList->Show();
        m_pListSizer->Add( m_wpList, 2, wxEXPAND | wxALL, 5 );
    }
//    GetSizer()->Fit( this );
    GetSizer()->Layout();
#endif


    InitializeList();

    UpdateProperties( pB );

    if( m_pPath )
        m_wpList->Show();

//    GetSizer()->Fit( this );
//    GetSizer()->Layout();

    Refresh( false );

}

void PathProp::InitializeList()
{
    if( NULL == m_pPath ) return;

    //  Iterate on Route Points, inserting blank fields starting with index 0
    wxOCPNPointListNode *pnode = m_pPath->pOCPNPointList->GetFirst();
    int in = 0;
    while( pnode ) {
        m_wpList->InsertItem( in, _T(""), 0 );
        m_wpList->SetItemPtrData( in, (wxUIntPtr)pnode->GetData() );
        in++;
        if( pnode->GetData()->m_seg_etd.IsValid() ) {
            m_wpList->InsertItem( in, _T(""), 0 );
            in++;
        }
        pnode = pnode->GetNext();
    }
}
