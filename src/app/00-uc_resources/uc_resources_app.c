/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include <string.h>
#include "dn_system.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_spi.h"
#include "dn_i2c.h"
#include "dn_onewire.h"
#include "dn_gpio.h"
#include "dn_exe_hdr.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================

#define MAX_UART_PACKET_SIZE   (128u)
#define DELAY_TIMER_DUMMY       10

//=========================== variables =======================================

typedef struct {
   INT32U         gpioNotifChannelBuf[1+DN_CH_ASYNC_RXBUF_SIZE(sizeof(dn_gpio_notif_t))/sizeof(INT32U)];
   INT32U         uartNotifChannelBuf[1+DN_CH_ASYNC_RXBUF_SIZE(MAX_UART_PACKET_SIZE)/sizeof(INT32U)];
   INT8U          numFreeSems;
   // resourceTask
   OS_STK         resourceTaskStack[TASK_APP_RESOURCE_STK_SIZE];
} uc_resources_app_vars_t;

uc_resources_app_vars_t uc_resources_app_vars;

//=========================== prototypes ======================================

//===== resourceTask task
static void resourceTask(void* unused);
//===== helpers
void  print_available_ECBs(char* msgString);
INT8U getNumFreeEcb();
void  print_available_timers();
INT8U getNumFreeTimers();
void  timer_dummy_cb(void* pTimer, void *pArgs);

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   dn_error_t      dnErr;
   INT8U           osErr;
   
   //==== initialize local variable
   memset(&uc_resources_app_vars,0,sizeof(uc_resources_app_vars));
   
   //==== initialize helper tasks
   
   cli_task_init(
      "uc_resources",                       // appName
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
   
   //===== resourceTask task
   
   osErr = OSTaskCreateExt(
      resourceTask,
      (void *) 0,
      (OS_STK*) (&uc_resources_app_vars.resourceTaskStack[TASK_APP_RESOURCE_STK_SIZE-1]),
      TASK_APP_RESOURCE_PRIORITY,
      TASK_APP_RESOURCE_PRIORITY,
      (OS_STK*) uc_resources_app_vars.resourceTaskStack,
      TASK_APP_RESOURCE_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_RESOURCE_PRIORITY, (INT8U*)TASK_APP_RESOURCE_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== resourceTask task ===============================

static void resourceTask(void* unused) {
   dn_error_t                dnErr;
   INT8U                     osErr;
   INT8U                     rc;
   OS_EVENT*                 tempSem;
   dn_spi_open_args_t        spiOpenArgs;
   dn_i2c_open_args_t        i2cOpenArgs;
   dn_ow_open_args_t         owOpenArgs;
   OS_MEM*                   gpioNotifChannelMem;
   CH_DESC                   gpioNotifChannel;
   OS_MEM*                   uartNotifChannelMem;
   CH_DESC                   uartNotifChannel;
   
   //=== wait for all delays related to booting to expire
   dnm_ucli_printf("waiting for part of boot...\r\n");
   OSTimeDly(2000);
   
   //=== create initial 
   
   print_available_ECBs("initially:         ");
   
   //=== create semaphore
   
   tempSem = OSSemCreate(0);
   ASSERT(tempSem!=NULL);
   
   print_available_ECBs("create semaphore:  ");
   
   //=== delete semaphore
   
   OSSemDel(tempSem,OS_DEL_NO_PEND,&osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   print_available_ECBs("delete semaphore:  ");
   
   //=== open the SPI driver
   spiOpenArgs.maxTransactionLenForCPHA_1 = 0;
   dnErr = dn_open(
      DN_SPI_DEV_ID,                        // device
      &spiOpenArgs,                         // args
      sizeof(spiOpenArgs)                   // argLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   print_available_ECBs("Open SPI:          ");
   
   //=== open the I2C driver
   
   i2cOpenArgs.frequency = DN_I2C_FREQ_92_KHZ;
   dnErr = dn_open(
      DN_I2C_DEV_ID,                        // device
      &i2cOpenArgs,                         // args
      sizeof(i2cOpenArgs)                   // argLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   print_available_ECBs("Open I2C:          ");
   
   //=== open the 1-Wire driver
   
   owOpenArgs.maxTransactionLength = 10;
   dnErr = dn_open(
      DN_1WIRE_UARTC1_DEV_ID,               // device
      &owOpenArgs,                          // args
      sizeof(owOpenArgs)                    // argLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   print_available_ECBs("Open 1-Wire:       ");
   
   //=== join a network
   
   dnErr = dnm_loc_joinCmd(&rc);
   ASSERT(dnErr==DN_ERR_NONE);
   ASSERT(rc==DN_ERR_NONE);
   
   print_available_ECBs("join network:      ");
   
   //=== GPIO notification channel
   
   gpioNotifChannelMem = OSMemCreate(
      uc_resources_app_vars.gpioNotifChannelBuf,
      1,
      sizeof(uc_resources_app_vars.gpioNotifChannelBuf),
      &osErr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   dnErr = dn_createAsyncChannel(
      gpioNotifChannelMem,
      &gpioNotifChannel
   );
   ASSERT(dnErr == DN_ERR_NONE);
   
   print_available_ECBs("GPIO notif chan.:  ");
   
   //=== UART notification channel
   
   uartNotifChannelMem = OSMemCreate(
      uc_resources_app_vars.uartNotifChannelBuf,
      1,
      sizeof(uc_resources_app_vars.uartNotifChannelBuf),
      &osErr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   dnErr = dn_createAsyncChannel(
      uartNotifChannelMem,
      &uartNotifChannel
   );
   ASSERT(dnErr == DN_ERR_NONE);
   
   print_available_ECBs("UART notif chan.:  ");
   
   //=== print periodically
   
   while(1) { // this is a task, it executes forever
      
      // wait a bit
      OSTimeDly(10000); 
     
      // print the number of available ECBs
      print_available_ECBs("                   ");
      
      // print the number of available timers
      print_available_timers();
   }
}

//=========================== helpers =========================================

void print_available_ECBs(char* msgString) {
    INT8U newNumSems;
    
    newNumSems = getNumFreeEcb();
    
    dnm_ucli_printf("%s %d ECBs available",msgString,newNumSems);
    if (uc_resources_app_vars.numFreeSems>0) {
       dnm_ucli_printf(" (%d)",newNumSems-uc_resources_app_vars.numFreeSems);
    }
    dnm_ucli_printf("\r\n");
    
    uc_resources_app_vars.numFreeSems = newNumSems;
}

INT8U getNumFreeEcb() {
   INT8U      numFreeECB;
   INT8U      ecbAvailable;
   OS_EVENT*  freeSems[OS_MAX_EVENTS];
   INT8U      i;
   INT8U      osErr;
   
   memset(freeSems,0,sizeof(freeSems));
   
   // reserve all available semaphores
   numFreeECB   = 0;
   ecbAvailable = 1;
   while(ecbAvailable==1) {
      freeSems[numFreeECB] = OSSemCreate(0);
      if (freeSems[numFreeECB]==NULL) {
         ecbAvailable = 0;
      } else {
         numFreeECB++;
      }
   }
   
   // free all reserved semaphores
   for (i=0;i<numFreeECB;i++) {
      OSSemDel(freeSems[i],OS_DEL_NO_PEND,&osErr);
      ASSERT(osErr==OS_ERR_NONE);
   }
   
   return numFreeECB;
}

void print_available_timers(char* msgString) {
    INT8U numTimers;
    
    numTimers = getNumFreeTimers();
    
    dnm_ucli_printf("                    %d timers available\r\n",numTimers);
}

INT8U getNumFreeTimers() {
   INT8U      numFreeTimers;
   INT8U      timerAvailable;
   OS_TMR*    freeTimers[OS_TMR_CFG_MAX];
   INT8U      i;
   INT8U      osErr;
   
   memset(freeTimers,0,sizeof(freeTimers));
   
   // reserve all available semaphores
   numFreeTimers   = 0;
   timerAvailable  = 1;
   while(timerAvailable==1) {
      freeTimers[numFreeTimers] = OSTmrCreate(
         DELAY_TIMER_DUMMY,                 // dly
         DELAY_TIMER_DUMMY,                 // period
         OS_TMR_OPT_PERIODIC,               // opt
         (OS_TMR_CALLBACK)&timer_dummy_cb,  // callback
         NULL,                              // callback_arg
         NULL,                              // pname
         &osErr                             // perr
      );
      if (freeTimers[numFreeTimers]==NULL) {
         ASSERT(osErr==OS_ERR_TMR_NON_AVAIL);
         timerAvailable = 0;
      } else {
         ASSERT(osErr==OS_ERR_NONE);
         numFreeTimers++;
      }
   }
   
   // free all reserved semaphores
   for (i=0;i<numFreeTimers;i++) {
      OSTmrDel(freeTimers[i],&osErr);
      ASSERT(osErr==OS_ERR_NONE);
   }
   
   return numFreeTimers;
}

void timer_dummy_cb(void* pTimer, void *pArgs) {
   // this function should never be called
   ASSERT(0);
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
