/*
Copyright (c) 2016, Dust Networks.  All rights reserved.

  There are 11 defined "glitch free" periods defined in dn_pwm.h - using an arbitrary period
  can result in an extra partial pulse when enabling/disabling.

  Duty cycle is supplied to the driver as the on portion in ns in dn_open and dn_icotl calls. Here we
  expose to the user as a percentage of on period.

  PWM output is on pin PWM0 (LTC5800 pin 49)
*/

// C includes
#include <string.h>
#include <stdio.h>

// SDK includes
#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_pwm.h"
#include "dn_exe_hdr.h"
#include "Ver.h"
#include "well_known_ports.h"

//=========================== definitions =====================================
#define MIN_PERIOD              10000        // ns

//=========================== prototypes ======================================

//===== CLI handlers
dn_error_t cli_openHandler(const char* arg, INT32U len);
dn_error_t cli_closeCmdHandler(const char* arg, INT32U len);
dn_error_t cli_enableCmdHandler(const char* arg, INT32U len);
dn_error_t cli_disableHandler(const char* arg, INT32U len);
dn_error_t cli_setHandler(const char* arg, INT32U len);

//===== tasks
//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_openHandler,       "open",             "open <period> <duty>",         DN_CLI_ACCESS_LOGIN},
   {&cli_enableCmdHandler,  "enable",           "enable",                       DN_CLI_ACCESS_LOGIN},
   {&cli_disableHandler,    "disable",          "disable",                      DN_CLI_ACCESS_LOGIN},
   {&cli_closeCmdHandler,   "close",            "close",                        DN_CLI_ACCESS_LOGIN},
   {&cli_setHandler,        "set",              "set <duty>",                   DN_CLI_ACCESS_LOGIN},
   {NULL,                   NULL,               NULL,                           DN_CLI_ACCESS_NONE},
};

//=========================== variables =======================================

typedef struct {
   dn_pwm_open_args_t    openArgs;
} pwm_app_vars_t;

pwm_app_vars_t    pwm_app_v;

//=========================== initialization ==================================
/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {

   //==== initialize local variables
   memset(&pwm_app_v, 0x00, sizeof(pwm_app_v));

   //==== initialize helper tasks

   cli_task_init(
      "PWM",                                // appName
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

   return 0;
}

//=========================== CLI handlers ====================================
// Open the PWM driver and configure the Period and initial Duty Cycle
dn_error_t cli_openHandler(const char* arg, INT32U len) {
   dn_error_t              dnErr;
   int                     length;
   INT32U                  period;
   INT8U                   dutyCycle;
   
   //parse the CLI command - 
   length = sscanf(arg, "%u %hhu", &period, &dutyCycle);
   if (length < 1) {
      dnm_ucli_printf("Usage: open <period (1-11)> <duty (%)>\r\n");
      return DN_ERR_NONE;
   }
   
   // use one of the glitch free periods if 1-11, or the supplied value if not
   switch (period){    
      case 1:
         pwm_app_v.openArgs.period = DN_PWM_139US;
         break;
      case 2:
         pwm_app_v.openArgs.period = DN_PWM_556US;
         break;
      case 3:
         pwm_app_v.openArgs.period = DN_PWM_2P2MS;
         break;
      case 4:
         pwm_app_v.openArgs.period = DN_PWM_4P4MS;
         break;
      case 5:
         pwm_app_v.openArgs.period = DN_PWM_8P9MS;
         break;
      case 6:
         pwm_app_v.openArgs.period = DN_PWM_17P8MS;
         break;
      case 7:
         pwm_app_v.openArgs.period = DN_PWM_35P5MS;
         break;
      case 8:
         pwm_app_v.openArgs.period = DN_PWM_71P1MS;
         break;
      case 9:
         pwm_app_v.openArgs.period = DN_PWM_142MS;
         break;
      case 10:
         pwm_app_v.openArgs.period = DN_PWM_569MS;
         break;
      case 11:
         pwm_app_v.openArgs.period = DN_PWM_1P138S;
         break;
      default: 
         if(period < MIN_PERIOD){
            pwm_app_v.openArgs.period = MIN_PERIOD;
         }
         else{
            pwm_app_v.openArgs.period = period;
         }
         break;
   }  
   
   pwm_app_v.openArgs.duty_cycle = (pwm_app_v.openArgs.period * dutyCycle)/100;
            
   dnm_ucli_printf("Opening PWM with period=%u ns and duty cycle=%u%% (%u ns)", pwm_app_v.openArgs.period, 
                                                                                 dutyCycle, pwm_app_v.openArgs.duty_cycle);

   dnErr = dn_open(DN_PWM_DEV_ID, &pwm_app_v.openArgs, sizeof(pwm_app_v.openArgs));
   if (dnErr!= DN_ERR_NONE){
      dnm_ucli_printf(" failed RC=%d\r\n", dnErr);
   }
   else{
      dnm_ucli_printf("\r\n");
   }
   return DN_ERR_NONE;
}

// Closing the PWM driver which will allow for a future open with a new Period setting
dn_error_t cli_closeCmdHandler(const char* arg, INT32U len) {
    dn_error_t              dnErr;

    dnm_ucli_printf("Closing PWM driver");
    dnErr = dn_close(DN_PWM_DEV_ID);

    if (dnErr!= DN_ERR_NONE){
        dnm_ucli_printf(" failed RC=%d\r\n", dnErr);
    }
    else{
        dnm_ucli_printf("\r\n");
    }
    return DN_ERR_NONE;
}

// Enable the PWM output on pin PWM0 (LTC5800 pin 49)
dn_error_t cli_enableCmdHandler(const char* arg, INT32U len) {
   dn_error_t               dnErr;

   dnm_ucli_printf("Enabling PWM");
   dnErr =  dn_ioctl(DN_PWM_DEV_ID, DN_IOCTL_PWM_ENABLE, NULL, 0);
    
   if (dnErr!= DN_ERR_NONE){
      dnm_ucli_printf(" failed RC=%d\r\n", dnErr);
   }
   else{     
        dnm_ucli_printf("\r\n");
   }
   return DN_ERR_NONE;
}

// Disable the PWM output
dn_error_t cli_disableHandler(const char* arg, INT32U len) {
   dn_error_t               dnErr;
   INT32U                   duty = 0U;

   // Disabling the PWM will leave it in an arbitrary state depending on where
   // the PWM was in the cycle. Thus it is possible to disable a PWM and close it,
   // and still leave the pin asserted (high). To ensure that the PWM output is 
   // de-asserted, first set the duty cycle to 0, then disable and close it.   
   dnErr =  dn_ioctl(DN_PWM_DEV_ID, DN_IOCTL_PWM_SET,
                (dn_pwm_ioctl_set_t *)&duty, sizeof(dn_pwm_ioctl_set_t));
   if (dnErr!= DN_ERR_NONE){
      dnm_ucli_printf("Unable to zero duty cycle before disabling\r\n", dnErr);
   }
   else{
      dnm_ucli_printf("Disabling PWM");
      dnErr =  dn_ioctl(DN_PWM_DEV_ID, DN_IOCTL_PWM_DISABLE, NULL, 0);
      if (dnErr!= DN_ERR_NONE){
         dnm_ucli_printf(" failed RC=%d\r\n", dnErr);
      }
      else{
         dnm_ucli_printf("\r\n");
      }
   }
   return DN_ERR_NONE;   
}

// Set the new Duty Cycle for the currently opened PWM
dn_error_t cli_setHandler(const char* arg, INT32U len) {
   dn_error_t      dnErr;
   int             length;
   INT32U          duty;

   //--- param 0: len
   length = sscanf(arg, "%u", &duty);
   if (length < 1) {
       dnm_ucli_printf("Usage: set <duty (%)>\r\n");
      return DN_ERR_NONE;
   }

   if (duty > 100){
      duty = 100;
   }
   pwm_app_v.openArgs.duty_cycle = (pwm_app_v.openArgs.period * duty)/100;
   dnm_ucli_printf("Setting duty cycle to %u%% (%u ns)", duty, pwm_app_v.openArgs.duty_cycle);

   dnErr =  dn_ioctl(DN_PWM_DEV_ID, DN_IOCTL_PWM_SET,
                (dn_pwm_ioctl_set_t *)&pwm_app_v.openArgs.duty_cycle, sizeof(pwm_app_v.openArgs.duty_cycle));
   if (dnErr!= DN_ERR_NONE){
        dnm_ucli_printf(" failed RC=%d\r\n", dnErr);
   }
   else{
      dnm_ucli_printf("\r\n");
   }

   return DN_ERR_NONE;
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
