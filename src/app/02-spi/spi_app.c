/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_gpio.h"
#include "dn_spi.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================

#define SPI_BUFFER_LENGTH 8

//=========================== variables =======================================

typedef struct {
   dnm_cli_cont_t            cliContext;
   INT8U                     ledToggleFlag;
   OS_STK                    spiTaskStack[TASK_APP_SPI_STK_SIZE];
   INT8U                     spiTxBuffer[SPI_BUFFER_LENGTH];
   INT8U                     spiRxBuffer[SPI_BUFFER_LENGTH];
} spi_app_vars_t;

spi_app_vars_t     spi_app_v;

//=========================== prototypes ======================================

static void   spiTask(void* unused);

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   dn_error_t              status;
   dn_error_t              dnErr;
   INT8U                   osErr;

   cli_task_init(
      &spi_app_v.cliContext,                // cliContext
      "spi",                                // appName
      NULL                                  // cliCmds
   );
   loc_task_init(
      &spi_app_v.cliContext,                // cliContext
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
   spiOpenArgs.maxTransactionLenForCPHA_1 = 0;
   err = dn_open(
      DN_SPI_DEV_ID,
      &spiOpenArgs,
      sizeof(spiOpenArgs)
   );
   if ((err < DN_ERR_NONE) && (err != DN_ERR_STATE)) {
      dnm_cli_printf("unable to open SPI device, error %d\n\r",err);
   }
   
   // initialize spi communication parameters
   spiTransfer.txData             = spi_app_v.spiTxBuffer;
   spiTransfer.rxData             = spi_app_v.spiRxBuffer;
   spiTransfer.transactionLen     = sizeof(spi_app_v.spiTxBuffer);
   spiTransfer.numSamples         = 1;
   spiTransfer.startDelay         = 0;
   spiTransfer.clockPolarity      = DN_SPI_CPOL_0;
   spiTransfer.clockPhase         = DN_SPI_CPHA_0;
   spiTransfer.bitOrder           = DN_SPI_MSB_FIRST;
   spiTransfer.slaveSelect        = DN_SPI_SSn0;
   spiTransfer.clockDivider       = DN_SPI_CLKDIV_16;
   
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
      if (err < DN_ERR_NONE) {
         dnm_cli_printf("Unable to communicate over SPI, err=%d\r\n",err);
      }
      
      // toggle green LED
      dn_write(
         DN_GPIO_PIN_23_DEV_ID,
         &spi_app_v.ledToggleFlag,
         sizeof(spi_app_v.ledToggleFlag)
      );
      
      // print on CLI
      dnm_cli_printf("SPI sent:    ",err);
      for (i=0;i<sizeof(spi_app_v.spiTxBuffer);i++) {
         dnm_cli_printf(" %02x",spi_app_v.spiTxBuffer[i]);
      }
      dnm_cli_printf("\r\n");
      
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
      dnm_cli_printf("SPI received:",err);
      for (i=0;i<sizeof(spi_app_v.spiTxBuffer);i++) {
         dnm_cli_printf(" %02x",spi_app_v.spiRxBuffer[i]);
      }
      dnm_cli_printf("\r\n");
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
