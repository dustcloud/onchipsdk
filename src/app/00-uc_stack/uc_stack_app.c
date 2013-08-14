/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include <string.h>
#include "stdio.h"
#include "cli_task.h"
#include "loc_task.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================

//=========================== prototypes ======================================

//===== CLI handlers
dn_error_t cli_iterCmdHandler(INT8U* arg, INT32U len, INT8U offset);
//===== tasks
static void stackTask(void* unused);
//===== helpers
INT32U recursiveSum(INT32U val);

//=========================== const ===========================================

const dnm_cli_cmdDef_t cliCmdDefs[] = {
   {&cli_iterCmdHandler,     "iter",   "[a]",         DN_CLI_ACCESS_USER},
   {NULL,                    NULL,     NULL,          0},
};

//=========================== variables =======================================

typedef struct {
   dnm_cli_cont_t cliContext;
   INT8U          iter;
   // stackTask
   OS_STK         stackTaskStack[TASK_APP_UC_STACK_STK_SIZE];
} uc_stack_app_vars_t;

uc_stack_app_vars_t uc_stack_app_vars;

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   dn_error_t      dnErr;
   INT8U           osErr;
   
   //==== initialize local variable
   
   memset(&uc_stack_app_vars,0,sizeof(uc_stack_app_vars));
   
   //==== initialize helper tasks
   
   cli_task_init(
      &uc_stack_app_vars.cliContext,        // cliContext
      "uc_stack",                           // appName
      &cliCmdDefs                           // cliCmds
   );
   loc_task_init(
      &uc_stack_app_vars.cliContext,        // cliContext
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== stackTask task
   
   osErr = OSTaskCreateExt(
      stackTask,
      (void *) 0,
      (OS_STK*) (&uc_stack_app_vars.stackTaskStack[TASK_APP_UC_STACK_STK_SIZE-1]),
      TASK_APP_UC_STACK_PRIORITY,
      TASK_APP_UC_STACK_PRIORITY,
      (OS_STK*) uc_stack_app_vars.stackTaskStack,
      TASK_APP_UC_STACK_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_UC_STACK_PRIORITY, (INT8U*)TASK_APP_UC_STACK_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== CLI handlers ====================================

dn_error_t cli_iterCmdHandler(INT8U* arg, INT32U len, INT8U offset) {
   char* token; 
   int   a;
   
   //--- param 0: iter
   token = dnm_cli_getNextToken(&arg, ' ');
   if (token == NULL) {
      return DN_ERR_INVALID;
   } else {  
      sscanf(token, "%d", &a);
   }
   
   //---- store
   uc_stack_app_vars.iter = (INT8U)a;
   
   return DN_ERR_NONE;
}

//=========================== stackTask task ==================================

static void stackTask(void* unused) {
   INT8U           osErr;
   OS_STK_DATA     stackSize;
   INT32U          val;
   
   while(1) { // this is a task, it executes forever
      
      // wait a bit
      OSTimeDly(2000);
      
      // fill stack
      val = recursiveSum(uc_stack_app_vars.iter);
      dnm_cli_printf("sum       %d\r\n",val);
      
      // print stack usage
      osErr = OSTaskStkChk(OS_PRIO_SELF, &stackSize);
      ASSERT(osErr==OS_ERR_NONE);
      dnm_cli_printf(
         "stack usage: %d bytes / %d bytes\r\n",
         4*stackSize.OSUsed,
         4*(stackSize.OSUsed + stackSize.OSFree)
      );
   }
}

//=========================== helpers =========================================

INT32U recursiveSum(INT32U val) {
    dnm_cli_printf("iteration %d\r\n",val);
    if (val==0) {
        return 0;
    } else {
        return val+recursiveSum(val-1);
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
