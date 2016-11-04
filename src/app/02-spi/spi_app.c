/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_gpio.h"
#include "dn_spi.h"
#include "dn_exe_hdr.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================

#define SPI_TX_BUFFER_LENGTH 2
#define SPI_RX_BUFFER_LENGTH 6

//=========================== variables =======================================

typedef struct {
   char                      ledToggleFlag;
   OS_STK                    spiTaskStack[TASK_APP_SPI_STK_SIZE];
   INT8U                     spiTxBuffer[SPI_TX_BUFFER_LENGTH];
   INT8U                     *spiRxBuffer;
} spi_app_vars_t;

spi_app_vars_t     spi_app_v;

// Each SPI transaction is required to write into RX buffer on a word boundary,
// so force word alignment is needed for the start address of the rx buffer
#pragma data_alignment = 4
INT8U                     spiRxBuffer[SPI_RX_BUFFER_LENGTH];

//=========================== prototypes ======================================

static void   spiTask(void* unused);

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   INT8U                   osErr;

   cli_task_init(
      "spi",                                // appName
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
   
   // create the SPI task
   osErr  = OSTaskCreateExt(
      spiTask,
      (void *)0,
      (OS_STK*)(&spi_app_v.spiTaskStack[TASK_APP_SPI_STK_SIZE-1]),
      TASK_APP_SPI_PRIORITY,
      TASK_APP_SPI_PRIORITY,
      (OS_STK*)spi_app_v.spiTaskStack,
      TASK_APP_SPI_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_SPI_PRIORITY, (INT8U*)TASK_APP_SPI_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

//=========================== SPI task ========================================

/**
\brief A demo task to show the use of the SPI.
*/
static void spiTask(void* unused) {
   int                          err;
   dn_spi_open_args_t           spiOpenArgs;
   dn_gpio_ioctl_cfg_out_t      gpioOutCfg;
   dn_ioctl_spi_transfer_t      spiTransfer;
   INT8U                        receivedSomething;
   INT8U                        i;
   
   //===== initialize LEDs
   // configure LEDs as output, with initial level low (LEDs off)
   gpioOutCfg.initialLevel = 0x00;
   // GPIO22 corresponds to the (blue) LED labeled D5 on the Huron board
   dn_open(DN_GPIO_PIN_22_DEV_ID,
           NULL,
           0);
   dn_ioctl(DN_GPIO_PIN_22_DEV_ID,
            DN_IOCTL_GPIO_CFG_OUTPUT,
            &gpioOutCfg,
            sizeof(gpioOutCfg));
   // GPIO23 corresponds to the (green) LED labeled D7 on the Huron board
   dn_open(DN_GPIO_PIN_23_DEV_ID,
           NULL,
           0);
   dn_ioctl(DN_GPIO_PIN_23_DEV_ID,
            DN_IOCTL_GPIO_CFG_OUTPUT,
            &gpioOutCfg,
            sizeof(gpioOutCfg));
   // GPIO20 corresponds to the (green) LED labeled D6 on the Huron board
   dn_open(DN_GPIO_PIN_20_DEV_ID,
           NULL,
           0);
   dn_ioctl(DN_GPIO_PIN_20_DEV_ID,
            DN_IOCTL_GPIO_CFG_OUTPUT,
            &gpioOutCfg,
            sizeof(gpioOutCfg));
   
   //===== initialize SPI
   // open the SPI device
   // see doxygen documentation on maxTransactionLenForCPHA_1 when setting
   // spiTransfer.clockPhase = DN_SPI_CPHA_1;
   spiOpenArgs.maxTransactionLenForCPHA_1 = 0;
   err = dn_open(
      DN_SPI_DEV_ID,
      &spiOpenArgs,
      sizeof(spiOpenArgs)
   );
   ASSERT((err == DN_ERR_NONE) || (err == DN_ERR_STATE));
   
   spi_app_v.spiRxBuffer = spiRxBuffer;
   
   // initialize spi communication parameters
   spiTransfer.txData             = spi_app_v.spiTxBuffer;
   spiTransfer.rxData             = spi_app_v.spiRxBuffer;
   spiTransfer.transactionLen     = SPI_TX_BUFFER_LENGTH;
   spiTransfer.numSamples         = 1;
   spiTransfer.startDelay         = 0;
   spiTransfer.clockPolarity      = DN_SPI_CPOL_0;
   spiTransfer.clockPhase         = DN_SPI_CPHA_0;
   spiTransfer.bitOrder           = DN_SPI_MSB_FIRST;
   spiTransfer.slaveSelect        = DN_SPIM_SS_0n;
   spiTransfer.clockDivider       = DN_SPI_CLKDIV_16;
   spiTransfer.rxBufferLen        = SPI_RX_BUFFER_LENGTH;
   
   while(1) {
      // infinite loop
      
      // this call blocks the task until the specified timeout expires (in ms)
      OSTimeDly(1000);
      
      spi_app_v.ledToggleFlag = ~spi_app_v.ledToggleFlag;
      
      // toggle blue LED
      dn_write(
         DN_GPIO_PIN_22_DEV_ID,
         &spi_app_v.ledToggleFlag,
         sizeof(spi_app_v.ledToggleFlag)
      );
      
      //===== step 1. write over SPI
      
      // set bytes to send
      for (i=0;i<sizeof(spi_app_v.spiTxBuffer);i++) {
         spi_app_v.spiTxBuffer[i] = i;
      }
      
      // send bytes
      err = dn_ioctl(
         DN_SPI_DEV_ID,
         DN_IOCTL_SPI_TRANSFER,
         &spiTransfer,
         sizeof(spiTransfer)
      );
      ASSERT(err >= DN_ERR_NONE);
      
      // toggle green LED
      dn_write(
         DN_GPIO_PIN_23_DEV_ID,
         &spi_app_v.ledToggleFlag,
         sizeof(spi_app_v.ledToggleFlag)
      );
      
      // print on CLI
      dnm_ucli_printf("SPI sent:    ",err);
      for (i=0;i<sizeof(spi_app_v.spiTxBuffer);i++) {
         dnm_ucli_printf(" %02x",spi_app_v.spiTxBuffer[i]);
      }
      dnm_ucli_printf("\r\n");
      
      //===== step 2. verify we received something over SPI
      
      receivedSomething = 0;
      for (i=0;i<sizeof(spi_app_v.spiRxBuffer);i++) {
         if (spi_app_v.spiRxBuffer[i]!=0xff) {
            receivedSomething = 1;
         }
      }
      
      if (receivedSomething==1) {
         // toggle (second) green LED
         dn_write(
            DN_GPIO_PIN_20_DEV_ID,
            &spi_app_v.ledToggleFlag,
            sizeof(spi_app_v.ledToggleFlag)
         );
      }
      
      // print on CLI
      dnm_ucli_printf("SPI received:",err);
      for (i=0;i<sizeof(spi_app_v.spiTxBuffer);i++) {
         dnm_ucli_printf(" %02x",spi_app_v.spiRxBuffer[i]);
      }
      dnm_ucli_printf("\r\n");
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
