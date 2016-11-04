/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "string.h"
#include "stdio.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_gpio.h"
#include "dnm_local.h"
#include "dn_fs.h"
#include "dn_exe_hdr.h"
#include "well_known_ports.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================
#define GPIO_NET_PORT                 WKP_USER_1

// LEDs on DC9003A
#define LED_GREEN1                DN_GPIO_PIN_20_DEV_ID
#define LED_GREEN2                DN_GPIO_PIN_23_DEV_ID

// DP0 on DC9003A
#define SAMPLE_PIN                DN_GPIO_PIN_0_DEV_ID

#define LOWVAL_DEFAULT            0
#define HIGHVAL_DEFAULT           1
#define PERIOD_DEFAULT            10000

#define GPIO_NET_CONFIG_FILENAME  "2gpioNet.cfg"

// format of config file
typedef struct{
   INT8U           lowval;
   INT8U           highval;
   INT16U          period;
} gpio_net_configFileStruct_t;

//=========================== variables =======================================

typedef struct {
   OS_STK          gpioSampleTaskStack[TASK_APP_GPIOSAMPLE_STK_SIZE];
   OS_EVENT*       joinedSem;
   INT8U           lowval;        ///< value transmitted when pin is low
   INT8U           highval;       ///< value transmitted when pin is high
   INT16U          period;        ///< period (in ms) between transmissions
} gpio_net_app_vars_t;

gpio_net_app_vars_t gpio_net_app_v;

//=========================== prototypes ======================================

//===== CLI
dn_error_t  gpioNet_cli_config(char const* buf, INT32U len);
dn_error_t  gpioNet_cli_lowval(char const* buf, INT32U len);
dn_error_t  gpioNet_cli_highval(char const* buf, INT32U len);
dn_error_t  gpioNet_cli_period(char const* buf, INT32U len);
//===== configFile
       void initConfigFile(void);
       void syncToConfigFile(void);
       void printConfig(void);
//===== GPIO notification task
static void gpioSampleTask(void* arg);

//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&gpioNet_cli_config,     "config", "config",                     DN_CLI_ACCESS_LOGIN},
   {&gpioNet_cli_lowval,     "lowval", "lowval [newVal]",            DN_CLI_ACCESS_LOGIN},
   {&gpioNet_cli_highval,    "highval","highval [newVal]",           DN_CLI_ACCESS_LOGIN},
   {&gpioNet_cli_period,     "period", "period [newPeriod in ms]",   DN_CLI_ACCESS_LOGIN},
   {NULL,                    NULL,     NULL,                         DN_CLI_ACCESS_NONE},
};

//=========================== initialization ==================================

/**
 \brief This is the entry point in the application code.
 */
int p2_init(void) {
   INT8U                     osErr;
   
   //===== initialize module variables
   memset(&gpio_net_app_v,0,sizeof(gpio_net_app_vars_t));
   gpio_net_app_v.lowval     = LOWVAL_DEFAULT;
   gpio_net_app_v.highval    = HIGHVAL_DEFAULT;
   gpio_net_app_v.period     = PERIOD_DEFAULT;
   
   //===== initialize helper tasks
   
   // create a semaphore to indicate mote joined
   gpio_net_app_v.joinedSem = OSSemCreate(0);
   ASSERT (gpio_net_app_v.joinedSem!=NULL);
   
   // CLI task
   cli_task_init(
      "gpio_net",                           // appName
      cliCmdDefs                            // cliCmds
   );
   
   // local interface task
   loc_task_init(
      JOIN_YES,                             // fJoin
      NETID_NONE,                           // netId
      GPIO_NET_PORT,                        // udpPort
      gpio_net_app_v.joinedSem,             // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== create the GPIO sample task
   osErr = OSTaskCreateExt(
      gpioSampleTask,
      (void*) 0,
      (OS_STK*)(&gpio_net_app_v.gpioSampleTaskStack[TASK_APP_GPIOSAMPLE_STK_SIZE- 1]),
      TASK_APP_GPIOSAMPLE_PRIORITY,
      TASK_APP_GPIOSAMPLE_PRIORITY,
      (OS_STK*)gpio_net_app_v.gpioSampleTaskStack,
      TASK_APP_GPIOSAMPLE_STK_SIZE,
      (void*) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_GPIOSAMPLE_PRIORITY, (INT8U*)TASK_APP_GPIOSAMPLE_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== CLI =============================================

// "config" command
dn_error_t gpioNet_cli_config(char const* buf, INT32U len) {
   printConfig();
   return DN_ERR_NONE;
}

// "lowval" command
dn_error_t gpioNet_cli_lowval(char const* buf, INT32U len) {
   int        sarg, l;
   
   //--- param 0: lowval
   l = sscanf (buf, "%d", &sarg);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   else {
      gpio_net_app_v.lowval = (INT8U)sarg;
   }
   
   // sync configuration to file
   syncToConfigFile();
   
   return DN_ERR_NONE;
}

// "highval" command
dn_error_t gpioNet_cli_highval(char const* buf, INT32U len) {
   int        sarg, l;
   
   //--- param 0: highval
   l = sscanf (buf, "%d", &sarg);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   else {
      gpio_net_app_v.highval = (INT8U)sarg;
   }
   
   // sync configuration to file
   syncToConfigFile();
   
   return DN_ERR_NONE;
}

// "period" command
dn_error_t gpioNet_cli_period(char const* buf, INT32U len) {
   int        sarg, l;
   
   //--- param 0: period
   l = sscanf (buf, "%d", &sarg);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   else {
      gpio_net_app_v.period = (INT16U)sarg;
   }
   
   // sync configuration to file
   syncToConfigFile();
   
   return DN_ERR_NONE;
}

//=========================== configFile ======================================

void initConfigFile(void) {
   dn_error_t                    dnErr;
   INT8U                         fileBytes[sizeof(gpio_net_configFileStruct_t)];
   gpio_net_configFileStruct_t*  fileContents;
   dn_fs_handle_t                configFileHandle;
   
   fileContents = (gpio_net_configFileStruct_t*)fileBytes;
   
   configFileHandle = dn_fs_find(GPIO_NET_CONFIG_FILENAME);
   if (configFileHandle>=0) {
      // file found: read it
      
      // open file
      configFileHandle = dn_fs_open(
         GPIO_NET_CONFIG_FILENAME,
         DN_FS_OPT_CREATE,
         sizeof(gpio_net_configFileStruct_t),
         DN_FS_MODE_OTH_RW
      );
      ASSERT(configFileHandle >= 0);
      
      // read file
      dnErr = dn_fs_read(
         configFileHandle,
         0, // offset
         (INT8U*)fileContents,
         sizeof(gpio_net_configFileStruct_t)
      );
      ASSERT(dnErr>=0);
      
      // store configuration read from file into module variable
      gpio_net_app_v.lowval  = fileContents->lowval;
      gpio_net_app_v.highval = fileContents->highval;
      gpio_net_app_v.period  = fileContents->period;
      
      // close file
      dn_fs_close(configFileHandle);
   } else {
      // file not found: create it
      
      // prepare file content
      fileContents->lowval   = gpio_net_app_v.lowval;
      fileContents->highval  = gpio_net_app_v.highval;
      fileContents->period   = gpio_net_app_v.period;
      
      // create file
      configFileHandle = dn_fs_open(
         GPIO_NET_CONFIG_FILENAME,
         DN_FS_OPT_CREATE,
         sizeof(gpio_net_configFileStruct_t),
         DN_FS_MODE_SHADOW
      );
      ASSERT(configFileHandle >= 0);
      
      // write file
      dnErr = dn_fs_write(
         configFileHandle,
         0, // offset
         (INT8U*)fileContents,
         sizeof(gpio_net_configFileStruct_t)
      );
      ASSERT(dnErr >= 0);
      
      // close file
      dn_fs_close(configFileHandle);
   }
   
   // print
   printConfig();
}

void syncToConfigFile(void) {
   dn_error_t                    dnErr;
   INT8U                         fileBytes[sizeof(gpio_net_configFileStruct_t)];
   gpio_net_configFileStruct_t*  fileContents;
   dn_fs_handle_t                configFileHandle;
   
   fileContents = (gpio_net_configFileStruct_t*)fileBytes;
   
   // prepare file content
   fileContents->lowval   = gpio_net_app_v.lowval;
   fileContents->highval  = gpio_net_app_v.highval;
   fileContents->period   = gpio_net_app_v.period;
   
   // open file
   configFileHandle = dn_fs_open(
      GPIO_NET_CONFIG_FILENAME,
      DN_FS_OPT_CREATE,
      sizeof(gpio_net_configFileStruct_t),
      DN_FS_MODE_OTH_RW
   );
   ASSERT(configFileHandle >= 0);
   
   // write file
   dnErr = dn_fs_write(
      configFileHandle,
      0, // offset
      (INT8U*)fileContents,
      sizeof(gpio_net_configFileStruct_t)
   );
   ASSERT(dnErr >= 0);
   
   // close file
   dn_fs_close(configFileHandle);
   
   // print
   printConfig();
}

void printConfig(void) {
   dnm_ucli_printf("Current config:\r\n");
   dnm_ucli_printf(" - lowval:  %d\r\n",gpio_net_app_v.lowval);
   dnm_ucli_printf(" - highval: %d\r\n",gpio_net_app_v.highval);
   dnm_ucli_printf(" - period:  %d\r\n",gpio_net_app_v.period);
}

//=========================== GPIO notif task =================================

static void gpioSampleTask(void* arg) {
   dn_error_t                     dnErr;
   INT8U                          osErr;
   dn_gpio_ioctl_cfg_in_t         gpioInCfg;
   dn_gpio_ioctl_cfg_out_t        gpioOutCfg;
   char                           samplePinLevel;
   INT8U                          pkBuf[sizeof(loc_sendtoNW_t) + 1];
   loc_sendtoNW_t*                pkToSend;
   INT8U                          rc;
   char                           ledState;
   
   //===== initialize the configuration file
   initConfigFile();
   
   //===== open and configure the LED pins
   
   dnErr = dn_open(
      LED_GREEN1,                 // device
      NULL,                       // args
      0                           // argLen 
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   gpioOutCfg.initialLevel = 0x00;
   dnErr = dn_ioctl(
      LED_GREEN1,                 // device
      DN_IOCTL_GPIO_CFG_OUTPUT,   // request
      &gpioOutCfg,                // args
      sizeof(gpioOutCfg)          // argLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   dnErr = dn_open(
      LED_GREEN2,                 // device
      NULL,                       // args
      0                           // argLen 
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   gpioOutCfg.initialLevel = 0x00;
   dnErr = dn_ioctl(
      LED_GREEN2,                 // device
      DN_IOCTL_GPIO_CFG_OUTPUT,   // request
      &gpioOutCfg,                // args
      sizeof(gpioOutCfg)          // argLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   //===== open and configure the SAMPLE_PIN
   dn_open(
      SAMPLE_PIN,
      NULL,
      0
   );
   gpioInCfg.pullMode = DN_GPIO_PULL_NONE;
   dn_ioctl(
      SAMPLE_PIN,
      DN_IOCTL_GPIO_CFG_INPUT,
      &gpioInCfg,
      sizeof(gpioInCfg)
   );
   
   //===== initialize packet variables
   pkToSend = (loc_sendtoNW_t*)pkBuf;
   
   //===== switch LEDs on
   
   ledState = 0x01;
   dnErr = dn_write(
      LED_GREEN1,                 // device
      &ledState,                  // buf
      sizeof(ledState)            // len
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   ledState = 0x01;
   dnErr = dn_write(
      LED_GREEN2,                 // device
      &ledState,                  // buf
      sizeof(ledState)            // len
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   //===== wait for the mote to have joined
   OSSemPend(gpio_net_app_v.joinedSem,0,&osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   //===== switch LEDs off
   
   ledState = 0x00;
   dnErr = dn_write(
      LED_GREEN1,                 // device
      &ledState,                  // buf
      sizeof(ledState)            // len
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   ledState = 0x00;
   dnErr = dn_write(
      LED_GREEN2,                 // device
      &ledState,                  // buf
      sizeof(ledState)            // len
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   while (1) { // this is a task, it executes forever
      
      //===== step 1. sample the state of the SAMPLE_PIN
      
      dn_read(
         SAMPLE_PIN,
         &samplePinLevel,
         sizeof(samplePinLevel)
      );
      
      //===== step 2. send packet
      
      // fill in packet "header"
      pkToSend->locSendTo.socketId          = loc_getSocketId();
      pkToSend->locSendTo.destAddr          = DN_MGR_IPV6_MULTICAST_ADDR;
      pkToSend->locSendTo.destPort          = GPIO_NET_PORT;
      pkToSend->locSendTo.serviceType       = DN_API_SERVICE_TYPE_BW;   
      pkToSend->locSendTo.priority          = DN_API_PRIORITY_MED;   
      pkToSend->locSendTo.packetId          = 0xFFFF;
      
      // fill in the packet payload
      if (samplePinLevel==0) {
         pkToSend->locSendTo.payload[0]     = gpio_net_app_v.lowval;
      } else {
         pkToSend->locSendTo.payload[0]     = gpio_net_app_v.highval;
      }
      
      // send the packet
      dnErr = dnm_loc_sendtoCmd(pkToSend, 1, &rc);
      ASSERT (dnErr == DN_ERR_NONE);
      
      // print level
      dnm_ucli_printf("samplePinLevel=%d, sent 0x%02x\r\n",
         samplePinLevel,
         pkToSend->locSendTo.payload[0]
      );
      
      //===== step 3. pause until next iteration
      
      // this call blocks the task until the specified timeout expires (in ms)
      OSTimeDly(gpio_net_app_v.period);
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
