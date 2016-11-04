/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

// OCSDK includes
#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_time.h"
#include "dn_exe_hdr.h"
#include "Ver.h"

//C includes
#include <inttypes.h>
#include <string.h>

// App includes
#include "app_task_cfg.h"

//=========================== defines =========================================

#define LOOP_PERIOD          10 * SECOND
//=========================== variables =======================================

typedef struct {
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
   INT8U           osErr;
   
   //===== initialize helper tasks
   
   cli_task_init(
      "time",                               // appName
      NULL                                  // cliCmds
   );
   
   loc_task_init(
      JOIN_YES,                             // fJoin
      NULL,                                 // netId
      NULL,                                 // udpPort - no port opened by local task
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
   
   //GIve stack time to print banner
   OSTimeDly(1 * SECOND);
   
   while (1) { // this is a task, it executes forever
     
      // network time
      dn_getNetworkTime(
         &time_app_vars.currentASN,
         &time_app_vars.currentUTC
      );
      
      // note that printing INT64S and INT64U doesn't work using %lld formatter - casting allows
      // printing but will eventually overflow
      dnm_ucli_printf("current time:\r\n");
      dnm_ucli_printf(" - ASN:              0x%010x\r\n",(INT32U)(time_app_vars.currentASN.asn));
      dnm_ucli_printf(" - offset:           %d us\r\n", time_app_vars.currentASN.offset);
      dnm_ucli_printf(" - utc seconds:      %ld s\r\n", (INT32U)time_app_vars.currentUTC.sec);
      dnm_ucli_printf(" - utc microseconds: %ld us\r\n", (INT32U)time_app_vars.currentUTC.usec);
      
      // system time
      dn_getSystemTime(
         &time_app_vars.sysTime
      );
      dnm_ucli_printf(" - sysTime:          %d 32KHz ticks\r\n\r\n",(INT32U)(time_app_vars.sysTime));
      
      // wait a bit
      OSTimeDly(LOOP_PERIOD);
   }
}

dn_error_t timeNotifCb(dn_api_loc_notif_time_t* timeNotif, INT8U length) {
   INT8U           i;
   
   ASSERT(length==sizeof(dn_api_loc_notif_time_t));
   // Note that  dn_api_loc_notif_time_t contains a field
   //     dn_utc_time_t   utcTime;
   // contrast this with the fields returned by dn_getNetworkTime
   //     dn_time_utc_t  currentUTC;
   // the contents of the two utc time structures are identical, but the stack
   // uses the two types in different layers.
   
   // copy notification to global variables for simpler debugging
   // notification fields other than ASN are in network order, so must be swapped for use
   time_app_vars.timeNotif.upTime = ntohl(timeNotif->upTime);
   memcpy(&time_app_vars.timeNotif.asn, &timeNotif->asn, sizeof(dn_asn_t));
   time_app_vars.timeNotif.offset = ntohs(timeNotif->offset);
   time_app_vars.timeNotif.asnSubOffset = ntohs(timeNotif->asnSubOffset);
   time_app_vars.timeNotif.utcTime.seconds = ntohll(timeNotif->utcTime.seconds);
   time_app_vars.timeNotif.utcTime.useconds = ntohl(timeNotif->utcTime.useconds);
   
   dnm_ucli_printf("time notification:\r\n");
   dnm_ucli_printf(" - ASN:              0x");
   for (i=0;i<sizeof(time_app_vars.timeNotif.asn);i++) {
      dnm_ucli_printf("%02x",time_app_vars.timeNotif.asn.byte[i]);
   }
   dnm_ucli_printf("\r\n");
   dnm_ucli_printf(" - offset:           %d us\r\n",time_app_vars.timeNotif.offset);
   dnm_ucli_printf(" - utc seconds:      %d s\r\n",(INT32U) time_app_vars.timeNotif.utcTime.seconds);
   dnm_ucli_printf(" - utc microseconds: %d us\r\n",(INT32U) time_app_vars.timeNotif.utcTime.useconds);
   // uptime is only nonzero after join
   dnm_ucli_printf(" - upTime:           %ld s (%ld 32KHz ticks)r\n",time_app_vars.timeNotif.upTime, 
                                         time_app_vars.timeNotif.upTime * 32768);
   dnm_ucli_printf("\r\n");
   
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
