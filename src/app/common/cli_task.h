/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#ifndef __OCFDK_CLI_TASK_H
#define __OCFDK_CLI_TASK_H

#include "dn_common.h"
#include "dnm_cli.h"

//============================ defines ========================================

//===== tasks

#define CLI_TASK_NAME             "cli"
#define CLI_TASK_PRIORITY         52
#define CLI_TASK_STK_SIZE         180

//=========================== prototypes ======================================

void cli_task_init(
   dnm_cli_cont_t*      cliContext,
   char*                appName,
   dnm_cli_cmdDef_t*    cliCommandDefinitions
);

#endif  
