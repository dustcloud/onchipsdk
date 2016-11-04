/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#ifndef __OCFDK_CLI_TASK_H
#define __OCFDK_CLI_TASK_H

#include "dn_common.h"
#include "dnm_ucli.h"

//============================ defines ========================================

//===== tasks

#define CLI_TASK_NAME             "cli"
#define CLI_TASK_PRIORITY         52
#define CLI_TASK_STK_SIZE         180

//=========================== prototypes ======================================

/**
\brief User-defined CLI command handler.

Typically, this function parses parameters and handles the 
command accordingly. 
*/
typedef dn_error_t (*dnm_ucli_cmdHandler_t)(char const* cmd, INT32U len);

/**
\brief CLI command descriptor

This structure describes a single command terminated by your application.
*/
typedef struct {
   const dnm_ucli_cmdHandler_t    handler;       ///< Function to handle the command.
   const char*                    command;       ///< Command string.
   const char*                    usage;         ///< Brief usage string.
   const dn_cli_access_t          accessLevel;   ///< Minimum access level required to run command.
} dnm_ucli_cmdDef_t;


void cli_task_init(
   char*                appName,
   dnm_ucli_cmdDef_t const*   cliCommandDefinitions
);

dn_error_t cli_procNotif(INT8U type, INT8U cmdId, char const *pCmdParams, INT8U paramsLen);

#endif  
