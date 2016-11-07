/*
Copyright (c) 2016, Dust Networks.  All rights reserved.
*/

// SDK includes
#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_gpio.h"
#include "dn_system.h"
#include "dn_exe_hdr.h"
#include "Ver.h"

// app includes
#include "app_task_cfg.h"

// C includes
#include <stdio.h>

//=========================== definitions =====================================

#define TEST_PIN        DN_GPIO_PIN_21_DEV_ID  // DP2
#define HI              1
#define LO              0

//=========================== variables =======================================

typedef struct {
   // tristate
   OS_STK         tristateTaskStack[TASK_APP_TRISTATE_STK_SIZE];
} tristate_app_vars_t;

tristate_app_vars_t tristate_app_v;

//=========================== externs =========================================

//=========================== prototypes ======================================
static void tristateTask(void* unused);

//=== Command Line Interface (CLI) handlers =======
dn_error_t cli_out(const char* arg, INT32U len);
dn_error_t cli_tristate(const char* arg, INT32U len);

//=========================== const  ==============================================
const dnm_ucli_cmdDef_t cliCmdDefs[] = {
  {&cli_out,                      "out",            "out <0|1>",                DN_CLI_ACCESS_LOGIN },
  {&cli_tristate,                 "tristate",       "tristate <0|1|2>",         DN_CLI_ACCESS_LOGIN },
  {NULL,                          NULL,             NULL,                       DN_CLI_ACCESS_NONE},
};

//=========================== initialization ==================================
int p2_init(void) {
   INT8U                  osErr;
   
   //==== initialize helper tasks 
   cli_task_init(
      "Tristate",                           // appName
      cliCmdDefs                            // cliCmds
   );
   
   loc_task_init(
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== tristate task 
   osErr = OSTaskCreateExt(
      tristateTask,
      (void *) 0,
      (OS_STK*) (&tristate_app_v.tristateTaskStack[TASK_APP_TRISTATE_STK_SIZE-1]),
      TASK_APP_TRISTATE_PRIORITY,
      TASK_APP_TRISTATE_PRIORITY,
      (OS_STK*) tristate_app_v.tristateTaskStack,
      TASK_APP_TRISTATE_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_TRISTATE_PRIORITY, (INT8U*)TASK_APP_TRISTATE_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return (DN_ERR_NONE);
}

//=========================== tristate task ================================
static void tristateTask(void* unused) {
   dn_error_t                   dnErr;
   
   // open pin
   dnErr = dn_open(
         TEST_PIN,      // device
         NULL,          // args
         0              // argLen 
   );
   ASSERT(dnErr==DN_ERR_NONE);
  
   while (1) { // this is a task, it executes forever

      // block the task for some time
      OSTimeDly(1 * SECOND);
   }
}

dn_error_t cli_out(const char* arg, INT32U len){ 
   dn_error_t                   dnErr;
   int                          length;
   dn_gpio_ioctl_cfg_out_t      gpioOutCfg;
   INT8U                        pinState;   
   
   // parse the CLI command - 
   length = sscanf(arg, "%hhu", &pinState);
   if (length < 1) {
      dnm_ucli_printf("Usage: out <0|1>\r\n");
      return DN_ERR_NONE;
   }
   
   dnm_ucli_printf("Setting pin to output");
   
   switch(pinState){
      case LO:
         gpioOutCfg.initialLevel = LO;
         break;
      case HI:
      default:
         gpioOutCfg.initialLevel = HI;
         break;
   }
   
   // configure as output
   dnErr = dn_ioctl(
                  TEST_PIN,                   // device
                  DN_IOCTL_GPIO_CFG_OUTPUT,   // request
                  &gpioOutCfg,                // args
                  sizeof(gpioOutCfg)          // argLen
   );
   if (dnErr != DN_ERR_NONE){
      dnm_ucli_printf(" failed with RC=%d\r\n", dnErr);
   }
   else{
      // write pin state
      dnErr = dn_write(
                     TEST_PIN,                // device
                     (const char*)&pinState,  // buf
                     sizeof(pinState)         // len
      );
      
      if (dnErr != DN_ERR_NONE){
         dnm_ucli_printf(" failed with RC=%d\r\n", dnErr);
      }
      else {
         dnm_ucli_printf(" %d\r\n", gpioOutCfg.initialLevel);
      }
   }
   
   return(DN_ERR_NONE);
}
   
// configure as tristate - pull is passed as optional arg
dn_error_t cli_tristate(const char* arg, INT32U len){
   dn_error_t                           dnErr;
   int                                  length;
   dn_gpio_ioctl_cfg_in_t               gpioInCfg;
   INT8U                                pull;
   char                                 *pullStr;
   
    // parse the CLI command - 
   length = sscanf(arg, "%hhu", &pull);
   if (length < 1) {
      dnm_ucli_printf("Usage: tristate <0|1|2>\r\n");
      return DN_ERR_NONE;
   }
  
   dnm_ucli_printf("Seting pin to tristate"); 
   
    switch(pull){
      case DN_GPIO_PULL_DOWN:
         gpioInCfg.pullMode = DN_GPIO_PULL_DOWN;
         pullStr = " with pull down";
         break;
      case DN_GPIO_PULL_UP:
         gpioInCfg.pullMode = DN_GPIO_PULL_UP;
         pullStr = " with pull up";
         break;
      case DN_GPIO_PULL_NONE:
      default:
         gpioInCfg.pullMode = DN_GPIO_PULL_NONE;
         pullStr = " with no pull";
         break;
   }

   dnErr = dn_ioctl(
      TEST_PIN,                         // device
      DN_IOCTL_GPIO_CFG_TRISTATE,       // request
      &gpioInCfg,                       // args
      sizeof(gpioInCfg)                 // argLen
   );
   
   if (dnErr != DN_ERR_NONE){
         dnm_ucli_printf(" failed with RC=%d\r\n", dnErr);
      }
   else {
      dnm_ucli_printf("%s\r\n", pullStr);
   }
   
   return(DN_ERR_NONE);
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
