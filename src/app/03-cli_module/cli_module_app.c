/*
Copyright (c) 2014, Dust Networks.  All rights reserved.
*/

#include "stdio.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dnm_ucli.h"
#include "dn_exe_hdr.h"
#include "Ver.h"

//=========================== defines =========================================

//=========================== prototypes ======================================

//===== CLI handlers
dn_error_t cli_sumCmdHandler(INT8U* arg, INT32U len);

//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_sumCmdHandler,      "sum",         "a b",         DN_CLI_ACCESS_LOGIN},
   {NULL,                    NULL,          NULL,          0},
};

//=========================== variables =======================================

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   dn_error_t      dnErr;
   INT8U           osErr;
   OS_MEM*         cliChannelMem;
   
   //==== initialize helper tasks
   
   cli_task_init(
      "cli",                                // appName
      &cliCmdDefs                           // cliCmds
   );
   loc_task_init(
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   return 0;
}

//=========================== CLI handlers ====================================

dn_error_t cli_sumCmdHandler(INT8U* arg, INT32U len) {
   char* token;
   int   a;
   int   b;
   int   l;
  
   //--- param 0: a
   l = sscanf(arg, "%d", &a);
   if (l < 1) {
      return DN_ERR_INVALID;
   }

   //--- param 1: b
   l = sscanf(arg+l, "%d", &b);
   if (l < 1) {
      return DN_ERR_INVALID;
   }

   //---- print sum
   dnm_ucli_printf("sum: %d",a+b);
   
   return DN_ERR_NONE;
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
