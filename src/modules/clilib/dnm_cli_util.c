/*
Copyright (c) 2010, Dust Networks.  All rights reserved.
*/

#include "dnm_cli_util.h"

#ifdef WIN32
   #include <time.h>
   #include <sys/timeb.h>
#endif

#include "ucos_ii.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "dn_common.h"

//=========================== prototypes ======================================
typedef struct {
   OS_EVENT*   blockingTraceMutex;   
} dnm_cli_util_t;

static dnm_cli_util_t dnm_cli_util_v = {NULL};
//=========================== public ==========================================

//===== Print formatting

/**
\brief Print a timestamp, followed by a formatted string.

\param[in] format Sprintf-style format string.
\param[in] ...    Optional format arguments.
 */
void dnm_cli_printfTimestamp(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    dnm_cli_printfTimestamp_v(format, args);
    va_end(args);
}

// internal function
void dnm_cli_printfTimestamp_v(const char *format, va_list arg)
{
#ifdef WIN32
   // Print Windows time
   struct _timeb t;
   struct tm     locTime;
   _ftime_s(&t);
   localtime_s(&locTime, &(t.time));
   dnm_cli_printf("(%02d:%02d:%02d.%03d) ", locTime.tm_hour, locTime.tm_min, locTime.tm_sec, t.millitm);
#endif
   dnm_cli_printf("%6d : ", OSTimeGet());   // TODO change to print sec.msec
   dnm_cli_printf_v(format, arg);
}

/**
\brief Print some binary data.

\param[in] data   Pointer to the start of the data to be printed.
\param[in] len    Number of bytes to print.
\param[in] format Sprintf-style format string.
\param[in] ...    Optional format arguments.
*/
void dnm_cli_dump(const INT8U *data, INT32S len, const char * format, ...)
{
   va_list marker;
   va_start(marker, format);
   dnm_cli_dump_v(data, len, format, marker);
   va_end(marker);
}

// internal function
void dnm_cli_dump_v(const INT8U *data, INT32S len, const char * format, va_list arg)
{
   int     i;
   dnm_cli_printfTimestamp_v(format, arg);
   for (i = 0; i < len; i++) {
      if (i % 20 == 0)
         dnm_cli_printf("\r\n   %03d : ", i);
      dnm_cli_printf("%02x ", *data++);
   }
   dnm_cli_printf("\r\n");
}

//===== Tracing

/**
\brief Print a formatted trace string if the corresponding trace flag is
   enabled.

\param[in] pCont     CLI context.
\param[in] traceFlag Trace module mask corresponding to this trace.
\param[in] format    Sprintf-style format string.
\param[in] ...       Optional format arguments.
*/
void dnm_cli_trace(dnm_cli_cont_t * pCont, INT32S traceFlag, const char* format, ...)
{
   if (dnm_cli_isTraceOn(pCont, traceFlag)) {
      va_list args;
      va_start(args, format);
      dnm_cli_printfTimestamp_v(format, args);
      va_end(args);
   }
}

/**
\brief Print binary data if the corresponding trace flag is enabled.

\param[in] pCont     CLI context.
\param[in] traceFlag Trace module mask corresponding to this trace
\param[in] data      Pointer to the start of the data to be printed.
\param[in] len       Number of bytes to print.
\param[in] format    Sprintf-style format string.
\param[in] ...       Optional format arguments.
*/
void    dnm_cli_traceDump(dnm_cli_cont_t *pCont, INT32S traceFlag, 
                         const INT8U* data, INT32S len, const char* format, ...)
{
   va_list marker;
   
   if(dnm_cli_isTraceOn(pCont, traceFlag)) {
      va_start(marker, format);
      dnm_cli_dump_v(data, len, format, marker);
      va_end(marker);
   }
}

/**
\brief Same as dnm_cli_traceDump with a Mutex to prevent overlapping prints
*/
void dnm_cli_traceDumpBlocking(dnm_cli_cont_t * pCont, INT32S traceFlag, 
                               const INT8U* data, INT32S len, const char* format, ...)
{
   va_list  marker;
   INT8U    err = OS_ERR_NONE;

   // create mutex if not created
   if(dnm_cli_util_v.blockingTraceMutex == NULL) {
      dnm_cli_util_v.blockingTraceMutex = OSSemCreate(1);
   }

   // wait for mutex
   OSSemPend(dnm_cli_util_v.blockingTraceMutex, 0, &err);
   ASSERT (err == OS_ERR_NONE);
   
   
   if(dnm_cli_isTraceOn(pCont, traceFlag)) {
      va_start(marker, format);
      dnm_cli_dump_v(data, len, format, marker);
      va_end(marker);
   }

   // release mutex
   err = OSSemPost(dnm_cli_util_v.blockingTraceMutex);
   ASSERT (err == OS_ERR_NONE);
}

/**
\brief Check whether some trace flag is enabled.

\param[in] pCont     CLI context.
\param[in] traceFlag Some trace flag.

\return TRUE  if the trace flag is enabled.
\return FALSE if the trace flag is not enabled.
*/
BOOLEAN dnm_cli_isTraceOn(dnm_cli_cont_t * pCont, INT32S traceFlag)
{
   INT32S traceBit = 1 << traceFlag;
   return (pCont->traceFlags & traceBit) ? TRUE : FALSE;
}

/**
\brief Set/clear some trace flag.

\param[in] pCont     CLI context.
\param[in] traceFlag Some trace flag.
\param[in] val       Set to <tt>1</tt> to enable  this flag;
                     set to <tt>0</tt> to disable this flag
*/
void dnm_cli_setTrace(dnm_cli_cont_t * pCont, INT32S traceFlag, BOOLEAN val)
{
   INT32S traceBit = 1 << traceFlag;
   if (val) {
      pCont->traceFlags |= traceBit;
   } else {
      pCont->traceFlags &= ~traceBit;
   }
}

/**
\brief Parse command string to set/clear some trace flag.

This function parses a string with the following format:

<tt>trace [module] [on|off]</tt>

Where <tt>[module]</tt> is a string identifying a trace module (as set by
#dnm_cli_trinfo_t::name).

After identifying the module by its name, it will set or clear that module's
traceflag if the command ends with <tt>on</tt> or <tt>off</tt>, respectively.

\post After this function returns successfully, the address written at the
   location pointed to by <tt>buf</tt> will have advanced.

\param[in]     pCont    CLI context.
\param[in]     trInfo   Trace descriptor array.
\param[in,out] buf      String received over CLI, starting at <tt>[module]</tt>.

\return TRUE if the function completes successfully.
\return FALSE otherwise.
*/
BOOLEAN  dnm_cli_setTraceByName(dnm_cli_cont_t * pCont, const dnm_cli_trinfo_t * trInfo, char ** buf)
{
   int    i, flag = -1;
   BOOLEAN isOn;
   BOOLEAN isEnable = FALSE;
   char * moduleName = dnm_cli_getNextToken(buf, ' ');
   char * on_off = dnm_cli_getNextToken(buf, ' ');

   if (moduleName == NULL) {
      dnm_cli_printTraceStateByName(pCont, trInfo);
      return FALSE;
   }
   if (on_off == NULL ||
       (strcmp(on_off, "on") && strcmp(on_off, "off")))
      return FALSE;

   isOn = strcmp(on_off, "on") == 0;
   
   if (strcmp(moduleName, "all") == 0) {
      if (!isOn || dnm_cli_isHighSpeedCLI()) {
         for (i=0; trInfo[i].name; i++) {
               dnm_cli_setTrace(pCont, trInfo[i].flag, isOn);
         }
      } else {
         dnm_cli_printf("Trace 'all' is not supported for this CLI baud rate\r\n");
      }
      return TRUE;
   }

   for (i=0; trInfo[i].name; i++) {
      if (strncmp(moduleName, trInfo[i].name, strlen(moduleName)) == 0) {
         flag = trInfo[i].flag;
         isEnable = dnm_cli_isHighSpeedCLI() || trInfo[i].isLowSpeedEnable;
         break;
      }
   }

   if (flag == -1)
      return FALSE;

   if (isEnable)
      dnm_cli_setTrace(pCont, flag, isOn);
   else 
      dnm_cli_printf("Trace '%s' is not supported for this CLI baud rate\r\n", moduleName);
   return TRUE;

}

/**
\brief Print the state of all trace flags.

This function iterates through all trace descriptors in the array passed in the
<tt>trInfo</tt> parameter, prints the name of each trace, and whether it is
enabled or not.

\param[in] pCont  CLI context.
\param[in] trInfo Trace descriptor array.
*/
void  dnm_cli_printTraceStateByName(dnm_cli_cont_t * pCont, const dnm_cli_trinfo_t * trInfo)
{
   int    i;
   const INT8U *pState;
   const INT8U *pOn, *pOff;
   INT32S trace;

   dnm_cli_printf("\n\rCurrent State\n\r");

   pOn   = (const INT8U *)"on";
   pOff  = (const INT8U *)"off";
   
   for (i=0; trInfo[i].name; i++) {
      trace  = 1 << trInfo[i].flag;
      pState = (pCont->traceFlags & trace) ? pOn : pOff;
      dnm_cli_printf("%s - %s\n\r", trInfo[i].name, pState);
   }
}

/**
\brief Print the list of all trace modules.

\param[in] trInfo Trace descriptor array.
*/
void dnm_cli_helpTrace(const dnm_cli_trinfo_t * trInfo)
{
   int i;
   for (i=0; trInfo[i].name; i++) {
      dnm_cli_printf("      %s: %s\r\n", trInfo[i].name, trInfo[i].description);
      OSTimeDly(100);
   }
}

/**
\brief Print the list of enabled trace modules.

This function iterates through all trace descriptors in the array passed in the
<tt>trInfo</tt> parameter and prints the name of the enabled traces.

\param[in] pCont  CLI context.
\param[in] trInfo Trace descriptor array.
*/
void dnm_cli_showTrace(dnm_cli_cont_t * pCont, const dnm_cli_trinfo_t * trInfo)
{
   int i;

   for (i=0; trInfo[i].name; i++) {
      if (dnm_cli_isTraceOn(pCont, trInfo[i].flag)) {
         dnm_cli_printf("    %s\r\n", trInfo[i].name);
         OSTimeDly(100);
      }
   }
}

//===== Miscellaneous Utility Functions

/**
\brief Compare the next token in some with a given keyword.

This function finds the next token in the <tt>buf</tt> and compares that
to the <tt>keyWord</tt>. Tokens in <tt>buf</tt> are separated by one or more
spaces, or one or more <tt>delimiter</tt> characters.

\param[in] buf       Buffer containing a string.
\param[in] keyWord   Buffer containing the keyword to match.
\param[in] delimiter A delimiter character.

\return TRUE if the next token in the <tt>buf</tt> is <tt>keyWord</tt>.
\return FALSE otherwise.
*/
BOOLEAN  dnm_cli_isNextToken(char * buf, const char * keyWord, INT8U delimiter)
{
   while(*buf && (isspace(*buf) || *buf==delimiter)) buf++;
   while(*buf && *keyWord && *buf++ == *keyWord++);
   return *keyWord==0 && (*buf==0 || isspace(*buf) || *buf==delimiter);
}

/**
\brief Get a pointer to the next token in a string.

Tokens are separated by one or more spaces, or one or more <tt>delimiter</tt>
characters.

This function iterates through the string at location <tt>p</tt>. It returns a
pointer to the start of the next token in the string, and updates the location
pointed at by <tt>p</tt> to the start of the token after the one found.

\post This function modifies the pointer value of <tt>p</tt> and the string
   itself.

\param[in,out] p         Buffer containing a string.
\param[in]     delimiter A delimiter character.

\return A pointer to the start of the next token in <tt>p</tt>.
\return <tt>NULL</tt> if no next token was found.
*/
char* dnm_cli_getNextToken(char **p, INT8U delimiter)
{
   char *res;

   if (*p == NULL)
      return NULL;

   // skip delimiters
   while (**p && (**p == delimiter || **p == ' ')) 
      (*p)++;

   if (**p == 0) 
      return NULL;

   res = *p;

   // move ptr past token end
   while (**p && **p != delimiter && **p != ' ')
      (*p)++;

   if (**p) 
      *((*p)++) = 0;

   // Skip delemiters
   while (**p && (**p == delimiter || **p == ' ')) 
      (*p)++;

   if (**p == 0)   // found end of string, make sure we return NULL next time
      *p = NULL;
   
   return res;
}


/**
\brief Compare two strings, but ignore case.

\param[in] s1 First string.
\param[in] s2 Second string.
 
\return TRUE  if the strings are equivalent.
\return FALSE if the strings are different.
*/
BOOLEAN dnm_cli_isEquIgnoreCase(const char * s1, const char * s2)
{
   if (s1 == NULL || s2 == NULL)
      return FALSE;

   for(;;) {
      if (tolower(*s1) != tolower(*s2)) return FALSE;
      if (*s1 == 0 || *s2 == 0)   return *s1 == *s2;
      s1++; s2++;
   }
}

/**
\brief Convert a INT64 number to a string.

\post At most <tt>size</tt> bytes are written to the buffer pointed to by
   <tt>buf</tt>.

\param[in]  num   The number of convert.
\param[out] buf   The buffer to write into.
\param[in]  size  The size of the buffer.

\return A pointer to the output buffer.
*/
char * dnm_cli_uint64ToString(INT64U num, char * buf, int size)
{
   INT32U  ar[3] = {0};
   int     i, l;
   char  * b = buf;
   BOOLEAN isFirst = TRUE;
   for (i=0; i<5; i++) {
      ar[i] = num % 1000000000;
      num /= 1000000000;
      if (num == 0) break;
   }
   while(i>=0 && size > 0) {
      if (isFirst)
         SNPRINTF(b, size, "%u", ar[i--]);
      else
         SNPRINTF(buf, size, "%09u", ar[i--]);
      buf[size-1] = 0;
      l = strlen(buf);
      size -= l; 
      buf += l;
      isFirst = FALSE;
   }
   return b;
}

//=========================== private =========================================
