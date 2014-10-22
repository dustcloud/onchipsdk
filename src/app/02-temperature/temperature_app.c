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

/// variables local to this application
typedef struct {
   OS_STK               tempTaskStack[TASK_APP_TEMP_STK_SIZE];
} temp_app_vars_t;

temp_app_vars_t  temp_app_v;

//=========================== prototypes ======================================

static void tempTask(void* unused);

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   INT8U        osErr;
   
   //==== initialize helper tasks
   
   cli_task_init(
      "temperature",                        // appName
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
   
   // create the temperature task
   osErr  = OSTaskCreateExt(
      tempTask,
      (void *)0,
      (OS_STK*)(&temp_app_v.tempTaskStack[TASK_APP_TEMP_STK_SIZE-1]),
      TASK_APP_TEMP_PRIORITY,
      TASK_APP_TEMP_PRIORITY,
      (OS_STK*)temp_app_v.tempTaskStack,
      TASK_APP_TEMP_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_TEMP_PRIORITY, (INT8U*)TASK_APP_TEMP_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== temperature task ================================

static void tempTask(void* unused) {
   dn_error_t              dnErr;
   int                     numBytesRead;
   INT16S                  temperature;
   
   // open temperature sensor
   dnErr = dn_open(
      DN_TEMP_DEV_ID,             // device
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
         DN_TEMP_DEV_ID ,         // device
         &temperature,            // buf
         sizeof(temperature)      // bufSize 
      );
      ASSERT(numBytesRead== sizeof(temperature));
      
      // print
      dnm_ucli_printf("temperature=%d\r\n",temperature);
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
