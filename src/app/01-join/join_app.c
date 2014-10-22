/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "dn_exe_hdr.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dnm_local.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== defines =========================================

#define PAYLOAD_LENGTH                 10

//=========================== variables =======================================

typedef struct {
   OS_EVENT*       joinedSem;
   OS_STK          sendTaskStack[TASK_APP_SEND_STK_SIZE];
} join_app_vars_t;

join_app_vars_t join_app_vars;

//=========================== prototypes ======================================

static void sendTask(void* unused);
dn_error_t rxNotifCb(dn_api_loc_notif_received_t* rxFrame, INT8U length);

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   dn_error_t      dnErr;
   INT8U           osErr;
   
   // create semaphore for loc_task to indicate when joined
   join_app_vars.joinedSem = OSSemCreate(0);
   
   //===== initialize helper tasks
   
   cli_task_init(
      "join",                               // appName
      NULL                                  // cliCmds
   );
   loc_task_init(
      JOIN_YES,                             // fJoin
      NULL,                                 // netId
      60000,                                // udpPort
      join_app_vars.joinedSem,              // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //==== register a callback for when receiving a packet
   
   dnm_loc_registerRxNotifCallback(rxNotifCb);
   
   //===== initialize sendTask
   
   osErr = OSTaskCreateExt(
      sendTask,
      (void *) 0,
      (OS_STK*) (&join_app_vars.sendTaskStack[TASK_APP_SEND_STK_SIZE - 1]),
      TASK_APP_SEND_PRIORITY,
      TASK_APP_SEND_PRIORITY,
      (OS_STK*) join_app_vars.sendTaskStack,
      TASK_APP_SEND_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_SEND_PRIORITY, (INT8U*)TASK_APP_SEND_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

static void sendTask(void* unused) {
   dn_error_t      dnErr;
   INT8U           osErr;
   INT8U           pkBuf[sizeof(loc_sendtoNW_t)+PAYLOAD_LENGTH];
   loc_sendtoNW_t* pkToSend;
   INT8U           i;
   INT8U           rc;
   
   // wait for the loc_task to finish joining the network
   OSSemPend(join_app_vars.joinedSem, 0, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   // print
   dnm_ucli_printf("Done joining!\r\n");
   
   while (1) { // this is a task, it executes forever
      
      // wait a bit between packets
      OSTimeDly(10000);
      
      // prepare packet to send
      pkToSend = (loc_sendtoNW_t*)pkBuf;
      pkToSend->locSendTo.socketId          = loc_getSocketId();
      pkToSend->locSendTo.destAddr          = DN_MGR_IPV6_MULTICAST_ADDR;
      pkToSend->locSendTo.destPort          = 60000;
      pkToSend->locSendTo.serviceType       = DN_API_SERVICE_TYPE_BW;   
      pkToSend->locSendTo.priority          = DN_API_PRIORITY_MED;   
      pkToSend->locSendTo.packetId          = 0xFFFF;
      for (i=0;i<PAYLOAD_LENGTH;i++) {
         pkToSend->locSendTo.payload[i]     = 0x10+i;
      }
      
      // send packet
      dnErr = dnm_loc_sendtoCmd(
         pkToSend,
         PAYLOAD_LENGTH,
         &rc
      );
      ASSERT(dnErr==DN_ERR_NONE);
      
      // print
      if (rc==DN_API_RC_OK) {
          dnm_ucli_printf("packet sent\r\n");
      } else {
          dnm_ucli_printf("rc = 0x%02x\r\n",rc);
      }
   }
}

dn_error_t rxNotifCb(dn_api_loc_notif_received_t* rxFrame, INT8U length) {
   INT8U i;
   
   dnm_ucli_printf("packet received:\r\n");
   dnm_ucli_printf(" - sourceAddr: ");
   for (i=0;i<sizeof(dn_ipv6_addr_t);i++) {
      dnm_ucli_printf("%02x",((INT8U*)&(rxFrame->sourceAddr))[i]);
   }
   dnm_ucli_printf("\r\n");
   dnm_ucli_printf(" - sourcePort: %d\r\n",rxFrame->sourcePort);
   dnm_ucli_printf(" - data:       (%d bytes) ",length-sizeof(dn_api_loc_notif_received_t));
   for (i=0;i<length-sizeof(dn_api_loc_notif_received_t);i++) {
      dnm_ucli_printf("%02x",rxFrame->data[i]);
   }
   dnm_ucli_printf("\r\n");
   
   return DN_ERR_NONE;
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
