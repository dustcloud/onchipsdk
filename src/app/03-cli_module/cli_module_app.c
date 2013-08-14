/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "stdio.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dnm_cli.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== defines =========================================

//=========================== prototypes ======================================

//===== CLI handlers
dn_error_t cli_sumCmdHandler(INT8U* arg, INT32U len, INT8U offset);
//===== tasks
static void cliTask(void* unused);

//=========================== const ===========================================

const dnm_cli_cmdDef_t cliCommandDefinitions[] = {
   {&cli_sumCmdHandler,      "sum",    "[a b]",       DN_CLI_ACCESS_USER},
   {NULL,                    NULL,     NULL,          0},
};

//=========================== variables =======================================

// Variables local to this application.
typedef struct {
   dnm_cli_cont_t  cliContext;
   INT8U           cliChannelBuffer[DN_CH_ASYNC_RXBUF_SIZE(DN_CLI_NOTIF_SIZE)];
   CH_DESC         cliChannelDesc;
   OS_STK          cliTaskStack[APP_TASK_CLI_STK_SIZE];
} cli_module_app_vars_t;

cli_module_app_vars_t cli_module_app_vars;

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   dn_error_t      dnErr;
   INT8U           osErr;
   OS_MEM*         cliChannelMem;
   
   loc_task_init(
      &cli_module_app_vars.cliContext,      // cliContext
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   // open CLI port
   dnErr = dnm_cli_openPort(DN_CLI_PORT_C0, 9600);
   ASSERT(dnErr==DN_ERR_NONE);

   // change CLI access level
   dnErr = dnm_cli_changeAccessLevel(DN_CLI_ACCESS_USER);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // print appName
   dnm_cli_printf("cli_module app, ver %d.%d.%d.%d\n\r",   VER_MAJOR,
                                                           VER_MINOR,
                                                           VER_PATCH,
                                                           VER_BUILD);
   
   // Create a memory block for CLI notification channel
   cliChannelMem = OSMemCreate(
       cli_module_app_vars.cliChannelBuffer,
       1,
       DN_CH_ASYNC_RXBUF_SIZE(DN_CLI_NOTIF_SIZE),
       &osErr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   // Create the CLI notification channel
   dnErr = dn_createAsyncChannel(
      cliChannelMem,
      &cli_module_app_vars.cliChannelDesc
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // Register the channel for CLI input messages
   dnErr = dn_registerChannel(
      cli_module_app_vars.cliChannelDesc,
      DN_MSG_TYPE_CLI_NOTIF
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // create CLI task
   osErr = OSTaskCreateExt(
      cliTask,
      NULL,
      (OS_STK*)&cli_module_app_vars.cliTaskStack[APP_TASK_CLI_STK_SIZE-1],
      APP_TASK_CLI_PRIORITY,
      APP_TASK_CLI_PRIORITY,
      (OS_STK*)cli_module_app_vars.cliTaskStack,
      APP_TASK_CLI_STK_SIZE,
      NULL,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(APP_TASK_CLI_PRIORITY, APP_TASK_CLI_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== CLI handlers ====================================

dn_error_t cli_sumCmdHandler(INT8U* arg, INT32U len, INT8U offset) {
   char* token;
   int   a;
   int   b;
  
   //--- param 0: a
   token = dnm_cli_getNextToken(&arg, ' ');
   if (token == NULL) {
      return DN_ERR_INVALID;
   } else {  
      sscanf(token, "%d", &a);
   }
   
   //--- param 1: b
   token = dnm_cli_getNextToken(&arg, ' ');
   if (token == NULL) {
      return DN_ERR_INVALID;
   } else {  
      sscanf(token, "%d", &b);
   }
   
   //---- print sum
   dnm_cli_printf("sum: %d",a+b);
   
   return DN_ERR_NONE;
}

//=========================== CLI task ========================================

static void cliTask(void* unused) {
   dn_error_t      dnErr;
   
   // initialize the CLI context and declare CLI commands
   dnErr = dnm_cli_initContext(
      &cli_module_app_vars.cliContext,
      cli_module_app_vars.cliChannelDesc,
      cliCommandDefinitions
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   while (1) { // this is a task, it executes forever
      
      dnErr = dnm_cli_input(&cli_module_app_vars.cliContext);
      ASSERT(dnErr==DN_ERR_NONE);
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
