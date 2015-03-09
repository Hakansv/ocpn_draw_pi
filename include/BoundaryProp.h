/***************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Path Properties
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

#ifndef BOUNDARYPROP_H
#define BOUNDARYPROP_H

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
#endif //precompiled headers

#include <PathProp.h>


class BoundaryProp : public PathProp
{
    public:
        BoundaryProp();
        virtual ~BoundaryProp();
    protected:
    private:
};

#endif // BOUNDARYPROP_H
