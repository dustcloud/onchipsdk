/*
Copyright (c) 2011, Dust Networks.  All rights reserved.
*/

#include <stdio.h>
#include <string.h>
#include "dn_common.h"
#include "dnm_ucli.h"
#include "dn_channel.h"
#include "dn_system.h"
#include "dn_gpio.h"
#include "dn_flash_info.h"
#ifdef WIN32
   #include <time.h>
   #include <sys/timeb.h>
#endif

struct {
   INT8U   port;
   INT32U  baudRate;
   OS_EVENT*   blockingTraceMutex;   
   procNotifCb_t notifCb;
#ifdef WIN32
   FILE * pCliLogFile;
#endif

} dnm_ucli_v = {
               DN_CLI_PORT_C0,   // port
               DEFAULT_BAUDRATE, // baud rate
               NULL,             // mutex
               NULL,             // callback
#ifdef WIN32
               NULL              // pCliLogFile
#endif
};

#define ERR_INDICATOR "..."

//=========================== prototypes ======================================

//=========================== public ==========================================

/** 
\brief Initialize the module.
 * 
\param[in] callback Function to process CLI notification
 */
void dnm_ucli_init (procNotifCb_t callback)
{
   dnm_ucli_v.notifCb = callback;
}

/**
\brief Wrapper to open the CLI serial port.

\pre You need to call this function exactly once before you can use the CLI.
\pre This function is invoked by #dnm_ucli_open() if you choose 
    to use it.

\param[in] port The CLI port to open. Acceptable values are listed in
   #dn_cli_port_t.
\param[in] baudRate The CLI's baudrate. Use #DEFAULT_BAUDRATE.

\return The same error are the ones returns by the #dn_open() function for
   the \ref device_cli.
*/
dn_error_t dnm_ucli_openPort (INT8U port, INT32U baudRate)
{
   dn_cli_open_args_t conf;

   conf.port = port;
   conf.baudRate = baudRate;
   dnm_ucli_v.port = port;
   dnm_ucli_v.baudRate = baudRate;
   return dn_open(DN_CLI_DEV_ID, &conf, sizeof(conf));
}

/**
\brief Open the CLI serial port using information stored in flash memory.

\pre This function calls #dnm_ucli_openPort

This function reads the CLI port info from the \ref device_flashinfo.
Baudrate is only read from \ref device_flashinfo if the <tt>baudRate</tt> parameter is set to
<tt>0</tt>)

\param[in] baudRate The CLI's baudrate. Use #DEFAULT_BAUDRATE or set to
   <tt>0</tt> to use the one from \ref device_flashinfo

\return #DN_ERR_ERROR if the CLI port information could not be read from the 
   \ref device_flashinfo.
\return #DN_ERR_INVALID if the information retrieved from the
   \ref device_flashinfo indicates CLI is not enabled.
\return The same error are the ones returns by the 
       #dnm_ucli_openPort().
*/
dn_error_t dnm_ucli_open (INT32U baudRate) {
   dn_bsp_param_read_t param;
   int                 len;
   INT8U               port;

   // Read CLI mode
   param.input.tag = DN_BSP_PARAM_PORT_OPTIONS;
   len = dn_read(DN_FLASHINFO_DEV_ID, (char *)&param.output.portOpt, sizeof(param.output.portOpt));
   if (len < sizeof(param.output.portOpt)) 
     return DN_ERR_ERROR;
   if ((param.output.portOpt & DN_PORT_OPT_CLI_ENABLED) != DN_PORT_OPT_CLI_ENABLED)
      return DN_ERR_INVALID;

   if ((param.output.portOpt & DN_PORT_OPT_CLI_UARTC1) == DN_PORT_OPT_CLI_UARTC1)
      port = DN_CLI_PORT_C1;
   else
      port = DN_CLI_PORT_C0;

   if (baudRate == 0) {
      // Read CLI baud rate
      param.input.tag = DN_BSP_PARAM_CLI_PORT_RATE;
      len = dn_read(DN_FLASHINFO_DEV_ID, (char *)&param.output.cliPortRate, sizeof(param.output.cliPortRate));
      if (len >= sizeof(param.output.cliPortRate))  {
         switch(param.output.cliPortRate) {
         case DN_BSP_PARAM_BAUD_9600:   baudRate = 9600;   break;
         case DN_BSP_PARAM_BAUD_19200:  baudRate = 19200;  break;
         case DN_BSP_PARAM_BAUD_38400:  baudRate = 38400;  break;
         case DN_BSP_PARAM_BAUD_57600:  baudRate = 57600;  break;
         case DN_BSP_PARAM_BAUD_115200: baudRate = 115200; break;
         case DN_BSP_PARAM_BAUD_230400: baudRate = 230400; break;
         case DN_BSP_PARAM_BAUD_460800: baudRate = 460800; break;
         case DN_BSP_PARAM_BAUD_921600: baudRate = 921600; break;
         default: baudRate = DEFAULT_BAUDRATE; break;
         }
      } else {
         baudRate = DEFAULT_BAUDRATE;
      }

   }

   return dnm_ucli_openPort (port, baudRate);
}

/**
\brief Print a formatted string.

Call this function to print a string with printf-like formatting. For example,
call

<tt>dnm_cli_printf("v=%d", v);</tt>

to print the value of variable <tt>v</tt> as a decimal number.

\param[in] format Sprintf-style format string.
\param[in] '...'  Optional format arguments.
*/
void dnm_ucli_printf (const char* format, ...)
{
    va_list args;
    va_start(args, format);
    dnm_ucli_printf_v(format, args);
    va_end(args);
}

// internal function
void dnm_ucli_printf_v (const char *format, va_list arg)
{
   static  BOOLEAN  wasError = FALSE;

   INT32S  len, hdrLen;
   INT32S  res;
   char    buf[DN_CLI_CTRL_SIZE];
   BOOLEAN prevVal;
   CS_LOCAL_VAR;

//   ((dn_cli_ctrlMsg_t *)buf)->cmdId   = DN_CLI_CMD_TYPE_TRACE;
#ifdef WIN32
   if (dnm_ucli_v.pCliLogFile != NULL) vfprintf(dnm_ucli_v.pCliLogFile, format, arg);
#endif
   
//   hdrLen = sizeof(dn_cli_ctrlMsg_t);
   hdrLen = 0;

   if (wasError) {   // Add "..."
      SNPRINTF(buf + hdrLen, sizeof(buf) - hdrLen, ERR_INDICATOR);
      hdrLen += strlen(ERR_INDICATOR);
   }

   len = VSPRINTF(buf + hdrLen, sizeof(buf) - hdrLen, format, arg);
   if(len < 0)   // error - print '***********'
      len = SNPRINTF(buf + hdrLen, sizeof(buf) - hdrLen, "*** CLI_LEN_ERROR ***\r\n");
   buf[sizeof(buf)-1] = 0;
   len += hdrLen;
   if (len>sizeof(buf)) 
      len = sizeof(buf);  
   
   prevVal = wasError;
   res = dn_write(DN_CLI_DEV_ID, buf, len);

   OS_ENTER_CRITICAL();
   if (res == DN_ERR_NO_RESOURCES || (!prevVal && wasError))
      wasError = TRUE;
   else
      wasError = FALSE;
   OS_EXIT_CRITICAL();
}

/**
\brief Wait for CLI input and process it.

This function blocks waiting for CLI input. When it receives input, it invokes
function to process CLI notification
\param[in] chDesc Channel descriptor of CLI input.

\return #DN_ERR_NONE if CLI input was received and handled correctly.
\return An error if the CLI channel could not be read (see 
*       #dn_readAsyncMsg()) or processing function returned
*       error.
*/
dn_error_t dnm_ucli_input (CH_DESC chDesc)
{
   INT32S              rxLen, msgType;
   INT8U               buf[DN_CLI_NOTIF_SIZE];
   INT8U               paramsLen;
   dn_error_t          res;
   dn_cli_notifMsg_t * pCliNotif = (dn_cli_notifMsg_t *)buf;

   if (dnm_ucli_v.notifCb==NULL) {
      return DN_ERR_NO_RESOURCES;
   }

   memset(buf, 0, sizeof(buf));
   res = dn_readAsyncMsg(chDesc, buf, &rxLen, &msgType, sizeof(buf), 0);
   if (res != DN_ERR_NONE)
      return res;

   paramsLen = (INT8U)rxLen - (INT8U)((((dn_cli_notifMsg_t*)(0))->data) - ((INT8U *)(dn_cli_notifMsg_t*)0)) - pCliNotif->offset;
   return dnm_ucli_v.notifCb(pCliNotif->type, pCliNotif->cmdId, pCliNotif->data+pCliNotif->offset, paramsLen);
}

/**
\brief Retrieve the CLI port.

\return The CLI serial port, one of the elements in #dn_cli_port_t.
*/
INT8U dnm_ucli_getPort (void)
{
   return dnm_ucli_v.port;
}

/**
\brief Retrieve the CLI baudrate.

\return The CLI serial baudrate.
*/
INT32U dnm_ucli_getBaudRate (void)
{
   return dnm_ucli_v.baudRate;
}

/**
\brief Set the current user access level.

Sets new current user access level. Each command is associated with a minimum
access level. See \ref device_cli for description of how to 
register commands. Raising the user access level gives the user 
access to more commands. 

It's your application's responsibility to raise/lower the user access level
appropriately. For example, you could implement a 'login' and 'logout'
CLI command to raise/lower the access level (a parameter for the 'login' CLI
command could be a password).

\post After this function returns, the user may have access to more/less CLI
   commands, depending on the user access level set.

\param[in] newAccessLevel New user access level. Acceptable values are listed
   in dn_cli_access_t.

\return The error received from calling #dn_ioctl() in the \ref device_cli.
*/
dn_error_t dnm_ucli_changeAccessLevel(dn_cli_access_t newAccessLevel)
{
   dn_error_t             rsp;
   INT8U                  buf[sizeof(dn_cli_chAccessCmd_t)];
   dn_cli_chAccessCmd_t * rCmd;

   rCmd = (dn_cli_chAccessCmd_t *)buf;
   rCmd->access = (INT8U)newAccessLevel;
   rsp = dn_ioctl(DN_CLI_DEV_ID, DN_IOCTL_CLI_CHANGE_ACCESS, (void *)rCmd, sizeof(dn_cli_chAccessCmd_t));
   return rsp;
}

//===== Print formatting

// internal function
void dnm_ucli_printfTimestamp_v(const char *format, va_list arg)
{
#ifdef WIN32
   // Print Windows time
   struct _timeb t;
   struct tm     locTime;
   _ftime_s(&t);
   localtime_s(&locTime, &(t.time));
   dnm_ucli_printf("(%02d:%02d:%02d.%03d) ", locTime.tm_hour, locTime.tm_min, locTime.tm_sec, t.millitm);
#endif
   dnm_ucli_printf("%6d : ", OSTimeGet());   // TODO change to print sec.msec
   dnm_ucli_printf_v(format, arg);
}

/**
\brief Print a timestamp, followed by a formatted string.

\param[in] format Sprintf-style format string.
\param[in] ...    Optional format arguments.
 */
void dnm_ucli_printfTimestamp(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    dnm_ucli_printfTimestamp_v(format, args);
    va_end(args);
}


// internal function
void dnm_ucli_dump_v(const INT8U *data, INT32S len, const char * format, va_list arg)
{
   int     i;
   dnm_ucli_printfTimestamp_v(format, arg);
   for (i = 0; i < len; i++) {
      if (i % 20 == 0)
         dnm_ucli_printf("\r\n   %03d : ", i);
      dnm_ucli_printf("%02x ", *data++);
   }
   dnm_ucli_printf("\r\n");
}

/**
\brief Print some binary data.

\param[in] data   Pointer to the start of the data to be printed.
\param[in] len    Number of bytes to print.
\param[in] format Sprintf-style format string.
\param[in] ...    Optional format arguments.
*/
void dnm_ucli_dump(const INT8U *data, INT32S len, const char * format, ...)
{
   va_list marker;
   va_start(marker, format);
   dnm_ucli_dump_v(data, len, format, marker);
   va_end(marker);
}


//===== Tracing

/**
\brief Print a formatted trace string if the corresponding trace flag is
   enabled.

\param[in] isTraceEnabled Flag if the trace in the calling 
     module is enabled.
\param[in] format    Sprintf-style format string.
\param[in] ...       Optional format arguments.
*/
void dnm_ucli_trace(BOOLEAN isTraceEnabled, const char* format, ...)
{
   if (isTraceEnabled) {
      va_list args;
      va_start(args, format);
      dnm_ucli_printfTimestamp_v(format, args);
      va_end(args);
   }
}

/**
\brief Print binary data if the corresponding trace flag is enabled.

\param[in] isTraceEnabled Flag if the trace in the calling 
     module is enabled.
\param[in] data      Pointer to the start of the data to be printed.
\param[in] len       Number of bytes to print.
\param[in] format    Sprintf-style format string.
\param[in] ...       Optional format arguments.
*/
void    dnm_ucli_traceDump(BOOLEAN isTraceEnabled, 
                         const INT8U* data, INT32S len, const char* format, ...)
{
   va_list marker;
   
   if (isTraceEnabled) {
      va_start(marker, format);
      dnm_ucli_dump_v(data, len, format, marker);
      va_end(marker);
   }
}

/**
\brief Same as dnm_cli_traceDump with a Mutex to prevent overlapping prints
*/
void dnm_ucli_traceDumpBlocking(BOOLEAN isTraceEnabled, 
                               const INT8U* data, INT32S len, const char* format, ...)
{
   va_list  marker;
   INT8U    err = OS_ERR_NONE;

   // create mutex if not created
   if (dnm_ucli_v.blockingTraceMutex == NULL) {
      dnm_ucli_v.blockingTraceMutex = OSSemCreate(1);
   }

   // wait for mutex
   OSSemPend(dnm_ucli_v.blockingTraceMutex, 0, &err);
   ASSERT (err == OS_ERR_NONE);
   
   
   if (isTraceEnabled) {
      va_start(marker, format);
      dnm_ucli_dump_v(data, len, format, marker);
      va_end(marker);
   }

   // release mutex
   err = OSSemPost(dnm_ucli_v.blockingTraceMutex);
   ASSERT (err == OS_ERR_NONE);
}

