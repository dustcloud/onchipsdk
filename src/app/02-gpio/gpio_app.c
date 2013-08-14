/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_gpio.h"
#include "dn_system.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================

// blue LED on DC9003A
#define PIN_TOGGLE      DN_GPIO_PIN_22_DEV_ID

// DP2 on DC9003A
#define PIN_NOTIF       DN_GPIO_PIN_21_DEV_ID

//=========================== variables =======================================

typedef struct {
   dnm_cli_cont_t cliContext;
   // gpioToggle
   OS_STK         gpioToggleTaskStack[TASK_APP_GPIOTOGGLE_STK_SIZE];
   // gpioNotif
   INT8U          gpioNotifChannelBuf[DN_CH_ASYNC_RXBUF_SIZE(sizeof(dn_gpio_notif_t))];
   OS_STK         gpioNotifTaskStack[TASK_APP_GPIONOTIF_STK_SIZE];
} gpio_app_vars_t;

gpio_app_vars_t gpio_app_v;

//=========================== externs =========================================

//=========================== prototypes ======================================

static void gpioToggleTask(void* unused);
static void gpioNotifTask(void* unused);

//=========================== initialization ==================================

int p2_init(void) {
   dn_error_t             status;
   dn_error_t             dnErr;
   INT8U                  osErr;
   
   //==== initialize helper tasks
   
   cli_task_init(
      &gpio_app_v.cliContext,               // cliContext
      "gpio",                               // appName
      NULL                                  // cliCmds
   );
   loc_task_init(
      &gpio_app_v.cliContext,               // cliContext
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== gpioToggle task
   
   osErr = OSTaskCreateExt(
      gpioToggleTask,
      (void *) 0,
      (OS_STK*) (&gpio_app_v.gpioToggleTaskStack[TASK_APP_GPIOTOGGLE_STK_SIZE-1]),
      TASK_APP_GPIOTOGGLE_PRIORITY,
      TASK_APP_GPIOTOGGLE_PRIORITY,
      (OS_STK*) gpio_app_v.gpioToggleTaskStack,
      TASK_APP_GPIOTOGGLE_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_GPIOTOGGLE_PRIORITY, (INT8U*)TASK_APP_GPIOTOGGLE_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   //===== gpioNotif task
   
   osErr = OSTaskCreateExt(
      gpioNotifTask,
      (void *) 0,
      (OS_STK*) (&gpio_app_v.gpioNotifTaskStack[TASK_APP_GPIONOTIF_STK_SIZE - 1]),
      TASK_APP_GPIONOTIF_PRIORITY,
      TASK_APP_GPIONOTIF_PRIORITY,
      (OS_STK*) gpio_app_v.gpioNotifTaskStack,
      TASK_APP_GPIONOTIF_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_GPIONOTIF_PRIORITY, (INT8U*)TASK_APP_GPIONOTIF_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== GPIO toggle task ================================

static void gpioToggleTask(void* unused) {
   dn_error_t              dnErr;
   dn_gpio_ioctl_cfg_out_t gpioOutCfg;
   INT8U                   pinState;
   
   // open pin
   dnErr = dn_open(
      PIN_TOGGLE,                 // device
      NULL,                       // args
      0                           // argLen 
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // configure as output
   gpioOutCfg.initialLevel = 0x00;
   dnErr = dn_ioctl(
      PIN_TOGGLE,                 // device
      DN_IOCTL_GPIO_CFG_OUTPUT,   // request
      &gpioOutCfg,                // args
      sizeof(gpioOutCfg)          // argLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   while (1) { // this is a task, it executes forever

      // block the task for some time
      OSTimeDly(1000);
      
      // change output value
      if (pinState==0x00) {
         pinState = 0x01;
      } else {
         pinState = 0x00;
      }

      // toggle pin
      dnErr = dn_write(
         PIN_TOGGLE,              // device
         &pinState,               // buf
         sizeof(pinState)         // len
      );
      ASSERT(dnErr==DN_ERR_NONE);
   }
}

//=========================== GPIO notif task =================================

static void gpioNotifTask(void* unused) {
   dn_error_t                     dnErr;
   INT8U                          osErr;
   OS_MEM*                        notifChannelMem;
   CH_DESC                        notifChannel;
   dn_gpio_ioctl_cfg_in_t         gpioInCfg;
   dn_gpio_ioctl_notif_enable_t   gpioNotifEnable;
   dn_gpio_notif_t                gpioNotif;
   INT32U                         rxLen;
   INT32U                         msgType;
   INT32U                         maxLen;
   
   // allocate memory for GPIO notification channel
   notifChannelMem = OSMemCreate(
      gpio_app_v.gpioNotifChannelBuf,
      1,
      DN_CH_ASYNC_RXBUF_SIZE(sizeof(dn_gpio_notif_t)),
      &osErr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   // create channel from memory
   dnErr = dn_createAsyncChannel(
      notifChannelMem,
      &notifChannel
   );
   ASSERT(dnErr == DN_ERR_NONE);
   
   // open pin
   dnErr = dn_open(
      PIN_NOTIF,
      NULL,
      0
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // enable pull down resistor
   gpioInCfg.pullMode = DN_GPIO_PULL_DOWN;
   dnErr = dn_ioctl(
      PIN_NOTIF,
      DN_IOCTL_GPIO_CFG_INPUT,
      &gpioInCfg,
      sizeof(gpioInCfg)
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // enable GPIO notification
   gpioNotifEnable.activeLevel    = 1;
   gpioNotifEnable.fEnable        = 1;
   gpioNotifEnable.notifChannelId = notifChannel;
   dnErr = dn_ioctl(
      PIN_NOTIF,
      DN_IOCTL_GPIO_ENABLE_NOTIF,
      &gpioNotifEnable,
      sizeof(gpioNotifEnable)
   );
   ASSERT(dnErr == DN_ERR_NONE);
   
   while (1) { // this is a task, it executes forever
      
      // wait for a GPIO notification
      dnErr = dn_readAsyncMsg(
         notifChannel,            // chDesc
         &gpioNotif,              // msg
         &rxLen,                  // rxLen
         &msgType,                // msgType
         sizeof(gpioNotif),       // maxLen
         0                        // timeout
      );
      ASSERT(dnErr==DN_ERR_NONE);
      
      // print
      dnm_cli_printf("gpioNotifTask: level=%d.\n\r",gpioNotif.level);

      // re-arm notification on opposite level
      if (gpioNotifEnable.activeLevel==0x01) {
         gpioNotifEnable.activeLevel = 0x00;
      } else {
         gpioNotifEnable.activeLevel = 0x01;
      }
      dnErr = dn_ioctl(
         PIN_NOTIF,
         DN_IOCTL_GPIO_ENABLE_NOTIF,
         &gpioNotifEnable,
         sizeof(gpioNotifEnable)
      );
      ASSERT(dnErr == DN_ERR_NONE);
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
