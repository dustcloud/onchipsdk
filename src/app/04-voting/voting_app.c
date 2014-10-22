/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "string.h"
#include "stdio.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_time.h"
#include "dn_gpio.h"
#include "dn_exe_hdr.h"
#include "dnm_local.h"
#include "app_task_cfg.h"
#include "dn_api_param.h"
#include "Ver.h"
#include "well_known_ports.h"

//=========================== definitions =====================================

// DC9003A PINS
#define BUTTON_A        DN_GPIO_PIN_13_DEV_ID  // SPIM_SS_1n
#define BUTTON_B        DN_GPIO_PIN_16_DEV_ID  // PWM0
#define BUTTON_C        DN_GPIO_PIN_21_DEV_ID  // DP2
#define BUTTON_D        DN_GPIO_PIN_26_DEV_ID  // SPIS_MOSI
#define BUTTON_WHITE    DN_GPIO_PIN_0_DEV_ID   // DP0 - nostuff
#define BUTTON_BLACK    DN_GPIO_PIN_23_DEV_ID  // DP4 - nostuff

// bitmasks for report
#define NONE            0x00
#define BUTTON_A_MASK   0x01
#define BUTTON_B_MASK   0x02
#define BUTTON_C_MASK   0x04
#define BUTTON_D_MASK   0x08

//DC9003A LEDs
#define GREEN1_LED      DN_GPIO_PIN_20_DEV_ID  // STATUS_0 - DP1
#define GREEN2_LED      DN_GPIO_PIN_23_DEV_ID  // STATUS_1 - DP4
#define BLUE_LED        DN_GPIO_PIN_22_DEV_ID  // INDICATOR_0 - DP3

#define HIGH            1
#define LOW             0
#define ENABLE          1

// durations
#define VOTING_LOCKOUT  10000  // ms
#define STATUS_LED_ON   100    // ms
#define STATUS_LED_OFF  1900   // ms
#define VOTING_LED_ON   10     // ms
#define VOTING_LED_OFF  10     // ms

//=========================== variables =======================================

typedef struct {
   //=== tasks
   // buttonTask
   OS_STK          buttonTaskStack[TASK_APP_BUTTON_STK_SIZE];
   INT32U          gpioNotifChannelBuf[1+DN_CH_ASYNC_RXBUF_SIZE(sizeof(dn_gpio_notif_t))/sizeof(INT32U)];
   // sendTask
   OS_STK          sendTaskStack[TASK_APP_SEND_STK_SIZE];
   // votingLEDTask
   OS_STK          votingLEDTaskStack[TASK_APP_VOTINGLED_STK_SIZE];
   // statusLEDsTask
   OS_STK          statusLEDsTaskStack[TASK_APP_STATUSLEDS_STK_SIZE];
   //=== app
   OS_EVENT*       joinedSem;          ///< posted when stack has joined
   OS_EVENT*       dataLock;           ///< mutex to access shared variables
   OS_EVENT*       sendNow;            ///< posted to trigger sendTask to start sending
   INT8U           busyVoting;         ///< 1 iff sendTask busy voting
   INT8U           buttonMask;         ///< bitmap of button(s) pressed
   OS_EVENT*       blinkNow;           ///< posted to trigger votingLEDTask to start blinking
} voting_app_vars_t;

voting_app_vars_t voting_v;

//=========================== prototypes ======================================

//===== tasks
static void buttonTask(void* unused);
static void sendTask(void* unused);
static void votingLEDTask(void* unused);
static void statusLEDsTask(void* unused);

//===== functions
void lockData();
void unlockData();

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   dn_error_t dnErr;
   INT8U      osErr;
   
   //===== initialize module variables
   memset(&voting_v,0,sizeof(voting_app_vars_t));
   
   // create joinedSem semaphore
   voting_v.joinedSem = OSSemCreate(0);
   ASSERT (voting_v.joinedSem!=NULL);
   
   // create dataLock semaphore
   voting_v.dataLock = OSSemCreate(1); // unlocked by default
   ASSERT (voting_v.dataLock!=NULL);
   
   // create sendNow semaphore
   voting_v.sendNow = OSSemCreate(0);
   ASSERT (voting_v.sendNow!=NULL);
   
   // create blinkNow semaphore
   voting_v.blinkNow = OSSemCreate(0);
   ASSERT (voting_v.blinkNow!=NULL);
   
   //===== initialize helper tasks
   
   // CLI task
   cli_task_init(
      "Mote + voting = moting",             // appName
      NULL                                  // cliCmds
   );
   
   // local interface task
   loc_task_init(
      JOIN_YES,                             // fJoin
      NETID_NONE,                           // netId
      WKP_USER_1,                           // udpPort
      voting_v.joinedSem,                   // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== create buttonTask
   
   osErr = OSTaskCreateExt(
      buttonTask,
      (void*) 0,
      (OS_STK*)(&voting_v.buttonTaskStack[TASK_APP_BUTTON_STK_SIZE- 1]),
      TASK_APP_BUTTON_PRIORITY,
      TASK_APP_BUTTON_PRIORITY,
      (OS_STK*)voting_v.buttonTaskStack,
      TASK_APP_BUTTON_STK_SIZE,
      (void*) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_BUTTON_PRIORITY, (INT8U*)TASK_APP_BUTTON_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   //===== create sendTask
   
   osErr = OSTaskCreateExt(
      sendTask,
      (void*) 0,
      (OS_STK*)(&voting_v.sendTaskStack[TASK_APP_SEND_STK_SIZE- 1]),
      TASK_APP_SEND_PRIORITY,
      TASK_APP_SEND_PRIORITY,
      (OS_STK*)voting_v.sendTaskStack,
      TASK_APP_SEND_STK_SIZE,
      (void*) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_SEND_PRIORITY, (INT8U*)TASK_APP_SEND_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   //===== create votingLEDTask
   
   osErr = OSTaskCreateExt(
      votingLEDTask,
      (void*) 0,
      (OS_STK*)(&voting_v.votingLEDTaskStack[TASK_APP_VOTINGLED_STK_SIZE- 1]),
      TASK_APP_VOTINGLED_PRIORITY,
      TASK_APP_VOTINGLED_PRIORITY,
      (OS_STK*)voting_v.votingLEDTaskStack,
      TASK_APP_VOTINGLED_STK_SIZE,
      (void*) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_VOTINGLED_PRIORITY, (INT8U*)TASK_APP_VOTINGLED_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   //===== create statusLEDsTask
   
   osErr = OSTaskCreateExt(
      statusLEDsTask,
      (void*) 0,
      (OS_STK*)(&voting_v.statusLEDsTaskStack[TASK_APP_STATUSLEDS_STK_SIZE- 1]),
      TASK_APP_STATUSLEDS_PRIORITY,
      TASK_APP_STATUSLEDS_PRIORITY,
      (OS_STK*)voting_v.statusLEDsTaskStack,
      TASK_APP_STATUSLEDS_STK_SIZE,
      (void*) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_STATUSLEDS_PRIORITY, (INT8U*)TASK_APP_STATUSLEDS_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== buttonTask ======================================

static void buttonTask(void* unused) {
   dn_error_t                    dnErr;
   INT8U                         osErr;
   OS_MEM*                       buttonNotifChMem;
   CH_DESC                       buttonNotifCh;
   dn_gpio_ioctl_cfg_in_t        gpioInCfg;
   dn_gpio_ioctl_notif_enable_t  gpioNotifEnable;
   dn_gpio_notif_t               gpioNotif;
   INT32U                        rxLen;
   INT32U                        msgType;
   INT8U                         rc;
   INT8U                         buttonMask;
   
   //===== wait for the mote to have joined
   OSSemPend(voting_v.joinedSem,0,&osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   //===== open and configure the button pins as input
   gpioInCfg.pullMode = DN_GPIO_PULL_UP;
   
   dnErr = dn_open(BUTTON_A, NULL,0);
   ASSERT(dnErr >= 0);
   dnErr = dn_ioctl(BUTTON_A, DN_IOCTL_GPIO_CFG_INPUT, &gpioInCfg, sizeof(gpioInCfg));
   ASSERT(dnErr >= 0);
   
   dnErr = dn_open(BUTTON_B, NULL,0);
   ASSERT(dnErr >= 0);
   dnErr = dn_ioctl(BUTTON_B, DN_IOCTL_GPIO_CFG_INPUT, &gpioInCfg, sizeof(gpioInCfg));
   ASSERT(dnErr >= 0);
   
   dnErr = dn_open(BUTTON_C, NULL,0);
   ASSERT(dnErr >= 0);
   dnErr = dn_ioctl(BUTTON_C, DN_IOCTL_GPIO_CFG_INPUT, &gpioInCfg, sizeof(gpioInCfg));
   ASSERT(dnErr >= 0);
   
   dnErr = dn_open(BUTTON_D, NULL,0);
   ASSERT(dnErr >= 0);
   dnErr = dn_ioctl(BUTTON_D, DN_IOCTL_GPIO_CFG_INPUT, &gpioInCfg, sizeof(gpioInCfg));
   ASSERT(dnErr >= 0);
   
   //===== enable interrupts on all buttons
   
   // allocate memory for GPIO notification channel
   buttonNotifChMem = OSMemCreate(
      voting_v.gpioNotifChannelBuf,
      1,
      DN_CH_ASYNC_RXBUF_SIZE(sizeof(dn_gpio_notif_t)),
      &osErr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   // create channel
   dnErr = dn_createAsyncChannel(buttonNotifChMem, &buttonNotifCh);
   ASSERT(dnErr == DN_ERR_NONE);
   
   // enable GPIO notifications
   gpioNotifEnable.fEnable             = ENABLE;
   gpioNotifEnable.activeLevel         = LOW;
   gpioNotifEnable.notifChannelId      = buttonNotifCh;
   
   dnErr = dn_ioctl(BUTTON_A, DN_IOCTL_GPIO_ENABLE_NOTIF, &gpioNotifEnable, sizeof(gpioNotifEnable));
   ASSERT(dnErr == DN_ERR_NONE);
   
   dnErr = dn_ioctl(BUTTON_B, DN_IOCTL_GPIO_ENABLE_NOTIF, &gpioNotifEnable, sizeof(gpioNotifEnable));
   ASSERT(dnErr == DN_ERR_NONE);
   
   dnErr = dn_ioctl(BUTTON_C, DN_IOCTL_GPIO_ENABLE_NOTIF, &gpioNotifEnable, sizeof(gpioNotifEnable));
   ASSERT(dnErr == DN_ERR_NONE);
   
   dnErr = dn_ioctl(BUTTON_D, DN_IOCTL_GPIO_ENABLE_NOTIF, &gpioNotifEnable, sizeof(gpioNotifEnable));
   ASSERT(dnErr == DN_ERR_NONE);
   
   while (1) { // this is a task, it executes forever
   
      // wait for any pending GPIO notification
      dnErr = dn_readAsyncMsg(
         buttonNotifCh,            // chDesc
         &gpioNotif,              // msg
         &rxLen,                  // rxLen
         &msgType,                // msgType
         sizeof(gpioNotif),       // maxLen
         0                        // timeout
      );
      ASSERT(dnErr==DN_ERR_NONE);
      ASSERT(msgType==DN_MSG_TYPE_GPIO_NOTIF);
      
      if (gpioNotif.level==LOW) {
      
         // print
         switch (gpioNotif.gpioDeviceId) {
            case BUTTON_A:
               buttonMask = BUTTON_A_MASK;
               dnm_ucli_printf("button A pressed\r\n");
               break;
            case BUTTON_B:
               buttonMask = BUTTON_B_MASK;
               dnm_ucli_printf("button B pressed\r\n");
               break;
            case BUTTON_C:
               buttonMask = BUTTON_C_MASK;
               dnm_ucli_printf("button C pressed\r\n");
               break;
            case BUTTON_D:
               buttonMask = BUTTON_D_MASK;
               dnm_ucli_printf("button D pressed\r\n");
               break;
            default:
               break;
         }
         
         //<<<<< lock
         lockData();
         
         if (voting_v.busyVoting==0) {
            
            // log
            dnm_ucli_printf("+++++ busy\r\n");
            
            // I'm busy
            voting_v.busyVoting = 1;
            
            // record buttonMask
            voting_v.buttonMask = buttonMask;
            
            // indicate to sendTask
            OSSemPost(voting_v.sendNow);
         }
         
         //>>>>> unlock
         unlockData();
      }
      
      // re-arm notification in opposite direction
      gpioNotifEnable.fEnable          = ENABLE;
      if (gpioNotif.level==LOW) {
         gpioNotifEnable.activeLevel   = HIGH;
      } else {
         gpioNotifEnable.activeLevel   = LOW;
      }
      gpioNotifEnable.notifChannelId   = buttonNotifCh;
      
      dnErr = dn_ioctl(gpioNotif.gpioDeviceId, DN_IOCTL_GPIO_ENABLE_NOTIF, &gpioNotifEnable, sizeof(gpioNotifEnable));
      ASSERT(dnErr == DN_ERR_NONE);
   }
}

//=========================== sendTask ========================================

/**
\brief Sends the contents of voting_v.buttonMask to manager
*/
static void sendTask(void* unused) {
   dn_error_t                    dnErr;
   INT8U                         osErr;
   INT8U                         pkBuf[sizeof(loc_sendtoNW_t) + 1];
   loc_sendtoNW_t*               packet;
   INT8U                         rc;
   
   //===== initialize packet variables
   packet = (loc_sendtoNW_t*)pkBuf;
   
   while (1) { // this is a task, it executes forever
      
      // wait to be told to send
      OSSemPend(voting_v.sendNow,0,&osErr);
      ASSERT(osErr == OS_ERR_NONE);
      
      // write packet metadata
      packet->locSendTo.socketId       = loc_getSocketId();
      packet->locSendTo.destAddr       = DN_MGR_IPV6_MULTICAST_ADDR;
      packet->locSendTo.destPort       = WKP_USER_1;
      packet->locSendTo.serviceType    = DN_API_SERVICE_TYPE_BW;   
      packet->locSendTo.priority       = DN_API_PRIORITY_MED;   
      packet->locSendTo.packetId       = 0xFFFF;
      
      //<<<<< lock
      lockData();
      
      // write packet payload
      packet->locSendTo.payload[0]     = voting_v.buttonMask;
      
      //>>>>> unlock
      unlockData();
      
      // log
      dnm_ucli_printf("sending 0x%02x\r\n",packet->locSendTo.payload[0]);
      
      // send packet
      dnErr = dnm_loc_sendtoCmd(packet, 1, &rc);
      ASSERT (dnErr == DN_ERR_NONE);
      
      // start votingLED
      OSSemPost(voting_v.blinkNow);
      
      // lock out voting for some time
      OSTimeDly(VOTING_LOCKOUT);
      
      //<<<<< lock
      lockData();
      
      // I'm NOT busy
      voting_v.busyVoting = 0;
      
      // log
      dnm_ucli_printf("----- NOT busy\r\n\r\n");
      
      //>>>>> unlock
      unlockData();
   }
}

//=========================== votingLEDTask =====================================

/**
\brief Blinks blue LED.
*/
static void votingLEDTask(void* unused) {
   dn_error_t                     dnErr;
   INT8U                          osErr;
   dn_gpio_ioctl_cfg_out_t        gpioOutCfg;
   INT8U                          ledState;
   INT8U                          keepBlinking;
   
   // open LED pin as output
   dnErr = dn_open(BLUE_LED, NULL, 0);
   ASSERT(dnErr==DN_ERR_NONE);
   
   // configure as output, LOW
   gpioOutCfg.initialLevel = LOW;
   dnErr = dn_ioctl(BLUE_LED, DN_IOCTL_GPIO_CFG_OUTPUT, &gpioOutCfg, sizeof(gpioOutCfg));
   ASSERT(dnErr==DN_ERR_NONE);
   
   while (1) {
      
      //===== wait to be asked to blink
      OSSemPend(voting_v.blinkNow,0,&osErr);
      ASSERT(osErr == OS_ERR_NONE);
      
      keepBlinking = 1;
      
      while(keepBlinking==1) {
         
         // switch high
         ledState = HIGH;
         dnErr = dn_write(BLUE_LED, &ledState, sizeof(ledState));
         OSTimeDly(VOTING_LED_ON);
         
         // switch low
         ledState = LOW;
         dnErr = dn_write(BLUE_LED, &ledState, sizeof(ledState));
         OSTimeDly(VOTING_LED_OFF);
         
         //<<<<< lock
         lockData();
         
         // check if needs to keep blinking
         keepBlinking = voting_v.busyVoting;
         
         //>>>>> unlock
         unlockData();
      }
   }
}

//=========================== statusLEDsTask ==================================

/**
\brief Blinks green LEDs to indicate status of mote.
*/
static void statusLEDsTask(void* unused) {
   dn_error_t                     dnErr;
   dn_gpio_ioctl_cfg_out_t        gpioOutCfg;
   dn_api_rsp_get_motestatus_t    *currentState;
   INT8U                          statusBuf[2+sizeof(dn_api_rsp_get_motestatus_t)];
   INT8U                          respLen;
   INT8U                          rc;
   INT8U                          ledState;
   
   //=== open LEDs
   dnErr = dn_open(GREEN1_LED, NULL, 0);
   ASSERT(dnErr==DN_ERR_NONE);
   
   dnErr = dn_open(GREEN2_LED, NULL, 0);
   ASSERT(dnErr==DN_ERR_NONE);
   
   //=== configure as output LOW
   gpioOutCfg.initialLevel = LOW;
   
   dnErr = dn_ioctl(GREEN1_LED, DN_IOCTL_GPIO_CFG_OUTPUT, &gpioOutCfg, sizeof(gpioOutCfg));
   ASSERT(dnErr==DN_ERR_NONE);
   
   dnErr = dn_ioctl(GREEN2_LED, DN_IOCTL_GPIO_CFG_OUTPUT, &gpioOutCfg, sizeof(gpioOutCfg));
   ASSERT(dnErr==DN_ERR_NONE);
   
   // wait for stack to initialize
   OSTimeDly(100);
   
   while (1) { // this is a task, it executes forever
      
      //===== read current state
      
      dnErr =  dnm_loc_getParameterCmd(
         DN_API_PARAM_MOTESTATUS,
         &statusBuf,                        // payload
         0,                                 // txPayloadLen
         &respLen,                          // rxPayloadLen
         &rc                                // rc
      );
      ASSERT(dnErr==DN_ERR_NONE);
      ASSERT(rc==DN_ERR_NONE);
      currentState = (dn_api_rsp_get_motestatus_t*)(&statusBuf[0]);
      
      switch (currentState->state) {
         case DN_API_ST_INIT:
         case DN_API_ST_IDLE:
            // no LEDs to blink
            
            // wait a bit before checking again
            OSTimeDly(1000);
            break;
         case DN_API_ST_SEARCHING:
         case DN_API_ST_NEGO:
         case DN_API_ST_CONNECTED:
            
            // switch on green1 LED
            ledState = HIGH;
            dnErr = dn_write(GREEN1_LED, &ledState, sizeof(ledState));
            ASSERT(dnErr==DN_ERR_NONE);
            OSTimeDly(STATUS_LED_ON);
            
            // switch off green1 LED
            ledState = LOW;
            dnErr = dn_write(GREEN1_LED, &ledState, sizeof(ledState));
            ASSERT(dnErr==DN_ERR_NONE);
            OSTimeDly(STATUS_LED_OFF);
            
            break;
         case DN_API_ST_OPERATIONAL:
            
            // switch on green1 and green 2 LEDs
            ledState = HIGH;
            dnErr = dn_write(GREEN1_LED, &ledState, sizeof(ledState));
            ASSERT(dnErr==DN_ERR_NONE);
            dnErr = dn_write(GREEN2_LED, &ledState, sizeof(ledState));
            ASSERT(dnErr==DN_ERR_NONE);
            OSTimeDly(STATUS_LED_ON);
            
            // switch off green1 and green 2 LEDs
            ledState = LOW;
            dnErr = dn_write(GREEN1_LED, &ledState, sizeof(ledState));
            ASSERT(dnErr==DN_ERR_NONE);
            dnErr = dn_write(GREEN2_LED, &ledState, sizeof(ledState));
            ASSERT(dnErr==DN_ERR_NONE);
            OSTimeDly(STATUS_LED_OFF);
            
            break;
         default:
            ASSERT(0);
            break;
      }
   }
}

//=========================== helpers =========================================

void lockData() {
   INT8U      osErr;
   
   OSSemPend(voting_v.dataLock,0,&osErr);
   ASSERT(osErr == OS_ERR_NONE);
}

void unlockData() {
   OSSemPost(voting_v.dataLock);
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
