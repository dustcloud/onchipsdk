/*
Copyright (c) 2010, Dust Networks.  All rights reserved.
*/

#ifndef dnm_cli_util_h
#define dnm_cli_util_h

#include "dn_typedef.h"
#include "dnm_cli.h"

/**
\addtogroup module_dnm_cli
\{
*/

//=========================== structs =========================================

/**
\brief Trace module descriptor.
*/
typedef struct {
   const char*      name;                 ///< Name of module.
   BOOLEAN          isLowSpeedEnable;     ///< Set to <tt>TRUE</tt>.
   INT32U           flag;                 ///< Bit that turns on the module.
   const char*      description;          ///< Description shown in help.
} dnm_cli_trinfo_t;

//=========================== prototypes ======================================

/** 
\name Print formatting
\{
*/
void     dnm_cli_printfTimestamp(const char* format, ...);
void     dnm_cli_printfTimestamp_v(const char *fmt, va_list arg);
void     dnm_cli_dump(const INT8U* data, INT32S len, const char* format, ...);
void     dnm_cli_dump_v(const INT8U *buf, INT32S len, const char * fmt, va_list arg);
/**
\}
*/ 

/** 
\name Tracing
\{
*/
void     dnm_cli_trace(dnm_cli_cont_t * pCont, INT32S traceFlag, const char* format, ...);
void     dnm_cli_traceDump(dnm_cli_cont_t * pCont, INT32S traceFlag, 
                         const INT8U* data, INT32S len, const char* format, ...);
void     dnm_cli_traceDumpBlocking(dnm_cli_cont_t * pCont, INT32S traceFlag, 
                                   const INT8U* data, INT32S len, const char* format, ...);
BOOLEAN  dnm_cli_isTraceOn(dnm_cli_cont_t * pCont, INT32S traceFlag);
void     dnm_cli_setTrace(dnm_cli_cont_t * pCont, INT32S traceFlag, BOOLEAN value);

BOOLEAN  dnm_cli_setTraceByName(dnm_cli_cont_t * pCont, const dnm_cli_trinfo_t * trInfo, char ** arg);
void     dnm_cli_printTraceStateByName(dnm_cli_cont_t * pCont, const dnm_cli_trinfo_t * trInfo);
void     dnm_cli_helpTrace(const dnm_cli_trinfo_t * trInfo); 
void     dnm_cli_showTrace(dnm_cli_cont_t * pCont, const dnm_cli_trinfo_t * trInfo); 
/**
\}
*/

/** 
\name Miscellaneous Utility Functions
\{
*/
BOOLEAN  dnm_cli_isNextToken(char * buf, const char * keyWord, INT8U delimiter);
char*    dnm_cli_getNextToken(char **p, INT8U delimiter);
BOOLEAN  dnm_cli_isEquIgnoreCase(const char * s1, const char * s2);
char*    dnm_cli_uint64ToString(INT64U asn, char * buf, int size);
/**
\}
*/

/**
// end of module_dnm_cli
\}
*/ 

#endif /* dnm_cli_util_h */
