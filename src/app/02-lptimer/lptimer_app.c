/*
Copyright (c) 2016, Dust Networks.  All rights reserved.

   * 16-bit lptimer is on pin DP2/GPIO21
   * Typical test flow is:
        1) set mode, one-shot, polarity, events
        2) open lptimer
        3) set compare register
        4) enable lptimer
        5) wait for events and/or revire counter/capture registers
        6) disable lptimer
        7) close lptimer
*/

// C includes
#include <string.h>
#include <stdio.h>

// OCSDK includes
#include "dn_common.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_lptimer.h"
#include "dn_exe_hdr.h"
#include "Ver.h"

// App includes
#include "app_task_cfg.h"

//=========================== definitions =====================================
#define EVENT_CHANNEL_SIZE      5
#define EVENT_CHANNEL_MSG_SIZE  (sizeof(dn_chan_msg_hdr_t)+sizeof(INT32U))

#define ENABLED     1
#define DISABLED    0

//=========================== prototypes ======================================

//===== CLI handlers
dn_error_t cli_openHandler(const char* arg, INT32U len);
dn_error_t cli_closeHandler(const char* arg, INT32U len);
dn_error_t cli_enableHandler(const char* arg, INT32U len);
dn_error_t cli_disableHandler(const char* arg, INT32U len);
dn_error_t cli_setCompareHandler(const char* arg, INT32U len);
dn_error_t cli_getCounterHandler(const char* arg, INT32U len);
dn_error_t cli_getCaptureHandler(const char* arg, INT32U len);

//===== tasks
//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_openHandler,           "open",         "open <mode> <events> <single> <pol> <cmp> <clrCmp> <capGate> <clrGate>", DN_CLI_ACCESS_LOGIN},
   {&cli_closeHandler,          "close",        NULL,                                                               DN_CLI_ACCESS_LOGIN},
   {&cli_enableHandler,         "enable",       NULL,                                                               DN_CLI_ACCESS_LOGIN},
   {&cli_disableHandler,        "disable",      NULL,                                                               DN_CLI_ACCESS_LOGIN},
   {&cli_setCompareHandler,     "set",          "set <cmp>",                                                        DN_CLI_ACCESS_LOGIN},
   {&cli_getCounterHandler,     "getcount",     NULL,                                                               DN_CLI_ACCESS_LOGIN},
   {&cli_getCaptureHandler,     "getcap",       NULL,                                                               DN_CLI_ACCESS_LOGIN},
   {NULL,                        NULL,          NULL,                                                               DN_CLI_ACCESS_NONE},
};

//=========================== variables =======================================
typedef struct {
    INT32U                      eventChIdMemBuf[EVENT_CHANNEL_SIZE][EVENT_CHANNEL_MSG_SIZE];
    OS_MEM*                     eventChIdMem;
    CH_DESC                     eventChId;
    OS_STK                      lptimerTaskStack[TASK_APP_LPTIMER_STK_SIZE];
    dn_lptimer_open_args_t      openArgs;
    INT32U                      overflow;
} lptimer_app_vars_t;

lptimer_app_vars_t    lptimer_app_v;

static void lptimerTask(void* unused);
//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
    INT8U       osErr;

    //==== initialize local variables
    memset(&lptimer_app_v, 0x00, sizeof(lptimer_app_v));

    //==== initialize helper tasks

    cli_task_init(
        "Low Power Timer",                    // appName
        cliCmdDefs                            // cliCmds
    );
    
    loc_task_init(
        JOIN_NO,                              // fJoin
        NETID_NONE,                           // netId
        UDPPORT_NONE,                         // udpPort - local module will not open a port if UDPPORT_NONE
        NULL,                                 // joinedSem
        BANDWIDTH_NONE,                       // bandwidth
        NULL                                  // serviceSem
    );

    // lptimer task
    osErr  = OSTaskCreateExt(
        lptimerTask,
        (void *)0,
        (OS_STK*)(&lptimer_app_v.lptimerTaskStack[TASK_APP_LPTIMER_STK_SIZE-1]),
        TASK_APP_LPTIMER_PRIORITY,
        TASK_APP_LPTIMER_PRIORITY,
        (OS_STK*)lptimer_app_v.lptimerTaskStack,
        TASK_APP_LPTIMER_STK_SIZE,
        (void *)0,
        OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
    );
    ASSERT(osErr == OS_ERR_NONE);
    OSTaskNameSet(TASK_APP_LPTIMER_PRIORITY, (INT8U*)TASK_APP_LPTIMER_NAME, &osErr);
    ASSERT(osErr == OS_ERR_NONE);

    return OS_ERR_NONE;
}

static void lptimerTask(void* unused) {
    dn_error_t          dnErr;
    INT8U               osErr;
    INT32U              msg;
    INT32U              rxLen;
    INT32U              msgType;
    
    // give time for stack banner to print
    OSTimeDly(1 * SECOND);

    // create the memory block for the UART channel
    lptimer_app_v.eventChIdMem = OSMemCreate( lptimer_app_v.eventChIdMemBuf,
                                    EVENT_CHANNEL_SIZE, EVENT_CHANNEL_MSG_SIZE, &osErr);
    ASSERT(osErr==OS_ERR_NONE);

    // create an asynchronous notification channel
    dnErr = dn_createAsyncChannel(lptimer_app_v.eventChIdMem, &lptimer_app_v.eventChId);
    ASSERT(dnErr==DN_ERR_NONE);

    // associate the channel descriptor with UART notifications
    dnErr = dn_registerChannel(lptimer_app_v.eventChId, DN_MSG_TYPE_LPTIMER_NOTIF);
    ASSERT(dnErr==DN_ERR_NONE);

    while(1) { // this is a task, it executes forever

        // wait for event message
        dnErr = dn_readAsyncMsg(lptimer_app_v.eventChId, &msg, &rxLen, &msgType, sizeof(msg), 0 );
        ASSERT(dnErr==DN_ERR_NONE);
        ASSERT(msgType==DN_MSG_TYPE_LPTIMER_NOTIF);

        if(msg & DN_LPTIMER_COMPARE_EVENT) {
            // print message received - the count will have increased slightly since the capture
            dnm_ucli_printf("COMP Event %d\r\n", msg);
        }
        
        if(msg & DN_LPTIMER_CAPTURE_EVENT) {
            // print message received
            dnm_ucli_printf("CAPT Event %d\r\n", msg);
        }
        
        if(msg & DN_LPTIMER_OVERFLOW_EVENT) {
            lptimer_app_v.overflow++;
        }
    }
}

//=========================== CLI handlers ====================================
//  Open the LPTimer device 
dn_error_t cli_openHandler(const char* arg, INT32U len) {
   dn_error_t              dnErr;
   int                     length;

   lptimer_app_v.overflow = 0;
    
   length = sscanf(arg, "%hhu %hhx %hhu %hhu %hu %hhu %hhu %hhu", &lptimer_app_v.openArgs.mode, &lptimer_app_v.openArgs.enabledEvents, 
                         &lptimer_app_v.openArgs.oneshot, &lptimer_app_v.openArgs.polarity, &lptimer_app_v.openArgs.compare,
                         &lptimer_app_v.openArgs.clearOnCompare, &lptimer_app_v.openArgs.captureOnGate, &lptimer_app_v.openArgs.clearOnGate);
   if (length != 8) {
      dnm_ucli_printf("Usage: open <mode> <events> <single> <pol> <cmp> <clrCmp> <capGate> <clrGate>\r\n");
      return DN_ERR_NONE;
   }
   
   dnm_ucli_printf("Opening lptimer");

   dnErr = dn_open(DN_LPTIMER_DEV_ID, &lptimer_app_v.openArgs, sizeof(dn_lptimer_open_args_t));
   if (dnErr != DN_ERR_NONE)
   {
      dnm_ucli_printf(" failed with RC=%d\r\n", dnErr);
      return DN_ERR_NONE;
   }
   else{
       dnm_ucli_printf("\r\n");
   }

   return DN_ERR_NONE;
}

// close the LPTimer device
// device must already be open
dn_error_t cli_closeHandler(const char* arg, INT32U len) {
    dn_error_t              dnErr;

    dnErr = dn_close(DN_LPTIMER_DEV_ID);
    if (dnErr != DN_ERR_NONE)
    {
        dnm_ucli_printf("Close failed with RC=%d\r\n", dnErr);
        return dnErr;
    }
    return DN_ERR_NONE;
}

// enable capture in the current mode
// device must already be open
dn_error_t cli_enableHandler(const char* arg, INT32U len) {
    dn_error_t               dnErr;

    dnErr =  dn_ioctl(DN_LPTIMER_DEV_ID, DN_IOCTL_LPTIMER_ENABLE, NULL, 0);
    if (dnErr != DN_ERR_NONE)
    {
        dnm_ucli_printf("Enable failed with RC=%d\r\n", dnErr);
        return dnErr;
    }

    return DN_ERR_NONE;
}

// disable capture in the current mode
dn_error_t cli_disableHandler(const char* arg, INT32U len) {
    dn_error_t               dnErr;

    dnErr =  dn_ioctl(DN_LPTIMER_DEV_ID, DN_IOCTL_LPTIMER_DISABLE, NULL, 0);
    if (dnErr != DN_ERR_NONE)
    {
        dnm_ucli_printf("Disable failed with RC=%d\r\n", dnErr);
        return dnErr;
    }

    return DN_ERR_NONE;
}
// set the value of the comparison register
// device must already be open
dn_error_t cli_setCompareHandler(const char* arg, INT32U len) {
    dn_error_t      dnErr;
    int             length;
    INT32U          compare;  
    
    //--- param 0: len
    length = sscanf(arg, "%u", &compare);
    if (length != 1) {
       dnm_ucli_printf("Usage: set <cmp>\r\n");   
       return DN_ERR_NONE;
    }
    
    // Registers are 32-bits wide but only 16 bits are useable here
    // for the compare. Setting to >65535 in the dn_ioctl call will 
    // result in an error in future versions of the stack
    dnErr =  dn_ioctl(DN_LPTIMER_DEV_ID, DN_IOCTL_LPTIMER_SET_COMPARE, &compare, sizeof(compare));
    if (dnErr != DN_ERR_NONE)
    {
        dnm_ucli_printf("Failed RC=%d\r\n", dnErr);
        return dnErr;
    }
    
    dnm_ucli_printf("Compare: %u\r\n", compare);
    return DN_ERR_NONE;
}

// get the current value of the counter register
dn_error_t cli_getCounterHandler(const char* arg, INT32U len) {
    dn_error_t      dnErr;
    INT32U          counter;

    dnErr =  dn_ioctl(DN_LPTIMER_DEV_ID, DN_IOCTL_LPTIMER_GET_COUNTER,
                &counter, sizeof(counter));
    if (dnErr != DN_ERR_NONE)
    {
        dnm_ucli_printf("Failed RC=%d\r\n", dnErr);
        return dnErr;
    }
    
    dnm_ucli_printf("Counter: %u\r\n", counter);
    return DN_ERR_NONE;
}

// get the current value of the capture register
// device must be open
dn_error_t cli_getCaptureHandler(const char* arg, INT32U len) {
    dn_error_t      dnErr;
    INT32U          capture;

    dnErr =  dn_ioctl(DN_LPTIMER_DEV_ID, DN_IOCTL_LPTIMER_GET_CAPTURE,
                &capture, sizeof(capture));
    if (dnErr != DN_ERR_NONE)
    {
        dnm_ucli_printf("Failed RC=%d\r\n", dnErr);
        return dnErr;
    }
    
    dnm_ucli_printf("Capture: %u\r\n", capture);
    return DN_ERR_NONE;
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
