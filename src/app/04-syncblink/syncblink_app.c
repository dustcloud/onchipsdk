/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dnm_local.h"
#include "dn_gpio.h"
#include "dn_time.h"
#include "dn_system.h"
#include "dn_exe_hdr.h"
#include "app_task_cfg.h"
#include "Ver.h"
#include "string.h"

//=========================== defines =========================================

// LEDs on DC9003A
#define LED_BLUE             DN_GPIO_PIN_22_DEV_ID
#define LED_GREEN1           DN_GPIO_PIN_20_DEV_ID
#define LED_GREEN2           DN_GPIO_PIN_23_DEV_ID

// durations (DO NOT CHANGE)
#define SLOT_LENGTH_US       7250 // in us
#define FLASH_DURATION         50 // in ms
#define COMPUTATION_DURATION  500 // in ms
#define BLINK_PERIOD_MS      1856 // in ms (==256*7.25ms)

//=========================== variables =======================================

typedef struct {
   OS_TMR*         blinkTmr;
   OS_EVENT*       blinkSem;
   OS_STK          blinkTaskStack[TASK_APP_BLINK_STK_SIZE];
} syncblink_app_vars_t;

syncblink_app_vars_t syncblink_app_vars;

//=========================== prototypes ======================================

// tasks
static void blinkTask(void* unused);

// callback
void blinkTmr_cb(OS_TMR *ptmr, void *p_arg);
void openLed(int led);
void blinkLed(int led);
void ledOn(int led);
void ledOff(int led);

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   INT8U           osErr;
   
   memset(&syncblink_app_vars,0,sizeof(syncblink_app_vars_t));
   
   // create timer
   syncblink_app_vars.blinkTmr         = OSTmrCreate(
      COMPUTATION_DURATION,  // dly
      0,                     // period
      OS_TMR_OPT_ONE_SHOT,   // opt
      blinkTmr_cb,           // callback
      NULL,                  // callback_arg
      NULL,                  // pname
      &osErr                 // perr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   // create semaphore
   syncblink_app_vars.blinkSem         = OSSemCreate(1);
   
   //===== initialize helper tasks
   
   cli_task_init(
      "syncblink",                          // appName
      NULL                                  // cliCmds
   );
   loc_task_init(
      JOIN_YES,                             // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== initialize tasks
   
   osErr = OSTaskCreateExt(
      blinkTask,
      (void *) 0,
      (OS_STK*) (&syncblink_app_vars.blinkTaskStack[TASK_APP_BLINK_STK_SIZE - 1]),
      TASK_APP_BLINK_PRIORITY,
      TASK_APP_BLINK_PRIORITY,
      (OS_STK*) syncblink_app_vars.blinkTaskStack,
      TASK_APP_BLINK_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_BLINK_PRIORITY, (INT8U*)TASK_APP_BLINK_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

//=========================== tasks ===========================================

static void blinkTask(void* unused) {
   INT8U                     osErr;
   INT32U                    periodRemainder;
   dn_time_asn_t             asn;
   dn_time_utc_t             utc;
   BOOLEAN                   timerStarted;
   INT8S                     desyncSlots;
   INT16U                    desyncOffset;
   INT32U                    desync_us;
   INT32U                    desync_ms;
   
   //=== open LEDs
   openLed(LED_BLUE);
   
   while (1) { // this is a task, it executes forever
      
      //===== step 1. computation
      
      // wait for semaphore
      OSSemPend(syncblink_app_vars.blinkSem, 0, &osErr);
      ASSERT(osErr==OS_ERR_NONE);
      
      // change timeout value
      syncblink_app_vars.blinkTmr->OSTmrDly = COMPUTATION_DURATION;
      
      // rearm timer
      timerStarted = OSTmrStart(
         syncblink_app_vars.blinkTmr,  // ptmr
         &osErr                        // perr
      );
      ASSERT(osErr==OS_ERR_NONE);
      ASSERT(timerStarted==OS_TRUE);
      
      // read time
      dn_getNetworkTime(&asn,&utc);
      
      // calculate new period
      periodRemainder   = BLINK_PERIOD_MS-COMPUTATION_DURATION;
      
      dnm_ucli_printf("\r\n");
      
      if (asn.asn!=0 && asn.offset!=0) {
         desyncSlots    = (INT8S)asn.asn;
         desyncOffset   = asn.offset;
         
         dnm_ucli_printf("asn              0x%x\r\n",(INT32U)asn.asn);
         dnm_ucli_printf("desyncSlots      %d\r\n",desyncSlots);
         dnm_ucli_printf("desyncOffset     %d\r\n",desyncOffset);
         
         desync_us      = (desyncSlots*SLOT_LENGTH_US)+desyncOffset;
         dnm_ucli_printf("desync_us        %d\r\n",desync_us);
         
         desync_ms      = desync_us/1000;
         dnm_ucli_printf("desync_ms        %d\r\n",desync_ms);
         
         if (desync_ms>periodRemainder) {
             periodRemainder      = 0;
         } else {
             periodRemainder     -= desync_ms;
         }
      }
      
      dnm_ucli_printf("periodRemainder  %d\r\n",periodRemainder);
      
      //===== step 2. wait for remainder of period
      
      // wait for semaphore
      OSSemPend(syncblink_app_vars.blinkSem, 0, &osErr);
      ASSERT(osErr==OS_ERR_NONE);
      
      // change timeout value
      syncblink_app_vars.blinkTmr->OSTmrDly = periodRemainder;
      
      // rearm timer
      timerStarted = OSTmrStart(
         syncblink_app_vars.blinkTmr,  // ptmr
         &osErr
      );
      ASSERT(osErr==OS_ERR_NONE);
      ASSERT(timerStarted==OS_TRUE);
      
      // blink LED
      blinkLed(LED_BLUE);
   }
}

//=========================== helpers =========================================

void openLed(int led) {
   dn_error_t                dnErr;
   dn_gpio_ioctl_cfg_out_t   gpioOutCfg;
   
   // open
   dnErr = dn_open(
      led,                             // device
      NULL,                            // args
      0                                // argLen 
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // configure as output
   gpioOutCfg.initialLevel = 0x00;
   dnErr = dn_ioctl(
      led,                             // device
      DN_IOCTL_GPIO_CFG_OUTPUT,        // request
      &gpioOutCfg,                     // args
      sizeof(gpioOutCfg)               // argLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
}

void blinkLed(int led) {
   ledOn(led);
   OSTimeDly(FLASH_DURATION);
   ledOff(led);
}

void ledOn(int led) {
   dn_error_t                dnErr;
   INT8U                     pinState;
   
   // turn LED on
   pinState = 0x01;
   dnErr = dn_write(
      led,                             // device
      &pinState,                       // buf
      sizeof(pinState)                 // len
   );
   ASSERT(dnErr==DN_ERR_NONE);
}

void ledOff(int led) {
   dn_error_t                dnErr;
   INT8U                     pinState;
   
   // turn LED off
   pinState = 0x00;
   dnErr = dn_write(
      led,                             // device
      &pinState,                       // buf
      sizeof(pinState)                 // len
   );
   ASSERT(dnErr==DN_ERR_NONE);
}

//=========================== callbacks =======================================

void blinkTmr_cb(OS_TMR *ptmr, void *p_arg) {
   INT8U           osErr;
   
   osErr = OSSemPost(syncblink_app_vars.blinkSem);
   ASSERT(osErr==OS_ERR_NONE);
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
