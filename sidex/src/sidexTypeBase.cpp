/* 
 *  libTML:  A BEEP based Messaging Suite
 *  Copyright (C) 2015 wobe-systems GmbH
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307 USA
 *  
 *  You may find a copy of the license under this software is released
 *  at COPYING file. This is LGPL software: you are welcome to develop
 *  proprietary applications using this library without any royalty or
 *  fee but returning back any change, improvement or addition in the
 *  form of source code, project image, documentation patches, etc.
 *
 *  Homepage:
 *    http://www.libtml.org
 *
 *  For professional support contact us:
 *
 *    wobe-systems GmbH
 *    support@libtml.org
 *
 * Contributors:
 *    wobe-systems GmbH
 */

#include "sidexTypeBase.h"
#include <sstream>

using namespace std;

/**
 * @brief    Constructor.
 *
 */
sidexBase::sidexBase()
{
   m_iRefCounter = 0;
}


/**
 * @brief    Destructor.
 */
sidexBase::~sidexBase()
{
}


/**
 * @brief   Get type of this data object.
 *
 */
SIDEX_DATA_TYPE sidexBase::getType()
{
   return type;
}


/**
 * @brief   Get the string representation of this data object.
 */
char* sidexBase::toString()
{
  return (char*) "TypeBase toString()";
}


/**
 * @brief   Decrement the reference counter value of this data object for the memory management.
 *
 */
int sidexBase::decRef(){
  return --m_iRefCounter;
}


/**
 * @brief   Increment the reference counter value of this data object for the memory management.
 */

int sidexBase::incRef(){
  return ++m_iRefCounter;
}


/**
     * @brief   Get the reference counter value of this data object for the memory management.
 */
int sidexBase::getRef(){
  return m_iRefCounter;
}
