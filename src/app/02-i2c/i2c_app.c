/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "string.h"
#include "stdio.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_i2c.h"
#include "dn_exe_hdr.h"
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================

#define I2C_SLAVE_ADDR       0x05
#define I2C_PAYLOAD_LENGTH   3

//=========================== variables =======================================

typedef struct {
   INT8U                     ledToggleFlag;
   dn_ioctl_i2c_transfer_t   i2cTransfer;
   OS_STK                    i2cTaskStack[TASK_APP_I2C_STK_SIZE];
   INT8U                     i2cBuffer[I2C_PAYLOAD_LENGTH];
} i2c_app_vars_t;

i2c_app_vars_t     i2c_app_v;

//=========================== prototypes ======================================

static void i2cTask(void* unused);

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   INT8U                   osErr;

   cli_task_init(
      "i2c",                                // appName
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
   
   // create the I2C task
   osErr  = OSTaskCreateExt(
      i2cTask,
      (void *)0,
      (OS_STK*)(&i2c_app_v.i2cTaskStack[TASK_APP_I2C_STK_SIZE-1]),
      TASK_APP_I2C_PRIORITY,
      TASK_APP_I2C_PRIORITY,
      (OS_STK*)i2c_app_v.i2cTaskStack,
      TASK_APP_I2C_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_I2C_PRIORITY, (INT8U*)TASK_APP_I2C_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

//=========================== I2C task ========================================

/**
\brief A demo task to show the use of the I2C.
*/
static void i2cTask(void* unused) {
   dn_error_t                   dnErr;
   dn_i2c_open_args_t           i2cOpenArgs;
   INT8U                        i;
   
   //===== open the I2C device
   
   // wait a bit
   OSTimeDly(1000);
   
   // open the I2C device
   i2cOpenArgs.frequency = DN_I2C_FREQ_184_KHZ;
   dnErr = dn_open(
      DN_I2C_DEV_ID,
      &i2cOpenArgs,
      sizeof(i2cOpenArgs)
   );
   ASSERT(dnErr==DN_ERR_NONE); 
   
   while(1) {
      // infinite loop
      
      //===== step 1. write to I2C slave
      
      // wait a bit
      OSTimeDly(1000);
     
      // prepare buffer
      for (i=0;i<I2C_PAYLOAD_LENGTH;i++) {
         i2c_app_v.i2cBuffer[i]             = 0x80+i;
      }
      
      // initialize I2C communication parameters
      i2c_app_v.i2cTransfer.slaveAddress    = I2C_SLAVE_ADDR;
      i2c_app_v.i2cTransfer.writeBuf        = i2c_app_v.i2cBuffer;
      i2c_app_v.i2cTransfer.readBuf         = NULL;
      i2c_app_v.i2cTransfer.writeLen        = sizeof(i2c_app_v.i2cBuffer);
      i2c_app_v.i2cTransfer.readLen         = 0;
      i2c_app_v.i2cTransfer.timeout         = 0xff;
      
      // initiate transaction
      dnErr = dn_ioctl(
         DN_I2C_DEV_ID,
         DN_IOCTL_I2C_TRANSFER,
         &i2c_app_v.i2cTransfer,
         sizeof(i2c_app_v.i2cTransfer)
      );
      
      // print
      if (dnErr==DN_ERR_NONE) {
         dnm_ucli_printf("Sent to I2C slave %02x: 0x",I2C_SLAVE_ADDR);
         for (i=0;i<I2C_PAYLOAD_LENGTH;i++) {
            dnm_ucli_printf("%02x",i2c_app_v.i2cBuffer[i]);
         }
         dnm_ucli_printf("\r\n");         
      } else {
         dnm_ucli_printf("Unable to write over I2C, err=%d\r\n",dnErr);
      }
      
      //===== step 2. read from I2C slave
      
      // wait a bit
      OSTimeDly(1000);
      
      // prepare buffer
      memset(i2c_app_v.i2cBuffer,0,sizeof(i2c_app_v.i2cBuffer));
      
      // initialize I2C communication parameters
      i2c_app_v.i2cTransfer.slaveAddress    = I2C_SLAVE_ADDR;
      i2c_app_v.i2cTransfer.writeBuf        = NULL;
      i2c_app_v.i2cTransfer.readBuf         = i2c_app_v.i2cBuffer;
      i2c_app_v.i2cTransfer.writeLen        = 0;
      i2c_app_v.i2cTransfer.readLen         = sizeof(i2c_app_v.i2cBuffer);
      i2c_app_v.i2cTransfer.timeout         = 0xff;
      
      // initiate transaction
      dnErr = dn_ioctl(
         DN_I2C_DEV_ID,
         DN_IOCTL_I2C_TRANSFER,
         &i2c_app_v.i2cTransfer,
         sizeof(i2c_app_v.i2cTransfer)
      );
      
      // print
      if (dnErr==DN_ERR_NONE) {
         dnm_ucli_printf("Received from I2C slave %02x: 0x",I2C_SLAVE_ADDR);
         for (i=0;i<I2C_PAYLOAD_LENGTH;i++) {
            dnm_ucli_printf("%02x",i2c_app_v.i2cBuffer[i]);
         }
         dnm_ucli_printf("\r\n");
      } else {
         dnm_ucli_printf("Unable to read over I2C, err=%d\r\n",dnErr);
      }
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
