/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "dnm_cli.h"
#include "dnm_local.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== defines =========================================

//=========================== variables =======================================

// Variables local to this application.
typedef struct {
   dnm_cli_cont_t  cliContext;
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
   dn_error_t      dnErr;
   INT8U           osErr;
   
   //===== initialize dnm_cli module
   
   // open CLI port
   dnErr = dnm_cli_openPort(DN_CLI_PORT_C0, 9600);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // change CLI access level
   dnErr = dnm_cli_changeAccessLevel(DN_CLI_ACCESS_USER);
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
      &hello_app_vars.cliContext,                // cliContext
      0,                                         // TraceFlag
      hello_app_vars.locNotifChannelBuf,         // pBuffer
      sizeof(hello_app_vars.locNotifChannelBuf)  // buffLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
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
     
   // print something over CLI
   dnm_cli_printf("Hello, World!\r\n");
   
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

#include "loader.h"

_Pragma("location=\".kernel_exe_hdr\"") __root
const exec_par_hdr_t kernelExeHdr = {
   {'E', 'X', 'E', '1'},
   OTAP_UPGRADE_IDLE,
   LOADER_CRC_IGNORE,
   0,
   {VER_MAJOR, VER_MINOR, VER_PATCH, VER_BUILD},
   0,
   DUST_VENDOR_ID,
   EXEC_HDR_RESERVED_PAD
};
