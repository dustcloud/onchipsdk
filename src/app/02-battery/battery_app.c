/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_adc.h"
#include "dn_exe_hdr.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== variables =======================================

typedef struct {
   OS_STK               batteryTaskStack[TASK_APP_BATTERY_STK_SIZE];
} battery_app_vars_t;

battery_app_vars_t  battery_app_v;

//=========================== prototypes ======================================

static void batteryTask(void* unused);

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   INT8U        osErr;
   
   //==== initialize helper tasks
   
   cli_task_init(
      "battery",                            // appName
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
   
   // create the battery task
   osErr  = OSTaskCreateExt(
      batteryTask,
      (void *)0,
      (OS_STK*)(&battery_app_v.batteryTaskStack[TASK_APP_BATTERY_STK_SIZE-1]),
      TASK_APP_BATTERY_PRIORITY,
      TASK_APP_BATTERY_PRIORITY,
      (OS_STK*)battery_app_v.batteryTaskStack,
      TASK_APP_BATTERY_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_BATTERY_PRIORITY, (INT8U*)TASK_APP_BATTERY_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== battery task ====================================

static void batteryTask(void* unused) {
   dn_error_t                dnErr;
   int                       numBytesRead;
   INT16U                    voltage;
   dn_adc_drv_open_args_t    openArgs;
   
   openArgs.loadBattery = DN_ADC_LOAD_BATT_NONE;

   // open battery sensor
   dnErr = dn_open(
      DN_BATT_DEV_ID,        // device
      &openArgs,             // args
      sizeof(openArgs)       // argLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // infinite loop
   while(1) {
      
      // this call blocks the task until the specified timeout expires (in ms)
      OSTimeDly(1000);
      
      // read battery value
      numBytesRead = dn_read(
         DN_BATT_DEV_ID,          // device
         &voltage,                // buf
         sizeof(voltage)          // bufSize 
      );
      ASSERT(numBytesRead== sizeof(INT16U));
      
      // print
      dnm_ucli_printf("voltage=%d\r\n",voltage);
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
