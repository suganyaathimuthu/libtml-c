
#include <tmlCore.h>
#include <string>
#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <sidex.h>

#include <logValues.h>

pthread_mutexattr_t  attr;
typedef pthread_mutex_t      HANDLE;
HANDLE m_mutex;


static TML_CORE_HANDLE m_CoreSenderHandle;
static TML_CORE_HANDLE m_CoreListenerHandle;
const std::string CORE_LISTENER_IP("0.0.0.0");
const std::string CORE_LISTENER_PORT("51212");
const std::string PEER_IP("127.0.0.1");
const int COMMAND_ID = 2000;
const std::string pathToFiles("/tmp/tmltest");
const std::string profileName("TEST_PROFILE");

void mutex_create(HANDLE* mutex_def){
#ifdef _WIN32
  (*mutex_def) = CreateMutex (NULL, false, NULL);
#else
  pthread_mutex_init(mutex_def, &attr);
#endif
}

void mutex_destroy(HANDLE* mutex_def)
{
#ifdef _WIN32
  CloseHandle (*mutex_def);
  (*mutex_def) = NULL;
#else
pthread_mutex_destroy(mutex_def);
#endif
  return;
}

void mutex_lock(HANDLE* mutex_def)
{
  if (mutex_def == NULL)
    return;
#ifdef _WIN32
  WaitForSingleObject (*mutex_def, INFINITE);
#else
pthread_mutex_lock(mutex_def);
#endif
  return;
}

void mutex_unlock(HANDLE* mutex_def)
{
  if (mutex_def == NULL)
    return;
#ifdef _WIN32
  ReleaseMutex (*mutex_def);
#else
pthread_mutex_unlock(mutex_def);
#endif
  return;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sender related code.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read file into std::string
int readFromFiletoStr(const std::string &fileName, std::string *fileContents)
{
    // Firewall check
    if (NULL == fileContents)
    {
        return -1;
    }

    std::ifstream t(fileName.c_str());
    std::stringstream buffer;
    buffer << t.rdbuf();
    fileContents->assign(buffer.str());
    return 0;
}

void onDisconnectCallback(TML_CONNECTION_HANDLE connectionHandle, TML_POINTER cbData)
{
    SIDEX_TCHAR* pAddress = NULL;
    std::string peerName;
    tml_Connection_Get_Address(connectionHandle, &pAddress);
    peerName.assign(pAddress);

    //tml_Connection_Close(&connectionHandle);
    std::cout << "Disconnected from " << peerName;

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize sender core
bool initializeSender()
{
    // Open libTML Core.
    TML_INT32 iErr = tml_Core_Open(&m_CoreSenderHandle, 0);
    if (TML_SUCCESS != iErr)
    {
        return false;
    }
    tml_Core_Set_LoggingValue(m_CoreSenderHandle, TML_LOG_VORTEX_CMD | TML_LOG_CORE_IO | TML_LOG_CORE_API | TML_LOG_MULTI_SYNC_CMDS |
        TML_LOG_VORTEX_FRAMES | TML_LOG_VORTEX_CH_POOL | TML_LOG_VORTEX_MUTEX | TML_LOG_INTERNAL_DISPATCH |
        TML_LOG_STREAM_HANDLING | TML_LOG_EVENT);

    tml_Core_Set_OnDisconnect(m_CoreSenderHandle, onDisconnectCallback, NULL);
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Open Connection to peer
bool connectToPeer(const std::string &peerName, const std::string &peerPort)
{
    mutex_lock(&m_mutex);
    TML_INT32 iErr = TML_SUCCESS;
    std::string peer(peerName);
    peer.append(":");
    peer.append(peerPort);

    // Connection is not created already. Create connection handle.
    TML_CONNECTION_HANDLE connHandle = TML_HANDLE_TYPE_NULL;

    // Get connection handle for the peer
    // If the conenction is already available, return immediately.
    iErr = tml_Core_Get_ConnectionByAddress(m_CoreSenderHandle, (char *)peer.c_str(), &connHandle);
    if (TML_SUCCESS == iErr)
    {
        TML_BOOL isConnected = TML_FALSE;
        TML_INT32 iErr = tml_Connection_Validate(connHandle, TML_TRUE, &isConnected);
        if ((TML_SUCCESS == iErr) && (TML_TRUE == isConnected))
        {
           printf ("RRRRRRRCONNECTED");
            mutex_unlock(&m_mutex);
            return true;
        }
    }

    // If it returned a handle that was not connected, clean up and try again.
    if (TML_HANDLE_TYPE_NULL != connHandle)
    {
        tml_Connection_Close(&connHandle);
    }

    iErr = tml_Core_Connect(m_CoreSenderHandle, peer.c_str(), &connHandle);

    if (TML_SUCCESS != iErr)
    {
            mutex_unlock(&m_mutex);
        return false;
    }

/*    if (withTls)
    {
        TML_BOOL bEncrypted = TML_FALSE;
        iErr = tml_Tls_Connection_StartNegotiation(m_CoreSenderHandle, TML_FALSE, &bEncrypted);
        if (TML_SUCCESS != iErr)
        {
            return RET_ERR_CODE(iErr);
    }
*/
            mutex_unlock(&m_mutex);
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create cmdMsg for sending file
bool createFileTxCmdForProfile(const std::string &filePath, TML_COMMAND_HANDLE *cmdMsg)
{
    TML_INT32 iErr = TML_SUCCESS;

    if (NULL == cmdMsg)
    {
        // Firewall checks. SHould not be seen ussually.
        return false;
    }

    // Create Command
    iErr = tml_Cmd_Create(cmdMsg);
    if (TML_SUCCESS != iErr)
    {
        return false;
    }

    iErr = tml_Cmd_Header_SetCommand(*cmdMsg, COMMAND_ID);
    if (TML_SUCCESS != iErr)
    {
        tml_Cmd_Free(cmdMsg);
        return false;
    }

    SIDEX_HANDLE sHandle = SIDEX_HANDLE_TYPE_NULL;
    iErr = tml_Cmd_Acquire_Sidex_Handle(*cmdMsg, &sHandle);
    if (TML_SUCCESS != iErr)
    {
        tml_Cmd_Free(cmdMsg);
        return false;
    }

    // Write out data
    std::string fileContents;
    // Commenting out support to delete a file.
    readFromFiletoStr(filePath, &fileContents);
    sidex_Binary_Write(sHandle, (char *)"PARAMS", (char *)"FILE_CONTENT", (const unsigned char *)fileContents.c_str(), fileContents.size());
    std::cout << "createFileTxCmdForProfile: Wrote File Contents with size" << fileContents.size() << std::endl;
    // Release handle
    iErr = tml_Cmd_Release_Sidex_Handle(*cmdMsg);

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send File
bool sendFileSync(const std::string peerName, const std::string peerPort,
    const std::string &filepath, int timeout)
{
    std::string connStr(peerName);
    connStr.append(":");
    connStr.append(peerPort);

    TML_CONNECTION_HANDLE connHandle = TML_HANDLE_TYPE_NULL;

    // Get connection handle for the peer
    // If the conenction is already available, return immediately.
    TML_INT32 iErr = tml_Core_Get_ConnectionByAddress(m_CoreSenderHandle, (char *)connStr.c_str(), &connHandle);
    if (TML_SUCCESS == iErr)
    {
        TML_BOOL isConnected = TML_FALSE;
        TML_INT32 iErr = tml_Connection_Validate(connHandle, TML_TRUE, &isConnected);
        if ((TML_SUCCESS != iErr) || (TML_TRUE != isConnected))
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    // Valid connection. Create command and send over that.
    TML_COMMAND_HANDLE cmdMsg = TML_HANDLE_TYPE_NULL;
    bool ret = createFileTxCmdForProfile(filepath, &cmdMsg);
    if (!ret)
    {
        return false;
    }

    // Send the command over the connection.
    iErr = tml_Connection_SendSync(connHandle, profileName.c_str(), cmdMsg, timeout);
    if (TML_SUCCESS != iErr)
    {
        if (TML_HANDLE_TYPE_NULL != cmdMsg)
            tml_Cmd_Free(&cmdMsg);
        return false;
    }

    // Free the command used for the message.
    if (TML_HANDLE_TYPE_NULL != cmdMsg)
        tml_Cmd_Free(&cmdMsg);

    return true;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LISTENER FUNCTIONS
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool initializeListener()
{
    // Initialize listener core.
    TML_INT32 iErr = TML_SUCCESS;
    iErr = tml_Core_Open(&m_CoreListenerHandle, 0);
    if (TML_SUCCESS != iErr)
    {
        return false;
    }

    // Declare the IP of the listener core interface.
    iErr = tml_Core_Set_ListenerIP(m_CoreListenerHandle, (char *)CORE_LISTENER_IP.c_str());
    if (TML_SUCCESS != iErr)
    {
        tml_Core_Close(&m_CoreListenerHandle);
        m_CoreListenerHandle = TML_HANDLE_TYPE_NULL;
        return false;
    }

    // Set the port on which the application would be listening for connections.
    iErr = tml_Core_Set_ListenerPort(m_CoreListenerHandle, (char *)CORE_LISTENER_PORT.c_str());
    if (TML_SUCCESS != iErr)
    {
        tml_Core_Close(&m_CoreListenerHandle);
        m_CoreListenerHandle = TML_HANDLE_TYPE_NULL;
        return false;
    }

    // Initialize TLS support
    // Allow to configure if the provided tml core will accept TLS incoming connections
//    TML_BOOL bAccepted;
 //   iErr = tml_Tls_Core_AcceptNegotiation(m_CoreListenerHandle,
  //      check_and_accept_tls_request, certificate_file_location, private_key_file_location, &bAccepted);
    // Ignore TLS errors for now. - TBD

    // Successfully set up listener. Profile registrations can now be done by the application
    return true;
}

void cmdCallback(TML_COMMAND_HANDLE cmdMsg, TML_POINTER data)
{
    TML_INT32 iErr = TML_SUCCESS;
    SIDEX_HANDLE sHandle = SIDEX_HANDLE_TYPE_NULL;
    TML_COMMAND_ID_TYPE cmdId = 0;


    iErr = tml_Cmd_Acquire_Sidex_Handle(cmdMsg, &sHandle);
    if (TML_SUCCESS != iErr)
    {
       // Error out.
        return;
    }

    // Get the command message callback value.
    iErr = tml_Cmd_Header_GetCommand(cmdMsg, &cmdId);
    if (TML_SUCCESS != iErr)
    {
        // Error out
        tml_Cmd_Release_Sidex_Handle(sHandle);
        return;
    }
    // Get the parameters from the command
    // If this is not a delete request, get file content.
    std::string strReqToken; // empty for now.
    std::string strFileContent;
    unsigned char *fileContent = NULL;
    int fcLength = -1;
    iErr = sidex_Binary_Read(sHandle, (char *)"PARAMS", (char *)"FILE_CONTENT", &fileContent, &fcLength);

    if ((SIDEX_SUCCESS == iErr) && (NULL != fileContent))
    {
        std::cout << "FC LENGTH" << fcLength << std::endl;
        strFileContent.assign((char *)fileContent);
    }
    sidex_Free_Binary_ReadString(fileContent);

    // Release handle
    tml_Cmd_Release_Sidex_Handle(cmdMsg);
    // Ignoring file content and removed Application logic to write file on disk. 
    // This will act as a SINK
    std::cout << "Received file of length " << strFileContent.length() << std::endl;
}

bool registerProfiles(const std::string &profileName, const int commandID)
{
    // Register profile on the listener handle
    TML_INT32 iErr = tml_Profile_Register(m_CoreListenerHandle, profileName.c_str());
    if (TML_SUCCESS != iErr)
    {
        return false;
    }

    // Register command dispatch callback for the profile.
    iErr = tml_Profile_Register_Cmd(m_CoreListenerHandle, profileName.c_str(), (TML_COMMAND_ID_TYPE)commandID,
        cmdCallback, TML_HANDLE_TYPE_NULL);
    if (TML_SUCCESS != iErr)
    {
        return false;
    }

    // Enable Listener
    iErr = tml_Core_Set_ListenerEnabled(m_CoreListenerHandle, TML_TRUE);
    if ((TML_SUCCESS != iErr) && (TML_ERR_LISTENER_ALREADY_STARTED != iErr))
    {
        return false;
    }

    return true;
}


/////////////////////////////////////////////////////////////////////////////////////////
/// MAIN FUNCTIONS
/////////////////////////////////////////////////////////////////////////////////////////
void *threadMain(void *args)
{
    while (1)
    {
        // Connect to Peer
        bool ret = connectToPeer(PEER_IP, CORE_LISTENER_PORT);
        if (!ret)
        {
            std::cout << "Unable to connect to peer" << std::endl;
        }
        else
        {
            // Path to file
            std::string filePath(pathToFiles);
            filePath.append("/");
            int *argPtr = (int *)args;
            int num = *argPtr;
            std::string fileName = std::to_string(num);
            filePath.append(fileName);
            // Send file 
            ret = sendFileSync(PEER_IP, CORE_LISTENER_PORT, filePath, 10000);
            if (!ret)
            {
                std::cout << "Failed to send file" << std::endl;
            }
        }
        // Sleep for 3 seconds. File sent every three seconds.
        usleep(3 * 1000 * 1000);
    }
            
        
}
void sendMultiThreaded()
{
    mutex_create(&m_mutex);
    // Initialize Sender
    bool ret = initializeSender();
    if (!ret)
    {
        std::cout << "Could not initialize sender" << std::endl;
        return;
    }

    // Spawn threads to send data
    pthread_t thread_id[8];
    int thread_args[8];

    ret = connectToPeer(PEER_IP, CORE_LISTENER_PORT);
    if (!ret)
        {
            std::cout << "Unable to connect to peer" << std::endl;
        }
    for(int i=0; i < 8; i++)
    {
        thread_args[i] = i;
        pthread_create(&thread_id[i], NULL, threadMain, &thread_args[i]);
    }

    for(int j=0; j < 8; j++)
    {
        pthread_join(thread_id[j], NULL);
    }
    mutex_destroy(&m_mutex);
}

void multiListener()
{
    // Initialize Listener
    bool ret = initializeListener();
    if (!ret)
    {
        std::cout << "Count not initialize listener" << std::endl;
        return;
    }

    // Register Profile
    ret = registerProfiles(profileName, COMMAND_ID);
    if (!ret)
    {
        std::cout << "Failed to register profile" << std::endl;
        return;
    }

    while (1)
    {
         usleep(10 * 1000 * 1000);
         std::cout << "Sleeping for callbacks" << std::endl;
    }    
}
