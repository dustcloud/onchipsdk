/*
Copyright (c) 2016, Dust Networks.  All rights reserved.
*/

// C includes
#include <string.h>
#include <stdio.h>

//OCSDK includes
#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_exe_hdr.h"
#include "Ver.h"

//app includes
#include "app_task_cfg.h"

//=========================== defines =========================================

//=========================== variables =======================================

typedef struct {
   OS_STK                    eventTaskStack[TASK_APP_EVENT_STK_SIZE];
   dn_api_loc_notif_events_t eventNotif;
} app_vars_t;

app_vars_t app_vars;

//=========================== prototypes ======================================
static void eventTask(void* unused);
dn_error_t eventNotifCb(dn_api_loc_notif_events_t* eventNotif, INT8U *rsp);

//===== CLI
dn_error_t  cli_reset(const char* arg, INT32U len);

//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_reset,                 "reset",        "reset",                   DN_CLI_ACCESS_LOGIN},
   {NULL,                        NULL,          NULL,                      DN_CLI_ACCESS_NONE},
};

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   INT8U           osErr;
   
   //===== initialize helper tasks
   
   cli_task_init(
      "Events",                             // appName
      cliCmdDefs                           // cliCmds
   );

   // we need to handle join on our own, because when we install an
   // event handler, the local module doesn't get the boot event and won't join
   loc_task_init(
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //==== register callbacks for time and event notifications
   dnm_loc_registerEventNotifCallback(eventNotifCb);
   
   //===== initialize eventTask
   
   osErr = OSTaskCreateExt(
      eventTask,
      (void *) 0,
      (OS_STK*) (&app_vars.eventTaskStack[TASK_APP_EVENT_STK_SIZE - 1]),
      TASK_APP_EVENT_PRIORITY,
      TASK_APP_EVENT_PRIORITY,
      (OS_STK*) app_vars.eventTaskStack,
      TASK_APP_EVENT_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_EVENT_PRIORITY, (INT8U*)TASK_APP_EVENT_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

//=========================== CLI =============================================

// Reset command - returns an error if device fails to reset
dn_error_t cli_reset(const char* arg, INT32U len){
   INT8U      rc;
   
   dnm_ucli_printf("Resetting...\r\n\n");
   
   // send reset to stack
   dnm_loc_resetCmd(&rc);	
   ASSERT(rc == DN_API_RC_OK);
   
   // mote will reset in 5s if rc=DN_API_RC_OK
   
   return(DN_ERR_NONE);
}

static void eventTask(void* unused) {
   dn_error_t      dnErr;
   INT8U           rc;
   
   // Give stack time to print banner
   OSTimeDly(1*SECOND);
   
   dnm_ucli_printf("Listening for advertisements...\r\n");
   dnErr = dnm_loc_joinCmd(&rc); 	
   ASSERT(dnErr == DN_ERR_NONE);
   ASSERT(rc == DN_API_RC_OK);
   
   while (1) { // this is a task, it executes forever
      
      // wait a bit
      OSTimeDly(5000);
   }
}
                        
 dn_error_t eventNotifCb(dn_api_loc_notif_events_t* eventNotif, INT8U *rsp) {
   
   // copy notification to local variables for simpler debugging
   memcpy(&app_vars.eventNotif, eventNotif, sizeof(dn_api_loc_notif_events_t));
   
   dnm_ucli_printf("Event received - ");
   dnm_ucli_printf("events: 0x%04x, state: 0x%02x, alarms: 0x%04x\r\n", htonl(app_vars.eventNotif.events),
                                       app_vars.eventNotif.state, htonl(app_vars.eventNotif.alarms));
   
   *rsp = DN_API_RC_OK;
   
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
