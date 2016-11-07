/*
Copyright (c) 2016, Dust Networks.  All rights reserved.

This sample assumes that if the txDone is received, the packet was sent
and moves on to the next one.  A production application should test whether txDone
returns a success and decide whether to retry or discard the packet instead of
resetting as is done here.

For demo purposes the blink period is short. In practice, blink is lower energy than
being in the network when the period is hours.

*/
// C includes
#include <string.h>

#include "dn_common.h"
#include "dn_exe_hdr.h"
#include "dn_api_param.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dnm_local.h"
#include "Ver.h"

//app includes
#include "app_task_cfg.h"

//=========================== defines =========================================
#define BLINK_PERIOD                   30 * SECOND 
#define BLINK_TIMEOUT                  2 * MINUTE  // if can't send for this long, reset
#define WITH_NEIGHBORS                 1
#define NO_NEIGHBORS                   0

#define MAX_PAYLOAD                    DN_API_LOC_MAX_BLINK_PAYLOAD_NO_NBRS

//=========================== variables =======================================

typedef struct {
   OS_STK                       blinkTaskStack[TASK_APP_BLINK_STK_SIZE];
   OS_EVENT*                    blinkSem;
} blink_app_vars_t;

blink_app_vars_t app_vars;

//=========================== prototypes ======================================

static void blinkTask(void* unused);
dn_error_t txDoneNotifCb(dn_api_loc_notif_txdone_t *txNotif, INT8U len);

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   INT8U           osErr;
   
   // register event callback
   dnm_loc_registerTxDoneNotifCallback(txDoneNotifCb);
   
   //Semaphore to tell blinkTask that it's OK to blink because prior packet went out
   app_vars.blinkSem = OSSemCreate(1);   
   ASSERT(app_vars.blinkSem!=NULL);
   
   //===== initialize helper tasks
 
   cli_task_init(
      "Blink",                              // appName
      NULL                                  // cliCmds
   );
   loc_task_init(
      JOIN_NO,                              // fJoin
      NULL,                                 // netId
      NULL,                                 // udpPort - blink packets use a predefined port
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== initialize blinkTask
   
   osErr = OSTaskCreateExt(
      blinkTask,
      (void *) 0,
      (OS_STK*) (&app_vars.blinkTaskStack[TASK_APP_BLINK_STK_SIZE - 1]),
      TASK_APP_BLINK_PRIORITY,
      TASK_APP_BLINK_PRIORITY,
      (OS_STK*) app_vars.blinkTaskStack,
      TASK_APP_BLINK_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_BLINK_PRIORITY, (INT8U*)TASK_APP_BLINK_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

static void blinkTask(void* unused) {
   dn_error_t           dnErr;
   INT8U                osErr = OS_ERR_NONE;
   INT8U                payload[MAX_PAYLOAD];
   INT8U                rc;
   INT8U                i;
   INT8U                fNeighbors = NO_NEIGHBORS;
   INT8U                size;
   
   // Give stack time to print banner
   OSTimeDly(1 * SECOND);
   
   // fill in blink payload
   for(i=0; i< DN_API_LOC_MAX_BLINK_PAYLOAD_NO_NBRS; i++){
            payload[i] = i;
   }
   
   while (1) { // this is a task, it executes forever    
      // Wait for txDone callback on previous blink
      OSSemPend(app_vars.blinkSem, BLINK_TIMEOUT, &osErr);
      if(osErr != OS_ERR_NONE){
         dnm_ucli_printf("Blink timed out - resetting\r\n");
         dnm_loc_resetCmd(&rc);
         ASSERT(rc == DN_API_RC_OK);
         // wait for stack to reset (normally 5 s)
         OSTimeDly(30 * SECOND);
      }
      
      if(fNeighbors == NO_NEIGHBORS){ 
         size = DN_API_LOC_MAX_BLINK_PAYLOAD_NO_NBRS;     
      }
      else {         
         size = DN_API_LOC_MAX_BLINK_PAYLOAD_WITH_NBRS;
      }
          
      // blink the payload - 
      dnErr = dnm_loc_blinkPayload(
                                    payload, 
                                    size, 
                                    fNeighbors, 
                                    &rc
                                    );
      ASSERT(dnErr == DN_ERR_NONE);
      
      dnm_ucli_printf("Blink packet queued ");
      
      if(fNeighbors == NO_NEIGHBORS){ 
         dnm_ucli_printf("without neighbors\r\n");
         fNeighbors = WITH_NEIGHBORS;
      }
      else{
         dnm_ucli_printf("with neighbors\r\n");
         fNeighbors = NO_NEIGHBORS;
      }
      
      // Wait for the blink period to elapse
      OSTimeDly(BLINK_PERIOD);
   }
}

// ========== callbacks
dn_error_t txDoneNotifCb(dn_api_loc_notif_txdone_t *txNotif, INT8U len) {
   INT8U                osErr;
 
   if(txNotif->status == DN_API_TXSTATUS_OK){
      dnm_ucli_printf("packet sent\r\n");
   }
   else{
      dnm_ucli_printf("packet failed at link layer with status %d\r\n", txNotif->status);
   }
   
   // Tell blinkTask OK to blink
   osErr = OSSemPost(app_vars.blinkSem);
   ASSERT(osErr==OS_ERR_NONE);
   
   return DN_ERR_NONE;
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
