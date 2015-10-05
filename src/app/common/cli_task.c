/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include <string.h>
#include "dn_common.h"
#include "dn_system.h"
#include "dnm_ucli.h"
#include "cli_task.h"
#include "Ver.h"

//=========================== variables =======================================

typedef struct {
   char*                appName;
   dnm_ucli_cmdDef_t*   cliCmds;
   INT32U               cliChannelBuffer[1+DN_CH_ASYNC_RXBUF_SIZE(DN_CLI_NOTIF_SIZE)/sizeof(INT32U)];
   CH_DESC              cliChannelDesc;
   OS_STK               cliTaskStack[CLI_TASK_STK_SIZE];
   INT8U                numCliCommands;
} cli_task_vars_t;

cli_task_vars_t cli_task_v;

//=========================== prototypes ======================================

static void cliTask(void* unused);

//=========================== public ==========================================

void cli_task_init(char* appName, dnm_ucli_cmdDef_t* cliCmds) {
   dn_error_t      dnErr;
   INT8U           osErr;
   OS_MEM*         cliChannelMem;
   
   // store params
   cli_task_v.appName        = appName;
   cli_task_v.cliCmds        = cliCmds;
   
   // open CLI port
   dnErr = dnm_ucli_openPort(DN_CLI_PORT_C0, DEFAULT_BAUDRATE);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // change CLI access level
   dnErr = dnm_ucli_changeAccessLevel(DN_CLI_ACCESS_USER);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // print appName
   dnm_ucli_printf("%s app, ver %d.%d.%d.%d\r\n", cli_task_v.appName,
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

/**
\brief CLI notification handler.

This function is called each time a command is entered by the user.

\pre This function needs to be passed to the #dnm_ucli_init() function.

\param[in] type  The nofication type.
\param[in] cmdId The identifier of the command entered.
\param[in] pCmdParams A pointer to the the parameter to pass to the handler.
\param[in] paramsLen The number of bytes in the pCmdParams buffer.
*/
void cli_procNotif(INT8U type, INT8U cmdId, INT8U *pCmdParams, INT8U paramsLen) {
   dn_error_t  dnErr = DN_ERR_NONE;

   if (
         cli_task_v.cliCmds==NULL ||
         cmdId > cli_task_v.numCliCommands
      ) {
      dnm_ucli_printf("Command not supported\r\n");
      return;
   }

   if (type == DN_CLI_NOTIF_INPUT) {
      dnErr = (cli_task_v.cliCmds[cmdId].handler)(pCmdParams, paramsLen);
      if (dnErr == DN_ERR_INVALID) {
         dnm_ucli_printf("Invalid argument(s)\r\n");
      }
   }

   // Print help
   if (type == DN_CLI_NOTIF_HELP) {
      dnm_ucli_printf("Usage: %s\r\n", cli_task_v.cliCmds[cmdId].usage);
   }
   
   dnm_ucli_printf("\r\n> ");
}

//=========================== private =========================================

/**
\brief Register the CLI commands.

\pre The list of commands have already been stored in the cli_task_v.cliCmds
   variable when calling #cli_task_init() function.

\return DN_ERR_ERROR is the #cli_task_init() hasn't been called when calling
   this function.
\return 
*/
static dn_error_t cli_registerCommands(void) {
   INT8U                  i;
   INT8U                  cmdLen;
   dn_cli_registerCmd_t*  rCmd;
   dnm_ucli_cmdDef_t*     pCmd;
   INT8U                  buf[DN_CLI_CTRL_SIZE];
   dn_error_t             rc;

   if (cli_task_v.cliCmds==NULL) {
      return DN_ERR_ERROR;
   }

   i  = 0;
   rc = DN_ERR_NONE;
   
   // go through the array of available commands and register them
   while (1) {
      
      // retrieve the next command to register
      pCmd = &cli_task_v.cliCmds[i];
      
      // stop the loop if no more commands
      if (pCmd->handler==NULL) {
         
         break;
      }
      
      // prepare the command registration parameter
      rCmd                   = (dn_cli_registerCmd_t*)buf;
      rCmd->hdr.cmdId        = (INT8U)i;
      rCmd->hdr.chDesc       = cli_task_v.cliChannelDesc;
      rCmd->hdr.lenCmd       = (INT8U)strlen(pCmd->command);
      rCmd->hdr.accessLevel  = pCmd->accessLevel;
      
      // verify the length of the resulting command
      cmdLen = sizeof(dn_cli_registerCmdHdr_t) + rCmd->hdr.lenCmd;
      if (cmdLen > sizeof(buf)) {
         rc = DN_ERR_SIZE;
         break;
      }
      
      // copy the command string
      memcpy(rCmd->data, pCmd->command, rCmd->hdr.lenCmd);
      
      // register the command with the CLI device
      rc = dn_ioctl(
         DN_CLI_DEV_ID,
         DN_IOCTL_CLI_REGISTER,
         (void*)rCmd,
         sizeof(dn_cli_registerCmd_t)
      );
      if (rc != DN_ERR_NONE) {
         break;
      }
      
      // increment to the next command
      i++;
   }
   
   // remember how many CLI commands there are
   cli_task_v.numCliCommands = i;
   
   // return the error returned during registration
   return rc;
}

/**
\brief Task which handles CLI interaction.

\param[in] unused Unused parameter
*/
static void cliTask(void* unused) {
   dn_error_t  dnErr;
   
   // initialize the CLI module
   dnm_ucli_init(cli_procNotif);
   
   // register the commands
   // Note: the commands are already stored in cli_task_v.cliCmds
   dnErr = cli_registerCommands();
   ASSERT(dnErr==DN_ERR_NONE);

   while (1) { // this is a task, it executes forever
      
      // the following line is blocking
      dnErr = dnm_ucli_input(cli_task_v.cliChannelDesc);
      ASSERT(dnErr==DN_ERR_NONE);
   }
}
