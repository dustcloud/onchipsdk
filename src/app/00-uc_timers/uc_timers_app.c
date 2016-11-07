/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "dn_exe_hdr.h"
#include <string.h>
#include "stdio.h"
#include "cli_task.h"
#include "loc_task.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================

#define DELAY_TIMER_A_TASK 100
#define DELAY_TIMER_B_TASK 105

//=========================== prototypes ======================================

//===== tasks
static void timerATask(void* unused);
static void timerBTask(void* unused);
//===== helpers
void timerB_cb(void* pTimer, void *pArgs);

//=========================== const ===========================================

//=========================== variables =======================================

typedef struct {
   INT8U          iter;
   OS_EVENT*      timerBsem;
   // timerATask
   OS_STK         timerATaskStack[TASK_APP_UC_TIMER_A_STK_SIZE];
   // timerBTask
   OS_STK         timerBTaskStack[TASK_APP_UC_TIMER_B_STK_SIZE];
} uc_timers_app_vars_t;

uc_timers_app_vars_t uc_timers_app_vars;

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   INT8U           osErr;
   
   //==== initialize local variable
   
   memset(&uc_timers_app_vars,0,sizeof(uc_timers_app_vars));
   
   //==== initialize helper tasks
   
   cli_task_init(
      "uc_timers",                          // appName
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
   
   //===== timerATask task
   
   osErr = OSTaskCreateExt(
      timerATask,
      (void *) 0,
      (OS_STK*) (&uc_timers_app_vars.timerATaskStack[TASK_APP_UC_TIMER_A_STK_SIZE-1]),
      TASK_APP_UC_TIMER_A_PRIORITY,
      TASK_APP_UC_TIMER_A_PRIORITY,
      (OS_STK*) uc_timers_app_vars.timerATaskStack,
      TASK_APP_UC_TIMER_A_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_UC_TIMER_A_PRIORITY, (INT8U*)TASK_APP_UC_TIMER_A_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   //===== timerBTask task
   
   osErr = OSTaskCreateExt(
      timerBTask,
      (void *) 0,
      (OS_STK*) (&uc_timers_app_vars.timerBTaskStack[TASK_APP_UC_TIMER_B_STK_SIZE-1]),
      TASK_APP_UC_TIMER_B_PRIORITY,
      TASK_APP_UC_TIMER_B_PRIORITY,
      (OS_STK*) uc_timers_app_vars.timerBTaskStack,
      TASK_APP_UC_TIMER_B_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_UC_TIMER_B_PRIORITY, (INT8U*)TASK_APP_UC_TIMER_B_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== tasks ===========================================

static void timerATask(void* unused) {
   
   while(1) { // this is a task, it executes forever
      
      // wait a bit
      OSTimeDly(DELAY_TIMER_A_TASK);
      
      // print
      dnm_ucli_printf("A");
   }
}

static void timerBTask(void* unused) {
   INT8U    osErr;
   OS_TMR*  timer;
   
   // create a semaphore
   uc_timers_app_vars.timerBsem = OSSemCreate(0);
   ASSERT (uc_timers_app_vars.timerBsem!=NULL);
   
   // create a timer
   timer = OSTmrCreate(
      DELAY_TIMER_B_TASK,              // dly
      DELAY_TIMER_B_TASK,              // period
      OS_TMR_OPT_PERIODIC,             // opt
      (OS_TMR_CALLBACK)&timerB_cb,     // callback
      NULL,                            // callback_arg
      NULL,                            // pname
      &osErr                           // perr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   // start the timer
   OSTmrStart(timer, &osErr);
   ASSERT (osErr == OS_ERR_NONE);
   
   while(1) { // this is a task, it executes forever
      
      // wait for the semaphore to be posted
      OSSemPend(
         uc_timers_app_vars.timerBsem, // pevent
         0,                            // timeout
         &osErr                        // perr
      );
      ASSERT (osErr == OS_ERR_NONE);
      
      // print
      dnm_ucli_printf("B");
   }
}

//=========================== helpers =========================================

void timerB_cb(void* pTimer, void *pArgs) {
   INT8U  osErr;
   
   // post the semaphore
   osErr = OSSemPost(uc_timers_app_vars.timerBsem);
   ASSERT(osErr == OS_ERR_NONE);
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

