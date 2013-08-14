/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "cli_task.h"
#include "dnm_cli.h"
#include "Ver.h"

//=========================== variables =======================================

typedef struct {
   dnm_cli_cont_t*      cliContext;
   char*                appName;
   dnm_cli_cmdDef_t*    cliCmds;
   INT8U                cliChannelBuffer[DN_CH_ASYNC_RXBUF_SIZE(DN_CLI_NOTIF_SIZE)];
   CH_DESC              cliChannelDesc;
   OS_STK               cliTaskStack[CLI_TASK_STK_SIZE];
} cli_task_vars_t;

cli_task_vars_t cli_task_v;

//=========================== prototypes ======================================

static void cliTask(void* unused);

//=========================== public ==========================================

void cli_task_init(dnm_cli_cont_t* cliContext, char* appName, dnm_cli_cmdDef_t* cliCmds) {
   dn_error_t      dnErr;
   INT8U           osErr;
   OS_MEM*         cliChannelMem;
   
   // store params
   cli_task_v.cliContext     = cliContext;
   cli_task_v.appName        = appName;
   cli_task_v.cliCmds        = cliCmds;
   
   // open CLI port
   dnErr = dnm_cli_openPort(DN_CLI_PORT_C0, 9600);
   ASSERT(dnErr==DN_ERR_NONE);

   // change CLI access level
   dnErr = dnm_cli_changeAccessLevel(DN_CLI_ACCESS_USER);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // print appName
   dnm_cli_printf("%s app, ver %d.%d.%d.%d\n\r", cli_task_v.appName,
                                                 VER_MAJOR,
                                                 VER_MINOR,
                                                 VER_PATCH,
                                                 VER_BUILD);
   
   // stop here is no CLI commands to register
   if (cli_task_v.cliCmds==NULL) {
      return;
   }
   
   // create a memory block for CLI notification channel
   cliChannelMem = OSMemCreate(
       cli_task_v.cliChannelBuffer,
       1,
       DN_CH_ASYNC_RXBUF_SIZE(DN_CLI_NOTIF_SIZE),
       &osErr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   // create the CLI notification channel
   dnErr = dn_createAsyncChannel(
      cliChannelMem,
      &cli_task_v.cliChannelDesc
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // register the channel for CLI input messages
   dnErr = dn_registerChannel(
      cli_task_v.cliChannelDesc,
      DN_MSG_TYPE_CLI_NOTIF
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // create the CLI task
   osErr = OSTaskCreateExt(
      cliTask,
      NULL,
      (OS_STK*)&cli_task_v.cliTaskStack[CLI_TASK_STK_SIZE-1],
      CLI_TASK_PRIORITY,
      CLI_TASK_PRIORITY,
      (OS_STK*)cli_task_v.cliTaskStack,
      CLI_TASK_STK_SIZE,
      NULL,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(CLI_TASK_PRIORITY, CLI_TASK_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
}

//=========================== private =========================================

static void cliTask(void* unused) {
   dn_error_t  dnErr;
   
   // initialize the CLI context and declare CLI commands
   dnErr = dnm_cli_initContext(
      cli_task_v.cliContext,
      cli_task_v.cliChannelDesc,
      cli_task_v.cliCmds
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   while (1) { // this is a task, it executes forever
      
      dnErr = dnm_cli_input(cli_task_v.cliContext);
      ASSERT(dnErr==DN_ERR_NONE);
   }
}
