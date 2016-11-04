/*
Copyright (c) 2016, Dust Networks.  All rights reserved.
*/

// C includes
#include <string.h>
#include <stdio.h>

//OCSDK includes
#include "dn_common.h"
#include "dn_api_common.h"
#include "dn_api_param.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_time.h"
#include "dn_exe_hdr.h"
#include "Ver.h"

//app includes
#include "app_task_cfg.h"

//=========================== defines =========================================
#define  SEARCH_TIME      30 * SECOND  
#define  PREFERRED_NET    1230         // motes and managers default to 1229 out of the box.
#define  INVALID_NET      0x0000       // network ID 0x0000 is never valid
#define  JOIN_DC          255          // 100%
#define  MAX_NETS         10           // maximum number of networks to store
#define  MAX_RSSI         127          // dBm
//=========================== variables =======================================

typedef struct {
   dn_api_loc_notif_adv_t      network;
   OS_STK                      searchTaskStack[TASK_APP_SEARCH_STK_SIZE];  
   INT8S                       bestRSSI;
} app_vars_t;

app_vars_t app_vars;

//=========================== prototypes ======================================
static void searchTask(void* unused);
dn_error_t advNotifCb(dn_api_loc_notif_adv_t* advNotif, INT8U length);
INT8U operationalCheck(void);

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
   dn_error_t      dnErr;
   
   memset(&app_vars, 0, sizeof(app_vars_t));
   
   //===== initialize helper tasks  
   cli_task_init(
      "Search",                            // appName
      cliCmdDefs                           // cliCmds
   );
   
   loc_task_init(
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //==== register callback for advertisement notifications
   dnErr = dnm_loc_registerAdvNotifCallback((advNotifCb_t)advNotifCb);
   ASSERT(dnErr == DN_ERR_NONE);
   
   //===== initialize timeTask
   
   osErr = OSTaskCreateExt(
      searchTask,
      (void *) 0,
      (OS_STK*) (&app_vars.searchTaskStack[TASK_APP_SEARCH_STK_SIZE - 1]),
      TASK_APP_SEARCH_PRIORITY,
      TASK_APP_SEARCH_PRIORITY,
      (OS_STK*) app_vars.searchTaskStack,
      TASK_APP_SEARCH_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_SEARCH_PRIORITY, (INT8U*)TASK_APP_SEARCH_NAME, &osErr);
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
   
   // mote will reset in 5s if rc=DN_ERR_NONE
   
   return(DN_ERR_NONE);
}

//=========================== TASKS ===========================================

// Search task
static void searchTask(void* unused) {
   dn_error_t   dnErr;
   INT8U        rc;
   dn_netid_t   netId;
   INT8U        state;
   
   // Wait for stack to print start messages
   OSTimeDly(1 * SECOND);
   
   //initialize RSSI
   app_vars.bestRSSI = -127;
   
   //initialize netId (in network order) - set to an invalid ID to catch if we don't hear any ADV
   app_vars.network.netId = INVALID_NET;
   
   // Go into search - advNotifCb will collect networks
   dnm_ucli_printf("Searching for preferred network %d for 30 s...\r\n", PREFERRED_NET);
   dnErr = dnm_loc_searchCmd(&rc);
   ASSERT(dnErr == DN_ERR_NONE);
   ASSERT(rc == DN_API_RC_OK);
   
   // Wait SEARCH_TIME seconds
   OSTimeDly(SEARCH_TIME);
   
   // set the network ID to the best advertisement heard
   netId = app_vars.network.netId;
   if(netId != INVALID_NET){
      dnErr = dnm_loc_setParameterCmd(DN_API_PARAM_NETID, (INT8U*)&netId, sizeof(dn_netid_t), &rc);
      ASSERT(dnErr == DN_ERR_NONE);
      ASSERT(rc == DN_API_RC_OK);
 
      // join the network
      dnm_ucli_printf("Joining network %d...\r\n", htons(app_vars.network.netId));
      dnErr =  dnm_loc_joinCmd(&rc);
      ASSERT(dnErr == DN_ERR_NONE);
      ASSERT(rc == DN_API_RC_OK);
   }
   
   while (1) { // this is a task, it executes forever
      
      // wait a bit
      OSTimeDly(5000);
      
      state = operationalCheck();
      
      switch (state){
         case DN_API_ST_OPERATIONAL:
            dnm_ucli_printf("I'm so happy in network %d!\r\n", htons(app_vars.network.netId));
            break;
         case DN_API_ST_SEARCHING:
         case DN_API_ST_NEGO:
         case DN_API_ST_CONNECTED:
            dnm_ucli_printf("I'm trying to join network %d...\r\n", htons(app_vars.network.netId));
            break;
         default:
            dnm_ucli_printf("I couldn't find a network to join\r\n");
            break;
      }
   }
}

//=========================== HELPERS =========================================
// checks to see if mote is in operational state
INT8U operationalCheck(void){
   dn_error_t                   dnErr;                                              // Error code for the getParameter call
   INT8U                        statusBuf[sizeof(dn_api_rsp_get_motestatus_t)];     // buffer to receive getParam reply
   dn_api_rsp_get_motestatus_t  *currentStatus;                                     // Struct containing Mote's status
   INT8U                        respLen;                                            // Length of the getParam reply
   INT8U                        rc;                                                 // response code for the specific parameter requested
   
   dnErr = dnm_loc_getParameterCmd(DN_API_PARAM_MOTESTATUS, (INT8U*)&statusBuf, 0, &respLen, &rc);
   
   currentStatus = (dn_api_rsp_get_motestatus_t*)(&statusBuf[0]);
   ASSERT(dnErr == DN_ERR_NONE);
   ASSERT(rc == DN_API_RC_OK);
   
   return (currentStatus->state);
}

//=========================== CALLBACKS =======================================

dn_error_t advNotifCb(dn_api_loc_notif_adv_t* advNotif, INT8U length){
   
   dn_netid_t   netId; 
      
   if(length == sizeof(dn_api_loc_notif_adv_t)){
      netId = ntohs(advNotif->netId);
    
      if(netId == PREFERRED_NET){
         dnm_ucli_printf("Heard the preferred network %d at %d dBm\r\n", netId, advNotif->rssi);
         memcpy(&app_vars.network, advNotif, sizeof(dn_api_loc_notif_adv_t));
         app_vars.bestRSSI = MAX_RSSI;
      }
      else if(advNotif->rssi > app_vars.bestRSSI){
               app_vars.bestRSSI = advNotif->rssi;
               dnm_ucli_printf("Heard network %d at %d dBm (louder than previous best)\r\n", netId, advNotif->rssi);   
               memcpy(&app_vars.network, advNotif, sizeof(dn_api_loc_notif_adv_t));
            }
            else{
               dnm_ucli_printf("Heard network %d at %d dBm\r\n", netId, advNotif->rssi);   
            }
      
      return (DN_ERR_NONE);
   }
      
   return (DN_ERR_SIZE);
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
