/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "dn_exe_hdr.h"
#include "dnm_ucli.h"
#include "dnm_local.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== defines =========================================

//=========================== variables =======================================

// Variables local to this application.
typedef struct {
   INT8U           locNotifChannelBuf[DN_API_LOC_MAXMSG_SIZE];
   CH_DESC         locNotifChannel;
   OS_STK          locNotifTaskStack[TASK_APP_LOCNOTIF_STK_SIZE];
} hello_app_vars_t;

hello_app_vars_t hello_app_vars;

//=========================== prototypes ======================================

//===== locNotifTask
static void        locNotifTask(void* unused);

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   INT8U           osErr;
   
   //===== initialize locNotifTask
   
   osErr = OSTaskCreateExt(
      locNotifTask,
      (void *) 0,
      (OS_STK*) (&hello_app_vars.locNotifTaskStack[TASK_APP_LOCNOTIF_STK_SIZE - 1]),
      TASK_APP_LOCNOTIF_PRIORITY,
      TASK_APP_LOCNOTIF_PRIORITY,
      (OS_STK*) hello_app_vars.locNotifTaskStack,
      TASK_APP_LOCNOTIF_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_LOCNOTIF_PRIORITY, (INT8U*)TASK_APP_LOCNOTIF_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

static void locNotifTask(void* unused) {
   dn_error_t      dnErr;
   
   //===== initialize dnm_ucli module
   
   // open CLI port
   dnErr = dnm_ucli_openPort(DN_CLI_PORT_C0, DEFAULT_BAUDRATE);
   ASSERT(dnErr==DN_ERR_NONE);
   
   //===== initialize dnm_local module
   
   // create a synchronous channel for the dnm_local module to receive notifications from the stack
   dnErr = dn_createSyncChannel(&hello_app_vars.locNotifChannel);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // register that channel to DN_MSG_TYPE_NET_NOTIF notifications
   dnErr = dn_registerChannel(hello_app_vars.locNotifChannel, DN_MSG_TYPE_NET_NOTIF);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // initialize the local interface
   dnErr = dnm_loc_init(
      PASSTHROUGH_OFF,                           // mode
      hello_app_vars.locNotifChannelBuf,         // pBuffer
      sizeof(hello_app_vars.locNotifChannelBuf)  // buffLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
  
   //===== print over CLI
   
   dnm_ucli_printf("Hello, World!\r\n");
   
   while (1) { // this is a task, it executes forever
      
      // have the dnm_local module process notifications
      dnm_loc_processNotifications();
   }
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
