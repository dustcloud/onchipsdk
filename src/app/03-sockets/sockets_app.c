/*
Copyright (c) 2016, Dust Networks.  All rights reserved.
*/

// C includes
#include <string.h>
#include <stdio.h>

//OCSDK includes
#include "dn_common.h"
#include "dnm_local.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_exe_hdr.h"
#include "Ver.h"
#include "well_known_ports.h"

//app includes
#include "app_task_cfg.h"

//=========================== defines =========================================
#define APP_SOCKETS             3
#define STACK_SOCKETS           3 // current stack opens 3 sockets if udpPort in loc_task_init is nonzero
#define BOUND                   1
#define UNBOUND                 0

#define SOCKET_APP_PORT         WKP_USER_1

//=========================== variables =======================================
typedef struct {
   INT8U           socketId;                    
   INT8U           protocol;                   
   INT8U           bindState;                  
   INT16U          port;           
} socket_info_t;


typedef struct {
   OS_STK               socketTaskStack[TASK_APP_SOCKETS_STK_SIZE];
   socket_info_t        sockets[APP_SOCKETS + STACK_SOCKETS];
} app_vars_t;

app_vars_t app_vars;

//=========================== prototypes ======================================
static void socketTask(void* unused);

//===== CLI
dn_error_t cli_reset(const char* arg, INT32U len);

//=========================== const ===========================================
const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_reset,                 "reset",        "reset",                   DN_CLI_ACCESS_LOGIN},
   {NULL,                        NULL,          NULL,                      DN_CLI_ACCESS_NONE},
};

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   INT8U           osErr;
   
   //===== initialize helper tasks  
   cli_task_init(
      "Sockets",                            // appName
      cliCmdDefs                           // cliCmds
   );
   
   loc_task_init(
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      SOCKET_APP_PORT,                      // udpPort - no port will be opened if NULL
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== initialize socketTask
   
   osErr = OSTaskCreateExt(
      socketTask,
      (void *) 0,
      (OS_STK*) (&app_vars.socketTaskStack[TASK_APP_SOCKETS_STK_SIZE - 1]),
      TASK_APP_SOCKETS_PRIORITY,
      TASK_APP_SOCKETS_PRIORITY,
      (OS_STK*) app_vars.socketTaskStack,
      TASK_APP_SOCKETS_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_SOCKETS_PRIORITY, (INT8U*)TASK_APP_SOCKETS_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

//=========================== CLI =============================================

// Reset command - returns an error if device fails to reset
dn_error_t cli_reset(const char* arg, INT32U len){
   INT8U      rc;
   
   dnm_ucli_printf("Resetting...\r\n\n");
   
   // send reset to stack
   dnm_loc_resetCmd(&rc);	
   
   // mote will reset in 5s if rc=DN_ERR_NONE
   
   return(DN_ERR_NONE);
}

static void socketTask(void* unused) {
   dn_error_t                   dnErr;
   INT8U                        rc;
   INT8U                        i;
   INT8U                        socketId;
   INT8U                        socketCount=0;
   dn_api_loc_rsp_socket_info_t socketInfo;
   
   //Give stack time to print
   OSTimeDly(1*SECOND);
   
   dnm_ucli_printf("\r\n");
   
   // print out stack sockets
   dnm_ucli_printf("Checking stack sockets...\r\n");
   do{
      OSTimeDly(100);  // 100 ms wait for CLI print;
      dnErr = dnm_loc_socketInfoCmd(socketCount, (INT8U*)&socketInfo, &rc);
      if(dnErr == DN_ERR_NONE && rc == DN_API_RC_OK){
         dnm_ucli_printf("socket ID: %d, bindState: %d, port: %x\r\n", socketInfo.socketId, 
                       socketInfo.bindState, htons(socketInfo.port));
         app_vars.sockets[socketCount].socketId = socketInfo.socketId;
         app_vars.sockets[socketCount].protocol = socketInfo.protocol;
         app_vars.sockets[socketCount].bindState = socketInfo.bindState;
         app_vars.sockets[socketCount].port = socketInfo.port;
         socketCount++;
         ASSERT(socketCount <= STACK_SOCKETS);  // check stack hasn't allocated more than STACK_SOCKETS sockets
      }
   } while (dnErr == DN_ERR_NONE && rc == DN_API_RC_OK); 
   
   dnm_ucli_printf("\r\n");
   dnm_ucli_printf("Found %d stack sockets\r\n", socketCount);
   dnm_ucli_printf("\r\n");
   
   // add 3 app sockets 
   dnm_ucli_printf("Opening & binding %d application sockets...\r\n", APP_SOCKETS);
   OSTimeDly(100);  // 100 ms wait for CLI print;
   for(i=0; i<APP_SOCKETS; i++){
      dnErr = dnm_loc_openSocketCmd(DN_API_PROTO_UDP, &socketId, &rc);
      ASSERT(dnErr == DN_ERR_NONE);
      ASSERT(rc == DN_API_RC_OK);   
      
      dnErr =  dnm_loc_bindSocketCmd(socketId, SOCKET_APP_PORT+1+i, &rc);
      ASSERT(dnErr == DN_ERR_NONE);
      ASSERT(rc == DN_API_RC_OK);
      
      dnErr = dnm_loc_socketInfoCmd(i+socketCount, (INT8U*)&socketInfo, &rc);
      if(dnErr == DN_ERR_NONE && rc == DN_API_RC_OK){
         app_vars.sockets[socketInfo.index].socketId = socketInfo.socketId;
         app_vars.sockets[socketInfo.index].protocol = socketInfo.protocol;
         app_vars.sockets[socketInfo.index].bindState = socketInfo.bindState;
         app_vars.sockets[socketInfo.index].port = socketInfo.port;
      }
   }

   dnm_ucli_printf("\r\n");
   
   dnm_ucli_printf("Listing all sockets:\r\n");
   for(i=0; i<(socketCount+APP_SOCKETS);i++){
      OSTimeDly(100);  // 100 ms wait for CLI print;
      dnm_ucli_printf("socket ID: %d, bindState: %d, port:%x\r\n", app_vars.sockets[i].socketId,
                      app_vars.sockets[i].bindState, htons(app_vars.sockets[i].port));
   }
   
   while (1) { // this is a task, it executes forever
      
      // wait a bit
      OSTimeDly(5000);
   }
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
