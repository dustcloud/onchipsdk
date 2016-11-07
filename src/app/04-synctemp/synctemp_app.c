/*
Copyright (c) 2014, Dust Networks.  All rights reserved.
*/

// C includes
#include "string.h"
#include "stdio.h"

// SDK includes
#include "dn_common.h"
#include "dnm_local.h"
#include "dn_time.h"
#include "dn_system.h"
#include "dn_api_param.h"
#include "dn_fs.h"
#include "well_known_ports.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_exe_hdr.h"
#include "Ver.h"

// project includes
#include "app_task_cfg.h"

//=========================== defines =========================================
#define SYNCTEMP_UDP_PORT    WKP_USER_3

#define APP_CONFIG_FILENAME  "2syncBlk.cfg"
#define DEFAULT_RPT_PERIOD   3600             // seconds
#define APP_MAGIC_NUMBER     0x73797470       // 'sytp'
#define MAX_RX_PAYLOAD       32

// command identifiers

#define CMDID_GET            0x01
#define CMDID_SET            0x02
#define CMDID_RESPONSE       0x03
#define CMDID_TEMP_DATA      0x04

// response codes

#define APP_RC_OK            0x00
#define APP_RC_ERR           0x01

//=== packet formats

PACKED_START

typedef struct{
   INT32U magic_number;      // APP_MAGIC_NUMBER, big endian
   INT8U  cmdId;             // command identifier
   INT8U  payload[];         // payload
} app_payload_ht;

PACKED_STOP

//=== contents of configuration file

typedef struct{
   INT32U reportPeriod;      // report period, in seconds
} app_cfg_t;

//=========================== variables =======================================

typedef struct {
   // tasks
   OS_STK               sampleTaskStack[TASK_APP_SAMPLE_STK_SIZE];
   OS_TMR*              sampleTaskStackTimer;
   OS_EVENT*            sampleTaskStackTimerSem;      ///< posted when sampleTaskStackTimer expires
   OS_STK               processRxTaskStack[TASK_APP_PROCESSRX_STK_SIZE];
   // network
   OS_EVENT*            joinedSem;                    ///< posted when stack has joined
   INT8U                isJoined;                     ///< 0x01 if the mote has joined the network, 0x00 otherwise
   // configuration
   OS_EVENT*            dataLock;                     ///< locks shared resources
   app_cfg_t            app_cfg;                      ///< structure containing the application's configuration
   INT64S               nextReportUTCSec;             ///< time next report is scheduled
   // rx packet
   OS_EVENT*            rxPkLock;                     ///< locks received packet information
   dn_ipv6_addr_t       rxPkSrc;                      ///< IPv6 address of the sender
   INT8U                rxPkPayload[MAX_RX_PAYLOAD];  ///< received payload
   INT8U                rxPkPayloadLen;               ///< number of bytes in the received payload
   OS_EVENT*            rxPkReady;                    ///< posted when an rx packet is ready for consumption
} synctemp_app_vars_t;

synctemp_app_vars_t synctemp_v;

//=========================== prototypes ======================================

//=== CLI handlers
dn_error_t cli_getPeriod(const char* arg, INT32U len);
dn_error_t cli_setPeriod(const char* arg, INT32U len);

//=== tasks
static void sampleTask(void* unused);
void   sampleTaskStackTimer_cb(void* pTimer, void *pArgs);
static void processRxTask(void* unused);

//=== helpers

// network
dn_error_t rxNotif(dn_api_loc_notif_received_t* rxFrame, INT8U length);

// formatting
void printf_buffer(INT8U* buf, INT8U len);

// configuration
void setPeriod(INT32U reportPeriod);

// configuration file
void loadConfigFile();
void syncToConfigFile();

// lock
void lockData();
void unlockData();
void lockRxPk();
void unlockRxPk();

//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_getPeriod,      "getperiod",    "getperiod",              DN_CLI_ACCESS_LOGIN},
   {&cli_setPeriod,      "setperiod",    "setperiod <period>",     DN_CLI_ACCESS_LOGIN},
   {NULL,                 NULL,          NULL,                     DN_CLI_ACCESS_NONE},
};

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   INT8U           osErr;
   
   // clear module variables
   memset(&synctemp_v,0,sizeof(synctemp_app_vars_t));
   
   // the mote is not joined
   synctemp_v.isJoined = 0x00;
   
   // semaphores
   
   synctemp_v.sampleTaskStackTimerSem = OSSemCreate(0);    // NOT expired by default
   ASSERT(synctemp_v.sampleTaskStackTimerSem!=NULL);
   
   synctemp_v.joinedSem = OSSemCreate(0);                  // NOT joined by default
   ASSERT(synctemp_v.joinedSem!=NULL);
   
   synctemp_v.dataLock = OSSemCreate(1);                   // data unlocked by default
   ASSERT(synctemp_v.dataLock!=NULL);
   
   synctemp_v.rxPkLock = OSSemCreate(1);                    // rx packet unlocked by default
   ASSERT(synctemp_v.rxPkLock!=NULL);
   
   synctemp_v.rxPkReady = OSSemCreate(0);                   // rx packet NOT ready by default
   ASSERT(synctemp_v.rxPkReady!=NULL);
   
   //===== register a callback to receive packets
   
   dnm_loc_registerRxNotifCallback(rxNotif);
   
   //===== initialize helper tasks
   
   cli_task_init(
      "synctemp",                    // appName
      cliCmdDefs                     // cliCmds
   );
   
   loc_task_init(
      JOIN_YES,                      // fJoin
      NETID_NONE,                    // netId
      SYNCTEMP_UDP_PORT,             // udpPort
      synctemp_v.joinedSem,          // joinedSem
      BANDWIDTH_NONE,                // bandwidth  BANDWIDTH_NONE = use default BW (set at manager)
      NULL                           // serviceSem
   );
   
   //===== initialize tasks (and timer)
   
   // sampleTask
   osErr = OSTaskCreateExt(
      sampleTask,
      (void *) 0,
      (OS_STK*) (&synctemp_v.sampleTaskStack[TASK_APP_SAMPLE_STK_SIZE - 1]),
      TASK_APP_SAMPLE_PRIORITY,
      TASK_APP_SAMPLE_PRIORITY,
      (OS_STK*) synctemp_v.sampleTaskStack,
      TASK_APP_SAMPLE_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_SAMPLE_PRIORITY, (INT8U*)TASK_APP_SAMPLE_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   // sampleTask Timer
   synctemp_v.sampleTaskStackTimer = OSTmrCreate(
      DEFAULT_RPT_PERIOD,                        // dly
      DEFAULT_RPT_PERIOD,                        // period
      OS_TMR_OPT_ONE_SHOT,                       // opt
      (OS_TMR_CALLBACK)&sampleTaskStackTimer_cb, // callback
      NULL,                                      // callback_arg
      NULL,                                      // pname
      &osErr                                     // perr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   // processRxTask
   osErr = OSTaskCreateExt(
      processRxTask,
      (void *) 0,
      (OS_STK*) (&synctemp_v.processRxTaskStack[TASK_APP_PROCESSRX_STK_SIZE - 1]),
      TASK_APP_PROCESSRX_PRIORITY,
      TASK_APP_PROCESSRX_PRIORITY,
      (OS_STK*) synctemp_v.processRxTaskStack,
      TASK_APP_PROCESSRX_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_PROCESSRX_PRIORITY, (INT8U*)TASK_APP_PROCESSRX_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

//=========================== CLI handlers ====================================

dn_error_t cli_getPeriod(const char* arg, INT32U len) {
   INT8U           isJoined;
   INT64S          nextReportUTCSec;
   dn_time_asn_t   currentASN;
   dn_time_utc_t   currentUTC;
   INT32U          timeToNext;
   
   // print current configuration
   dnm_ucli_printf(
      "Current configuration: reportPeriod = %u seconds\r\n",
      synctemp_v.app_cfg.reportPeriod
   );
   
   // retrieve what I need
   lockData();
   isJoined             = synctemp_v.isJoined;
   nextReportUTCSec     = synctemp_v.nextReportUTCSec;
   unlockData();
   
   // print next transmission time
   if (isJoined==0x01) {
      
      // get current time
      dn_getNetworkTime(
         &currentASN,
         &currentUTC
      );
      
      // calculate timeToNext
      timeToNext = (INT32U)nextReportUTCSec - (INT32U)currentUTC.sec;
      
      // print timeToNext
      dnm_ucli_printf(
         "Next trigger in %d seconds\r\n",
         timeToNext
      );
   }
   
   return DN_ERR_NONE;
}

dn_error_t cli_setPeriod(const char* arg, INT32U len) {
   INT32U     newReportPeriod;
   int        l;
   
   //--- param 0: a
   l = sscanf(arg, "%u", &newReportPeriod);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   
   // set the period
   setPeriod(newReportPeriod);
  
   return DN_ERR_NONE;
}

//=========================== tasks ===========================================

static void sampleTask(void* unused) {
   INT8U                osErr;
   INT8U                dnErr;
   dn_time_asn_t        currentASN;
   dn_time_utc_t        currentUTC;
   INT8U                numBytesRead;
   INT16S               temperature;
   INT32U               reportRateMs;
   INT64U               timeToWaitMs;
   INT8U                pkBuf[sizeof(loc_sendtoNW_t) + sizeof(app_payload_ht) + sizeof(INT16U)];
   loc_sendtoNW_t*      pkToSend;
   app_payload_ht*      payloadToSend;
   INT8U                rc;
   
   // wait a bit before sampling the first time
   OSTimeDly(1000);
   
   // configure reporting period
   loadConfigFile();
   
   // open temperature sensor
   dnErr = dn_open(
      DN_TEMP_DEV_ID,             // device
      NULL,                       // args
      0                           // argLen 
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // wait for mote to join
   OSSemPend(synctemp_v.joinedSem, 0, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   // the mote is joined
   lockData();
   synctemp_v.isJoined = 0x01;
   unlockData();
   
   while (1) { // this is a task, it executes forever
      
      //=== wait
      
      // get the current time
      dn_getNetworkTime(
         &currentASN,
         &currentUTC
      );
      
      // convert reporting period into ms
      lockData();
      reportRateMs = 1000*synctemp_v.app_cfg.reportPeriod;
      unlockData();
      
      // figure out how long to wait
      timeToWaitMs = (INT64U)(reportRateMs - (((currentUTC.sec * 1000) + (currentUTC.usec / 1000)) % reportRateMs));
      
      // update when the next report will be
      lockData();
      synctemp_v.nextReportUTCSec = currentUTC.sec + timeToWaitMs/1000;
      unlockData();
      
      // start the timer
      synctemp_v.sampleTaskStackTimer->OSTmrDly = timeToWaitMs;
      OSTmrStart(synctemp_v.sampleTaskStackTimer, &osErr);
      ASSERT (osErr == OS_ERR_NONE);
      
      // wait for timer to expire
      OSSemPend(
         synctemp_v.sampleTaskStackTimerSem,     // pevent
         0,                                      // timeout
         &osErr                                  // perr
      );
      ASSERT (osErr == OS_ERR_NONE);
      
      //=== read temperature
      
      // read temperature
      numBytesRead = dn_read(
         DN_TEMP_DEV_ID ,         // device
         (char*)&temperature,     // buf
         sizeof(temperature)      // bufSize 
      );
      ASSERT(numBytesRead==sizeof(temperature));
      
      dnm_ucli_printf("sending temperature=%d 1/100 C (next in %d s, period=%d)\r\n",
         temperature,
         timeToWaitMs/1000,
         synctemp_v.app_cfg.reportPeriod
      );
      
      //=== send packet
      
      // fill in packet metadata
      pkToSend = (loc_sendtoNW_t*)pkBuf;
      pkToSend->locSendTo.socketId          = loc_getSocketId();
      pkToSend->locSendTo.destAddr          = DN_MGR_IPV6_MULTICAST_ADDR;
      pkToSend->locSendTo.destPort          = SYNCTEMP_UDP_PORT;
      pkToSend->locSendTo.serviceType       = DN_API_SERVICE_TYPE_BW;   
      pkToSend->locSendTo.priority          = DN_API_PRIORITY_MED;   
      pkToSend->locSendTo.packetId          = 0xFFFF;
      
      // fill in packet payload
      payloadToSend = (app_payload_ht*)pkToSend->locSendTo.payload;
      payloadToSend->magic_number           = htonl(APP_MAGIC_NUMBER);
      payloadToSend->cmdId                  = CMDID_TEMP_DATA;
      temperature                           = htons(temperature);
      memcpy(payloadToSend->payload,&temperature,sizeof(INT16S));
      
      // send packet
      dnErr = dnm_loc_sendtoCmd(
         pkToSend,
         sizeof(app_payload_ht)+sizeof(INT16S),
         &rc
      );
      ASSERT (dnErr == DN_ERR_NONE);
      if (rc!=DN_ERR_NONE){
         dnm_ucli_printf("ERROR sending data (RC=%d)\r\n", rc);
      }
      
      //=== extra delay
      // we add this delay to avoid for the node to send two packets in a row
      // because of a rounding error
      
      OSTimeDly(1000);
   }
}

void sampleTaskStackTimer_cb(void* pTimer, void *pArgs) {
   INT8U  osErr;
   
   // post the semaphore
   osErr = OSSemPost(synctemp_v.sampleTaskStackTimerSem);
   ASSERT(osErr == OS_ERR_NONE);
}

static void processRxTask(void* unused) {
   INT8U           osErr;
   app_payload_ht* appHdr;
   
   while (1) { // this is a task, it executes forever
      
      // wait for the rx packet to be ready
      OSSemPend(synctemp_v.rxPkReady, 0, &osErr);
      ASSERT(osErr==OS_ERR_NONE);
      
      // print received packet
      /*
      dnm_ucli_printf("rx packet\r\n");
      dnm_ucli_printf("- from:     ");
      printf_buffer(synctemp_v.rxPkSrc.byte,sizeof(dn_ipv6_addr_t));
      dnm_ucli_printf("\r\n");
      dnm_ucli_printf("- payload:  ");
      printf_buffer(synctemp_v.rxPkPayload,synctemp_v.rxPkPayloadLen);
      dnm_ucli_printf(" (%d bytes)\r\n", synctemp_v.rxPkPayloadLen);
      */
      
      // trick to be able to use "break"
      do {
         
         // parse header
         appHdr = (app_payload_ht*)synctemp_v.rxPkPayload;
         
         // filter magic_number
         if (appHdr->magic_number!=htonl(APP_MAGIC_NUMBER)) {
            // wrong magic number
            break;
         }
         
         switch(appHdr->cmdId) {
            case CMDID_GET:
               // filter length
               if (synctemp_v.rxPkPayloadLen!=sizeof(app_payload_ht)) {
                  break;
               }
               
               dnm_ucli_printf("TODO: GET\r\n");
               
               break;
            case CMDID_SET:
               // filter length
               if (synctemp_v.rxPkPayloadLen!=sizeof(app_payload_ht)+sizeof(INT32U)) {
                  break;
               }
               
               // set the period
               setPeriod(htonl(*((INT32U*)appHdr->payload)));
              
               break;
            default:
               dnm_ucli_printf("WARNING: unexpected cmdId %d\r\n", appHdr->cmdId);
               break;
         }
         
      } while(0);
      
      // unlock the rx packet
      unlockRxPk();
   }
}

//=========================== helpers =========================================

//===== network

/**
\brief Callback function when receiving a packet OTA.

\param[in] rxFrame The received packet.
\param[in] length  The length of the notification, including the metadata
   (#dn_api_loc_notif_received_t).

\return DN_ERR_NONE always
*/
dn_error_t rxNotif(dn_api_loc_notif_received_t* rxFrame, INT8U length) {
   INT8U                          payloadLen;

   // calc data size
   payloadLen = length-sizeof(dn_api_loc_notif_received_t);
   
   // filter packet
   if (rxFrame->socketId!=loc_getSocketId()) {
      // wrong destination UDP port
      return DN_ERR_NONE;
   }
   if (htons(rxFrame->sourcePort)!=SYNCTEMP_UDP_PORT) {
      // wrong source UDP port
      return DN_ERR_NONE;
   }
   if (payloadLen>MAX_RX_PAYLOAD) {
      // payload too long
      return DN_ERR_NONE;
   }
   
   // if you get here, the packet will be passed to sampleTask
   
   // lock the rx packet information
   // NOTE: will be unlocked by sampleTask when ready
   lockRxPk();
   
   // copy packet information to module variables
   memcpy(synctemp_v.rxPkSrc.byte,rxFrame->sourceAddr.byte,DN_IPV6_ADDR_SIZE);
   memcpy(synctemp_v.rxPkPayload,rxFrame->data,payloadLen);
   synctemp_v.rxPkPayloadLen = payloadLen;
   
   // tell sampleTask the rx packet is ready
   OSSemPost(synctemp_v.rxPkReady);
   
   // NOTE: sampleTask will call unlockRxPk()
   
   return DN_ERR_NONE;
}

//===== formatting

void printf_buffer(INT8U* buf, INT8U len) {
   INT8U i;
   
   for (i=0;i<len;i++) {
      dnm_ucli_printf("%02x",buf[i]);
   }
}

// configuration
void setPeriod(INT32U newPeriod) {
   INT8U      osErr;
   BOOLEAN    rc;
   INT32U     currentPeriod;
   
   // get current period
   lockData();
   currentPeriod = synctemp_v.app_cfg.reportPeriod;
   unlockData();
   
   // abort if nothing to change
   if (currentPeriod==newPeriod) {
      return;
   }
   
   // update period
   lockData();
   synctemp_v.app_cfg.reportPeriod = newPeriod;
   unlockData();
   
   // write to file
   syncToConfigFile();
   
   // print period
   dnm_ucli_printf(
      "Configuration updated: reportPeriod = %d seconds\r\n",
      newPeriod
   );
   
   // rearm timer
   rc = OSTmrStop(
      synctemp_v.sampleTaskStackTimer, // ptmr
      OS_TMR_OPT_NONE,                 // opt
      NULL,                            // callback_arg
      &osErr                           // perr
   );
   ASSERT(rc==OS_TRUE);
   ASSERT(osErr == OS_ERR_NONE);
   
   // call the timer callback
   sampleTaskStackTimer_cb(NULL,NULL);
   
}

//===== configuration file

void loadConfigFile(){
   dn_error_t        dnErr;
   dn_fs_handle_t    configFileHandle;
   
   configFileHandle = dn_fs_find(APP_CONFIG_FILENAME);
   
   if (configFileHandle>=0) {
      // file found: read it
      
      // open file
      configFileHandle = dn_fs_open(
         APP_CONFIG_FILENAME,
         DN_FS_OPT_CREATE,
         sizeof(app_cfg_t),
         DN_FS_MODE_OTH_RW
      );
      ASSERT(configFileHandle >= 0);
      
      // read file
      lockData();
      dnErr = dn_fs_read(
         configFileHandle,
         0, // offset
         (INT8U*)&(synctemp_v.app_cfg),
         sizeof(app_cfg_t)
      );
      unlockData();
      ASSERT(dnErr>=0);
      
      // close file
      dn_fs_close(configFileHandle);
      
      dnm_ucli_printf(
         "Current configuration: reportPeriod = %u seconds\r\n",
         synctemp_v.app_cfg.reportPeriod
      );
      
   } else {
      // file not found: create it
      
      // prepare file content
      lockData();
      synctemp_v.app_cfg.reportPeriod = DEFAULT_RPT_PERIOD;
      unlockData();
      
      // create file
      dnm_ucli_printf("Create config file\r\n");
      configFileHandle = dn_fs_open(
         APP_CONFIG_FILENAME,
         DN_FS_OPT_CREATE,
         sizeof(app_cfg_t),
         DN_FS_MODE_SHADOW
      );
      ASSERT(configFileHandle>=0);
      
      // write file
      lockData();
      dnErr = dn_fs_write(
         configFileHandle,
         0, // offset
         (INT8U*)&(synctemp_v.app_cfg),
         sizeof(app_cfg_t)
      );
      unlockData();
      ASSERT(dnErr >= 0);
      
      // close file
      dn_fs_close(configFileHandle);
      
      dnm_ucli_printf(
         "Default Config created:  reportPeriod = %u seconds\r\n",
         synctemp_v.app_cfg.reportPeriod
      );
   }
}

void syncToConfigFile() {
   dn_error_t          dnErr;
   dn_fs_handle_t      configFileHandle;
   
   // open file
   configFileHandle = dn_fs_open(
      APP_CONFIG_FILENAME,
      DN_FS_OPT_CREATE,
      sizeof(app_cfg_t),
      DN_FS_MODE_OTH_RW
   );
   ASSERT(configFileHandle >= 0);
   
   // write file
   lockData();
   dnErr = dn_fs_write(
      configFileHandle,
      0, // offset
      (INT8U*)&(synctemp_v.app_cfg),
      sizeof(app_cfg_t)
   );
   unlockData();
   ASSERT(dnErr >= 0);
   
   // close file
   dn_fs_close(configFileHandle);
}

//===== lock

// data

void lockData() {
   INT8U      osErr;
   
   OSSemPend(synctemp_v.dataLock, 0, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
}

void unlockData() {
   OSSemPost(synctemp_v.dataLock);
}

// rxPacket

void lockRxPk() {
   INT8U      osErr;
   
   OSSemPend(synctemp_v.rxPkLock, 0, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
}

void unlockRxPk() {
   OSSemPost(synctemp_v.rxPkLock);
}

//=============================================================================
//=========================== install a kernel header =========================
//=============================================================================

/**
A kernel header is a set of bytes prepended to the actual binary image of this
application. This header is needed for your application to start running.
*/

DN_CREATE_EXE_HDR(DN_VENDOR_ID_NOT_SET,
                  DN_APP_ID_NOT_SET,
                  VER_MAJOR,
                  VER_MINOR,
                  VER_PATCH,
                  VER_BUILD);
