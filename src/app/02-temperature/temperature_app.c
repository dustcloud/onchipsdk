/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_adc.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== variables =======================================

/// variables local to this application
typedef struct {
   dnm_cli_cont_t       cliContext;
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
      &temp_app_v.cliContext,               // cliContext
      "temperature",                        // appName
      NULL                                  // cliCmds
   );
   loc_task_init(
      &temp_app_v.cliContext,               // cliContext
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
      dnm_cli_printf("temperature=%d\r\n",temperature);
   }
}

//=============================================================================
//=========================== install a kernel header =========================
//=============================================================================

/**
A kernel header is a set of bytes prepended to the actual binary image of this
application. Thus header is needed for your application to start running.
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
