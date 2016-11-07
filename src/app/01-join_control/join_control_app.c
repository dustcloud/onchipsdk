/*
Copyright (c) 2016, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "dn_exe_hdr.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dnm_local.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== defines =========================================
#define HI_DUTY            255
#define LOW_DUTY           5
#define HI_TIMEOUT         15 * SECOND   // 2x average sync time at 100%, so if a manager is in range we'll likely hear it
#define LOW_TIMEOUT        150 * SECOND  // Average sync time to manager at 5%
#define IDLE_TIMEOUT       30 * SECOND   // If we don't hear at either two, might want to wait a lot longer
//=========================== variables =======================================
typedef struct {
   OS_EVENT*       joinedSem;
   OS_STK          joinTaskStack[TASK_APP_JOIN_STK_SIZE];
} join_app_vars_t;

join_app_vars_t join_app_vars;

//=========================== prototypes ======================================

static void joinTask(void* unused);

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   INT8U           osErr;
   
   // create semaphore for loc_task to indicate when joined
   join_app_vars.joinedSem = OSSemCreate(0);
   ASSERT(join_app_vars.joinedSem != NULL);
   
   //===== initialize helper tasks
   
   cli_task_init(
      "Join Control",                       // appName
      NULL                                  // cliCmds
   );
   
   loc_task_init(
      JOIN_YES,                             // fJoin - local module will try to join
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      join_app_vars.joinedSem,              // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== initialize joinTask  
   osErr = OSTaskCreateExt(
      joinTask,
      (void *) 0,
      (OS_STK*) (&join_app_vars.joinTaskStack[TASK_APP_JOIN_STK_SIZE - 1]),
      TASK_APP_JOIN_PRIORITY,
      TASK_APP_JOIN_PRIORITY,
      (OS_STK*) join_app_vars.joinTaskStack,
      TASK_APP_JOIN_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_JOIN_PRIORITY, (INT8U*)TASK_APP_JOIN_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

static void joinTask(void* unused) {
   dn_error_t      dnErr;
   INT8U           osErr;
   INT8U           joinDC;
   INT8U           rc;
   INT8U           fJoined = FALSE;
   
   // Give stack time to print banner
   OSTimeDly(1 * SECOND);
   
   while (1) { // this is a task, it executes forever
      
      if(!fJoined){
         dnm_ucli_printf("Setting Join Duty Cycle to 100%% for %d seconds\r\n", HI_TIMEOUT/SECOND);
         joinDC = HI_DUTY;
         dnErr = dnm_loc_setParameterCmd(
                              DN_API_PARAM_JOINDUTYCYCLE,   // parameter
                              &joinDC,                      // payload (parameter value)
                              sizeof(INT8U),                // length
                              &rc                           // rc
         );
         ASSERT(dnErr == DN_ERR_NONE);
         ASSERT(rc == DN_API_RC_OK);
             
         // wait for the loc_task to finish joining the network with a timeout
         OSSemPend(join_app_vars.joinedSem, HI_TIMEOUT, &osErr);
         // We only expect OSSemPend to return OS_ERR_NONE or OS_ERR_TIMEOUT
         if(osErr==OS_ERR_NONE){
            fJoined = TRUE;
         }     
      }
      
      // We will get here if we've already started but not completed joining within the HI_TIMEOUT, but
      // changing joinDC at this point won't interrupt the join process
      if(!fJoined){          
         dnm_ucli_printf("Setting Join Duty Cycle to 5%% for %d seconds\r\n", LOW_TIMEOUT/SECOND);
         joinDC = LOW_DUTY;
         dnErr = dnm_loc_setParameterCmd(
                              DN_API_PARAM_JOINDUTYCYCLE,   // parameter
                              &joinDC,                      // payload (parameter value)
                              sizeof(INT8U),                // length
                              &rc                           // rc
         );
         ASSERT(dnErr == DN_ERR_NONE);
         ASSERT(rc == DN_API_RC_OK);
                                 
         // wait for the loc_task to finish joining the network
         OSSemPend(join_app_vars.joinedSem, LOW_TIMEOUT, &osErr);
         if(osErr==OS_ERR_NONE){
            fJoined = TRUE;
         }     
      }
     
      if(!fJoined){
         dnm_ucli_printf("Stop listening and return to idle state for %d seconds\r\n", IDLE_TIMEOUT/SECOND);
         dnErr = dnm_loc_stopSearchCmd(&rc);
         ASSERT(dnErr == DN_ERR_NONE);
         ASSERT(rc == DN_API_RC_OK);
                                 
         OSTimeDly(IDLE_TIMEOUT);
         
         dnm_ucli_printf("Starting to try joining again...\r\n");
         dnErr = dnm_loc_joinCmd(&rc); 	
         ASSERT(dnErr == DN_ERR_NONE);
         ASSERT(rc == DN_API_RC_OK);
      }
             
      if(fJoined){
         dnm_ucli_printf("I finally joined!\r\n");
         OSTimeDly(35 * SECOND);
      }
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
