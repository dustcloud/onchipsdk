/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "string.h"
#include "stdio.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_spi.h"
#include "dnm_local.h"
#include "dn_system.h"
#include "dn_gpio.h"
#include "dn_fs.h"
#include "dn_exe_hdr.h"
#include "well_known_ports.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================

#define SPI_BUFFER_LENGTH         4
#define APP_DATA_BUF_SIZE         SPI_BUFFER_LENGTH
#define PERIOD_DEFAULT            10000

#define ACCELAPP_CONFIG_FILENAME  "2spiNApp.cfg"

// format of config file
typedef struct{
   INT16U          period;
} spiNetApp_configFileStruct_t;

//=========================== variables =======================================

/// variables local to this application
typedef struct {
   OS_STK                    spiTaskStack[TASK_APP_SPI_STK_SIZE];
   INT8U                     spiTxBuffer[SPI_BUFFER_LENGTH];
   INT8U                     spiRxBuffer[SPI_BUFFER_LENGTH];
   OS_EVENT*                 joinedSem;
   INT16U                    period;        ///< period (in ms) between transmissions
} spiNetApp_vars_t;

spiNetApp_vars_t     spiNetApp_vars;

//=========================== prototypes ======================================

//===== initialization
       void app_init(void);
//===== CLI
dn_error_t  spiNetApp_cli_config(INT8U* buf, INT32U len);
dn_error_t  spiNetApp_cli_period(INT8U* buf, INT32U len);
//===== configFile
       void initConfigFile(void);
       void syncToConfigFile(void);
       void printConfig(void);
//===== SPI task
static void spiTask(void* unused);

//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&spiNetApp_cli_config,   "config", "config",                     DN_CLI_ACCESS_LOGIN},
   {&spiNetApp_cli_period,   "period", "period [newPeriod in ms]",   DN_CLI_ACCESS_LOGIN},
   {NULL,                    NULL,     NULL,                         0},
};

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   dn_gpio_ioctl_cfg_out_t   gpioOutCfg;
   dn_error_t                status;
   dn_error_t                dnErr;
   INT8U                     osErr;
   
   //===== initialize module variables
   memset(&spiNetApp_vars,0,sizeof(spiNetApp_vars_t));
   spiNetApp_vars.period     = PERIOD_DEFAULT;
   
   //===== initialize helper tasks
   
   // create a semaphore to indicate mote joined
   spiNetApp_vars.joinedSem = OSSemCreate(0);
   ASSERT(spiNetApp_vars.joinedSem!=NULL);
   
   // CLI task
   cli_task_init(
      "spi_net",                            // appName
      &cliCmdDefs                           // cliCmds
   );
   
   // local interface task
   loc_task_init(
      JOIN_YES,                             // fJoin
      NULL,                                 // netId
      WKP_SPI_NET,                          // udpPort
      spiNetApp_vars.joinedSem,             // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== create the SPI task
   osErr  = OSTaskCreateExt(
      spiTask,
      (void *)0,
      (OS_STK*)(&spiNetApp_vars.spiTaskStack[TASK_APP_SPI_STK_SIZE-1]),
      TASK_APP_SPI_PRIORITY,
      TASK_APP_SPI_PRIORITY,
      (OS_STK*)spiNetApp_vars.spiTaskStack,
      TASK_APP_SPI_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_SPI_PRIORITY, (INT8U*)TASK_APP_SPI_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== CLI =============================================

// "config" command
dn_error_t spiNetApp_cli_config(INT8U* buf, INT32U len) {
   printConfig();
   return DN_ERR_NONE;
}

// "period" command
dn_error_t spiNetApp_cli_period(INT8U* buf, INT32U len) {
   int        sarg;
   int        l;
   
   //--- param 0: period
   l = sscanf (buf, "%d", &sarg);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   else {
      spiNetApp_vars.period = (INT16U)sarg;
   }
   
   // sync configuration to file
   syncToConfigFile();
   
   return DN_ERR_NONE;
}

//=========================== configFile ======================================

void initConfigFile(void) {
   dn_error_t                    dnErr;
   INT8U                         fileBytes[sizeof(spiNetApp_configFileStruct_t)];
   spiNetApp_configFileStruct_t*  fileContents;
   dn_fs_handle_t                configFileHandle;
   
   fileContents = (spiNetApp_configFileStruct_t*)fileBytes;
   
   configFileHandle = dn_fs_find(ACCELAPP_CONFIG_FILENAME);
   if (configFileHandle>=0) {
      // file found: read it
      
      // open file
      configFileHandle = dn_fs_open(
         ACCELAPP_CONFIG_FILENAME,
         DN_FS_OPT_CREATE,
         sizeof(spiNetApp_configFileStruct_t),
         DN_FS_MODE_OTH_RW
      );
      ASSERT(configFileHandle >= 0);
      
      // read file
      dnErr = dn_fs_read(
         configFileHandle,
         0, // offset
         (INT8U*)fileContents,
         sizeof(spiNetApp_configFileStruct_t)
      );
      ASSERT(dnErr>=0);
      
      // store configuration read from file into module variable
      spiNetApp_vars.period  = fileContents->period;
      
      // close file
      dn_fs_close(configFileHandle);
      
      // print
      printConfig();
      
   } else {
      // file not found: create it
      
      // prepare file content
      fileContents->period   = spiNetApp_vars.period;
      
      // create file
      configFileHandle = dn_fs_open(
         ACCELAPP_CONFIG_FILENAME,
         DN_FS_OPT_CREATE,
         sizeof(spiNetApp_configFileStruct_t),
         DN_FS_MODE_SHADOW
      );
      ASSERT(configFileHandle >= 0);
      
      // write file
      dnErr = dn_fs_write(
         configFileHandle,
         0, // offset
         (INT8U*)fileContents,
         sizeof(spiNetApp_configFileStruct_t)
      );
      ASSERT(dnErr >= 0);
      
      // close file
      dn_fs_close(configFileHandle);
      
      // print
      printConfig();
   }
}

void syncToConfigFile(void) {
   dn_error_t                    dnErr;
   INT8U                         fileBytes[sizeof(spiNetApp_configFileStruct_t)];
   spiNetApp_configFileStruct_t*  fileContents;
   dn_fs_handle_t                configFileHandle;
   
   fileContents = (spiNetApp_configFileStruct_t*)fileBytes;
   
   // prepare file content
   fileContents->period   = spiNetApp_vars.period;
   
   // open file
   configFileHandle = dn_fs_open(
      ACCELAPP_CONFIG_FILENAME,
      DN_FS_OPT_CREATE,
      sizeof(spiNetApp_configFileStruct_t),
      DN_FS_MODE_OTH_RW
   );
   ASSERT(configFileHandle >= 0);
   
   // write file
   dnErr = dn_fs_write(
      configFileHandle,
      0, // offset
      (INT8U*)fileContents,
      sizeof(spiNetApp_configFileStruct_t)
   );
   ASSERT(dnErr >= 0);
   
   // close file
   dn_fs_close(configFileHandle);
   
   // print
   printConfig();
}

void printConfig(void) {
   dnm_ucli_printf("Current config:\r\n");
   dnm_ucli_printf(" - period:  %d\r\n",spiNetApp_vars.period);
}

//=========================== SPI task ========================================

/**
\brief A demo task to show the use of the SPI.
*/
static void spiTask(void* unused) {
   INT8U                     i;
   dn_error_t                dnErr;
   INT8U                     osErr;
   dn_spi_open_args_t        spiOpenArgs;
   INT8U                     sendStatus;
   INT8U                     pkBuf[sizeof(loc_sendtoNW_t) + APP_DATA_BUF_SIZE];
   loc_sendtoNW_t*           pkToSend;
   dn_ioctl_spi_transfer_t   spiTransfer;
   
   //===== initialize the configuration file
   initConfigFile();
   
   //===== initialize packet variables
   pkToSend = (loc_sendtoNW_t*)pkBuf;
   
   //===== initialize SPI
   // open the SPI device
   spiOpenArgs.maxTransactionLenForCPHA_1 = 0;
   dnErr = dn_open(
      DN_SPI_DEV_ID,
      &spiOpenArgs,
      sizeof(spiOpenArgs)
   );
   ASSERT((dnErr == DN_ERR_NONE) || (dnErr == DN_ERR_STATE));
   
   // initialize spi communication parameters
   spiTransfer.txData             = spiNetApp_vars.spiTxBuffer;
   spiTransfer.rxData             = spiNetApp_vars.spiRxBuffer;
   spiTransfer.transactionLen     = sizeof(spiNetApp_vars.spiTxBuffer);
   spiTransfer.numSamples         = 1;
   spiTransfer.startDelay         = 0;
   spiTransfer.clockPolarity      = DN_SPI_CPOL_0;
   spiTransfer.clockPhase         = DN_SPI_CPHA_0;
   spiTransfer.bitOrder           = DN_SPI_MSB_FIRST;
   spiTransfer.slaveSelect        = DN_SPIM_SS_0n;
   spiTransfer.clockDivider       = DN_SPI_CLKDIV_16;
   
   //===== wait for the mote to have joined
   OSSemPend(spiNetApp_vars.joinedSem,0,&osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   while(1) { // this is a task, it executes forever
      
      //===== step 1. write over SPI
      
      // set bytes to send
      for (i=0;i<sizeof(spiNetApp_vars.spiTxBuffer);i++) {
         spiNetApp_vars.spiTxBuffer[i] = i;
      }
      
      // send bytes
      dnErr = dn_ioctl(
         DN_SPI_DEV_ID,
         DN_IOCTL_SPI_TRANSFER,
         &spiTransfer,
         sizeof(spiTransfer)
      );
      ASSERT(dnErr >= DN_ERR_NONE);
      
      //===== step 2. print over CLI
      
      dnm_ucli_printf("SPI sent:    ");
      for (i=0;i<sizeof(spiNetApp_vars.spiTxBuffer);i++) {
         dnm_ucli_printf(" %02x",spiNetApp_vars.spiTxBuffer[i]);
      }
      dnm_ucli_printf("\r\n");
      
      dnm_ucli_printf("SPI received:");
      for (i=0;i<sizeof(spiNetApp_vars.spiRxBuffer);i++) {
         dnm_ucli_printf(" %02x",spiNetApp_vars.spiRxBuffer[i]);
      }
      dnm_ucli_printf("\r\n");
      
      //===== step 3. send data to manager
      
      // fill in packet "header"
      // Note: sendto->header is filled in dnm_loc_sendtoCmd
      pkToSend->locSendTo.socketId          = loc_getSocketId();
      pkToSend->locSendTo.destAddr          = DN_MGR_IPV6_MULTICAST_ADDR; // IPv6 address
      pkToSend->locSendTo.destPort          = WKP_SPI_NET;
      pkToSend->locSendTo.serviceType       = DN_API_SERVICE_TYPE_BW;   
      pkToSend->locSendTo.priority          = DN_API_PRIORITY_MED;   
      pkToSend->locSendTo.packetId          = 0xFFFF;
      
      // fill in the packet payload
      memcpy(&pkToSend->locSendTo.payload[0],&spiNetApp_vars.spiRxBuffer[0],APP_DATA_BUF_SIZE);
      
      // send the packet
      dnErr = dnm_loc_sendtoCmd(pkToSend, APP_DATA_BUF_SIZE, &sendStatus);
      ASSERT (dnErr == DN_ERR_NONE);
      
      //===== step 4. pause until next iteration
      
      // this call blocks the task until the specified timeout expires (in ms)
      OSTimeDly(spiNetApp_vars.period);
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
