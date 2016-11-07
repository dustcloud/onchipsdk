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
dn_error_t cli_levelHandler(const char *arg, INT32U len);
dn_error_t cli_cloginHandler(const char *arg, INT32U len);
dn_error_t cli_cviewerHandler(const char *arg, INT32U len);
dn_error_t cli_cuserHandler(const char *arg, INT32U len);
dn_error_t cli_cdustHandler(const char *arg, INT32U len);

//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_levelHandler,       "level",       "",       DN_CLI_ACCESS_LOGIN},
   {&cli_cloginHandler,      "clogin",      "",       DN_CLI_ACCESS_LOGIN},
   {&cli_cviewerHandler,     "cviewer",     "",       DN_CLI_ACCESS_VIEWER},
   {&cli_cuserHandler,       "cuser",       "",       DN_CLI_ACCESS_USER},
   {&cli_cdustHandler,       "cdust",       "",       DN_CLI_ACCESS_DUST},
   {NULL,                    NULL,          NULL,     DN_CLI_ACCESS_NONE},
};

//=========================== variables =======================================

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   
   //==== initialize helper tasks
   
   cli_task_init(
      "cli accesslevel",                    // appName
      cliCmdDefs                            // cliCmds
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

dn_error_t cli_levelHandler(const char *arg, INT32U len) {
   dn_error_t      dnErr;
   int             l; 
   dn_cli_access_t level;
  
   //--- param 0: level
   l = sscanf(arg, "%d", &level);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   
   //--- change access level
   switch(level) {
      case DN_CLI_ACCESS_LOGIN:
      case DN_CLI_ACCESS_VIEWER:
      case DN_CLI_ACCESS_USER:
      case DN_CLI_ACCESS_DUST:
         dnErr = dnm_ucli_changeAccessLevel(level);
         ASSERT(dnErr==DN_ERR_NONE);
         dnm_ucli_printf("switched to CLI access level %d\r\n",level);
         break;
      default:
         dnm_ucli_printf("only levels 1-4 supported\r\n");
         break;
   }
   
   return DN_ERR_NONE;
};

dn_error_t cli_cloginHandler(const char *arg, INT32U len) {
   dnm_ucli_printf("handling clogin command\r\n");
   return DN_ERR_NONE;
};

dn_error_t cli_cviewerHandler(const char *arg, INT32U len) {
   dnm_ucli_printf("handling cviewer command\r\n");
   return DN_ERR_NONE;
};

dn_error_t cli_cuserHandler(const char *arg, INT32U len) {
   dnm_ucli_printf("handling cuser command\r\n");
   return DN_ERR_NONE;
};

dn_error_t cli_cdustHandler(const char *arg, INT32U len) {
   dnm_ucli_printf("handling cdust command\r\n");
   return DN_ERR_NONE;
};

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
