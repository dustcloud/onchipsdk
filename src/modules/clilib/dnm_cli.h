/*
Copyright (c) 2011, Dust Networks.  All rights reserved.
*/
 
#ifndef DNM_CLI_H
#define DNM_CLI_H

#include "dn_typedef.h"
#include "dn_errno.h"
#include "dn_cli.h"
#include "stdarg.h"
#include "dn_channel.h"

/**
\addtogroup module_dnm_cli CLI module
\{
*/

//=========================== defines =========================================

/// \brief Baudrate for the CLI serial port. 
#define DEFAULT_BAUDRATE 9600

//=========================== function pointers/structs =======================

/**
\brief User-defined CLI command handler.

This function should parse parameters and execute command.

The CLI module invokes this function via the command descriptors in
#dnm_cli_cont_t.

\return #DN_ERR_NONE when the function terminates successfully.
\return #DN_ERR_ERROR when the function can not terminate successfully.
\return #DN_ERR_INVALID when the user entered an invalid parameter.
*/
typedef dn_error_t (*dnm_cli_cmdHandler_t)(INT8U * cmd, INT32U len, INT8U access);

/**
\brief CLI command descriptor

This structure describes a single command terminated by your application.
*/
typedef struct {
   const dnm_cli_cmdHandler_t     handler;       ///< Function to handle the command.
   const char*                    command;       ///< Command string.
   const char*                    usage;         ///< Brief usage string.
   const dn_cli_access_t          accessLevel;   ///< Minimum access level required to run command.
} dnm_cli_cmdDef_t;

/**
\brief User-defined CLI help handler.

The library has a default handler that prints usage string for each command's
help.

You can define a custom function to handle help for all application commands.
*/
typedef void (*dnm_cli_helpHandler_t)(const dnm_cli_cmdDef_t * pCmdDef);

/**
\brief Application's CLI context.

This structure contains the application context for a CLI session. 
It should be allocated by your application and passed to
#dnm_cli_initContext() when the application starts.
*/
typedef struct {
   CH_DESC                        inpCh;          ///< Input channel.
   const dnm_cli_cmdDef_t*        cmdArr;         ///< Array of command descriptors; Note: the last element must have a null handler pointer.
   dnm_cli_helpHandler_t          pfHelpHandler;  ///< Custom help handler.
   INT8U                          numCmd;         ///< Number of elements in the <tt>cmdArr</tt>.
   INT32U                         traceFlags;     ///< Trace flags.
} dnm_cli_cont_t;

//=========================== prototypes ======================================

/** 
\name CLI module API
\{
*/

dn_error_t dnm_cli_openPort (INT8U port, INT32U baudRate);
dn_error_t dnm_cli_open (INT32U baudRate);
dn_error_t dnm_cli_initContext(dnm_cli_cont_t *context, CH_DESC inpCh, const dnm_cli_cmdDef_t  *cmdArr);
void       dnm_cli_setHelpHandler (dnm_cli_cont_t *pCont, dnm_cli_helpHandler_t pfHelpHandler);
dn_error_t dnm_cli_changeAccessLevel (dn_cli_access_t newAccessLevel);
void       dnm_cli_printf(const char* format, ...);
void       dnm_cli_printf_v (const char *fmt, va_list arg);
dn_error_t dnm_cli_input(dnm_cli_cont_t *pContext);
dn_error_t dnm_cli_procNotif (dnm_cli_cont_t *pCont, INT32S msgType, dn_cli_notifMsg_t *pCliNotif, INT32S rxLen);
void       dnm_cli_usage(const dnm_cli_cmdDef_t * pCmdDef);
void       dnm_cli_setLogFile (const char    * fileName);
void       dnm_cli_setOutputMode (BOOLEAN fBlocking);
INT8U      dnm_cli_getPort (void);
INT32U     dnm_cli_getBaudRate (void);
BOOLEAN    dnm_cli_isHighSpeedCLI( void );
void       dnm_cli_forceHighSpeedFlag(BOOLEAN isHighSpeed);

/**
// end of CLI module API
\} 
*/

/**
// end of module_dnm_cli
\}
*/
 
#endif

