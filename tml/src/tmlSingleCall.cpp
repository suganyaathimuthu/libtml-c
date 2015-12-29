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
 *  For commercial support on build UJO enabled solutions contact us:
 *  
 * Contributors:
 *    wobe-systems GmbH
 */
 
#include "tmlGlobalCallback.h"
#include "tmlErrors.h"
#include "tmlCore.h"
#include "tmlObjWrapper.h"
#include "tmlCmdDefines.h"
#include "tmlSingleCall.h"
#include "tmlCoreWrapper.h"
#include "systemBase.h"
#include "logValues.h"

/*********************************************************************************************************************************
*                                             "C" / Global methods / Callbacks & Threads
*********************************************************************************************************************************/

/**
 * @brief  callback in case of a close of the connection (initiated by the listener)
 */
void connectionCloseHandler(VortexConnection *connection, axlPointer user_data)
{
  // Call the class callback handling method with all it's member- attributes 
  // to handle the lost connection:
  globalCallback(user_data, connection);
}


/**
 * @brief   Disconnect all open Connections.
 */
 void tmlSingleCall::DropAllOpenConnections(){
  ///////////////////////////////////////////////////////////////////////////
  // Begin of critical section
  enterCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "DropAllOpenConnections", "Vortex CMD", "vortex_mutex_lock");

  ////////////////////////////////////////////////////////////////////////
  // First Deregister connection close notice:
  DeregisterConnectionLost();

  ////////////////////////////////////////////////////////////////////////
  // Then I have to wait until all async pending commands are finished:
  WaitUntilLastSenderIsIdle();

  ShutdownAllOpenConnections();

  CloseAllOpenConnections();
  ///////////////////////////////////////////////////////////////////////////
  // End of critical section
  leaveCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "DropAllOpenConnections", "Vortex CMD", "vortex_mutex_unlock");
}


/**
 * @brief   Deregister connection lost callback messages
 */
void tmlSingleCall::DeregisterConnectionLost()
{
  // Her I don't need do fetch the mutex because it will be done in the method above / DropAllOpenConnections

  if (m_bDeregistered){
    return;
  }
  // Note: vortex_connection_remove_on_close_full() seems not to work,
  // SoI do use a member to flag deregistration too;
  m_bDeregistered = true;
  int iSize;
  TML_INT32 iRet = m_ConnectionElementHT->hashSize(&iSize);

  if (TML_SUCCESS == iRet && 0 < iSize){
    TML_INT64* iKeys;
    iRet = m_ConnectionElementHT->getKeys(&iKeys);
    if (TML_SUCCESS == iRet){
      tmlConnectionObj*  connectionObj;
      for (int i = 0; i < iSize && TML_SUCCESS == iRet;++i){
        iRet = m_ConnectionElementHT->getValue(iKeys[i], (void**) &connectionObj);
        if (TML_SUCCESS == iRet){
          TMLCoreSender* sender;
          connectionObj->getSender(&sender);
          VortexConnection* connection;
          connectionObj->getConnection(&connection);
          m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "DeregisterConnectionLost", "Vortex CMD", "vortex_connection_get_status");
          VortexStatus cStatus = vortex_connection_get_status(connection);
          if (VortexOk == cStatus){
            //////////////////////////////////////////////////////////////////////////////////////////////
            // This should only happen in case of the first time I found the connection in the link list:
            ////////////////////////////////////////////////////////////////////////
            // Deregister callback for connection close:
            m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "ShutdownAllOpenConnections", "Vortex CMD", "vortex_connection_remove_on_close_full");
            vortex_connection_remove_on_close_full (connection, connectionCloseHandler, &m_internalConnectionCloseHandlerMethod);
          }
        }
      }
      delete (iKeys);
    }
  }
}


/**
 * @brief   Shutdown all open Connections / has to becalled before CloseAllOpenConnections
 */
void tmlSingleCall::ShutdownAllOpenConnections()
{
  // Her I don't need do fetch the mutex because it will be done in the method above / DropAllOpenConnections


  int iSize;

  TML_INT32 iRet = m_ConnectionElementHT->hashSize(&iSize);
  if (TML_SUCCESS == iRet && 0 < iSize){
    TML_INT64* iKeys;
    iRet = m_ConnectionElementHT->getKeys(&iKeys);
    if (TML_SUCCESS == iRet){
      tmlConnectionObj*  connectionObj;
      for (int i = 0; i < iSize && TML_SUCCESS == iRet;++i){
        iRet = m_ConnectionElementHT->getValue(iKeys[i], (void**) &connectionObj);
        if (TML_SUCCESS == iRet){
          TMLCoreSender* sender;
          connectionObj->getSender(&sender);
          VortexConnection* connection;
          connectionObj->getConnection(&connection);
          m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "ShutdownAllOpenConnections", "Vortex CMD", "vortex_connection_get_status");
          VortexStatus cStatus = vortex_connection_get_status(connection);
          if (VortexOk == cStatus){
            //////////////////////////////////////////////////////////////////////////////////////////////////////////
            // shutdown the connection but don't close it - this will be done later after TMLCoreSender- destruction :
            m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "ShutdownAllOpenConnections", "Vortex CMD", "vortex_connection_shutdown");
            vortex_connection_shutdown(connection);
          }
          //////////////////////////////////////////////////////////////////////////////////////////////////////////
          // I can do TMLCoreSender- destruction now:
          delete (sender);
          sender = NULL;
        }
      }
      delete (iKeys);
    }
  }
}


/**
 * @brief  helper method / sleep for millisecond
*/
void tmlSingleCall::SleepForMilliSeconds(DWORD mSecs){
#ifdef LINUX // LINUX
  ///////////////////////////////////////////////////////////////////////////
  // Delay for one millisecond:
  timespec delay;
  delay.tv_sec = 0;
  delay.tv_nsec = 1000000 * mSecs;  // 1 milli sec * mSecs
  // sleep for delay time
  nanosleep(&delay, NULL);
  return;
#else // LINUX
  Sleep (mSecs);
#endif // LINUX
}


/**
 * @brief   Close all open Connections / has to becalled after ShutdownAllOpenConnections
 */
void tmlSingleCall::CloseAllOpenConnections()
{
  // Her I don't need do fetch the mutex because it will be done in the method above / DropAllOpenConnections

  int iSize;
  TML_INT32 iRet = m_ConnectionElementHT->hashSize(&iSize);

  if (TML_SUCCESS == iRet && 0 < iSize){
    TML_INT64* iKeys;
    iRet = m_ConnectionElementHT->getKeys(&iKeys);
    if (TML_SUCCESS == iRet){
      tmlConnectionObj*  connectionObj;
      for (int i = 0; i < iSize && TML_SUCCESS == iRet;++i){
        iRet = m_ConnectionElementHT->getValue(iKeys[i], (void**) &connectionObj);
        if (TML_SUCCESS == iRet){
          VortexChannel* channel;
          connectionObj->getChannel(&channel);
          TMLCoreSender* sender;
          connectionObj->getSender(&sender);

          VortexConnection* connection;
          connectionObj->getConnection(&connection);
          m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "CloseAllOpenConnections", "Vortex CMD", "vortex_connection_get_status");
          VortexStatus cStatus = vortex_connection_get_status(connection);
          if (VortexConnectionForcedClose == cStatus){
            //////////////////////////////////////////////////////////////////////////////////////////////
            // This should only happen in case of the first time I found the connection in the link list:
            //////////////////////////////////////////////////////////////////////////////////////////////////////////
            // Now close the connection to get rid if m_ctx references:
            m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "CloseAllOpenConnections", "Vortex CMD", "vortex_connection_close");
            vortex_connection_close(connection);
            connection = NULL; 
          }
          //////////////////////////////////////
          // Now I can delete the list element:
          m_ConnectionElementHT->removeEntry(iKeys[i]);
          ////////////////////////////////////////
          // Delete the value stored in the hash:
          delete (connectionObj);
          ////////////////////////////////////////
          connectionObj = NULL;
        }
      }
      delete (iKeys);
    }
  }
}


/*********************************************************************************************************************************
*                                             Class definitions:
*********************************************************************************************************************************/


/**
 * @brief    Constructor.
 */
tmlSingleCall::tmlSingleCall(VortexCtx* ctx, tmlLogHandler* loghandler, tmlProfileHandler* pHandler, tmlCoreWrapper* pCore)
{
  m_bDeregistered = true;
  m_tmlCoreHandle = pCore;

  m_iLogValue = 0;
  ////////////////////////////////////////////////////
  // Remember the vortex execution context reference:
  m_ctx = ctx;

  m_pHandler = pHandler;

  m_log = loghandler;

  ////////////////////////////////
  // init list for connections:
  m_ConnectionElementHT = new TMLUniversalHashTable(loghandler);
  m_ConnectionElementHT->createHashTable(false);

  /////////////////////////////////////////////////////////////////////////////
  //  init the internal class callback method to handle a lost of connection
  m_internalConnectionCloseHandlerMethod.SetCallback(this, &tmlSingleCall::SignalConnectionCloseToSender);

  ////////////////////////////////////////////////////////////////////////////
  // create the mutex that protect critial section about communication data:
  axl_bool bSuccess;
  bSuccess = createCriticalSectionObject(TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, "tmlSingleCall", "tmlSingleCall", "Vortex CMD", "vortex_mutex_create");
  m_iMutexCriticalSectionLockCount = 0;

  m_iLogFileIndex = 0;

  m_collectCallDestObjHandler = NULL;
}


/**
 * @brief    Destructor.
 */
tmlSingleCall::~tmlSingleCall()
{
  ////////////////////////////////////////
  // TMLCoreSender / drop all connections /
  // if there are async calls pendig it will wait:
  DropAllOpenConnections();

  ////////////////////////////////////////////////////////////////////////////
  // Destroy the mutex that protect critial section about communication data:
  destroyCriticalSectionObject(TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, "tmlSingleCall", "~tmlSingleCall", "Vortex CMD", "vortex_mutex_destroy");
   
  ////////////////////////////////////////////////////////////////////////
  // we don't have to exit here:
  ////////////////////////////////
  // vortex execution context
  //m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "~tmlSingleCall", "Vortex CMD", "vortex_exit_ctx");
  //vortex_exit_ctx (m_ctx, false);
  m_ctx = NULL;

  delete m_ConnectionElementHT;
}


/**
 * @brief   Set log value to all open Connections.
 */
void tmlSingleCall::setLoggingValue(int iLog)
{
  m_iLogValue = iLog;
  ///////////////////////////////////////////////////////////////////////////
  // Begin of critical section
  enterCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "LogValueToOpenConnections", "Vortex CMD", "vortex_mutex_lock");

  int iSize;
  TML_INT32 iRet = m_ConnectionElementHT->hashSize(&iSize);

  if (TML_SUCCESS == iRet && 0 < iSize){
    TML_INT64* iKeys;
    iRet = m_ConnectionElementHT->getKeys(&iKeys);
    if (TML_SUCCESS == iRet){
      tmlConnectionObj*  connectionObj;
      for (int i = 0; i < iSize && TML_SUCCESS == iRet;++i){
        iRet = m_ConnectionElementHT->getValue(iKeys[i], (void**) &connectionObj);
        if (TML_SUCCESS == iRet){
          TMLCoreSender* sender;
          connectionObj->getSender(&sender);
          sender->TMLCoreSender_Set_Logging_Value(iLog);
        }
      }
      delete (iKeys);
    }
  }
  ///////////////////////////////////////////////////////////////////////////
  // End of critical section
  leaveCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "LogValueToOpenConnections", "Vortex CMD", "vortex_mutex_unlock");
}


/**
  * @brief   Class callback method that will be called by the registered callback method in case of a close of the connection (initiated by the listener).
 */
bool tmlSingleCall::SignalConnectionCloseToSender(void* connection)
{
  if (m_bDeregistered){
    return true;
  }
  /////////////////////////////////////////////////////////////////////////////
  // Maybe we are in a shutdown process / in that case don't do anything here:
  if (TML_SUCCESS == m_tmlCoreHandle->tmlCoreWrapper_IsAccessible()){
    ///////////////////////////////////////////////////////////////////////////
    // Begin of critical section
    enterCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "SignalConnectionCloseToSender", "Vortex CMD", "vortex_mutex_lock");
    int iSize;
    TML_INT32 iRet = m_ConnectionElementHT->hashSize(&iSize);

    if (TML_SUCCESS == iRet && 0 < iSize){
      TML_INT64* iKeys;
      iRet = m_ConnectionElementHT->getKeys(&iKeys);
      if (TML_SUCCESS == iRet){
        tmlConnectionObj*  intConnectionObj;
        for (int i = 0; i < iSize && TML_SUCCESS == iRet;++i){
          iRet = m_ConnectionElementHT->getValue(iKeys[i], (void**) &intConnectionObj);
          if (TML_SUCCESS == iRet){
            VortexConnection* refConnection;
            intConnectionObj->getConnection(&refConnection);
            if (refConnection == connection){
              TMLCoreSender* sender;
              intConnectionObj->getSender(&sender);
              ///////////////////////////////////////////////////
              // The list element may be removed afterwards:
              intConnectionObj->setToBeRemoved();
              ///////////////////////////////////////
              // Because of the concept of "tml_Bal_Set_MaxConnectionFailCount"
              // and "tml_Evt_Set_MaxConnectionFailCount" there should be mot an
              // unsubscription here - whis will be done internal dependent of that
              // max value 
              //
              // In case of concept changest remove the omments here:

              ///////////////////////////////////////////////////
              // disable possible subsrciptions:
              /*
              char* sProfile;
              intConnectionObj->getProfile(&sProfile);
              char* sHost;
              intConnectionObj->getHost(&sHost);
              char* sPort;
              intConnectionObj->getPort(&sPort);
              */
              ////////////////////////////
              // Maybe it is subscribed for events:
              //m_collectCallDestObjHandler->unsubscribeEventMessageDestination(sProfile, sHost, sPort, true);
              ////////////////////////////
              // Maybe it is subscribed for load balancing:
              //m_collectCallDestObjHandler->unsubscribeLoadBalancedMessageDestination(sProfile, sHost, sPort, true);
              if (intConnectionObj->isLocked(true, false)){
                ///////////////////////////////////////////////////
                // Signal the sender that the connection is lost:
                sender->TMLCoreSender_ConnectionClose();
              }
            }
          }
        }
        delete (iKeys);
      }
    }
    ////////////////////////////////////////////////////////////////////////
    // RemoveMarkedSenderOutOfConnectionList causes an exception on here
    // So I put it into comments / this can be done, because
    // it will be  called on every sendCommand in the future:
    // Begin of comment:
          ////////////////////////////////////////////////////////////
          // It makes sense to sleep for a while because afterwards
          // it will be possible that the connection is unlocked
          // and will be able to be removed out of the list:
        //  SleepForMilliSeconds(20);
          ////////////////////////////////////////////////////////////
          // Now remove all "marked to be remove" entries in the list:
        //  RemoveMarkedSenderOutOfConnectionList(false);
    // End of comment
    //
    ////////////////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////////////////
    // End of critical section
    leaveCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "SignalConnectionCloseToSender", "Vortex CMD", "vortex_mutex_unlock");
  }
  return true;
}


/**
 * @brief    Create a Vortex communication channel.
 */
int tmlSingleCall::CreateChannel(const char*  profile, int iChannel, int iWindowSize, VortexConnection* m_connection, VortexChannel** channel)
{
  // Not Used because we do use vortex channel pool

  int iRet = TML_SUCCESS;
  VortexChannel* m_channel;

  // Is there a valid vortex execution context and a valid connection:
  if (NULL != m_ctx && NULL != m_connection){
    // create a new channel (by chosing 0 as channel number the
    // Vortex Library will automatically assign the new channel
    // number free. */
    m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "CreateChannel", "Vortex CMD", "vortex_channel_new");
    m_channel = vortex_channel_new (m_connection, iChannel,
                profile,
                /* no close handling */
                NULL, NULL,
                // Queued replys:
                //vortex_channel_queue_reply, m_queue,
                // Non Queued replys:
                NULL, NULL,
                /* no async channel creation */
                NULL, NULL);
    if (NULL == m_channel) {
      printf ("Unable to create the channel..\n");
      iRet = TML_ERR_CHANNEL_NOT_INITIALIZED;
    }
    else{
      // serialize channel I/O
      m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "CreateChannel", "Vortex CMD", "vortex_channel_set_serialize");
      vortex_channel_set_serialize(m_channel, true);

      // Set window window size:
      m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "CreateChannel", "Vortex CMD", "vortex_channel_set_window_size");
      vortex_channel_set_window_size(m_channel, iWindowSize);
      *channel = m_channel;
    }
  }
  else{
    iRet = TML_ERR_SENDER_NOT_INITIALIZED;
  }
  return iRet;
}

  
/**
 * @brief    Check for valid connection
 */
bool tmlSingleCall::IsConnectionValid(VortexConnection * connection, VortexChannel* channel){

  // Not Used because we do use vortex channel pool

  bool bValid = false;
  if (NULL != m_ctx){
    if (NULL != connection){
      if (NULL != channel){
        // Maybe the channel has been closed by the listener:
        m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "IsConnectionValid", "Vortex CMD", "vortex_channel_is_being_closed");
        if (! vortex_channel_is_being_closed(channel)){
          // Channel is open, now is it ok or disconnected ?
          m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "IsConnectionValid", "Vortex CMD", "vortex_connection_is_ok");
          if (vortex_connection_is_ok (connection, axl_false)){
            // The channel is ok, I must check if it is busy:
            m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "IsConnectionValid", "Vortex CMD", "vortex_channel_is_ready");
            if ( vortex_channel_is_ready (channel)){
            // The channel is ok and idle:
              bValid = true;
            }
            else{
              m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "IsConnectionValid", "Vortex CMD", "vortex_channel_is_opened");
              if (vortex_channel_is_opened (channel)){
                // The channel is ok and busy (maybe an async CMD:
                bValid = true;
              }
            }
          }
          else{
            m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "IsConnectionValid", "Vortex CMD", "vortex_connection_get_message");
            const char* msg = vortex_connection_get_message(connection);
            m_log->log ("tmlSingleCall", "IsConnectionValid", "vortex_connection_get_message", msg);
          }
        }
      }
    }
  }
  return bValid;
}


/**
  * @brief   Ask if any connection with the requested parameter is locked
  */
bool tmlSingleCall::AnyConnectionLocked(bool bLockCritical, char* sRefPort, char* sRefHost, char* sRefProfile)
{
  ///////////////////////////////////////////////////////////////////////////
  // Begin of critical section
  if (bLockCritical){
    enterCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "AnyConnectionLocked", "Vortex CMD", "vortex_mutex_lock");
  }

  bool bAnyLockedConnection = false;
  int iSize;
  TML_INT32 iRet = m_ConnectionElementHT->hashSize(&iSize);

  if (TML_SUCCESS == iRet && 0 < iSize){
    TML_INT64* iKeys;
    iRet = m_ConnectionElementHT->getKeys(&iKeys);
    if (TML_SUCCESS == iRet){
      tmlConnectionObj*  connectionObj;
      for (int i = iSize - 1; i >= 0 && TML_SUCCESS == iRet && !bAnyLockedConnection;--i){
        iRet = m_ConnectionElementHT->getValue(iKeys[i], (void**) &connectionObj);
        if (TML_SUCCESS == iRet){
          if (connectionObj->isEqual(sRefProfile, sRefHost,sRefPort)){
            if (connectionObj->isLocked(true, false)){
              bAnyLockedConnection = true;
            }
          }
        }
      }
      delete (iKeys);
    }
  }
  ///////////////////////////////////////////////////////////////////////////
  // End of critical section
  if (bLockCritical){
    leaveCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "AnyConnectionLocked", "Vortex CMD", "vortex_mutex_unlock");
  }
  if (TML_SUCCESS != iRet){
    printf ("######   AnyConnectionLocked reports error %d #######\n", iRet);
    bAnyLockedConnection = true;
  }
  return bAnyLockedConnection;
}


 /**
  * @brief   Remove all pending unsubscriptions.
  */
void tmlSingleCall::RemoveMarkedUnsubscriptions(bool bLockCritical)
{
  if (NULL != m_collectCallDestObjHandler){

    bool bAnyRemoveDone = false;
    do{
      bAnyRemoveDone = false;
      DestinationAddressHandlingListElement** pList = m_collectCallDestObjHandler->acquireUnsubscribedLinkList();
      DestinationAddressHandlingListElement* element = *pList;
      //m_collectCallDestObjHandler->dumpUnsubscribedObjToLinkList("RemoveMarkedUnsubscriptions BEGIN", true);

      if (NULL != element){
        do{
          ///////////////////////////////////////////////////
          // Element to be deleted:
          DestinationAddressHandlingListElement* delElement;
          delElement = element;

          ///////////////////////////////////////////////////
          // The Profile / Host / Port
          char* sRefProfile;
          element->destinationObj->getMessageDestinationProfile(&sRefProfile);
          char* sRefHost;
          element->destinationObj->getMessageDestinationHost(&sRefHost);
          char* sRefPort;
          element->destinationObj->getMessageDestinationPort(&sRefPort);

          bool bAnyLockedConnection;
          bAnyLockedConnection = AnyConnectionLocked(false, sRefPort, sRefHost, sRefProfile);
          /////////////////////////////////////////
          // Next element:
          element = element->prev;

          if (!bAnyLockedConnection){
            bAnyRemoveDone = true;
            //printf ("RemoveMarkedUnsubscriptions %s - %s - %s \n",sRefPort, sRefHost, sRefProfile);
            /////////////////////////////////////////
            // Next element:
            m_log->log (TML_LOG_EVENT, "tmlSingleCall", "RemoveMarkedUnsubscriptions", sRefHost, sRefPort);
            delete (delElement->destinationObj);
            delElement->destinationObj = NULL;

            if (NULL != delElement->prev){
              delElement->prev->next = delElement->next;
            }
            if (NULL != delElement->next){
              delElement->next->prev = delElement->prev;
            }


            if (delElement == *pList){
              *pList = element;
            }
            /////////////////////////////////////////
            // Delete the link list object:
            delete (delElement);
          }
        }
        while (NULL != element && !bAnyRemoveDone);
      }
      //m_collectCallDestObjHandler->dumpUnsubscribedObjToLinkList("RemoveMarkedUnsubscriptions END", true);
      m_collectCallDestObjHandler->unlockUnsubscribedLinkList();
    }
    while (bAnyRemoveDone);
  }
}


/**
  * @brief   Remove all sender marked to be removed.
  */
void tmlSingleCall::RemoveMarkedSenderOutOfConnectionList(bool bLockCritical)
{
  ///////////////////////////////////////////////////////////////////////////
  // Begin of critical section
  if (bLockCritical){
    enterCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "RemoveMarkedSenderOutOfConnectionList", "Vortex CMD", "vortex_mutex_lock");
  }

  int iSize;
  TML_INT32 iRet = m_ConnectionElementHT->hashSize(&iSize);

  if (TML_SUCCESS == iRet && 0 < iSize){
    TML_INT64* iKeys;
    iRet = m_ConnectionElementHT->getKeys(&iKeys);
    if (TML_SUCCESS == iRet){
      tmlConnectionObj*  connectionObj;
      for (int i = iSize - 1; i >= 0 && TML_SUCCESS == iRet;--i){
        iRet = m_ConnectionElementHT->getValue(iKeys[i], (void**) &connectionObj);
        if (TML_SUCCESS == iRet){
          if (connectionObj->isPendingToBeRemoved()){
            char* sHost;
            char* sPort;
            connectionObj->getHost(&sHost);
            connectionObj->getPort(&sPort);
            if (!connectionObj->isLocked(true, false)){
              TMLCoreSender* sender;
              connectionObj->getSender(&sender);
              VortexChannel* channel;
              connectionObj->getChannel(&channel);
              ////////////////////////////////////////
              // Wait until pending cmd is finished:
              sender->TMLCoreSender_WaitForPendingAsyncCmd();
              m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "RemoveMarkedSenderOutOfConnectionList", "Vortex CMD", "vortex_connection_shutdown");
              VortexConnection* connection = NULL;
              connectionObj->getConnection(&connection);
              vortex_connection_shutdown(connection);
              // free memory:
              delete (sender);
              //////////////////////////////////////
              // Now I can delete the list element:
              m_ConnectionElementHT->removeEntry(iKeys[i]);
              delete (connectionObj);
              connectionObj = NULL;
            }
          }
        }
      }
      delete (iKeys);
    }
  }
  ///////////////////////////////////////////////////////////////////////////
  // End of critical section
  if (bLockCritical){
    leaveCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "RemoveMarkedSenderOutOfConnectionList", "Vortex CMD", "vortex_mutex_unlock");
  }
}


/**
 * @brief    Add a Vortex connection element to the connection link list.
 */
void tmlSingleCall::AddConnectionElement(tmlConnectionObj* connectionObj, bool bLock)
{
  if (bLock){
    ///////////////////////////////////////////////////////////////////////////
    // Begin of critical section
    enterCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "AddConnectionElement", "Vortex CMD", "vortex_mutex_lock");
  }
  ////////////////////////// 
  // Get a unique number:
  TML_INT64 iMagic = connectionObj->getMagicNumber();
  ////////////////////////// 
  // Set the value:
  char* sHost;
  char* sPort;
  connectionObj->getHost(&sHost);
  connectionObj->getPort(&sPort);


  m_ConnectionElementHT->setValue(iMagic, connectionObj);

  if (bLock){
    ///////////////////////////////////////////////////////////////////////////
    // End of critical section
    leaveCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "AddConnectionElement", "Vortex CMD", "vortex_mutex_unlock");
  }
}


/**
 * @brief   Wait until the last sender is idle.
 */
int tmlSingleCall::WaitUntilLastSenderIsIdle()
{
   // Her I don't need do fetch the mutex because it will be done in the method above / ShutdownAllOpenConnections called by DropAllOpenConnections

  int iSize;
  TML_INT32 iRet = m_ConnectionElementHT->hashSize(&iSize);

  if (TML_SUCCESS == iRet && 0 < iSize){
    TML_INT64* iKeys;
    iRet = m_ConnectionElementHT->getKeys(&iKeys);
    if (TML_SUCCESS == iRet){
      tmlConnectionObj*  connectionObj;
      for (int i = 0; i < iSize && TML_SUCCESS == iRet;++i){
        iRet = m_ConnectionElementHT->getValue(iKeys[i], (void**) &connectionObj);
        if (TML_SUCCESS == iRet){
          TMLCoreSender*  sender;
          connectionObj->getSender(&sender);
          iRet = sender->TMLCoreSender_WaitForPendingAsyncCmd();
        }
      }
      delete (iKeys);
    }
  }
  return iRet;
}


/**
 * @brief    Create a Vortex communication connection.
 */
int tmlSingleCall::GetConnection(const char* profile, const char* sHost, const char* sPort, VortexConnection** connection)
{
  int iRet = TML_SUCCESS;
  VortexConnection* m_connection;

  // Is there a valid vortex execution context
  if (NULL == m_ctx){
    iRet = TML_ERR_SENDER_NOT_INITIALIZED;
  }
  else{
    ///////////////////////////////////////////////////
    // Set the connection timeout to 5 seconds:
    m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_connection_connect_timeout");
    vortex_connection_connect_timeout (m_ctx, 5000000);
  // CMD sticking during stream download
    m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_connection_new");
    m_connection = vortex_connection_new (m_ctx, sHost, sPort, NULL, NULL);
  // CMD sticking during stream download
    m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_connection_is_ok");
    if (vortex_connection_is_ok (m_connection, axl_false))
    {
      // Check if the requested profile is supported:
      m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_connection_is_profile_supported");
      if (vortex_connection_is_profile_supported (m_connection, profile)){
        m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection profile", profile, " is supported");


        bool bRegisterProfile = false;
        bool bRegisterCB = false;
        m_pHandler->tmlProfileRegister(profile, false, NULL, NULL, &bRegisterProfile, &bRegisterCB);

        // Register the profile:
        m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_profiles_register");
        if (bRegisterProfile)
        {
          if (!vortex_profiles_register (m_ctx, profile, 
                  NULL, NULL, 
                  NULL, NULL,
                  NULL, NULL))
          {
            ////////////////////////////////////////////////////////////////////////
            // shutdown connection:
            m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_connection_shutdown");
            vortex_connection_shutdown(m_connection);
            ////////////////////////////////////////////////////////////////////////
            // close connection to get rid of references:
            m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_connection_close");
            vortex_connection_close(m_connection);
            m_connection = NULL;
            // Problem with the register;
            iRet = TML_ERR_SENDER_PROFILE_REGISTRATION;
          }
        }
      }
      else{
        m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection profile ", profile, " is not supported !");
        ////////////////////////////////////////////////////////////////////////
        // shutdown connection:
        m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_connection_shutdown");
        vortex_connection_shutdown(m_connection);
        ////////////////////////////////////////////////////////////////////////
        // close connection to get rid of references:
        m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_connection_close");
        vortex_connection_close(m_connection);
        m_connection = NULL;
        // Profile is not supported:
        iRet = TML_ERR_SENDER_PROFILE_NOT_SUPPORTED;
      }
    }
    else{
      const char* msg = vortex_connection_get_message(m_connection);
      m_log->log ("tmlSingleCall", "GetConnection", "vortex_connection_get_message", msg);
      ////////////////////////////////////////////////////////////////////////
      // shutdown connection:
      m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_connection_shutdown");
      vortex_connection_shutdown(m_connection);
      ////////////////////////////////////////////////////////////////////////
      // close connection to get rid of references:
      m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnection", "Vortex CMD", "vortex_connection_close");
      vortex_connection_close(m_connection);
      m_connection = NULL;
      // Connection isn't ok
      iRet = TML_ERR_SENDER_INVALID_PARAMS;
    }
  }
  if (TML_SUCCESS == iRet){
    *connection = m_connection;
  }
  return iRet;
}


/**
 * @brief    Search for an existing tmlConnectionObj in a VORTEXConnectionListElement for the requested parameter
 */
int tmlSingleCall::SearchForConnectionObjInHT(const char* profile, const char* sHost, const char* sPort, tmlConnectionObj** connectionObj, VortexChannelPool** channelPool, bool* bFoundRet, bool bRemoveMarkedObjs, unsigned int iTimeout){
  ///////////////////////////////////////////////////////////////////////////
  // Begin of critical section

  enterCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "SearchForConnectionObjInHT", "Vortex CMD", "vortex_mutex_lock");
  if (bRemoveMarkedObjs){
    //////////////////////////////////////////////////////////////////////////
    // It's a good idea to remove connections that are "marked to be remove":
    RemoveMarkedSenderOutOfConnectionList(false);

    //////////////////////////////////////////////////////////////////////////
    // It's a good idea to remove pending event / load balanced unsubscriptions here:
    RemoveMarkedUnsubscriptions(false);
  }
  
  int iSize;

  *bFoundRet = false;

  TML_INT32 iRet = m_ConnectionElementHT->hashSize(&iSize);

  if (TML_SUCCESS == iRet && 0 < iSize){
    TML_INT64* iKeys;
    iRet = m_ConnectionElementHT->getKeys(&iKeys);
    if (TML_SUCCESS == iRet){
      tmlConnectionObj*  intAnyConnectionObj = NULL;
      bool bFound = false;
      bool bUnlockedFound = false;
      bool bAnyFound = false;
      tmlConnectionObj*  intConnectionObj;


      bool bRetry = false;
      int  iBusyChannelCount = 0;
      int  maxBusyCount = iTimeout / SLEEP_TIME_WAIT_IDLE_CANNEL;
      do{
        bRetry = false;
        for (int i = 0; i < iSize && TML_SUCCESS == iRet && !bUnlockedFound;++i){
          iRet = m_ConnectionElementHT->getValue(iKeys[i], (void**) &intConnectionObj);
          if (TML_SUCCESS == iRet){
            bFound = intConnectionObj->isEqual(profile, sHost, sPort);
            if (bFound){
              if (intConnectionObj->isPendingToBeRemoved()){
                bFound = false;
              }
            }
            if (bFound){
              if (!intConnectionObj->isLocked(true, true)){
                intConnectionObj->lock(false, true);
                *connectionObj = intConnectionObj;
                bUnlockedFound = true;
              }
              else{
                ++iBusyChannelCount;
                //////////////////////////////////////////////
                // the second async send request on this connection may
                // have happened without a valid channelPool, so do the following 
                // loop:
                intConnectionObj->getChannelPool(channelPool);
                if (NULL == *channelPool){
                  int iCount = 0;
                  ///////////////////////////////////////////////
                  // loop to get the for channelPool.
                  // In case of multiple async commands it may
                  // be possible, that the value is about to be
                  // set of the predecessor
                  do{
                    intConnectionObj->getChannelPool(channelPool);
                    if (NULL == *channelPool){
                      SleepForMilliSeconds(20);
                      ++iCount;
                    }
                  }
                  while (NULL == *channelPool && iCount < 50);
                  if (NULL == *channelPool){
                    m_log->log ("tmlSingleCall", "SearchForConnectionObjInHT", "###################", "Object is looked but no channelPool value");
                    iRet = TML_ERR_SENDER_NOT_INITIALIZED;
                  }
                }
                if (!bAnyFound){
                  bAnyFound = true;
                  intAnyConnectionObj = intConnectionObj;
                }
                bFound = false;
              }
            }
          }
        }
        if (TML_SUCCESS == iRet){
          if (iBusyChannelCount == MAX_ALLOWED_CHANNEL_PER_CONNECTION){
            if (0 == --maxBusyCount){
              m_log->log ("tmlSingleCall", "SearchForConnectionObjInHT", "###################", "timeout elapsed but still all channel busy");
              iRet = TML_ERR_SENDER_NOT_INITIALIZED;
            }
            else{
              iBusyChannelCount = 0;
              Sleep (SLEEP_TIME_WAIT_IDLE_CANNEL);
              bRetry = true;
            }
          }
        }
      }while (TML_SUCCESS == iRet && bRetry);
      if (TML_SUCCESS == iRet){
        if (!bUnlockedFound){
          if (bAnyFound){
            // Found but not unlocked:
            //m_log->log ("tmlSingleCall", "SearchForConnectionObjInHT", "###################", " found but not unlocked");


            //////////////////////////////////////////////////////////////////////
            // Connection must not be created again,  but I need a new channel:

            /////////////////////////////////////////////////////////////////////
            // I have to create a sender for the requested connection parameter:
            TMLCoreSender* coreSenderAttr = new TMLCoreSender(m_ctx, m_log);
            iRet = coreSenderAttr->getInitError();
            if (TML_SUCCESS == iRet){
              VortexConnection* connectionAttr = NULL;
              intAnyConnectionObj->getConnection(&connectionAttr);
              ////////////////////////////////////////////////////////////
              // Register callback for the case of a lost of connection:
              tmlConnectionObj* newConnectionObj = new tmlConnectionObj(m_log);
              VortexChannelPool* channelPoolAttr = NULL;
              intAnyConnectionObj->getChannelPool(&channelPoolAttr);
              newConnectionObj->setConnectionObj(profile, sHost, sPort, coreSenderAttr, channelPoolAttr, connectionAttr);
              newConnectionObj->lock(false, false);
              //m_log->log ("tmlSingleCall", "SearchForConnectionObjInHT", "AddConnectionElement", "");
              AddConnectionElement(newConnectionObj, false);
              *channelPool = channelPoolAttr;
              *connectionObj = newConnectionObj;
              *bFoundRet  = true;
            }
          }
        }
        else{
          *bFoundRet  = true;
        }
      }
      delete (iKeys);
    }
  }

  ///////////////////////////////////////////////////////////////////////////
  // End of critical section
  leaveCriticalSection (TML_LOG_VORTEX_MUTEX, &m_mutexCriticalSection, &m_iMutexCriticalSectionLockCount, "tmlSingleCall", "SearchForConnectionObjInHT", "Vortex CMD", "vortex_mutex_unlock");
  return iRet;
}


/**
 * @brief    Get the Vortex connection element for the requested parameter.
 */
int tmlSingleCall::GetConnectionElement(const char* profile, const char* sHost, const char* sPort, tmlConnectionObj** pConnectionObj, bool bRawViaVortexPayloadFeeder, bool bRemoveMarkedObjs, unsigned int iTimeout)
{
  int iRet = TML_SUCCESS;

  tmlConnectionObj* connectionObj = NULL;

  TMLCoreSender* coreSenderAttr = NULL;
  VortexConnection* connectionAttr = NULL;
  bool bFound;
    
  bFound = false;

  //////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Get the connection out of my connection list for the attributes: profile, sHost, sPort / if it exists:
  VortexChannelPool* channelPool = NULL;
  iRet = SearchForConnectionObjInHT(profile, sHost, sPort, &connectionObj, &channelPool, &bFound, bRemoveMarkedObjs, iTimeout);
  if (TML_SUCCESS == iRet){
    if (!bFound){
      /////////////////////////////////////////////////////////////////////////////
      // I have to create a new connection for the requested connection parameter:
      iRet = GetConnection(profile, sHost, sPort, &connectionAttr);
      if (TML_SUCCESS == iRet){
        /////////////////////////////////////////////////////////////////////
        // I have to create a sender for the requested connection parameter:
        coreSenderAttr = new TMLCoreSender(m_ctx, m_log);
        iRet = coreSenderAttr->getInitError();
        if (TML_SUCCESS == iRet){
          ////////////////////////////////////////////////////////////
          // Register callback for the case of a lost of connection:
          m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "GetConnectionElement", "Vortex CMD", "vortex_connection_set_on_close_full");
          m_bDeregistered = false;
          vortex_connection_set_on_close_full (connectionAttr, connectionCloseHandler, &m_internalConnectionCloseHandlerMethod);
          ///////////////////////////////////////////////////////////////////////
          // Remember the connection list element:
          connectionObj = new tmlConnectionObj(m_log);
          connectionObj->setConnectionObj(profile, sHost, sPort, coreSenderAttr, channelPool, connectionAttr);
          connectionObj->lock(true, true);
          AddConnectionElement(connectionObj, true);
        }
      }
    }
    else{
      connectionObj->getSender(&coreSenderAttr);
    }
  }
  if (TML_SUCCESS == iRet){
    connectionObj->setRawViaVortexPayloadFeeder(bRawViaVortexPayloadFeeder);
    ////////////////////////////////////////////////////////////
    // Possible new logging value:
    iRet = coreSenderAttr->TMLCoreSender_Set_Logging_Value(m_iLogValue);

    *pConnectionObj = connectionObj;
  }
  return iRet;
}


/**
 * @brief   Callback to test window size
 */
int my_next_frame_size(VortexChannel  *channel, int next_seq_no, int message_size, int max_seq_no, axlPointer user_data){
  if (message_size > 0x4000)
    return 0x4000;
  else
    return message_size;
}


/**
 * @brief   Set the window size for the following outgoing data.
 */
int tmlSingleCall::setChannelWindowSize(tmlConnectionObj* connectionObj, int iWindowSize){
  int iRet = TML_SUCCESS;

  VortexChannel* channel;

  connectionObj->getChannel(&channel);

  if (NULL != m_log){
    m_log->log (TML_LOG_VORTEX_CMD, "tmlSingleCall", "SetChannelWindowSize", "Vortex CMD", "vortex_channel_set_window_size");
  }
 
  vortex_channel_set_window_size(channel, iWindowSize);

  return iRet;
}


/**
 * @brief    Send a synchron Message.
 */
int tmlSingleCall::sender_SendSyncMessage(const char* profile,
    const char* sHost, const char* sPort, int iWindowSize,
    TML_COMMAND_HANDLE tmlhandle, unsigned int iTimeout,
    VortexMutex* mutexCriticalSection, bool bRemoveMarkedObjs)
{
  TML_INT32  iRet = TML_SUCCESS;
  int  iMsgNo;
  try{

  /////////////////////////////////////////////////
  // First I reset the error code and error message:
  iRet = tml_Cmd_Header_SetError(tmlhandle, TML_SUCCESS);
  if (TML_SUCCESS == iRet){
    tml_Cmd_Header_SetErrorMessage(tmlhandle, (char*)"", 0);
  }

  tmlConnectionObj* connectionObj = NULL;
  /////////////////////////////////////////////////////////////////
  // The tmlConnectionObj containing the TMLCoreSender:

  if (TML_SUCCESS == iRet){
    iRet = GetConnectionElement(profile, sHost, sPort, &connectionObj, false, bRemoveMarkedObjs, iTimeout);
  }

  ///////////////////////////////////////////////////////////////////////////////////////////////////
  // Now it's time to leave a critical section if set / look at tmlcollectCall.loadBalancedSendSyncMessage():
  if (NULL != mutexCriticalSection){
    m_log->log (TML_LOG_VORTEX_MUTEX, "tmlSingleCall", "sender_SendSyncMessage",  "Vortex CMD", "vortex_mutex_unlock");
    intern_mutex_unlock (mutexCriticalSection, m_log, "sender_SendSyncMessage");
  }


  TML_COMMAND_ID_TYPE iCmd;

  if (TML_SUCCESS == iRet){
    ////////////////////////////////////////////////////////////
    // Check CMD for valid range (if CMD is no an Internal CMD):
    iRet = tml_Cmd_Header_GetCommand(tmlhandle, &iCmd);
    if (TML_SUCCESS == iRet){
      ////////////////////////////////////////////////////////////
      // Set the command state:
      iRet = tml_Cmd_Header_SetState(tmlhandle, TMLCOM_CSTATE_CREATED);
      if (TML_SUCCESS == iRet){
        ////////////////////////////////////////////////////////////
        // Set the command mode:
        iRet = tml_Cmd_Header_SetMode(tmlhandle, TMLCOM_MODE_SYNC);
        if (TML_SUCCESS == iRet){
          ////////////////////////////////////////////////////////////
          // Possible new window size:
          iRet = setChannelWindowSize(connectionObj, iWindowSize);
          if (TML_SUCCESS == iRet){
            if (TML_SUCCESS == iRet){
              ////////////////////////////////
              // send the message:
              TMLCoreSender* coreSender;
              connectionObj->getSender(&coreSender);
              iRet = coreSender->TMLCoreSender_SendMessage(connectionObj, tmlhandle, iTimeout, &iMsgNo);
            }
          }
        }
      }
    }
  }
  if (TML_SUCCESS != iRet){
    if (NULL != connectionObj){
      connectionObj->unlock();
    }
  }
  //////////////////////////////////////////////////////////////
  // Set XML error ID & Message
  ((tmlObjWrapper*)tmlhandle)->tmlObjWrapper_Header_setLogicalError((int)iRet,DEFAULT_ERROR_MSG);
  }
  catch (...){
    tml_logI_A(TML_LOG_EVENT, "tmlSingleCall", "sender_SendSyncMessage", "EXCEPTION / DebugVal", 0);
  }
  return (int)iRet;
}


/**
 * @brief    Send an asynchron Message.
 */
int tmlSingleCall::sender_SendAsyncMessage(const char* profile, const char* sHost, const char* sPort, int iWindowSize, TML_COMMAND_HANDLE tmlhandle, unsigned int iTimeout, int iMode, bool bLockCritical, bool bRawViaVortexPayloadFeeder)
{
  TML_INT32  iRet = TML_SUCCESS;
  try{

  /////////////////////////////////////////////////
  // First I reset the error code and error message:
  iRet = tml_Cmd_Header_SetError(tmlhandle, TML_SUCCESS);
  if (TML_SUCCESS == iRet){
    tml_Cmd_Header_SetErrorMessage(tmlhandle, (char*)"", 0);
  }

  tmlConnectionObj* connectionObj = NULL;
  /////////////////////////////////////////////////////////////////
  // The tmlConnectionObj containing the TMLCoreSender:
  
  //bLockCritical not used anymore:

  if (TML_SUCCESS == iRet){
    iRet = GetConnectionElement(profile, sHost, sPort, &connectionObj, bRawViaVortexPayloadFeeder, true, iTimeout);
  }

  TML_COMMAND_ID_TYPE iCmd;
  if (TML_SUCCESS == iRet){
    ////////////////////////////////////////////////////////////
    // Check CMD for valid range (if CMD is no an Internal CMD):
    iRet = tml_Cmd_Header_GetCommand(tmlhandle, &iCmd);
    if (TML_SUCCESS == iRet){
      ////////////////////////////////////////////////////////////
      // Set the command state:
      iRet = tml_Cmd_Header_SetState(tmlhandle, TMLCOM_CSTATE_CREATED);
      if (TML_SUCCESS == iRet){
        ////////////////////////////////////////////////////////////
        // Set the command mode:
        iRet = tml_Cmd_Header_SetMode(tmlhandle, iMode);
        if (TML_SUCCESS == iRet){
          ////////////////////////////////////////////////////////////
          // Possible new window size:
          iRet = setChannelWindowSize(connectionObj, iWindowSize);
          if (TML_SUCCESS == iRet){
            if (TML_SUCCESS == iRet){
              ////////////////////////////////
              // send the message:
              TMLCoreSender* coreSender;
              connectionObj->getSender(&coreSender);
              iRet = coreSender->TMLCoreSender_SendAsyncMessage(connectionObj, tmlhandle, iTimeout);
            }
          }
        }
      }
    }
  }
  if (TML_SUCCESS != iRet){
    if (NULL != connectionObj){
      connectionObj->unlock();
    }
  }

  ////////////////////////////////////////////////////////////////////////////////////////
  // Set XML error ID & Message will be done in tmlCoreSender / AsyncHandlingThreadMethod
  // and is not allowed here because a  Registered CommandReady entry may have freed the memory:
  //((tmlObjWrapper*)tmlhandle)->tmlObjWrapper_Header_setLogicalError(iRet,DEFAULT_ERROR_MSG);
  }
  catch (...){
    tml_logI_A(TML_LOG_EVENT, "tmlSingleCall", "sender_SendAsyncMessage", "EXCEPTION / DebugVal", 0);
  }
  return (int)iRet;
}


/**
 * @brief    Create the mutex that protect critial section about communication data
 */
axl_bool tmlSingleCall::createCriticalSectionObject(int iLogMask, VortexMutex* mutex, const char* sClass, const char* sMethod, const char* sFormatLog, const char* sLog)
{
  m_log->log (iLogMask, sClass, sMethod, sFormatLog, sLog);
  axl_bool bSuccess = intern_mutex_create (mutex);
  return bSuccess;
}


/**
 * @brief    Destroy the mutex that protect critial section about communication data
 */
axl_bool tmlSingleCall::destroyCriticalSectionObject(int iLogMask, VortexMutex* mutex, const char* sClass, const char* sMethod, const char* sFormatLog, const char* sLog)
{
  m_log->log (iLogMask, sClass, sMethod, sFormatLog, sLog);
  axl_bool bSuccess = intern_mutex_destroy (mutex, (char*)"tmlSingleCall::destroyCriticalSectionObject");
  return bSuccess;
}


/**
 * @brief    Enter a critical Section
 */
void tmlSingleCall::enterCriticalSection(int iLogMask, VortexMutex* mutex, int* iLockCount, const char* sClass, const char* sMethod, const char* sFormatLog, const char* sLog)
{
  m_log->log (iLogMask, sClass, sMethod, sFormatLog, sLog);
  intern_mutex_lock (mutex, m_log, sMethod);
  return;
}


/**
 * @brief    Leave a critical Section
 */
void tmlSingleCall::leaveCriticalSection(int iLogMask, VortexMutex* mutex, int* iLockCount, const char* sClass, const char* sMethod, const char* sFormatLog, const char* sLog)
{
  m_log->log (iLogMask, sClass, sMethod, sFormatLog, sLog);
  intern_mutex_unlock (mutex, m_log, sMethod);
  return;
}


/**
 * @brief   Set the log- file index for explicit logs with closing the file
 */
void tmlSingleCall::setLogFileIndex(int iLogFileIndex){
  m_iLogFileIndex = iLogFileIndex;
}
