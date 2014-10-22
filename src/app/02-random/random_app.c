/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_randhw.h"
#include "dn_exe_hdr.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== variables =======================================

/// variables local to this application
typedef struct {
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
      "random",                             // appName
      NULL                                  // cliCmds
   );
   loc_task_init(
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
      dnm_ucli_printf("randomBuf: ");
      for (i=0;i<sizeof(randomBuf);i++) {
         dnm_ucli_printf("%02x",randomBuf[i]);
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
