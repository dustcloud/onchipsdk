/*
Copyright (c) 2011, Dust Networks.  All rights reserved.
*/
 
#ifndef DNM_UCLI_H
#define DNM_UCLI_H

#include "dn_typedef.h"
#include "dn_errno.h"
#include "dn_cli.h"
#include "stdarg.h"
#include "dn_channel.h"

/**
\addtogroup module_dnm_ucli CLI module
\{
*/

//=========================== defines =========================================

/// \brief Baudrate for the CLI serial port. 
#define DEFAULT_BAUDRATE 9600

//=========================== function pointers/structs =======================

/**
\brief User-defined function to process CLI notification. 
*/
typedef dn_error_t (*procNotifCb_t) (INT8U type, INT8U cmdId, INT8U *pCmdParams, INT8U paramsLen);


//=========================== prototypes ======================================

/** 
\name CLI module API
\{
*/

void       dnm_ucli_init (procNotifCb_t callback);
dn_error_t dnm_ucli_openPort (INT8U port, INT32U baudRate);
dn_error_t dnm_ucli_open (INT32U baudRate);
void       dnm_ucli_printf(const char* format, ...);
void       dnm_ucli_printf_v (const char *fmt, va_list arg);
dn_error_t dnm_ucli_input(CH_DESC chDesc);
dn_error_t dnm_ucli_changeAccessLevel(dn_cli_access_t newAccessLevel);
INT8U      dnm_ucli_getPort (void);
INT32U     dnm_ucli_getBaudRate (void);
void       dnm_ucli_printfTimestamp(const char* format, ...);
void       dnm_ucli_dump(const INT8U *data, INT32S len, const char * format, ...);
void       dnm_ucli_trace(BOOLEAN isTraceEnabled, const char* format, ...);
void       dnm_ucli_traceDump(BOOLEAN isTraceEnabled, const INT8U* data, INT32S len, const char* format, ...);
void       dnm_ucli_traceDumpBlocking(BOOLEAN isTraceEnabled, const INT8U* data, INT32S len, const char* format, ...);
void       dnm_ucli_printBuf(INT8U* buf, INT8U len);
INT8S      dnm_ucli_hex2byte(const char * str, INT8U * buf, int bufSize);

/**
// end of CLI module API
\} 
*/

/**
// end of module_dnm_ucli
\}
*/
 
#endif

