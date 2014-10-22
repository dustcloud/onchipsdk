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
   OS_STK               adcTaskStack[TASK_APP_ADC_STK_SIZE];
} adc_app_vars_t;

adc_app_vars_t  adc_app_v;

//=========================== prototypes ======================================

static void adcTask(void* unused);

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   INT8U        osErr;
   
   //==== initialize helper tasks
   
   cli_task_init(
      "adc",                                // appName
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
   
   // create the ADC task
   osErr  = OSTaskCreateExt(
      adcTask,
      (void *)0,
      (OS_STK*)(&adc_app_v.adcTaskStack[TASK_APP_ADC_STK_SIZE-1]),
      TASK_APP_ADC_PRIORITY,
      TASK_APP_ADC_PRIORITY,
      (OS_STK*)adc_app_v.adcTaskStack,
      TASK_APP_ADC_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_ADC_PRIORITY, (INT8U*)TASK_APP_ADC_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== ADC task ========================================

static void adcTask(void* unused) {
   dn_error_t              dnErr;
   int                     numBytesRead;
   dn_adc_drv_open_args_t  openArgs;
   INT16U                  adcVal;
   
   // open ADC channel
   openArgs.rdacOffset  = 0;
   openArgs.vgaGain     = 0;
   openArgs.fBypassVga  = 1;
   dnErr = dn_open(
      DN_ADC_AI_0_DEV_ID,         // device
      &openArgs,                  // args
      sizeof(openArgs)            // argLen 
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // infinite loop
   while(1) {
      
      // this call blocks the task until the specified timeout expires (in ms)
      OSTimeDly(1000);
      
      // read ADC value
      numBytesRead = dn_read(
         DN_ADC_AI_0_DEV_ID ,        // device
         &adcVal,                    // buf
         sizeof(adcVal)              // bufSize 
      );
      ASSERT(numBytesRead== sizeof(adcVal));
      
      // print
      dnm_ucli_printf("adcVal=%d\r\n",adcVal);
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
