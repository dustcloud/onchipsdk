/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "string.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_time.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== defines =========================================

//=========================== variables =======================================

typedef struct {
   dnm_cli_cont_t            cliContext;
   OS_STK                    timeTaskStack[TASK_APP_TIME_STK_SIZE];
   dn_time_asn_t             currentASN;
   dn_time_utc_t             currentUTC;
   INT64U                    sysTime;
   dn_api_loc_notif_time_t   timeNotif;
} time_app_vars_t;

time_app_vars_t time_app_vars;

//=========================== prototypes ======================================

static void timeTask(void* unused);
dn_error_t timeNotifCb(dn_api_loc_notif_time_t* timeNotif, INT8U length);

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   dn_error_t      dnErr;
   INT8U           osErr;
   
   //===== initialize helper tasks
   
   cli_task_init(
      &time_app_vars.cliContext,            // cliContext
      "time",                               // appName
      NULL                                  // cliCmds
   );
   loc_task_init(
      &time_app_vars.cliContext,            // cliContext
      JOIN_YES,                             // fJoin
      NULL,                                 // netId
      60000,                                // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //==== register a callback for when receiving a time notification
   
   dnm_loc_registerTimeNotifCallback(timeNotifCb);
   
   //===== initialize timeTask
   
   osErr = OSTaskCreateExt(
      timeTask,
      (void *) 0,
      (OS_STK*) (&time_app_vars.timeTaskStack[TASK_APP_TIME_STK_SIZE - 1]),
      TASK_APP_TIME_PRIORITY,
      TASK_APP_TIME_PRIORITY,
      (OS_STK*) time_app_vars.timeTaskStack,
      TASK_APP_TIME_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_TIME_PRIORITY, (INT8U*)TASK_APP_TIME_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

static void timeTask(void* unused) {
   dn_error_t      dnErr;
   INT8U           osErr;
   
   while (1) { // this is a task, it executes forever
     
      // network time
      dn_getNetworkTime(
         &time_app_vars.currentASN,
         &time_app_vars.currentUTC
      );
      dnm_cli_printf("current time:\r\n");
      dnm_cli_printf(" - ASN:     0x%010x\r\n",(INT32U)(time_app_vars.currentASN.asn));
      dnm_cli_printf(" - offset:  %d us\r\n",   time_app_vars.currentASN.offset);
      
      // system time
      dn_getSystemTime(
         &time_app_vars.sysTime
      );
      dnm_cli_printf(" - sysTime: %d ticks\r\n",(INT32U)(time_app_vars.sysTime));
      
      // wait a bit
      OSTimeDly(5000);
   }
}

dn_error_t timeNotifCb(dn_api_loc_notif_time_t* timeNotif, INT8U length) {
   INT8U           i;
   
   ASSERT(length==sizeof(dn_api_loc_notif_time_t));
   
   // copy notification to local variables for simpler debugging
   memcpy(&time_app_vars.timeNotif,timeNotif,length);
   
   dnm_cli_printf("time notification:\r\n");
   dnm_cli_printf(" - upTime:  %d s\r\n",htonl(time_app_vars.timeNotif.upTime));
   dnm_cli_printf(" - ASN:     0x");
   for (i=0;i<sizeof(time_app_vars.timeNotif.asn);i++) {
      dnm_cli_printf("%02x",time_app_vars.timeNotif.asn.byte[i]);
   }
   dnm_cli_printf("\r\n");
   dnm_cli_printf(" - offset:  %d us\r\n",time_app_vars.timeNotif.offset);
   
   return DN_ERR_NONE;
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
