/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "string.h"
#include "stdio.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_onewire.h"
#include "dn_exe_hdr.h"
#include "Ver.h"

#include <string.h>
#include <ctype.h>

//=========================== defines =========================================

/// device to use for 1-Wire
#define OWI_DEVICE                     DN_1WIRE_UARTC1_DEV_ID

/// Maximum number of bytes to be ever written/read over 1-Wire
#define OWI_BUFF_SIZE                  8

/// Size of the address of a 1-Wire device
#define OWI_ADDR_SIZE                  8

/// Maximum number of devices discovered when issuing a search command
#define OWI_MAX_DEVICES                8

/// Delay between iterations of the search routing, in ms
#define OWI_SEARCH_DELAY               20

/// 1-Wire command to read the slave address
#define OWI_CMD_READ_ROM               0x33

/// 1-Wire command to start a search routine
#define OWI_CMD_SEARCH_ROM             0xF0

/// 1-Wire command to specify the address of the slave to talk to
#define OWI_CMD_MATCH_ROM              0x55

/// 1-Wire command to skip specifying a slave address
#define OWI_CMD_SKIP_ROM               0xCC

//=========================== prototypes ======================================

//===== CLI handlers
dn_error_t    cli_detectCmdHandler(  INT8U* arg, INT32U len);
dn_error_t    cli_writeCmdHandler(   INT8U* arg, INT32U len);
dn_error_t    cli_writeBitCmdHandler(INT8U* arg, INT32U len);
dn_error_t    cli_readCmdHandler(    INT8U* arg, INT32U len);
dn_error_t    cli_readBitCmdHandler( INT8U* arg, INT32U len);
dn_error_t    cli_searchCmdHandler(  INT8U* arg, INT32U len);
//===== helpers
INT8U         owi_searchDev(
                 INT8U*  romNums,
                 INT32S* lastDiscrepancy,
                 INT32S* lastFamilyDiscr,
                 INT32S* lastDeviceFlag,
                 INT8U*  crc8
              );
dn_error_t    owi_writeBit(INT8U writeBit);
dn_error_t    owi_readBit(INT8U* readBit);
void          owi_iterate_crc(INT8U value, INT8U* crc8);
void          printBuf(INT8U* buf, INT8U len);
int           hex2array(const char * str, INT8U * buf, int bufSize);
int           hex2array_p(char c);

//=========================== const ===========================================

static const INT8U crc8_table[] = {
     0,  94, 188, 226,  97,  63, 221, 131, 194, 156, 126,  32, 163, 253,  31,  65,
   157, 195,  33, 127, 252, 162,  64,  30,  95,   1, 227, 189,  62,  96, 130, 220,
    35, 125, 159, 193,  66,  28, 254, 160, 225, 191,  93,   3, 128, 222,  60,  98,
   190, 224,   2,  92, 223, 129,  99,  61, 124,  34, 192, 158,  29,  67, 161, 255,
    70,  24, 250, 164,  39, 121, 155, 197, 132, 218,  56, 102, 229, 187,  89,   7,
   219, 133, 103,  57, 186, 228,   6,  88,  25,  71, 165, 251, 120,  38, 196, 154,
   101,  59, 217, 135,   4,  90, 184, 230, 167, 249,  27,  69, 198, 152, 122,  36,
   248, 166,  68,  26, 153, 199,  37, 123,  58, 100, 134, 216,  91,   5, 231, 185,
   140, 210,  48, 110, 237, 179,  81,  15,  78,  16, 242, 172,  47, 113, 147, 205,
    17,  79, 173, 243, 112,  46, 204, 146, 211, 141, 111,  49, 178, 236,  14,  80,
   175, 241,  19,  77, 206, 144, 114,  44, 109,  51, 209, 143,  12,  82, 176, 238,
    50, 108, 142, 208,  83,  13, 239, 177, 240, 174,  76,  18, 145, 207,  45, 115,
   202, 148, 118,  40, 171, 245,  23,  73,   8,  86, 180, 234, 105,  55, 213, 139,
    87,   9, 235, 181,  54, 104, 138, 212, 149, 203,  41, 119, 244, 170,  72,  22,
   233, 183,  85,  11, 136, 214,  52, 106,  43, 117, 151, 201,  74,  20, 246, 168,
   116,  42, 200, 150,  21,  75, 169, 247, 182, 232,  10,  84, 215, 137, 107,  53
};

//===== CLI

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_detectCmdHandler,   "d",      "",       DN_CLI_ACCESS_LOGIN},
   {&cli_writeCmdHandler,    "w",      "",       DN_CLI_ACCESS_LOGIN},
   {&cli_writeBitCmdHandler, "wb",     "",       DN_CLI_ACCESS_LOGIN},
   {&cli_readCmdHandler,     "r",      "",       DN_CLI_ACCESS_LOGIN},
   {&cli_readBitCmdHandler,  "rb",     "",       DN_CLI_ACCESS_LOGIN},
   {&cli_searchCmdHandler,   "s",      "",       DN_CLI_ACCESS_LOGIN},
   {NULL,                    NULL,     NULL,     0},
};

//=========================== variables =======================================

// Variables local to this application.
typedef struct {
   /// The number of devides found during an 1-Wire search operation.
   INT8U           numDetectedDevices;
   /// The buffer used for communicating over the 1-Wire interface.
   INT8U           owi_buff[OWI_BUFF_SIZE];
   /// 64-bit addresses of the devides found during an 1-Wire search operation.
   INT8U           detectedDevices[OWI_MAX_DEVICES][OWI_ADDR_SIZE];
} onewire_app_vars_t;

onewire_app_vars_t onewire_app_vars;

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   dn_error_t           dnErr;
   INT8U                osErr;
   dn_ow_open_args_t    dn_ow_open_args;
   
   //===== initialize helper tasks
   
   // CLI task
   cli_task_init(
      "onewire",                            // appName
      &cliCmdDefs                           // cliCmds
   );
   
   // local interface task
   loc_task_init(
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== open 1-Wire device
   
   dn_ow_open_args.maxTransactionLength=OWI_BUFF_SIZE;
   dnErr = dn_open(
      OWI_DEVICE,                           // device
      &dn_ow_open_args,                     // args
      sizeof(dn_ow_open_args)               // argLen
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   return 0;
}

//=========================== CLI handlers ====================================

//===== 'd' (detect) CLI command

dn_error_t cli_detectCmdHandler(INT8U* arg, INT32U len) {
   dn_error_t           dnErr;
   dn_ow_ioctl_detect_t dn_ow_ioctl_detect;
   
   dnErr = dn_ioctl(
      OWI_DEVICE,
      DN_IOCTL_1WIRE_DETECT,
      &dn_ow_ioctl_detect,
      sizeof(dn_ow_ioctl_detect_t)
   );
   if (dnErr==DN_ERR_NONE) {
      dnm_ucli_printf("slavePresent=%d\r\n",dn_ow_ioctl_detect.slavePresent);
   } else {
      dnm_ucli_printf("WARNING dn_ioctl() returns %d\r\n",dnErr);
   }
   
   return DN_ERR_NONE;
}

//===== 'w' (write) CLI command

dn_error_t cli_writeCmdHandler(INT8U* arg, INT32U len) {
   dn_error_t dnErr;
   int        numBytes;
   int        err;
   
   //--- param 0: writeBuf
   
   numBytes = hex2array(arg, onewire_app_vars.owi_buff, sizeof(onewire_app_vars.owi_buff));
   if (numBytes<=0) {
      return DN_ERR_INVALID;
   }
   
   //--- write
   
   dnErr = dn_write(
      OWI_DEVICE,
      onewire_app_vars.owi_buff,
      numBytes
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   return DN_ERR_NONE;
}

//===== 'wb' (write bit) CLI command

dn_error_t cli_writeBitCmdHandler(INT8U* arg, INT32U len) {
   dn_error_t                dnErr;
   int                       bitToWrite;
   dn_ow_ioctl_writebit_t    dn_ow_ioctl_writebit;
   int                       err;
   int                       l;
   
   //--- param 0: bitToWrite
   
   l = sscanf (arg, "%d", &bitToWrite);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   if (bitToWrite!=0 && bitToWrite!=1) {
      return DN_ERR_INVALID;
   }
   
   //--- write
   
   dn_ow_ioctl_writebit.writeData = bitToWrite;
   dnErr = dn_ioctl(
      OWI_DEVICE,
      DN_IOCTL_1WIRE_WRITEBIT,
      &dn_ow_ioctl_writebit,
      sizeof(dn_ow_ioctl_writebit_t)
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   return DN_ERR_NONE;
}

//===== 'r' (read) CLI command

dn_error_t cli_readCmdHandler(INT8U* arg, INT32U len) {
   int        numBytes;
   int        numRead;
   int        l;
   
   //--- param 0: numBytes
   
   l = sscanf (arg, "%d", &numBytes);
   if (l < 1) {
      return DN_ERR_INVALID;
   }
   if (numBytes>sizeof(onewire_app_vars.owi_buff)) {
      return DN_ERR_INVALID;
   }
   
   //--- read
   
   numRead = dn_read(
      OWI_DEVICE,
      onewire_app_vars.owi_buff,
      numBytes
   );
   ASSERT(numRead==numBytes);
   
   //--- print
   
   printBuf(onewire_app_vars.owi_buff,numBytes);
   
   return DN_ERR_NONE;
}

//===== 'rb' (read bit) CLI command

dn_error_t cli_readBitCmdHandler(INT8U* arg, INT32U len) {
   dn_error_t dnErr;
   dn_ow_ioctl_readbit_t  dn_ow_ioctl_readbit;
   
   //--- read
   
   dnErr = dn_ioctl(
      OWI_DEVICE,
      DN_IOCTL_1WIRE_READBIT,
      &dn_ow_ioctl_readbit,
      sizeof(dn_ow_ioctl_readbit_t)
   );
   
   dnm_ucli_printf("%d\r\n",dn_ow_ioctl_readbit.readData);
   
   return DN_ERR_NONE;
}

//===== 's' (search) CLI command

dn_error_t cli_searchCmdHandler(INT8U* arg, INT32U len) {
   INT32S foundDevice;
   INT32S lastDiscrepancy;
   INT32S lastFamilyDiscr;
   INT32S lastDeviceFlag;
   INT8U  crc8;
   INT8U  i;
   
   //--- reset state
   
   onewire_app_vars.numDetectedDevices = 0;
   lastDiscrepancy = 0;
   lastDeviceFlag  = FALSE;
   lastFamilyDiscr = 0;
   
   //--- search for devices
   
   while (1) { // will break if either no more device found, or list full
      
      // wait between detecting devices
      OSTimeDly(OWI_SEARCH_DELAY);
      
      // search for a device
      foundDevice = owi_searchDev(
         &onewire_app_vars.detectedDevices[onewire_app_vars.numDetectedDevices][0],
         &lastDiscrepancy,
         &lastFamilyDiscr,
         &lastDeviceFlag,
         &crc8
      );
      
      // stop if no device found
      if (foundDevice==FALSE) {
         break;
      }
      
      // increment number of detected devices
      onewire_app_vars.numDetectedDevices++;
      
      // stop if list is full
      if (onewire_app_vars.numDetectedDevices==OWI_MAX_DEVICES) {
         break;
      }
   }
   
   //--- print
   
   dnm_ucli_printf("found %d device(s)\r\n",onewire_app_vars.numDetectedDevices);
   for (i=0;i<onewire_app_vars.numDetectedDevices;i++) {
      dnm_ucli_printf(" - ");
      printBuf(&onewire_app_vars.detectedDevices[i][0],OWI_ADDR_SIZE);
   }
   
   return DN_ERR_NONE;
}

//=========================== helpers =========================================

/**
\Brief Function to identify device's registration number.
 * 
 * @param romNums         pointer to store the registration number
 * @param lastDiscrepancy pointer to last discrepancy
 * @param lastFamilyDiscr pointer to the last family discrepancy
 * @param lastDeviceFlag  pointer to the last device flag
 * @param crc8            pointer to the crc value
 *
 * return 
 *        - TRUE on identification of device
 *        - FALSE on no new device found
 */
INT8U owi_searchDev (INT8U *romNums, INT32S *lastDiscrepancy,
                     INT32S *lastFamilyDiscr, INT32S *lastDeviceFlag, INT8U *crc8)
{
   dn_ow_ioctl_detect_t dn_ow_ioctl_detect;
   INT32S               sysErr;
   INT32S               id_bit_number;
   INT32S               last_zero;
   INT32S               rom_byte_number;
   INT8U                search_result;
   INT8U                id_bit;
   INT8U                cmp_id_bit;
   INT8U                rom_byte_mask;
   INT8U                search_direction;
   INT8U                byte;
   dn_error_t           dnErr;
   
   // initialize
   id_bit          = 0;
   cmp_id_bit      = 0;
   id_bit_number   = 1;
   last_zero       = 0;
   rom_byte_number = 0;
   rom_byte_mask   = 1;
   search_result   = 0;
   *crc8           = 0;
   
   if (!*lastDeviceFlag) {
      // previous call was not the last one
      
      // issues a 1-Wire reset pulse
      dn_ioctl(
         OWI_DEVICE,
         DN_IOCTL_1WIRE_DETECT,
         &dn_ow_ioctl_detect,
         sizeof(dn_ow_ioctl_detect_t)
      );
      
      if (dn_ow_ioctl_detect.slavePresent==0x00) {
         // no slave is present on the 1-Wire bus
         
         // stop the search
         *lastDiscrepancy    = 0;
         *lastDeviceFlag     = FALSE;
         *lastFamilyDiscr    = 0;
         return FALSE;
      }
      
      // if you get here, there is at least one 1-Wire device on the bus
      
      // issue the search command
      byte = OWI_CMD_SEARCH_ROM;
      dnErr = dn_write(
         OWI_DEVICE,
         &byte,
         sizeof(byte)
      );
      ASSERT(dnErr==DN_ERR_NONE);
      
      do { // one iteration per byte in the address of a device
         
         // read a bit
         dnErr = owi_readBit(&id_bit);
         ASSERT(dnErr==DN_ERR_NONE);
         
         // read its complement
         dnErr = owi_readBit(&cmp_id_bit);
         ASSERT(dnErr==DN_ERR_NONE);
         
         if ((id_bit == 1) && (cmp_id_bit == 1)) {
            // no device on the bus
            break;
         } else {
            /* all devices coupled have 0 or 1 */
            if (id_bit != cmp_id_bit) {
               /* bit write value for search */
               search_direction = id_bit;
            } else {
               /* if this discrepancy if before the Last Discrepancy 
                * on a previous next then pick the same as last time */
               if (id_bit_number < *lastDiscrepancy) {
                  search_direction = ((romNums[rom_byte_number] & rom_byte_mask) > 0);
               } else {
                  /* if equal to last pick 1, if not then pick 0 */
                  search_direction = (id_bit_number == *lastDiscrepancy);
               }
               /* if 0 was picked then record its position in LastZero */
               if (search_direction == 0) {
                  last_zero = id_bit_number;
                  
                  /* check for Last discrepancy in family */
                  if (last_zero < 9) {
                     *lastFamilyDiscr = last_zero;
                  }
               }
            }
            
            /* set or clear the bit in the ROM byte rom_byte_number
             * with mask rom_byte_mask */
            if (search_direction == 1) {
               romNums[rom_byte_number] |= rom_byte_mask;
            } else {
               romNums[rom_byte_number] &= (INT8U)~rom_byte_mask;
            }
            
            /* serial number search direction write bit */
            dnErr = owi_writeBit(search_direction);
            if (dnErr!=DN_ERR_NONE) {
               dnm_ucli_printf("ow write (0), err=%d\r\n", dnErr);
            }
            
            /* increment the byte counter id_bit_number
             * and shift the mask rom_byte_mask */
            id_bit_number++;
            rom_byte_mask <<= 1;
            
            /* if the mask is 0 then go to new SerialNum byte rom_byte_number
            * and reset mask */
            if (rom_byte_mask == 0) {
               /* accumulate the CRC */
               owi_iterate_crc(romNums[rom_byte_number], crc8);
               rom_byte_number++;
               rom_byte_mask = 1;
            }
         }
         /* loop until through all ROM bytes 0-7 */
      }  while (rom_byte_number < OWI_ADDR_SIZE);
      
      /* if the search was successful then */
      if (!((id_bit_number <= (OWI_ADDR_SIZE * 8)) || (*crc8 != 0))) {
         /* search successful so set lastDiscrepancy,lastDeviceFlag,search_result */
         *lastDiscrepancy = last_zero;
         
         /* check for last device */
         if (*lastDiscrepancy == 0) {
            *lastDeviceFlag = TRUE;
         }
         
         search_result = TRUE;
      }
   }
   
   /* if no device found then reset counters so next 'search' will be like a first */
   if (!search_result || !romNums[0]) {
      *lastDiscrepancy  = 0;
      *lastDeviceFlag   = FALSE;
      *lastFamilyDiscr  = 0;
      search_result     = FALSE;
   }
   
   return search_result;
}

dn_error_t owi_writeBit(INT8U writeBit) {
   dn_ow_ioctl_writebit_t  dn_ow_ioctl_writebit;
   dn_error_t              dnErr;
   
   dn_ow_ioctl_writebit.writeData = writeBit;
   dnErr = dn_ioctl(
      OWI_DEVICE,
      DN_IOCTL_1WIRE_WRITEBIT,
      &dn_ow_ioctl_writebit,
      sizeof(dn_ow_ioctl_writebit_t)
   );
   
   return dnErr;
}

dn_error_t owi_readBit(INT8U* readBit) {
   dn_ow_ioctl_readbit_t  dn_ow_ioctl_readbit;
   dn_error_t             dnErr;
   
   dnErr = dn_ioctl(
      OWI_DEVICE,
      DN_IOCTL_1WIRE_READBIT,
      &dn_ow_ioctl_readbit,
      sizeof(dn_ow_ioctl_readbit_t)
   );
   
   *readBit = dn_ow_ioctl_readbit.readData;
   return dnErr;
}

void owi_iterate_crc(INT8U value, INT8U* crc8) {
   *crc8 = crc8_table[*crc8 ^ value];
}

void printBuf(INT8U* buf, INT8U len) {
   INT8U i;
   
   dnm_ucli_printf("(%d bytes)",len);
   for (i=0;i<len;i++) {
      dnm_ucli_printf(" %02x",buf[i]);
   }
   dnm_ucli_printf("\r\n");
}

int hex2array(const char * str, INT8U * buf, int bufSize) {
   int i;

   if (str == NULL || buf == NULL || bufSize<=0)
      return 0;

   while(*str == ' ') str++;
   memset(buf, 0, bufSize);
   for(i=0; *str && !isspace(*str) && i<bufSize; i++) {
      if (*str == '-' || *str == ':') 
         str++;
      if (!isxdigit(*str))
         break;
      buf[i] = hex2array_p(*str++);
      if (isxdigit(*str)) 
         buf[i] = (buf[i] << 4) | hex2array_p(*str++);
   }
   if (*str && !isspace(*str))
      return -3;                 // Invalid value
   return i;
}

int hex2array_p(char c) {
   c = tolower(c);
   return c>='0' && c<='9' ? c - '0' : c - 'a' + 10;
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
