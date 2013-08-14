/*
Copyright (c) 2011, Dust Networks.  All rights reserved.
*/

#include <stdio.h>
#include <string.h>
#include "dn_common.h"
#include "dnm_cli.h"
#include "dn_channel.h"
#include "dn_system.h"
#include "dn_gpio.h"
#include "dn_flash_info.h"

#define  HIGHSPEED_BAUDRATE 57600

struct {
   INT8U   port;
   INT32U  baudRate;
   BOOLEAN isHighSpeed;  // Return High Speed ON independet from real baudrate

#ifdef WIN32
   FILE * pCliLogFile;
#endif

} dnm_cli_v = {
               DN_CLI_PORT_C0,   // port
               DEFAULT_BAUDRATE, // baud rate
               0,                // isHighSpeed
#ifdef WIN32
               NULL              // pCliLogFile
#endif
};

#define ERR_INDICATOR "..."

//=========================== prototypes ======================================

static dn_error_t registerCommands(dnm_cli_cont_t * pCont);

//=========================== public ==========================================

/**
\brief Wrapper to open the CLI serial port.

\pre You need to call this function exactly once before you can use the CLI.
\pre This function is invoked by #dnm_cli_open() if you choose to use it.

\param[in] port The CLI port to open. Acceptable values are listed in
   #dn_cli_port_t.
\param[in] baudRate The CLI's baudrate. Use #DEFAULT_BAUDRATE.

\return The same error are the ones returns by the #dn_open() function for
   the \ref device_cli.
*/
dn_error_t dnm_cli_openPort (INT8U port, INT32U baudRate)
{
   dn_cli_open_args_t conf;

   conf.port = port;
   conf.baudRate = baudRate;
   dnm_cli_v.port = port;
   dnm_cli_v.baudRate = baudRate;
   dnm_cli_v.isHighSpeed = FALSE;
   return dn_open(DN_CLI_DEV_ID, &conf, sizeof(conf));
}

/**
\brief Open the CLI serial port using information stored in flash memory.

\pre This function calls #dnm_cli_openPort

This function reads the CLI port info from the \ref device_flashinfo.
Baudrate is only read from \ref device_flashinfo if the <tt>baudRate</tt> parameter is set to
<tt>0</tt>)

\param[in] baudRate The CLI's baudrate. Use #DEFAULT_BAUDRATE or set to
   <tt>0</tt> to use the one from \ref device_flashinfo

\return #DN_ERR_ERROR if the CLI port information could not be read from the 
   \ref device_flashinfo.
\return #DN_ERR_INVALID if the information retrieved from the
   \ref device_flashinfo indicates CLI is not enabled.
\return The same error are the ones returns by the #dnm_cli_openPort().
*/
dn_error_t dnm_cli_open (INT32U baudRate) {
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

   return dnm_cli_openPort (port, baudRate);
}

/**
\brief Initialize the CLI context.

The CLI context is defined in the #dnm_cli_cont_t structure. This function
initializes a pre-allocated variable of that type.  Every context needs a
unique channel.

\note Call this function after the OS has started. 

\warning The last <tt>cmdArr</tt> entry must contain a <tt>NULL</tt> command
   pointer.

\param[in,out] pCont  Pointer to a pre-allocated CLI context. This function will
   clear the information contained in that variable. 
\param[in]     inpCh  Channel for receiving CLI input.
\param[in]     cmdArr Array of commands to register with the \ref device_cli.

\return #DN_ERR_NONE if the initialization is successful.
\return #DN_ERR_ERROR if a command length specified in the command array is
   longer than #DN_CLI_CTRL_SIZE.
\return The error received from calling #dn_ioctl() in the \ref device_cli, if
   that call fails.
*/
dn_error_t dnm_cli_initContext (dnm_cli_cont_t *pCont, CH_DESC inpCh, const dnm_cli_cmdDef_t  *cmdArr)                      
{
   memset(pCont, 0, sizeof(dnm_cli_cont_t));
   pCont->inpCh = inpCh;
   pCont->cmdArr = cmdArr;

   return (registerCommands (pCont));   
}

/**
\brief Register a custom help handler.

Allows your application to register a function that will be called when help is
invoked for one of the commands. If you do not register your custon handler
(i.e. if your application does not call this function), the usage string
associated with the command will be displayed by default.

\post This function modifies the CLI context.

\param[in,out] pCont         The CLI context to modify.
\param[in]     pfHelpHandler Pointer to the help handler.
*/
void dnm_cli_setHelpHandler (dnm_cli_cont_t *pCont, dnm_cli_helpHandler_t pfHelpHandler)
{
   pCont->pfHelpHandler = pfHelpHandler;
}

/**
\brief Set the current user access level.

Sets new current user access level. Each command is associated a minimum
access level for each command (#dn_cli_registerCmdHdr_t::accessLevel). Raising
the user access level gives the user access to more commands.

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
dn_error_t dnm_cli_changeAccessLevel(dn_cli_access_t newAccessLevel)
{
   dn_error_t             rsp;
   INT8U                  buf[sizeof(dn_cli_chAccessCmd_t)];
   dn_cli_chAccessCmd_t * rCmd;

   rCmd = (dn_cli_chAccessCmd_t *)buf;
   rCmd->access = (INT8U)newAccessLevel;
   rsp = dn_ioctl(DN_CLI_DEV_ID, DN_IOCTL_CLI_CHANGE_ACCESS, (void *)rCmd, sizeof(dn_cli_chAccessCmd_t));
   return rsp;
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
void dnm_cli_printf (const char* format, ...)
{
    va_list args;
    va_start(args, format);
    dnm_cli_printf_v(format, args);
    va_end(args);
}

// internal function
void dnm_cli_printf_v (const char *format, va_list arg)
{
   static  BOOLEAN  wasError = FALSE;

   INT32S  len, hdrLen;
   INT32S  res;
   char    buf[DN_CLI_CTRL_SIZE];
   BOOLEAN prevVal;
   CS_LOCAL_VAR;

//   ((dn_cli_ctrlMsg_t *)buf)->cmdId   = DN_CLI_CMD_TYPE_TRACE;
#ifdef WIN32
   if (dnm_cli_v.pCliLogFile != NULL) vfprintf(dnm_cli_v.pCliLogFile, format, arg);
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
the appropriate CLI command handler. If the CLI command handler returns an
error, the help handler is invoked.

\param[in] pCont CLI context.

\return #DN_ERR_NONE if CLI input was received and handled correctly.
\return A error if the CLI channel could not be read (see #dn_readAsyncMsg()).
\return A error if CLI command could not be processed (see
   #dnm_cli_procNotif()).
*/
dn_error_t dnm_cli_input (dnm_cli_cont_t * pCont)
{
   INT32S              rxLen, msgType;
   INT8U               buf[DN_CLI_NOTIF_SIZE];
   dn_error_t          res;
   dn_cli_notifMsg_t * pCliNotif = (dn_cli_notifMsg_t *)buf;

   memset(buf, 0, sizeof(buf));
   res = dn_readAsyncMsg(pCont->inpCh, buf, &rxLen, &msgType, sizeof(buf), 0);
   if (res != DN_ERR_NONE)
      return res;
      
   return dnm_cli_procNotif (pCont, msgType, pCliNotif, rxLen);
      
      
      
}

/**
\brief Process some CLI input.

Your application can call this function with input received from the CLI
channel. It invokes the appropriate CLI command handler, or the help handler if
the CLI command handler returns an error.

\param[in] pCont CLI context.
\param[in] msgType Type of the message received over the CLI channel. This
   function expects this to be set to #DN_MSG_TYPE_CLI_NOTIF.
\param[in] pCliNotif Notification received on the CLI channel.
\param[in] rxLen Length of the notification received on the CLI channel.

\return #DN_ERR_NONE if CLI input was handled correctly.
\return #DN_ERR_READ if the notification type is not DN_MSG_TYPE_CLI_NOTIF, or
   if <tt>rxLen</tt> is <tt>0</tt>.
\return #DN_ERR_NOT_SUPPORTED if the comand ID is not supported.
*/
dn_error_t dnm_cli_procNotif (dnm_cli_cont_t *pCont, INT32S msgType, dn_cli_notifMsg_t *pCliNotif, INT32S rxLen)
{
   dn_error_t res = DN_ERR_NONE;
   INT32U cmdLen;
   
   if ((msgType != DN_MSG_TYPE_CLI_NOTIF) || (rxLen == 0))
      return DN_ERR_READ;
   if (pCliNotif->cmdId >= pCont->numCmd)
      return DN_ERR_NOT_SUPPORTED;

   if (pCliNotif->type == DN_CLI_NOTIF_INPUT) {
      // adjust length: remove header of pCliNotif and length of the command itself
      cmdLen = rxLen - ((((dn_cli_notifMsg_t*)(0))->data) - ((INT8U *)(dn_cli_notifMsg_t*)0)) - pCliNotif->offset;
      res = (pCont->cmdArr[pCliNotif->cmdId].handler)(pCliNotif->data+pCliNotif->offset, cmdLen, 0);
      if (res == DN_ERR_INVALID)
         dnm_cli_printf("\rinvalid argument(s)");
   } 
   // Print help
   if (pCliNotif->type == DN_CLI_NOTIF_HELP || res == DN_ERR_ERROR) {
      if (pCont->pfHelpHandler)
         pCont->pfHelpHandler(pCont->cmdArr + pCliNotif->cmdId);
      else
         dnm_cli_usage(pCont->cmdArr + pCliNotif->cmdId);
   }
   dnm_cli_printf("\n\r> ");

   return DN_ERR_NONE;
}

/**
\brief Print a CLI command usage string.

When defining a CLI command, #dnm_cli_cmdDef_t::usage allows your
application to associate a usage string, i.e. some text explaining how to use
this CLI command.

Call this function to print this usage string over the CLI serial port.

\param[in] pCmdDef Descriptor of a command.
*/
void dnm_cli_usage (const dnm_cli_cmdDef_t * pCmdDef)
{
   if (pCmdDef->usage)
      dnm_cli_printf("Usage: %s\r\n", pCmdDef->usage);
}

// internal function
void dnm_cli_setLogFile(const char * fileName)
{
#ifdef WIN32 
   if (dnm_cli_v.pCliLogFile != NULL) {
      fclose(dnm_cli_v.pCliLogFile);
      dnm_cli_v.pCliLogFile = NULL;
   }
   if (fileName)
      dnm_cli_v.pCliLogFile = fopen(fileName, "wb");
#endif // WIN32      
}

/**
\brief Switch between blocking and non-blocking output modes.

The CLI device supports blocking and non-blocking output modes:
- in blocking mode, a call to #dn_write() blocks until the bytes are written in
  the CLI output buffer and are scheduled to be printed over the serial port.
- in non-blocking mode, a call to #dn_write() returns immediately, even when
  the serial transmit buffer is full. In this case, the bytes are dropped and
  not printed over the serial port; the characters <tt>...</tt> print over the
  serial port to indicate to the user some characters were dropped.

\param[in] fBlocking Set to <tt>TRUE</tt>  to swicth to     blocking mode;
                     set to <tt>FALSE</tt> to swicth to non-blocking mode.
*/
void dnm_cli_setOutputMode (BOOLEAN fBlocking)
{
   dn_cli_setOutMode_t cliMode;

   cliMode.mode = fBlocking ? DN_CLI_OUT_MODE_BLOCKING : DN_CLI_OUT_MODE_NONBLOCKING;
   dn_ioctl(DN_CLI_DEV_ID, DN_IOCTL_CLI_SET_OUTPUT_MODE, &cliMode, sizeof(cliMode));
}

/**
\brief Retrieve the CLI port.

\return The CLI serial port, one of the elements in #dn_cli_port_t.
*/
INT8U dnm_cli_getPort (void)
{
   return dnm_cli_v.port;
}

/**
\brief Retrieve the CLI baudrate.

\return The CLI serial baudrate.
*/
INT32U dnm_cli_getBaudRate (void)
{
   return dnm_cli_v.baudRate;
}

// internal function
BOOLEAN dnm_cli_isHighSpeedCLI( void )
{
   return dnm_cli_v.baudRate >= HIGHSPEED_BAUDRATE || dnm_cli_v.isHighSpeed;
}

// internal function
void dnm_cli_forceHighSpeedFlag(BOOLEAN isHighSpeed)
{
   dnm_cli_v.isHighSpeed = isHighSpeed;
}

//=========================== private =========================================

// Register a single command.
static dn_error_t registerCommand(CH_DESC inpCh, const dnm_cli_cmdDef_t * pCmd, int id)
{
   dn_error_t rsp = DN_ERR_NONE;

   INT8U                  cmdLen;
   dn_cli_registerCmd_t  * rCmd;
   INT8U                  buf[DN_CLI_CTRL_SIZE];

   if (pCmd->handler == NULL) {
      return DN_ERR_INVALID;
   }

//   ((dn_cli_ctrlMsg_t *)buf)->cmdId = DN_CLI_CMD_TYPE_REGISTER;

//   rCmd = (dn_cli_registerCmd_t *)(((dn_cli_ctrlMsg_t *)buf)->data);
   rCmd = (dn_cli_registerCmd_t *)buf;
   rCmd->hdr.cmdId    = id;
   rCmd->hdr.chDesc   = inpCh;
   rCmd->hdr.lenCmd   = strlen(pCmd->command);
   rCmd->hdr.accessLevel = pCmd->accessLevel;

   // validate length of registration command
//   cmdLen = sizeof(dn_cli_ctrlMsg_t) + sizeof(dn_cli_registerCmdHdr_t) +
//            rCmd->hdr.lenCmd;
   cmdLen = sizeof(dn_cli_registerCmdHdr_t) + rCmd->hdr.lenCmd;
   
   if (cmdLen > sizeof(buf)) 
      return DN_ERR_ERROR;

   memcpy(rCmd->data, pCmd->command, rCmd->hdr.lenCmd);

   rsp = dn_ioctl(DN_CLI_DEV_ID, DN_IOCTL_CLI_REGISTER, (void *)rCmd, sizeof(dn_cli_registerCmd_t));
   return rsp;
}

// Register multiple commands.
static dn_error_t registerCommands (dnm_cli_cont_t * pCont)
{
   dn_error_t res;

   for (pCont->numCmd = 0; pCont->cmdArr[pCont->numCmd].command; pCont->numCmd++) {
      res = registerCommand(pCont->inpCh, pCont->cmdArr + pCont->numCmd, pCont->numCmd);
      if (res != DN_ERR_NONE)
         return res;
   }
   return DN_ERR_NONE;
}
