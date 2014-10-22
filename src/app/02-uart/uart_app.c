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

#define DLFT_LEN               10
#define DLFT_DELAY             100
#define TX_BUFFER_PATTERN      0x0a
#define MAX_UART_PACKET_SIZE   (128u)
#define MAX_UART_TRX_CHNL_SIZE (sizeof(dn_chan_msg_hdr_t) + MAX_UART_PACKET_SIZE)

//=========================== prototypes ======================================

//===== CLI handlers
dn_error_t cli_lenCmdHandler(INT8U* arg, INT32U len);
dn_error_t cli_delayCmdHandler(INT8U* arg, INT32U len);
dn_error_t cli_txCmdHandler(INT8U* arg, INT32U len);
//===== tasks
static void  uartTxTask(void* unused);
static void  uartRxTask(void* unused);

//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_lenCmdHandler,      "len",         "length",      DN_CLI_ACCESS_LOGIN},
   {&cli_delayCmdHandler,    "delay",       "num ms",      DN_CLI_ACCESS_LOGIN},
   {&cli_txCmdHandler,       "tx",          "num packets", DN_CLI_ACCESS_LOGIN},
   {NULL,                    NULL,          NULL,          0},
};

//=========================== variables =======================================

typedef struct {
   // uartTxTask
   OS_STK          uartTxTaskStack[TASK_APP_UART_TX_STK_SIZE];
   INT8U           uartTxBuffer[MAX_UART_PACKET_SIZE];
   INT16U          uartTxLen;
   INT16U          uartTxDelay;
   OS_EVENT*       uartTxSem;
   INT16U          uartTxNumLeft;
   // uartRxTask
   OS_STK          uartRxTaskStack[TASK_APP_UART_RX_STK_SIZE];
   INT32U          uartRxChannelMemBuf[1+MAX_UART_TRX_CHNL_SIZE/sizeof(INT32U)];
   OS_MEM*         uartRxChannelMem;
   CH_DESC         uartRxChannel;
   INT8U           uartRxBuffer[MAX_UART_PACKET_SIZE];
} uart_app_vars_t;

uart_app_vars_t    uart_app_v;

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   INT8U  osErr;
   
   //==== initialize local variables
   memset(&uart_app_v,0x00,sizeof(uart_app_v));
   uart_app_v.uartTxLen      = DLFT_LEN;
   uart_app_v.uartTxDelay    = DLFT_DELAY;
   
   //==== initialize helper tasks
   
   cli_task_init(
      "uart",                               // appName
      &cliCmdDefs                           // cliCmds
   );
   loc_task_init(
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== create tasks
   
   // uartTxTask task
   osErr  = OSTaskCreateExt(
      uartTxTask,
      (void *)0,
      (OS_STK*)(&uart_app_v.uartTxTaskStack[TASK_APP_UART_TX_STK_SIZE-1]),
      TASK_APP_UART_TX_PRIORITY,
      TASK_APP_UART_TX_PRIORITY,
      (OS_STK*)uart_app_v.uartTxTaskStack,
      TASK_APP_UART_TX_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_UART_TX_PRIORITY, (INT8U*)TASK_APP_UART_TX_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   // uartRxTask task
   osErr  = OSTaskCreateExt(
      uartRxTask,
      (void *)0,
      (OS_STK*)(&uart_app_v.uartRxTaskStack[TASK_APP_UART_RX_STK_SIZE-1]),
      TASK_APP_UART_RX_PRIORITY,
      TASK_APP_UART_RX_PRIORITY,
      (OS_STK*)uart_app_v.uartRxTaskStack,
      TASK_APP_UART_RX_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_UART_RX_PRIORITY, (INT8U*)TASK_APP_UART_RX_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== CLI handlers ====================================

dn_error_t cli_lenCmdHandler(INT8U* arg, INT32U len) {
   int   uartTxLen, l;
   
   //--- param 0: len
   l = sscanf(arg, "%d", &uartTxLen);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   
   //---- store
   uart_app_v.uartTxLen = (INT16U)uartTxLen;
   
   return DN_ERR_NONE;
}

dn_error_t cli_delayCmdHandler(INT8U* arg, INT32U len) {
   int   delay, l;
   
   //--- param 0: len
   l = sscanf(arg, "%d", &delay);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   
   //---- store
   uart_app_v.uartTxDelay = (INT16U)delay;
   
   return DN_ERR_NONE;
}

dn_error_t cli_txCmdHandler(INT8U* arg, INT32U len) {
   int   numLeft, l;
   INT8U osErr;
   
   //--- param 0: len
   l = sscanf(arg, "%d", &numLeft);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   
   //---- store
   uart_app_v.uartTxNumLeft = (INT16U)numLeft;
   
   //---- post semaphore
   osErr = OSSemPost(uart_app_v.uartTxSem);
   ASSERT(osErr == OS_ERR_NONE);
   
   return DN_ERR_NONE;
}

//=========================== tasks ===========================================

static void uartTxTask(void* unused) {
   INT8U      osErr;
   dn_error_t dnErr;
   INT8U      reply;
   INT32U     replyLen;
   
   // create a semaphore
   uart_app_v.uartTxSem = OSSemCreate(0);
   ASSERT (uart_app_v.uartTxSem!=NULL);
   
   // prepare TX buffer
   memset(uart_app_v.uartTxBuffer,TX_BUFFER_PATTERN,sizeof(uart_app_v.uartTxBuffer));
   
   while(1) { // this is a task, it executes forever
      
      // wait for the semaphore to be posted
      OSSemPend(
         uart_app_v.uartTxSem,         // pevent
         0,                            // timeout
         &osErr                        // perr
      );
      ASSERT (osErr == OS_ERR_NONE);
      
      // print
      dnm_ucli_printf("Sending %d UART packets, %d bytes, delay %d ms\r\n",
         uart_app_v.uartTxNumLeft,
         uart_app_v.uartTxLen,
         uart_app_v.uartTxDelay
      );
      
      while(uart_app_v.uartTxNumLeft>0) {
         
         // send packet
         dnErr = dn_sendSyncMsgByType(
             uart_app_v.uartTxBuffer,
             uart_app_v.uartTxLen,
             DN_MSG_TYPE_UART_TX_CTRL,
             (void*)&reply,
             sizeof(reply),
             &replyLen
         );
         ASSERT(replyLen==sizeof(INT8U));
         ASSERT(reply==DN_ERR_NONE);
         
         // decrement
         uart_app_v.uartTxNumLeft--;
         
         // wait a bit
         if (uart_app_v.uartTxDelay) {
            OSTimeDly(uart_app_v.uartTxDelay);
         }
      }
      
      // print
      dnm_ucli_printf("done.\r\n");
   }
}

static void uartRxTask(void* unused) {
   dn_error_t           dnErr;
   INT8U                osErr;
   dn_uart_open_args_t  uartOpenArgs;
   INT32U               rxLen;
   INT32U               msgType;
   INT8U                i;
   INT32S               err;
   
   // create the memory block for the UART channel
   uart_app_v.uartRxChannelMem = OSMemCreate(
      uart_app_v.uartRxChannelMemBuf,
      1,
      sizeof(uart_app_v.uartRxChannelMemBuf),
      &osErr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   // create an asynchronous notification channel
   dnErr = dn_createAsyncChannel(uart_app_v.uartRxChannelMem, &uart_app_v.uartRxChannel);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // associate the channel descriptor with UART notifications
   dnErr = dn_registerChannel(uart_app_v.uartRxChannel, DN_MSG_TYPE_UART_NOTIF);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // open the UART device
   uartOpenArgs.rxChId    = uart_app_v.uartRxChannel;
   uartOpenArgs.eventChId   = 0;
   uartOpenArgs.rate        = 115200u;
   uartOpenArgs.mode        = DN_UART_MODE_M4;
   uartOpenArgs.ctsOutVal   = 0;
   uartOpenArgs.fNoSleep    = 0;
   err = dn_open(
      DN_UART_DEV_ID,
      &uartOpenArgs,
      sizeof(uartOpenArgs)
   );
   ASSERT(err>=0);
   
   while(1) { // this is a task, it executes forever
      
      // wait for UART messages
      dnErr = dn_readAsyncMsg(
         uart_app_v.uartRxChannel,          // chDesc
         uart_app_v.uartRxBuffer,           // msg
         &rxLen,                            // rxLen
         &msgType,                          // msgType
         MAX_UART_PACKET_SIZE,              // maxLen
         0                                  // timeout (0==never)
      );
      ASSERT(dnErr==DN_ERR_NONE);
      ASSERT(msgType==DN_MSG_TYPE_UART_NOTIF);
      
      // print message received
      dnm_ucli_printf("uart RX (%d bytes)",rxLen);
      for (i=0;i<rxLen;i++) {
         dnm_ucli_printf(" %02x",uart_app_v.uartRxBuffer[i]);
      }
      dnm_ucli_printf("\r\n");
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
