/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include <string.h>
#include "stdio.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_uart.h"
#include "dn_exe_hdr.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================

#define MAX_UART_PACKET_SIZE (128u)
#define MAX_UART_CHNL_SIZE (sizeof(dn_chan_msg_hdr_t) + MAX_UART_PACKET_SIZE)

#define MAX_PAYLOAD_LENGTH             79        ///< Maximum number of payload bytes
#define SERIAL_MAX_RETRIES             5         ///< Maximum number of retries over serial API
#define SERIAL_ACK_TIMEOUT             500       ///< Maximum time to wait for reply from manager (ms)
#define MAX_RETRIES                    5
#define RETRY_PERIOD                   5000

//======= definitions from Mesh-of-Meshes Application Note

#define MOM_UDP_PORT                   0xf0bf    ///< UDP port used in tunneling protocol

// request dispatch bytes
#define MOM_REQ_DISPATCH_CMD           0x00
#define MOM_REQ_DISPATCH_RPC           0x01

// response dispatch bytes
#define MOM_RESP_DISPATCH_SERIALRESP   0x00
#define MOM_RESP_DISPATCH_RPC          0x01
#define MOM_RESP_DISPATCH_SERIALNOTIF  0x02

// procedure IDs
#define MOM_PROC_GETOPERATIONALMOTES   0x00

//======= definitions from SmartMesh IP manager serial API guide

// mote states
#define MOTE_STATE_OPERATIONAL         4

// serial packet types
#define PKT_TYPE_HELLO                 1
#define PKT_TYPE_HELLORESPONSE         2
#define PKT_TYPE_MGRHELLO              3
#define PKT_TYPE_NOTIFICATION          20
#define PKT_TYPE_SUBSCRIBE             22
#define PKT_TYPE_SENDDATA              44
#define PKT_TYPE_GETMOTECONFIG         47

// return codes
#define RC_OK                          0
#define RC_NO_RESOURCES                12
#define RC_WRITE_FAIL                  15

// notification types
#define PKT_NOTIF_TYPE_EVENT           1
#define PKT_NOTIF_TYPE_LOG             2
#define PKT_NOTIF_TYPE_DATA            4
#define PKT_NOTIF_TYPE_IPDATA          5
#define PKT_NOTIF_TYPE_HR              6

// control byte flags
#define PKT_CTRL_ACKPKT_FLAG           0x01
#define PKT_CTRL_ACKNOWLEDGED_FLAG     0x02

//=========================== prototypes ======================================

//===== CLI handlers

dn_error_t    cli_getOperationalMotesCmdHandler(INT8U* arg, INT32U len);

//===== tasks

static void   discoverTask(void* unused);
static void   managerConnectionTask(void* unused);
static void   uartTask(void* unused);

//===== network interaction

dn_error_t    wirelessRxNotif(dn_api_loc_notif_received_t* rxFrame, INT8U length);

//===== Mesh-of-meshes procedures

       void   mom_proc_getOperationalMotes();
       INT8U  discoverMotes(INT8U numMotesIn, INT8U* numMotesOut, INT8U* pkBuf, INT8U maxPayloadLen);
       INT8U  sendDiscoveredMotes(INT8U* buf, INT8U bufLen);

//===== serial API

       void   serial_api_tx_hello();
       void   serial_api_rx_helloresponse(INT8U* payload, INT8U payloadLen);
       void   serial_api_tx_subscribe();
       void   serial_api_rx_notification(INT8U* payload, INT8U payloadLen);

//===== serial

       INT8U  serial_tx(
          INT8U  packetType,
          INT8U* txPayload,
          INT8U  txPayloadLen,
          INT8U* rxPayload,
          INT8U  rxPayloadMaxLen,
          INT8U* rxPayloadLen
       );
       void   serial_rx(INT8U* rxBuf, INT8U rxLen);
       void   serial_rx_dispatch(INT8U packetType, INT8U* payload, INT8U payloadLen);

//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_getOperationalMotesCmdHandler, "motes", "motes",  DN_CLI_ACCESS_LOGIN},
   {NULL,                               NULL,    NULL,     0},
};

//=========================== struct ==========================================

PACKED_START

//===== serial formats

typedef struct{
   INT8U  control;
   INT8U  packetType;
   INT8U  seqNum;
   INT8U  payloadLen;
   INT8U  payload[];
} serial_ht;

typedef struct{
   INT8U  version;
   INT8U  cliSeqNo;
   INT8U  mode;
} serial_hello_ht;

typedef struct{
   INT8U  responseCode;
   INT8U  version;
   INT8U  mgrSeqNo;
   INT8U  cliSeqNo;
   INT8U  mode;
} serial_helloresponse_ht;

typedef struct{
   INT32U filter;
   INT32U unackFilter;
} serial_subscribe_ht;

typedef struct{
   INT8U  rc;
} serial_subscribe_ack_ht;

typedef struct{
   INT8U  macAddress[8];
   INT8U  next;
} serial_getMoteConfig_ht;

typedef struct{
   INT8U  rc;
   INT8U  macAddress[8];
   INT16U moteId;
   INT8U  isAP;
   INT8U  state;
   INT8U  reserved;
   INT8U  isRouting;
} serial_getMoteConfig_ack_ht;

typedef struct{
   INT8U  notifType;
   INT8U  payload[];
} serial_notif_ht;

typedef struct{
   INT8U  timestamp[12];
   INT8U  macAddress[8];
   INT16U srcPort;
   INT16U dstPort;
   INT8U  data[];
} serial_notif_data_ht;

typedef struct{
   INT8U  macAddress[8];
   INT8U  priority;
   INT16U srcPort;
   INT16U dstPort;
   INT8U  options;
   INT8U  data[];
} serial_sendData_ht;

typedef struct{
   INT8U  rc;
   INT32U callbackId;
} serial_sendData_ack_ht;

//===== mesh-of-meshes tunneling protocol packet formats

typedef struct{
   INT8U  dispatch;
   INT8U  payload[];
} mom_request_ht;

typedef struct{
   INT8U  dispatch;
   INT8U  payload[];
} mom_response_ht;

typedef struct{
   INT8U  procID;
   INT8U  payload[];
} mom_request_rpc_ht;

typedef struct{
   INT8U  procID;
   INT8U  payload[];
} mom_response_rpc_ht;

typedef struct{
   INT8U  number;
   INT8U  index;
} mom_motes_ht;

PACKED_STOP

//=========================== variables =======================================

typedef struct {
   // admin
   OS_EVENT*       joinedSem;          ///< Posted when mote has joined the network.
   // discoverTask
   OS_STK          discoverTaskStack[TASK_APP_DISCO_STK_SIZE];
   OS_EVENT*       discoSem;
   // managerConnectionTask
   OS_STK          managerTaskStack[TASK_APP_UART_STK_SIZE];
   INT8U           connected;
   OS_EVENT*       disconnectedSem;          ///< Posted when mote is disconnected from manager API.
   // uartTask
   OS_STK          uartTaskStack[TASK_APP_UART_STK_SIZE];
   INT32U          uartRxChannelMemBuf[1+MAX_UART_CHNL_SIZE/sizeof(INT32U)];
   OS_MEM*         uartRxChannelMem;
   CH_DESC         uartRxChannel;
   INT8U           uartRxBuffer[MAX_UART_PACKET_SIZE];
   INT8U           uartTxBuffer[MAX_UART_PACKET_SIZE];
   INT8U           seqNoRx;
   INT8U           seqNoTx;
   INT8U           requestPacketType;
   INT8U*          responsePayload;
   INT8U           responsePayloadMaxLen;
   INT8U           responsePayloadLen;
   OS_EVENT*       serialTxDataAvailable;
   OS_EVENT*       waitForResponseSem;
   // wireless communication
   INT8U           pkBuf[sizeof(loc_sendtoNW_t)+MAX_PAYLOAD_LENGTH];
   // serial sendData
   INT8U           serialSendDataBuf[sizeof(serial_sendData_ht)+MAX_PAYLOAD_LENGTH];
} bridge_app_vars_t;

bridge_app_vars_t  bridge_app_v;

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   INT8U  osErr;
   
   //==== initialize local variables
   
   memset(&bridge_app_v,0x00,sizeof(bridge_app_v));
   bridge_app_v.connected              = 0;
   bridge_app_v.joinedSem              = OSSemCreate(0);
   bridge_app_v.discoSem               = OSSemCreate(0);
   bridge_app_v.disconnectedSem        = OSSemCreate(0);
   bridge_app_v.serialTxDataAvailable  = OSSemCreate(1);
   bridge_app_v.waitForResponseSem     = OSSemCreate(0);
   
   //==== initialize helper tasks
   
   cli_task_init(
      "bridge",                             // appName
      &cliCmdDefs                           // cliCmds
   );
   
   loc_task_init(
      JOIN_YES,                             // fJoin
      NETID_NONE,                           // netId
      MOM_UDP_PORT,                         // udpPort
      bridge_app_v.joinedSem,               // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== register a callback to receive packets
   dnm_loc_registerRxNotifCallback(wirelessRxNotif);
   
   //===== create tasks
   
   // uartTask task
   osErr  = OSTaskCreateExt(
      uartTask,
      (void *)0,
      (OS_STK*)(&bridge_app_v.uartTaskStack[TASK_APP_UART_STK_SIZE-1]),
      TASK_APP_UART_PRIORITY,
      TASK_APP_UART_PRIORITY,
      (OS_STK*)bridge_app_v.uartTaskStack,
      TASK_APP_UART_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_UART_PRIORITY, (INT8U*)TASK_APP_UART_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   // managerConnectionTask task
   osErr  = OSTaskCreateExt(
      managerConnectionTask,
      (void *)0,
      (OS_STK*)(&bridge_app_v.managerTaskStack[TASK_APP_MGRCONN_STK_SIZE-1]),
      TASK_APP_MGRCONN_PRIORITY,
      TASK_APP_MGRCONN_PRIORITY,
      (OS_STK*)bridge_app_v.managerTaskStack,
      TASK_APP_MGRCONN_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_MGRCONN_PRIORITY, (INT8U*)TASK_APP_MGRCONN_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== CLI handlers ====================================

dn_error_t cli_getOperationalMotesCmdHandler(INT8U* arg, INT32U len) {
   
   // emulate a getOperationalMotes procedure call
   mom_proc_getOperationalMotes();
   
   return DN_ERR_NONE;
}

//=========================== tasks ===========================================

static void discoverTask(void* unused) {
   INT8U      osErr;
   INT8U      pkBuf[sizeof(loc_sendtoNW_t)+MAX_PAYLOAD_LENGTH];
   INT8U      rc;
   INT8U      numMotes;
   
   while(1) { // this is a task, it executes forever
      
      // wait to be asked to discover
      OSSemPend(bridge_app_v.discoSem, 0, &osErr);
      ASSERT(osErr==OS_ERR_NONE);
      
      // find the number of motes
      rc = discoverMotes(
         0,                  // numMotesIn
         &numMotes,          // numMotesOut
         NULL,               // pkBuf
         0                   // maxPayloadLen
      );
      if (rc!=RC_OK) {
         continue;
      }
      
      // find motes, and write in packets
      rc = discoverMotes(
         numMotes,           // numMotesIn
         NULL,               // numMotesOut
         pkBuf,              // pkBuf
         MAX_PAYLOAD_LENGTH  // maxPayloadLen
      );
   }
}

static void managerConnectionTask(void* unused) {
   dn_error_t           dnErr;
   INT8U                osErr;
   
   // wait for the loc_task to finish joining the network
   OSSemPend(bridge_app_v.joinedSem, 0, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   // start discoverTask task
   osErr  = OSTaskCreateExt(
      discoverTask,
      (void *)0,
      (OS_STK*)(&bridge_app_v.discoverTaskStack[TASK_APP_DISCO_STK_SIZE-1]),
      TASK_APP_DISCO_PRIORITY,
      TASK_APP_DISCO_PRIORITY,
      (OS_STK*)bridge_app_v.discoverTaskStack,
      TASK_APP_DISCO_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_DISCO_PRIORITY, (INT8U*)TASK_APP_DISCO_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   while(1) { // this is a task, it executes forever
      
      // connect
      serial_api_tx_hello();
      OSTimeDly(1000);
      if (bridge_app_v.connected!=0x01) {
          // try to reconnect
          continue;
      }
      
      // subscribe to all notifications
      serial_api_tx_subscribe();
      
      // wait for disconnection
      OSSemPend(bridge_app_v.disconnectedSem, 0, &osErr);
      ASSERT(osErr==OS_ERR_NONE);
      dnm_ucli_printf("WARNING: disconnected from manager API\r\n");
   }
}

static void uartTask(void* unused) {
   dn_error_t           dnErr;
   INT8U                osErr;
   dn_uart_open_args_t  uartOpenArgs;
   INT32U               rxLen;
   INT32U               channelMsgType;
   INT32S               err;
   
   // create the memory block for the UART channel
   bridge_app_v.uartRxChannelMem = OSMemCreate(
      bridge_app_v.uartRxChannelMemBuf,
      1,
      sizeof(bridge_app_v.uartRxChannelMemBuf),
      &osErr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   // create an asynchronous notification channel
   dnErr = dn_createAsyncChannel(bridge_app_v.uartRxChannelMem, &bridge_app_v.uartRxChannel);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // associate the channel descriptor with UART notifications
   dnErr = dn_registerChannel(bridge_app_v.uartRxChannel, DN_MSG_TYPE_UART_NOTIF);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // open the UART device
   uartOpenArgs.rxChId    = bridge_app_v.uartRxChannel;
   uartOpenArgs.rate        = 115200u;
   uartOpenArgs.mode        = DN_UART_MODE_M4;
   uartOpenArgs.ctsOutVal   = 0;
   err = dn_open(
      DN_UART_DEV_ID,
      &uartOpenArgs,
      sizeof(uartOpenArgs)
   );
   ASSERT(err>=0);
   
   while(1) { // this is a task, it executes forever
      
      // wait for UART messages
      dnErr = dn_readAsyncMsg(
         bridge_app_v.uartRxChannel,   // chDesc
         bridge_app_v.uartRxBuffer,    // msg
         &rxLen,                       // rxLen
         &channelMsgType,              // msgType
         MAX_UART_PACKET_SIZE,         // maxLen
         0                             // timeout (0==never)
      );
      ASSERT(dnErr==DN_ERR_NONE);
      ASSERT(channelMsgType==DN_MSG_TYPE_UART_NOTIF);
      
      // call the callback
      serial_rx(bridge_app_v.uartRxBuffer,rxLen);
   }
}

//=========================== network interaction =============================

dn_error_t wirelessRxNotif(dn_api_loc_notif_received_t* rxFrame, INT8U length) {
   serial_sendData_ht*       serial_sendData;
   serial_sendData_ack_ht    serial_sendData_ack;
   INT8U                     dataLen;
   INT8U                     responseLen;
   INT8U                     rc;
   
   if (rxFrame->sourcePort==htons(MOM_UDP_PORT)) {
      // request
      
      if (length<sizeof(dn_api_loc_notif_received_t)+2) {
         dnm_ucli_printf("ERROR: downstream request too short (%d bytes)\r\n",length);
         return DN_ERR_NONE;
      }
      
      if (
             rxFrame->data[0]==MOM_REQ_DISPATCH_RPC &&
             rxFrame->data[1]==MOM_PROC_GETOPERATIONALMOTES
          ) {
         mom_proc_getOperationalMotes();
      }
      
   } else {
      // downstream data
      
      if (length<=sizeof(dn_api_loc_notif_received_t)+8) {
         dnm_ucli_printf("ERROR: downstream data too short (%d bytes)\r\n",length);
         return DN_ERR_NONE;
      }
     
      // create serial frame
      
      dataLen = length-sizeof(dn_api_loc_notif_received_t)-8;
      
      serial_sendData = (serial_sendData_ht*)bridge_app_v.serialSendDataBuf;
      memcpy(serial_sendData->macAddress,&rxFrame->data[0],8);
      serial_sendData->priority = DN_API_PRIORITY_MED;
      serial_sendData->srcPort  = rxFrame->sourcePort;
      serial_sendData->dstPort  = rxFrame->sourcePort;
      serial_sendData->options  = 0x00;
      memcpy(serial_sendData->data,&rxFrame->data[8],dataLen);
      
      // send serial frame
      rc = serial_tx(
         PKT_TYPE_SENDDATA,                      // packetType
         (INT8U*)serial_sendData,                // txPayload
         sizeof(serial_sendData_ht)+dataLen,     // txPayloadLen
         &serial_sendData_ack,                   // rxPayload
         sizeof(serial_sendData_ack_ht),         // rxPayloadMaxLen
         &responseLen                            // rxPayloadLen
      );
      if (
            rc!=RC_OK ||
            responseLen<sizeof(serial_sendData_ack_ht) ||
            serial_sendData_ack.rc!=RC_OK
         ) {
             // print
             dnm_ucli_printf("ERROR: sendData error\r\n");
             // disconnect
             OSSemPost(bridge_app_v.disconnectedSem);
         }
   }
   
   return DN_ERR_NONE;
}

//=========================== Mesh-of-meshes procedures =======================

void mom_proc_getOperationalMotes() {
   OSSemPost(bridge_app_v.discoSem);
}

INT8U discoverMotes(INT8U numMotesIn, INT8U* numMotesOut, INT8U* pkBuf, INT8U maxPayloadLen) {
   INT8U                          lastWrittenMac[8];
   INT8U                          firstMACwritten;
   INT8U                          currentMac[8];
   serial_getMoteConfig_ht        request;
   serial_getMoteConfig_ack_ht    response;
   INT8U                          responseLen;
   INT8U                          rc;
   INT8U                          lastMAC[8];
   mom_response_ht*               mom_response_h;
   mom_response_rpc_ht*           mom_response_rpc_h;
   mom_motes_ht*                  mom_motes_h;
   INT8U*                         payload;
   INT8U                          payloadIdx;
   INT8U                          moteidx;
   INT8U                          i;
   INT8U                          lenMAC;
   
   // start with empty MAC address
   memset(currentMac,0,8);
   firstMACwritten = 1;
   payload    = ((loc_sendtoNW_t*)pkBuf)->locSendTo.payload;
   payloadIdx = 0;
   moteidx    = 0;
   
   while (1) {
      
      // prepare next request
      memcpy(request.macAddress,currentMac,8);
      request.next               = 0x01;
      
      // send
      rc = serial_tx(
         PKT_TYPE_GETMOTECONFIG,                 // packetType
         (INT8U*)&request,                       // txPayload
         sizeof(serial_getMoteConfig_ht),        // txPayloadLen
         &response,                              // rxPayload
         sizeof(serial_getMoteConfig_ack_ht),    // rxPayloadMaxLen
         &responseLen                            // rxPayloadLen
      );
      
      if (rc!=RC_OK || responseLen<1) {
         // there was a problem communicating with the manager
          
         // print
         dnm_ucli_printf("ERROR: problem communicating with manager\r\n");
        
         // disconnect
         OSSemPost(bridge_app_v.disconnectedSem);
         
         // abort, abort
         return RC_WRITE_FAIL;
      
      } else if (response.rc!=RC_OK) {
         // end of list of motes
         
         if (numMotesOut==NULL) {
            // send remainder of buffer
            if (payloadIdx>0) {
               rc = sendDiscoveredMotes(pkBuf,payloadIdx);
               if (rc!=RC_OK) {
                  return rc;
               }
            }
         } else {
            // write number of motes
            *numMotesOut = moteidx;
         }
         
         // stop
         return RC_OK;
          
      } else {
         // not the end of the list of motes
          
         if (response.isAP==0x00 && response.state==MOTE_STATE_OPERATIONAL) {
            // I have found a new mote
            
            if (numMotesOut==NULL) {
               // write motes
               
               //===== start of packet
               if (payloadIdx==0) {
                  
                  // mom_response_ht
                  mom_response_h = (mom_response_ht*)&payload[payloadIdx];
                  mom_response_h->dispatch  = MOM_RESP_DISPATCH_RPC;
                  payloadIdx               += sizeof(mom_response_ht);
                  
                  // mom_response_rpc_ht
                  mom_response_rpc_h = (mom_response_rpc_ht*)&payload[payloadIdx];
                  mom_response_rpc_h->procID= MOM_PROC_GETOPERATIONALMOTES;
                  payloadIdx               += sizeof(mom_response_rpc_ht);
                  
                  // motes header
                  mom_motes_h = (mom_motes_ht*)&payload[payloadIdx];
                  mom_motes_h->number       = numMotesIn;
                  mom_motes_h->index        = moteidx;
                  payloadIdx               += sizeof(mom_motes_ht);
               }
               
               //===== body of packet
               
               // determine how many bytes are different with previous MAC (delta encoding)
               lenMAC=8;
               if (firstMACwritten==1) {
                  firstMACwritten=0;
               } else {
                  for (i=0;i<8;i++) {
                     if (response.macAddress[i]==lastWrittenMac[i]) {
                        lenMAC--;
                     } else {
                        break;
                     }
                  }
               }
               
               // write header
               payload[payloadIdx] = lenMAC;
               payloadIdx++;
               
               // write lenMAC last address of MAC address
               memcpy(&payload[payloadIdx],&response.macAddress[8-lenMAC],lenMAC);
               payloadIdx += lenMAC;
               
               // remember last written MAC address
               memcpy(lastWrittenMac,response.macAddress,8);
               
               //===== end of packet
               
               if (payloadIdx+8>=maxPayloadLen) {
                  // not enough space to write another MAC address
                  
                  // send buffer
                  rc = sendDiscoveredMotes(pkBuf,payloadIdx);
                  if (rc!=RC_OK) {
                     return rc;
                  }
                  
                  // reset buffer
                  firstMACwritten = 1;
                  payloadIdx      = 0;
               }
            }
            
            // increment number of motes
            moteidx++;
         }
          
         // store MAC address for next iteration
         memcpy(currentMac,response.macAddress,8);
      }
   }
}

INT8U sendDiscoveredMotes(INT8U* pkBuf, INT8U payloadLen) {
   dn_error_t           dnErr;
   INT8U                rc;
   INT8U                retries;
   loc_sendtoNW_t*      pkToSend;
   INT8U                returnVal;
   
   // fill in packet header
   pkToSend = (loc_sendtoNW_t*)pkBuf;
   pkToSend->locSendTo.socketId       = loc_getSocketId();
   pkToSend->locSendTo.destAddr       = DN_MGR_IPV6_MULTICAST_ADDR;
   pkToSend->locSendTo.destPort       = MOM_UDP_PORT;
   pkToSend->locSendTo.serviceType    = DN_API_SERVICE_TYPE_BW;   
   pkToSend->locSendTo.priority       = DN_API_PRIORITY_MED;   
   pkToSend->locSendTo.packetId       = 0xFFFF;
   
   returnVal = RC_NO_RESOURCES;
   retries   = 0;
   while (retries<MAX_RETRIES) {
      
      dnErr = dnm_loc_sendtoCmd(
         pkToSend,
         payloadLen,
         &rc
      );
      if (dnErr==DN_ERR_NONE) {
         dnm_ucli_printf("INFO: motes sent\r\n");
         
         break;
      } else {
         // increment number of retries
         retries++;
         
         // wait before trying again
         OSTimeDly(RETRY_PERIOD);
      }
   }
   
   return returnVal;
}

//=========================== serial API ======================================

void serial_api_tx_hello() {
   serial_hello_ht msg;
   
   // create
   msg.version     = 4;
   msg.cliSeqNo    = bridge_app_v.seqNoTx;
   msg.mode        = 0x00;
   
   // send
   serial_tx(
      PKT_TYPE_HELLO,             // packetType
      (INT8U*)&msg,               // txPayload
      sizeof(serial_hello_ht),    // txPayloadLen
      NULL,                       // rxPayload
      0,                          // rxPayloadMaxLen
      NULL                        // rxPayloadLen
   );
}

void serial_api_rx_helloresponse(INT8U* payload, INT8U payloadLen) {
   
   serial_helloresponse_ht* msg;
   
   // drop packet if wrong length
   if (payloadLen!=sizeof(serial_helloresponse_ht)) {
      // print
      dnm_ucli_printf("ERROR: helloresponse wrong length (%d bytes)\r\n",payloadLen);
      // disconnect
      OSSemPost(bridge_app_v.disconnectedSem);
      return;
   }
   
   // cast message
   msg = (serial_helloresponse_ht*)payload;
   
   // store seqNoRx
   bridge_app_v.seqNoRx = msg->mgrSeqNo;
   
#ifdef PRINT_TRACE
   dnm_ucli_printf("      seqNoRx %d\r\n",bridge_app_v.seqNoRx);
#endif
   
   // I'm now connected
   bridge_app_v.connected = 0x01;
}

void serial_api_tx_subscribe() {
   serial_subscribe_ht     request;
   serial_subscribe_ack_ht response;
   INT8U                   responseLen;
   INT8U                   rc;
   
   // create
   request.filter      = 0xffffffff;
   request.unackFilter = 0xffffffff;
   
   // send
   rc = serial_tx(
      PKT_TYPE_SUBSCRIBE,              // packetType
      (INT8U*)&request,                // txPayload
      sizeof(serial_subscribe_ht),     // txPayloadLen
      &response,                       // rxPayload
      sizeof(serial_subscribe_ack_ht), // rxPayloadMaxLen
      &responseLen                     // rxPayloadLen
   );
   if (
         rc!=RC_OK ||
         responseLen<sizeof(serial_subscribe_ack_ht) ||
         response.rc!=RC_OK
      ) {
          // print
          dnm_ucli_printf("ERROR: subscription error\r\n");
          // disconnect
          OSSemPost(bridge_app_v.disconnectedSem);
      }
}

void serial_api_rx_notification(INT8U* payload, INT8U payloadLen) {
   dn_error_t                dnErr;
   serial_notif_ht*          serial_notif;
   serial_notif_data_ht*     serial_notif_data;
   INT8U                     dataLen;
   loc_sendtoNW_t*           pkToSend;
   INT8U                     rc;
   
   // drop packet if wrong length
   if (payloadLen<=sizeof(serial_notif_ht)) {
      // print
      dnm_ucli_printf("ERROR: notification wrong length (%d bytes)\r\n",payloadLen);
      // disconnect
      OSSemPost(bridge_app_v.disconnectedSem);
      return;
   }
   
   // cast message
   serial_notif = (serial_notif_ht*)payload;
   
   // handle notification
   switch(serial_notif->notifType) {
       case PKT_NOTIF_TYPE_DATA:
           
           // cast serial message
           serial_notif_data = (serial_notif_data_ht*)serial_notif->payload;
           dataLen           = payloadLen-sizeof(serial_notif_ht)-sizeof(serial_notif_data);
           
           if (dataLen+8>MAX_PAYLOAD_LENGTH) {
               dnm_ucli_printf("WARNING: data packet too long (%d bytes)\r\n",dataLen);
           } else {
              // prepare wireless packet
              pkToSend = (loc_sendtoNW_t*)bridge_app_v.pkBuf;
              pkToSend->locSendTo.socketId       = loc_getSocketId();
              pkToSend->locSendTo.destAddr       = DN_MGR_IPV6_MULTICAST_ADDR;
              pkToSend->locSendTo.destPort       = htons(serial_notif_data->dstPort);
              pkToSend->locSendTo.serviceType    = DN_API_SERVICE_TYPE_BW;   
              pkToSend->locSendTo.priority       = DN_API_PRIORITY_MED;   
              pkToSend->locSendTo.packetId       = 0xFFFF;
              memcpy(&pkToSend->locSendTo.payload[0],serial_notif_data->macAddress,8);
              memcpy(&pkToSend->locSendTo.payload[8],serial_notif_data->data,dataLen);
              
              // send wireless packet
              dnErr = dnm_loc_sendtoCmd(
                 pkToSend,
                 8+dataLen,
                 &rc
              );
              if (dnErr==DN_ERR_NONE) {
                 dnm_ucli_printf("INFO: DATA tunneled\r\n");
              } else {
                 dnm_ucli_printf("ERROR: DATA tunneling dnErr=%d\r\n",dnErr);
              }
           }
           
           break;
       case PKT_NOTIF_TYPE_EVENT:
       case PKT_NOTIF_TYPE_HR:
           
           if (payloadLen+1>MAX_PAYLOAD_LENGTH) {
               dnm_ucli_printf("WARNING: notification too long (%d bytes)\r\n",dataLen);
           } else {
              // prepare wireless packet
              pkToSend = (loc_sendtoNW_t*)bridge_app_v.pkBuf;
              pkToSend->locSendTo.socketId       = loc_getSocketId();
              pkToSend->locSendTo.destAddr       = DN_MGR_IPV6_MULTICAST_ADDR;
              pkToSend->locSendTo.destPort       = MOM_UDP_PORT;
              pkToSend->locSendTo.serviceType    = DN_API_SERVICE_TYPE_BW;   
              pkToSend->locSendTo.priority       = DN_API_PRIORITY_MED;   
              pkToSend->locSendTo.packetId       = 0xFFFF;
              pkToSend->locSendTo.payload[0]     = MOM_RESP_DISPATCH_SERIALNOTIF;
              memcpy(&pkToSend->locSendTo.payload[1],payload,payloadLen);
              
              // send wireless packet
              dnErr = dnm_loc_sendtoCmd(
                 pkToSend,
                 1+payloadLen,
                 &rc
              );
              if (dnErr==DN_ERR_NONE) {
                 dnm_ucli_printf("INFO: NOTIF tunneled\r\n");
              } else {
                 dnm_ucli_printf("ERROR: NOTIF tunneling dnErr=%d\r\n",dnErr);
              }
           }  
           
           break;
       case PKT_NOTIF_TYPE_IPDATA:
       case PKT_NOTIF_TYPE_LOG:
           dnm_ucli_printf("WARNING notifType %d not handled\r\n",serial_notif->notifType);
           break;
       default:
          // print
         dnm_ucli_printf("ERROR: unexpected notifType %d\r\n",serial_notif->notifType);
         // disconnect
         OSSemPost(bridge_app_v.disconnectedSem);
         return;
   }
}

//=========================== serial ==========================================

/**
\brief Send a frame to the serial API of the SmartMesh IP manager.

A response is expected from the manager if rxPayload!=NULL. In this case, this
function retries transmitting if no response is received after
SERIAL_ACK_TIMEOUT ms. If no response is received after SERIAL_MAX_RETRIES
retries, the function returns RC_WRITE_FAIL.

\param[in]  packetType The payload type.
\param[in]  txPayload Payload to transmit to the manager.
\param[in]  txPayloadLen Number of bytes into the payload to transmit to the
   manager.
\param[out] rxPayload Pointer to a buffer to write the received payload into.
   manager.
\param[in]  rxPayloadMaxLen Maximum number of bytes which fit in the rxPayload
   buffer. If the manager responds with more bytes, this function returns
   RC_NO_RESOURCES, and no bytes are written to the rxPayload buffer.
\param[out] rxPayloadLen Expected number of bytes returned by the manager.

\return RC_OK if the operation succeeded successfully.
\return RC_NO_RESOURCES if the manager returned a number of bytes different
   from rxPayloadLen.
\return RC_WRITE_FAIL if no answer from the manager is received after
   SERIAL_MAX_RETRIES retries.
*/
INT8U serial_tx(
      INT8U  packetType,
      INT8U* txPayload,
      INT8U  txPayloadLen,
      INT8U* rxPayload,
      INT8U  rxPayloadMaxLen,
      INT8U* rxPayloadLen
      ) {
   INT8U           osErr;
   dn_error_t      dnErr;
   INT8U           i;
   INT8U           len;
   INT8U           functionReply;
   INT8U           retryCounter;
   INT32U          functionReplyLen;
   serial_ht*      tx_frame;
   INT8U           returnVal;
   
   // I'm busy sending a DATA packet over serial
   OSSemPend(bridge_app_v.serialTxDataAvailable,0,&osErr);
   ASSERT(osErr==RC_OK);
   
   // create packet
   tx_frame                  = (serial_ht*)bridge_app_v.uartTxBuffer;
   tx_frame->control         = 0x00;
   tx_frame->packetType      = packetType;
   tx_frame->seqNum          = bridge_app_v.seqNoTx++;
   tx_frame->payloadLen      = txPayloadLen;
   if (txPayload!=NULL) {
      memcpy(tx_frame->payload,txPayload,txPayloadLen);
   }
   len                       = sizeof(serial_ht)+txPayloadLen;
   
   // store packet details
   bridge_app_v.requestPacketType           = packetType;
   bridge_app_v.responsePayload             = rxPayload;
   bridge_app_v.responsePayloadMaxLen       = rxPayloadMaxLen;
   
   retryCounter                             = SERIAL_MAX_RETRIES;
   while (retryCounter>0) {
   
#ifdef PRINT_TRACE
      dnm_ucli_printf("---TX--> (%d bytes)",len);
      for (i=0;i<len;i++) {
         dnm_ucli_printf(" %02x",bridge_app_v.uartTxBuffer[i]);
      }
      dnm_ucli_printf("\r\n");
      dnm_ucli_printf("   packetType %d\r\n",tx_frame->packetType);
      dnm_ucli_printf("   seqNum     %d\r\n",tx_frame->seqNum);
#endif
      
      // send packet
      dnErr = dn_sendSyncMsgByType(
         bridge_app_v.uartTxBuffer,
         len,
         DN_MSG_TYPE_UART_TX_CTRL,
         (void*)&functionReply,
         sizeof(functionReply),
         &functionReplyLen
      );
      ASSERT(functionReplyLen==sizeof(INT8U));
      ASSERT(functionReply==DN_ERR_NONE);
      
      // wait for response, if appropriate
      if (rxPayload!=NULL) {
         
         // wait for response
         OSSemPend(
             bridge_app_v.waitForResponseSem,
             SERIAL_ACK_TIMEOUT,
             &osErr
         );
         
         // return appropriate value
         switch (osErr) {
             case OS_ERR_NONE:
                 *rxPayloadLen = bridge_app_v.responsePayloadLen;
                 if (bridge_app_v.responsePayloadLen<=bridge_app_v.responsePayloadMaxLen) {
                    returnVal = RC_OK;
                 } else {
                    returnVal = RC_NO_RESOURCES;
                 }
                 retryCounter = 0;
                 break;
             case OS_ERR_TIMEOUT:
               dnm_ucli_printf("WARNING: serial API timeout.\r\n");
                 retryCounter--;
                 break;
             default:
                 ASSERT(0);
         }
      } else {
         retryCounter = 0;
      }
   }
   
   // reset packet details
   bridge_app_v.requestPacketType             = 0x00;
   
   // I'm NOT busy sending a DATA packet over serial
   OSSemPost(bridge_app_v.serialTxDataAvailable);
   
   return returnVal;
}

void  serial_rx(INT8U* rxBuf, INT8U rxLen) {
   INT8U           i;
   serial_ht*      rx_frame;
   
#ifdef PRINT_TRACE
   dnm_ucli_printf("<--RX--- (%d bytes)",rxLen);
   for (i=0;i<rxLen;i++) {
      dnm_ucli_printf(" %02x",rxBuf[i]);
   }
   dnm_ucli_printf("\r\n");
#endif
   
   // drop frame if too short
   if (rxLen<sizeof(serial_ht)) {
      // print
      dnm_ucli_printf("ERROR: frame too short (%d bytes)\r\n",rxLen);
      // disconnect
      OSSemPost(bridge_app_v.disconnectedSem);
      return;
   }
   
   // parse incoming
   rx_frame = (serial_ht*)rxBuf;

#ifdef PRINT_TRACE
   dnm_ucli_printf("   packetType %d\r\n",rx_frame->packetType);
   dnm_ucli_printf("   seqNum     %d\r\n",rx_frame->seqNum);
#endif
   
   // drop frame if wrong size
   if (rxLen!=sizeof(serial_ht)+rx_frame->payloadLen) {
      // print
      dnm_ucli_printf("ERROR: wrong length (%d bytes, expected %d)\r\n",rxLen,sizeof(serial_ht)+rx_frame->payloadLen);
      // disconnect
      OSSemPost(bridge_app_v.disconnectedSem);
      return;
   }
   
   // detect ACKs
   if (rx_frame->control & PKT_CTRL_ACKPKT_FLAG) {
      // ACK
      
#ifdef PRINT_TRACE
      dnm_ucli_printf("   ACK\r\n");
#endif
      
      // drop frame if wrong sequence number
      if (rx_frame->seqNum!=bridge_app_v.seqNoTx-1) {
         // print
         dnm_ucli_printf(
            "ERROR: wrong ACK seqNum %d (expected %d)\r\n",
            rx_frame->seqNum,
            bridge_app_v.seqNoTx-1
         );
         return;
      }
      
      // drop frame if wrong packet type
      if (rx_frame->packetType!=bridge_app_v.requestPacketType) {
         // print
         dnm_ucli_printf(
            "ERROR: wrong ACK packetType %d (expected %d)\r\n",
            rx_frame->packetType,
            bridge_app_v.requestPacketType
         );
         return;
      }
      
      // report payload len
      bridge_app_v.responsePayloadLen = rx_frame->payloadLen;
      
      // copy response
      if (rx_frame->payloadLen<=bridge_app_v.responsePayloadMaxLen) {
         memcpy(bridge_app_v.responsePayload,rx_frame->payload,rx_frame->payloadLen);
      }
      
      // release semaphore
      OSSemPost(bridge_app_v.waitForResponseSem);
      
   } else {
      // DATA
     
      // handle sequence number and acknowledgement, if applicable
     
      if (rx_frame->control & PKT_CTRL_ACKNOWLEDGED_FLAG) {
         // drop frame if wrong sequence number
         if (rx_frame->seqNum!=bridge_app_v.seqNoRx) {
            // print
            dnm_ucli_printf(
               "ERROR: wrong DATA seqNum %d (expected %d)\r\n",
               rx_frame->seqNum,
               bridge_app_v.seqNoRx
            );
            // disconnect
            OSSemPost(bridge_app_v.disconnectedSem);
            return;
         }
        
         // increment seqNoRx
         bridge_app_v.seqNoRx++;
      }
      
      // sent to DATA dispatcher
      serial_rx_dispatch(
         rx_frame->packetType, // packetType
         rx_frame->payload,    // payload
         rx_frame->payloadLen  // payloadLen
      );
   }
}

void  serial_rx_dispatch(INT8U packetType, INT8U* payload, INT8U payloadLen) {

   switch (packetType) {
      case PKT_TYPE_HELLORESPONSE:
         serial_api_rx_helloresponse(payload,payloadLen);
         break;
     case PKT_TYPE_NOTIFICATION:
         serial_api_rx_notification(payload,payloadLen);
         break;
      default:
         dnm_ucli_printf("WARNING: unknown DATA packetType=%d\r\n",packetType);
         break;
   }
}

//=============================================================================
//=========================== install a kernel header =========================
//=============================================================================

/**
A kernel header is a set of bytes prepended to the actual binary image of this
application. Thus header is needed for your application to start running.
*/

DN_CREATE_EXE_HDR(DN_VENDOR_ID_NOT_SET,
                  DN_APP_ID_NOT_SET,
                  VER_MAJOR,
                  VER_MINOR,
                  VER_PATCH,
                  VER_BUILD);
