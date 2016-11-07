/*
Copyright (c) 2016, Dust Networks.  All rights reserved.
*/

// C includes
#include <string.h>
#include <stdio.h>

// SDK includes
#include "cli_task.h"
#include "loc_task.h"
#include "dn_common.h"
#include "dn_system.h"
#include "dn_uart_raw.h"
#include "dn_exe_hdr.h"
#include "Ver.h"
#include "well_known_ports.h"

// App includes
#include "app_task_cfg.h"

//=========================== definitions =====================================
#define EXAMPLE_BAUDRATE        DN_UART_RAW_BAUD_115200 // Example of a common baudrate
#define EXAMPLE_PARITY          DN_UART_RAW_PARITY_NONE // Common parity setting
#define EXAMPLE_STOPBITS        DN_UART_RAW_STOPBITS_1  // Common number of stopbits
#define EXAMPLE_TIMEOUT         100                     // ms

#define RX_CHANNEL_SIZE        10                           // Enough to support per packet data rate plus processing
#define MAX_UART_PACKET_SIZE   DN_UART_RAW_MAX_PACKET_LEN   // Setting to driver maximum for this example
#define MAX_UART_TRX_CHNL_SIZE (sizeof(dn_chan_msg_hdr_t) + MAX_UART_PACKET_SIZE)

//=========================== prototypes ======================================

//===== CLI handlers
dn_error_t cli_txCmdHandler(const char* arg, INT32U len);

//===== tasks
static void  uartTxTask(void* unused);
static void  uartRxTask(void* unused);

//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_txCmdHandler,       "tx",          "tx <num_packets> <length> <delay>", DN_CLI_ACCESS_LOGIN},
   {NULL,                     NULL,         NULL,          DN_CLI_ACCESS_NONE},
};

//=========================== variables =======================================

typedef struct {
   // uartTxTask
   OS_STK          uartTxTaskStack[TASK_APP_UART_TX_STK_SIZE];
   INT8U           uartTxBuffer[MAX_UART_PACKET_SIZE];
   OS_EVENT*       uartTxSem;
   INT16U          uartTxLen;
   INT16U          uartTxDelay;
   INT16U          uartTxNumLeft;
   // uartRxTask
   OS_STK          uartRxTaskStack[TASK_APP_UART_RX_STK_SIZE];
   INT32U          uartRxChannelMemBuf[RX_CHANNEL_SIZE][1+MAX_UART_TRX_CHNL_SIZE/sizeof(INT32U)];
   OS_MEM*         uartRxChannelMem;
   CH_DESC         uartRxChannel;
   INT8U           uartRxBuffer[MAX_UART_TRX_CHNL_SIZE];
   // Connection Parameters
   dn_uart_raw_open_args_t  connectParams;
} uart_app_vars_t;

uart_app_vars_t    uart_app_v;

//=========================== initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
    INT8U  osErr;

    //==== initialize local variables
    memset(&uart_app_v, 0x00, sizeof(uart_app_v));

    // Filling the example TX buffer with just sequential byte count
    for(int x = 0; x < MAX_UART_PACKET_SIZE; x++){
        uart_app_v.uartTxBuffer[x] = x;
    }
    
    // create a semaphore to wait on for TX requests from user
    uart_app_v.uartTxSem = OSSemCreate(0);
    ASSERT (uart_app_v.uartTxSem!=NULL);

    //==== initialize helper tasks

    cli_task_init(
        "uart_raw",                           // appName
        cliCmdDefs                            // cliCmds
    );

    // An OCSDK applications must initialize the loc_task, but this
    // code example does not enable auto join nor utilize the network.
    loc_task_init(
        JOIN_NO,                              // fJoin
        NETID_NONE,                           // netId
        UDPPORT_NONE,                         // udpPort
        NULL,                                 // joinedSem
        BANDWIDTH_NONE,                       // bandwidth
        NULL                                  // serviceSem
    );

    //===== create tasks

    // uartTxTask task
    osErr  = OSTaskCreateExt(
          uartTxTask,
          (void *)0,
          (OS_STK*)(&uart_app_v.uartTxTaskStack[TASK_APP_UART_TX_STK_SIZE-1]),
          TASK_APP_UART_TX_PRIORITY,
          TASK_APP_UART_TX_PRIORITY,
          (OS_STK*)uart_app_v.uartTxTaskStack,
          TASK_APP_UART_TX_STK_SIZE,
          (void *)0,
          OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
    );
    ASSERT(osErr == OS_ERR_NONE);
    OSTaskNameSet(TASK_APP_UART_TX_PRIORITY, (INT8U*)TASK_APP_UART_TX_NAME, &osErr);
    ASSERT(osErr == OS_ERR_NONE);

    // uartRxTask task
    osErr  = OSTaskCreateExt(
          uartRxTask,
          (void *)0,
          (OS_STK*)(&uart_app_v.uartRxTaskStack[TASK_APP_UART_RX_STK_SIZE-1]),
          TASK_APP_UART_RX_PRIORITY,
          TASK_APP_UART_RX_PRIORITY,
          (OS_STK*)uart_app_v.uartRxTaskStack,
          TASK_APP_UART_RX_STK_SIZE,
          (void *)0,
          OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
    );
    ASSERT(osErr == OS_ERR_NONE);
    OSTaskNameSet(TASK_APP_UART_RX_PRIORITY, (INT8U*)TASK_APP_UART_RX_NAME, &osErr);
    ASSERT(osErr == OS_ERR_NONE);

    return 0;
}

//=========================== CLI handlers ====================================
// send 'num_packets' packets of size 'length' with interpacket delay 'delay' ms
dn_error_t cli_txCmdHandler(const char* arg, INT32U len) {
    INT8U osErr;
    int read;
    INT16U numLeft;
    INT16U length;
    INT16U delay;

    //--- param 0: len
    read = sscanf(arg, "%hu %hu %hu", &numLeft, &length, &delay);
    if (read != 3) {
       dnm_ucli_printf("Usage: tx <num_packets> <length> <delay>\r\n");
       return DN_ERR_NONE;
    }

    //---- update the tx number
    uart_app_v.uartTxNumLeft = numLeft;
    uart_app_v.uartTxLen = length;
    uart_app_v.uartTxDelay = delay;

    //---- post semaphore to start the transmit in the task
    osErr = OSSemPost(uart_app_v.uartTxSem);
    ASSERT(osErr == OS_ERR_NONE);

    return DN_ERR_NONE;
}

//=========================== tasks ===========================================
// This task transmits the number of packets specified in the 'tx' CLI command
static void uartTxTask(void* unused) {
    INT8U      osErr;
    dn_error_t dnErr;
    INT8U      reply;
    INT32U     replyLen;
    int        x;

    // set the default connection parameters - see dn_uart_raw.h for valid options
    uart_app_v.connectParams.rate          = EXAMPLE_BAUDRATE;
    uart_app_v.connectParams.stopBits      = EXAMPLE_STOPBITS;
    uart_app_v.connectParams.parity        = EXAMPLE_PARITY;
    uart_app_v.connectParams.ibTimeout     = EXAMPLE_TIMEOUT;

    // give the stack time to print out the version strings
    OSTimeDly(1* SECOND);

    // open the UART device
    uart_app_v.connectParams.rxChId      = uart_app_v.uartRxChannel;

    dnm_ucli_printf("Connecting to UART: Rate=%d, SB=%d, Parity=%d, IBT=%d\r\n",
                    uart_app_v.connectParams.rate,
                    uart_app_v.connectParams.stopBits,
                    uart_app_v.connectParams.parity,
                    uart_app_v.connectParams.ibTimeout);

    dnErr = dn_open(DN_UART_RAW_DEV_ID, &uart_app_v.connectParams, sizeof(dn_uart_raw_open_args_t) );
    ASSERT(dnErr==DN_ERR_NONE);

    while(1) { // this is a task, it executes forever

        // wait for the semaphore to be posted
        OSSemPend(uart_app_v.uartTxSem, 0, &osErr);
        ASSERT (osErr == OS_ERR_NONE);

        dnm_ucli_printf("Sending %u UART packets, %u bytes, delay %u ms\r\n",
                        uart_app_v.uartTxNumLeft,
                        uart_app_v.uartTxLen,
                        uart_app_v.uartTxDelay);

        while(uart_app_v.uartTxNumLeft>0) {
            // send packet
            dnErr = dn_sendSyncMsgByType(
                                          uart_app_v.uartTxBuffer,
                                          uart_app_v.uartTxLen,
                                          DN_MSG_TYPE_UART_TX_CTRL,
                                          (void*)&reply,
                                          sizeof(reply),
                                          &replyLen
            );
            ASSERT(dnErr==DN_ERR_NONE);
            ASSERT(replyLen==sizeof(INT8U));
            ASSERT(reply==DN_ERR_NONE);

            // print message sent details
            dnm_ucli_printf("uart TX (%3d):",uart_app_v.uartTxNumLeft);
            for (x=0; x<uart_app_v.uartTxLen; x++) {
                dnm_ucli_printf(" %02X", uart_app_v.uartTxBuffer[x]);
            }
            dnm_ucli_printf("\r\n");

            // decrement our packet counter
            uart_app_v.uartTxNumLeft--;

            // wait for the specified delay time
            if (uart_app_v.uartTxDelay) {
                OSTimeDly(uart_app_v.uartTxDelay);
            }
        }

        // print our completion message
        dnm_ucli_printf("done.\r\n");
    }
}

// This task prints bytes received on the UART
static void uartRxTask(void* unused) {
    dn_error_t           dnErr;
    INT8U                osErr;
    INT32U               rxLen;
    INT32U               msgType;
    INT16U                i;

    // create the memory block for the UART channel
    uart_app_v.uartRxChannelMem = OSMemCreate(
        uart_app_v.uartRxChannelMemBuf,
        RX_CHANNEL_SIZE,
        sizeof(uart_app_v.uartRxChannelMemBuf)/RX_CHANNEL_SIZE,
        &osErr
    );
    ASSERT(osErr==OS_ERR_NONE);

    // create an asynchronous notification channel
    dnErr = dn_createAsyncChannel(uart_app_v.uartRxChannelMem, &uart_app_v.uartRxChannel);
    ASSERT(dnErr==DN_ERR_NONE);

    // associate the channel descriptor with UART notifications
    dnErr = dn_registerChannel(uart_app_v.uartRxChannel, DN_MSG_TYPE_UART_NOTIF);
    ASSERT(dnErr==DN_ERR_NONE);

    while(1) { // this is a task, it executes forever
        // wait for UART messages
        dnErr = dn_readAsyncMsg(
                    uart_app_v.uartRxChannel,          // chDesc
                    uart_app_v.uartRxBuffer,           // msg
                    &rxLen,                            // rxLen
                    &msgType,                          // msgType
                    MAX_UART_TRX_CHNL_SIZE,            // maxLen
                    0                                  // timeout (0==never)
        );
        ASSERT(dnErr==DN_ERR_NONE);
        ASSERT(msgType==DN_MSG_TYPE_UART_NOTIF);

        // print message received
        dnm_ucli_printf("uart RX (%d bytes)",rxLen);
        for (i=0;i<rxLen;i++) {
            dnm_ucli_printf(" %02x",uart_app_v.uartRxBuffer[i]);
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
