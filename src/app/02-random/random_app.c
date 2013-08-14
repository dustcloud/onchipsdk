/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_randhw.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== variables =======================================

/// variables local to this application
typedef struct {
   dnm_cli_cont_t       cliContext;
   OS_STK               randomTaskStack[TASK_APP_RANDOM_STK_SIZE];
} random_app_vars_t;

random_app_vars_t  random_app_v;

//=========================== prototypes ======================================

static void randomTask(void* unused);

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   INT8U        osErr;
   
   //==== initialize helper tasks
   
   cli_task_init(
      &random_app_v.cliContext,             // cliContext
      "random",                             // appName
      NULL                                  // cliCmds
   );
   loc_task_init(
      &random_app_v.cliContext,             // cliContext
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   // create the random task
   osErr  = OSTaskCreateExt(
      randomTask,
      (void *)0,
      (OS_STK*)(&random_app_v.randomTaskStack[TASK_APP_RANDOM_STK_SIZE-1]),
      TASK_APP_RANDOM_PRIORITY,
      TASK_APP_RANDOM_PRIORITY,
      (OS_STK*)random_app_v.randomTaskStack,
      TASK_APP_RANDOM_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_RANDOM_PRIORITY, (INT8U*)TASK_APP_RANDOM_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== random task =====================================

static void randomTask(void* unused) {
   dn_error_t      dnErr;
   int             numBytesRead;
   INT8U           randomBuf[16];
   INT8U           i;
   
   // open random device
   dnErr = dn_open(
      DN_RAND_DEV_ID,             // device
      NULL,                       // args
      0                           // argLen 
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // infinite loop
   while(1) {
      
      // this call blocks the task until the specified timeout expires (in ms)
      OSTimeDly(1000);
      
      // read temperature value
      numBytesRead = dn_read(
         DN_RAND_DEV_ID,          // device
         randomBuf,               // buf
         sizeof(randomBuf)        // bufSize 
      );
      ASSERT(numBytesRead==sizeof(randomBuf));
      
      // print
      dnm_cli_printf("randomBuf: ");
      for (i=0;i<sizeof(randomBuf);i++) {
         dnm_cli_printf("%02x",randomBuf[i]);
      }
      dnm_cli_printf("\r\n");
   }
}

//=============================================================================
//=========================== install a kernel header =========================
//=============================================================================

/**
A kernel header is a set of bytes prepended to the actual binary image of this
application. Thus header is needed for your application to start running.
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
