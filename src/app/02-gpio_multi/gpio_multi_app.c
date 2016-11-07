/*
Copyright (c) 2015, Dust Networks.  All rights reserved.

Toggle multiple GPIO simultaneously

*/

// SDK includes
#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_gpio.h"
#include "dnm_local.h"
#include "dn_fs.h"
#include "dn_exe_hdr.h"
#include "Ver.h"

// app includes
#include "app_task_cfg.h"

// C includes
#include <string.h>
#include <stdio.h>

//=========================== definitions =====================================
// LEDs will blink as a 3-bit counter
#define COUNTER_BITS    3   
#define LED_BIT_0       0x40   // bit 0 = D7 = GPIO 22 - Group 2, bit 6
#define LED_BIT_1       0x80   // bit 1 = D6 = GPIO 23 - Group 2, bit 7
#define LED_BIT_2       0x10   // bit 2 = D5 = GPIO 20 - Group 2, bit 4

//=========================== data structures ============================

//=========================== variables =======================================

typedef struct {
   OS_STK                       gpioMultiTaskStack[TASK_APP_GPIOMULTI_STK_SIZE];
   INT8U                        count;  
} gpio_multi_vars_t;

gpio_multi_vars_t gpio_multi_v;

//=========================== prototypes ======================================

//===== GPIO notification task
static void gpioMultiTask(void* arg);

//helpers
dn_error_t  initPins(void);

//=========================== const ===========================================

//=========================== initialization ==================================

/**
 \brief This is the entry point in the application code.
 */
int p2_init(void) {
   INT8U                     osErr;
   
   //===== initialize module variables
   memset(&gpio_multi_v,0,sizeof(gpio_multi_vars_t));
   
   // CLI task
   cli_task_init(
      "GPIO multi",                         // appName
       NULL                                 // cliCmds
   );
   
   // local interface task
   loc_task_init(
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== create the gpio task
   osErr = OSTaskCreateExt(
      gpioMultiTask,
      (void*) 0,
      (OS_STK*)(&gpio_multi_v.gpioMultiTaskStack[TASK_APP_GPIOMULTI_STK_SIZE- 1]),
      TASK_APP_GPIOMULTI_PRIORITY,
      TASK_APP_GPIOMULTI_PRIORITY,
      (OS_STK*)gpio_multi_v.gpioMultiTaskStack,
      TASK_APP_GPIOMULTI_STK_SIZE,
      (void*) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_GPIOMULTI_PRIORITY, (INT8U*)TASK_APP_GPIOMULTI_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== GPIO task =================================

static void gpioMultiTask(void* unused) {
   dn_error_t                     dnErr;
   dn_gpio_out_pins_cfg_t         pinConfig;
   INT16U                         setting[8];
   
   OSTimeDly(1*SECOND);
   
   //===== open and configure the IO pins
   dnErr = initPins();
   ASSERT(dnErr == DN_ERR_NONE);
   
   //set up a map between count and which pins are active to use LEDs as a binary counter
   setting[0] = 0x00;
   setting[1] = LED_BIT_0;
   setting[2] = LED_BIT_1;
   setting[3] = LED_BIT_1 | LED_BIT_0;
   setting[4] = LED_BIT_2;
   setting[5] = LED_BIT_2 | LED_BIT_0;
   setting[6] = LED_BIT_2 | LED_BIT_1;
   setting[7] = LED_BIT_2 | LED_BIT_1 | LED_BIT_0;
   
   //only driving group 2 pins, so set group 1 to 0
   pinConfig.gpioBitMapToSetGr1 = 0;
   pinConfig.gpioBitMapToClrGr1 = 0;
   
   while (1) { // this is a task, it executes forever
      
      // Set the pins to the counter value          
      pinConfig.gpioBitMapToSetGr2 = setting[gpio_multi_v.count];
      pinConfig.gpioBitMapToClrGr2 = setting[gpio_multi_v.count] ^ (LED_BIT_0 | LED_BIT_1 | LED_BIT_2);
      
      dnm_ucli_printf("Count = %d\r\n", gpio_multi_v.count); 
      
      // IO are set using a dn_ioctl call
      dnErr = dn_ioctl(
                    DN_GPIO_OUT_PINS_DEV_ID,       // device
                    DN_IOCTL_GPIO_CFG_OUTPUT,      // this argument is ignored
                    &pinConfig,                    // pointer to pin bitmaps struct
                    sizeof(pinConfig));            // size of pin bitmap struct
      ASSERT(dnErr==DN_ERR_NONE);
        
      // increment count - roll over after 7
      gpio_multi_v.count++;
      gpio_multi_v.count %= (1<<COUNTER_BITS);

      OSTimeDly(SECOND/2);
      
      // blank the pins so we can see that they're set as a group
      pinConfig.gpioBitMapToSetGr2 = 0;
      pinConfig.gpioBitMapToClrGr2 = LED_BIT_0 | LED_BIT_1 | LED_BIT_2;
      
      dnErr = dn_ioctl(
                    DN_GPIO_OUT_PINS_DEV_ID, 
                    DN_IOCTL_GPIO_CFG_OUTPUT, 
                    &pinConfig,
                    sizeof(pinConfig));
      ASSERT(dnErr==DN_ERR_NONE);
       
      OSTimeDly(100); // 100 ms
   }
}

//========================== Helpers ==========================================

//============ Initialize GPIO pins
dn_error_t  initPins(){
   dn_error_t                     dnErr;
  
   // open the "out pins" device
   dnErr =  dn_open(
                    DN_GPIO_OUT_PINS_DEV_ID,    //device
                    NULL,                       // args
                    0                           //argLen 
   );	
   ASSERT(dnErr==DN_ERR_NONE); 

   return (DN_ERR_NONE);
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
