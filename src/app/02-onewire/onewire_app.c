/*
Copyright (c) 2015, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "stdio.h"
#include "string.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_onewire.h"
#include "dn_exe_hdr.h"
#include "Ver.h"

//=========================== defines =========================================

/// device to use for 1-Wire
#define OWI_DEVICE                     DN_1WIRE_UARTC1_DEV_ID

/// Maximum number of bytes to be ever written/read over 1-Wire
#define OWI_BUFF_SIZE                  8

/// Size of the address of a 1-Wire device
#define OWI_ADDR_SIZE                  8

/// Last bit of OWI ADDRESS, bit starts with 1, end with 64
#define OWI_ADDR_LAST_BIT              64

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
dn_error_t    cli_detectCmdHandler(   INT8U* arg, INT32U len);
dn_error_t    cli_writeCmdHandler(    INT8U* arg, INT32U len);
dn_error_t    cli_writeBitCmdHandler( INT8U* arg, INT32U len);
dn_error_t    cli_readCmdHandler(     INT8U* arg, INT32U len);
dn_error_t    cli_readBitCmdHandler(  INT8U* arg, INT32U len);
dn_error_t    cli_searchCmdHandler(   INT8U* arg, INT32U len);
dn_error_t    cli_familyFindCmdHandler(INT8U* arg, INT32U len);
dn_error_t    cli_familySkipCmdHandler(INT8U* arg, INT32U len);

BOOLEAN       owi_first();
BOOLEAN       owi_next();
BOOLEAN       owi_verify();
void          owi_targetSetup(INT8U family_code);
void          owi_familySkipSetup();
BOOLEAN       owi_reset();
void          owi_writeByte(INT8U byte_value);
void          owi_writeBit(INT8U bit_value);
INT8U         owi_readBit();
BOOLEAN       owi_search();
void          owi_doCrc8(INT8U value);
void          owi_resetCounters();


//=========================== const ===========================================

static const INT8U dscrc_table[] = {
        0, 94,188,226, 97, 63,221,131,194,156,126, 32,163,253, 31, 65,
      157,195, 33,127,252,162, 64, 30, 95,  1,227,189, 62, 96,130,220,
       35,125,159,193, 66, 28,254,160,225,191, 93,  3,128,222, 60, 98,
      190,224,  2, 92,223,129, 99, 61,124, 34,192,158, 29, 67,161,255,
       70, 24,250,164, 39,121,155,197,132,218, 56,102,229,187, 89,  7,
      219,133,103, 57,186,228,  6, 88, 25, 71,165,251,120, 38,196,154,
      101, 59,217,135,  4, 90,184,230,167,249, 27, 69,198,152,122, 36,
      248,166, 68, 26,153,199, 37,123, 58,100,134,216, 91,  5,231,185,
      140,210, 48,110,237,179, 81, 15, 78, 16,242,172, 47,113,147,205,
       17, 79,173,243,112, 46,204,146,211,141,111, 49,178,236, 14, 80,
      175,241, 19, 77,206,144,114, 44,109, 51,209,143, 12, 82,176,238,
       50,108,142,208, 83, 13,239,177,240,174, 76, 18,145,207, 45,115,
      202,148,118, 40,171,245, 23, 73,  8, 86,180,234,105, 55,213,139,
       87,  9,235,181, 54,104,138,212,149,203, 41,119,244,170, 72, 22,
      233,183, 85, 11,136,214, 52,106, 43,117,151,201, 74, 20,246,168,
      116, 42,200,150, 21, 75,169,247,182,232, 10, 84,215,137,107, 53};

//===== CLI

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_detectCmdHandler,       "d",      "",       DN_CLI_ACCESS_LOGIN},
   {&cli_writeCmdHandler,        "w",      "",       DN_CLI_ACCESS_LOGIN},
   {&cli_writeBitCmdHandler,     "wb",     "",       DN_CLI_ACCESS_LOGIN},
   {&cli_readCmdHandler,         "r",      "",       DN_CLI_ACCESS_LOGIN},
   {&cli_readBitCmdHandler,      "rb",     "",       DN_CLI_ACCESS_LOGIN},
   {&cli_searchCmdHandler,       "s",      "",       DN_CLI_ACCESS_LOGIN},
   {&cli_familyFindCmdHandler,   "ff",     "",       DN_CLI_ACCESS_LOGIN},
   {&cli_familySkipCmdHandler,   "fs",     "",       DN_CLI_ACCESS_LOGIN},
   {NULL,                        NULL,     NULL,     0},
};

//=========================== variables =======================================

// Variables local to this application.
typedef struct {
   /// The buffer used for communicating over the 1-Wire interface.
   INT8U           owi_buff[OWI_BUFF_SIZE];
   /// 64-bit address of the device found during an 1-wire search operation.
   INT8U           ROM_NO[OWI_ADDR_SIZE];
   BOOLEAN         fLastDeviceFlag;
   INT8U           crc8;
   INT8U           lastFamilyDiscrepancy;
   INT8U           lastDiscrepancy;

} onewire_app_vars_t;

onewire_app_vars_t onewire_app_vars;

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   dn_error_t           dnErr;
   dn_ow_open_args_t    dn_ow_open_args;
   
   //===== initialize helper tasks
   
   // CLI task
   cli_task_init(
      "onewire",                            // appName
      cliCmdDefs                            // cliCmds
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
   
   if (owi_reset()) {
      dnm_ucli_printf("1-wire device found\r\n");
   } else {
      dnm_ucli_printf("1-wire device not found!\r\n");
   }
   
   return DN_ERR_NONE;
}

//===== 'w' (write) CLI command

dn_error_t cli_writeCmdHandler(INT8U* arg, INT32U len) {
   dn_error_t dnErr;
   INT8S      numBytes;
   
   //--- param 0: writeBuf
   
   numBytes = dnm_ucli_hex2byte(arg, onewire_app_vars.owi_buff, sizeof(onewire_app_vars.owi_buff));
   if (numBytes<=0) {
      dnm_ucli_printf("Usage: w <data>\r\n\r\n");
      dnm_ucli_printf("       Write up to 8 bytes of hex values to device, no space\r\n");
      dnm_ucli_printf("\r\nExample:\r\n");
      dnm_ucli_printf("       w 55\r\n");
      dnm_ucli_printf("       w 26549c9e000000f1\r\n");
      return DN_ERR_NONE;
   }
   
   //--- write
   
   dnErr = dn_write(
      OWI_DEVICE,
      (char*)onewire_app_vars.owi_buff,
      numBytes
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   return DN_ERR_NONE;
}

//===== 'wb' (write bit) CLI command

dn_error_t cli_writeBitCmdHandler(INT8U* arg, INT32U len) {
   dn_error_t                dnErr;
   int                       bitToWrite;
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
   owi_writeBit(bitToWrite);
   
   return DN_ERR_NONE;
}

//===== 'r' (read) CLI command

dn_error_t cli_readCmdHandler(INT8U* arg, INT32U len) {
   int        numBytes;
   int        numRead;
   int        l;
   
   //--- param 0: numBytes
   
   l = sscanf (arg, "%d", &numBytes);
   if (l < 1 || numBytes > sizeof(onewire_app_vars.owi_buff)) {
      dnm_ucli_printf("Usage: r [1-8]\r\n\r\n");
      dnm_ucli_printf("       Read up to 8 bytes from device\r\n");
      return DN_ERR_NONE;
   }
   
   //--- read
   
   numRead = dn_read(
      OWI_DEVICE,
      (char*)onewire_app_vars.owi_buff,
      numBytes
   );
   ASSERT(numRead==numBytes);
   
   //--- print
   
   dnm_ucli_printf("(%d bytes)",len);
   dnm_ucli_printBuf(onewire_app_vars.owi_buff,numBytes);
   dnm_ucli_printf("\r\n");

   return DN_ERR_NONE;
}

//===== 'rb' (read bit) CLI command

dn_error_t cli_readBitCmdHandler(INT8U* arg, INT32U len) {
   dn_error_t dnErr;
   dn_ow_ioctl_readbit_t  dn_ow_ioctl_readbit;
   INT8U      bitToRead;
   
   //--- read
   bitToRead = owi_readBit();
   dnm_ucli_printf("%d\r\n",bitToRead);
   
   return DN_ERR_NONE;
}

//===== 's' (search) CLI command

dn_error_t cli_searchCmdHandler(INT8U* arg, INT32U len) {
   INT8U      counter;
   BOOLEAN    result;
   
   dnm_ucli_printf("Find all devices\r\n");
   
   counter = 0;
   result = owi_first();
   while (result)
   {
      // print device found
      dnm_ucli_printf("\r\n %d - ",++counter);
      dnm_ucli_printBuf(onewire_app_vars.ROM_NO, OWI_ADDR_SIZE);
      result = owi_next();
   }
   
   dnm_ucli_printf("\r\n\r\nfound %d device(s)\r\n",counter);
   
   return DN_ERR_NONE;
}

//===== 'ff' (family find) CLI command
// find all onewire devices has the specified family code
dn_error_t cli_familyFindCmdHandler(INT8U* arg, INT32U len) {
   INT8S      numBytes;
   INT8U      counter;
   INT8U      familyCode;

  //--- param 0: family code to match
   
   numBytes = dnm_ucli_hex2byte(arg, onewire_app_vars.owi_buff, sizeof(onewire_app_vars.owi_buff));
   if (numBytes <= 0 || numBytes > sizeof(familyCode)) {
      dnm_ucli_printf("Usage: ff <familyCode>\r\n");
      dnm_ucli_printf("          <familyCode>: 00 - FF in hex format\r\n\r\n");
      dnm_ucli_printf("       Find all devices that has the specified family code\r\n");
      return DN_ERR_NONE;
   }

   familyCode = onewire_app_vars.owi_buff[0];
   // find only specified family code
   dnm_ucli_printf("Find devices with family code 0x%2X\r\n", familyCode);
   counter = 0;
   owi_targetSetup(familyCode);
   while (owi_next())
   {
      // check for incorrect type
     if (onewire_app_vars.ROM_NO[0] != familyCode) {
         break;
     }
      
      // print device found
      dnm_ucli_printf("\r\n %d - ",++counter);
      dnm_ucli_printBuf(onewire_app_vars.ROM_NO, OWI_ADDR_SIZE);
   }

   dnm_ucli_printf("\r\n\r\nfound %d device(s)\r\n",counter);
   
   return DN_ERR_NONE;
}


//===== 'fs' (family skip) CLI command
// find all except the device has the specified family code

dn_error_t cli_familySkipCmdHandler(INT8U* arg, INT32U len) {
   INT8S      numBytes;
   INT8U      counter;
   INT8U      familyCode;
   BOOLEAN    result;

  //--- param 0: family code to skip
   
   numBytes = dnm_ucli_hex2byte(arg, onewire_app_vars.owi_buff, sizeof(onewire_app_vars.owi_buff));
   if (numBytes <= 0 || numBytes > sizeof(familyCode)) {
      dnm_ucli_printf("Usage: fs <familyCode>\r\n");
      dnm_ucli_printf("          <familyCode>: 00 - FF in hex format\r\n\r\n");
      dnm_ucli_printf("       Skip all devices that have the specified family code\r\n");
      return DN_ERR_NONE;
   }

   familyCode = onewire_app_vars.owi_buff[0];

   // find only specified family code
   dnm_ucli_printf("Skip devices with family code 0x%2X\r\n", familyCode);
   counter = 0;
   result = owi_first();
   while (result) {
      // check for incorrect type
      if (onewire_app_vars.ROM_NO[0] == familyCode) {
         owi_familySkipSetup();
      }
      else {
         // print device found
         dnm_ucli_printf("\r\n %d - ",++counter);
         dnm_ucli_printBuf(onewire_app_vars.ROM_NO, OWI_ADDR_SIZE);
      }
      result = owi_next();
   }

   dnm_ucli_printf("\r\n\r\nfound %d device(s)\r\n",counter);
   
   return DN_ERR_NONE;
}


//--------------------------------------------------------------------------
// Find the 'first' devices on the 1-Wire bus
// Return TRUE  : device found, ROM number in ROM_NO buffer
//        FALSE : no device present
//
BOOLEAN owi_first()
{
   // reset the search state
   onewire_app_vars.lastDiscrepancy = 0;
   onewire_app_vars.fLastDeviceFlag = FALSE;
   onewire_app_vars.lastFamilyDiscrepancy = 0;

   return owi_search();
}

//--------------------------------------------------------------------------
// Find the 'next' devices on the 1-Wire bus
// Return TRUE  : device found, ROM number in ROM_NO buffer
//        FALSE : device not found, end of search
//
BOOLEAN owi_next()
{
   // leave the search state alone
   return owi_search();
}

//--------------------------------------------------------------------------
// Perform the 1-Wire Search Algorithm on the 1-Wire bus using the existing
// search state.
// Return TRUE  : device found, ROM number in ROM_NO buffer
//        FALSE : device not found, end of search
//
BOOLEAN owi_search()
{
   int id_bit_number;
   int last_zero, rom_byte_number, search_result;
   int id_bit, cmp_id_bit;
   unsigned char rom_byte_mask, search_direction;

   // initialize for search
   id_bit_number = 1;
   last_zero = 0;
   rom_byte_number = 0;
   rom_byte_mask = 1;
   search_result = 0;
   onewire_app_vars.crc8 = 0;

   // if the last call was not the last one
   if (!onewire_app_vars.fLastDeviceFlag) {
      // 1-Wire reset
      if (!owi_reset()) {
         // reset the search
         owi_resetCounters();
         return FALSE;
      }

      // issue the search command 
      owi_writeByte(0xF0);  

      // loop to do the search
      do {
         // read a bit and its complement
         id_bit = owi_readBit();
         cmp_id_bit = owi_readBit();

         // check for no devices on 1-wire
         if ((id_bit == 1) && (cmp_id_bit == 1)) {
            break;
         }
         else {
            // all devices coupled have 0 or 1
            if (id_bit != cmp_id_bit) {
               search_direction = id_bit;  // bit write value for search
            }
            else {
               // if this discrepancy if before the Last Discrepancy
               // on a previous next then pick the same as last time
               if (id_bit_number < onewire_app_vars.lastDiscrepancy) {
                  search_direction = ((onewire_app_vars.ROM_NO[rom_byte_number] & rom_byte_mask) > 0);
               }
               else {
                  // if equal to last pick 1, if not then pick 0
                  search_direction = (id_bit_number == onewire_app_vars.lastDiscrepancy);
               }

               // if 0 was picked then record its position in LastZero
               if (search_direction == 0) {
                  last_zero = id_bit_number;

                  // check for Last discrepancy in family
                  if (last_zero < 9) {
                     onewire_app_vars.lastFamilyDiscrepancy = last_zero;
                  }
               }
            }

            // set or clear the bit in the ROM byte rom_byte_number
            // with mask rom_byte_mask
            if (search_direction == 1) {
               onewire_app_vars.ROM_NO[rom_byte_number] |= rom_byte_mask;
            }
            else {
               onewire_app_vars.ROM_NO[rom_byte_number] &= ~rom_byte_mask;
            }

            // serial number search direction write bit
            owi_writeBit(search_direction);

            // increment the byte counter id_bit_number
            // and shift the mask rom_byte_mask
            id_bit_number++;
            rom_byte_mask <<= 1;

            // if the mask is 0 then go to new SerialNum byte rom_byte_number and reset mask
            if (rom_byte_mask == 0) {
                owi_doCrc8(onewire_app_vars.ROM_NO[rom_byte_number]);  // accumulate the CRC
                rom_byte_number++;
                rom_byte_mask = 1;
            }
         }
      }
      while(rom_byte_number < OWI_ADDR_SIZE);  // loop until through all ROM bytes 0-7

      // if the search was successful then
      if (!((id_bit_number < OWI_ADDR_LAST_BIT + 1) || (onewire_app_vars.crc8 != 0))) {
         // search successful so set onewire_app_vars.lastDiscrepancy,onewire_app_vars.fLastDeviceFlag,search_result
         onewire_app_vars.lastDiscrepancy = last_zero;

         // check for last device
         if (onewire_app_vars.lastDiscrepancy == 0) {
            onewire_app_vars.fLastDeviceFlag = TRUE;
         }
         
         search_result = TRUE;
      }
   }

   // if no device found then reset counters so next 'search' will be like a first
   if (!search_result || !onewire_app_vars.ROM_NO[0]) {
      owi_resetCounters();
      search_result = FALSE;
   }

   return search_result;
}

//--------------------------------------------------------------------------
// Verify the device with the ROM number in ROM_NO buffer is present.
// Return TRUE  : device verified present
//        FALSE : device not present
//
BOOLEAN owi_verify()
{
   unsigned char rom_backup[OWI_ADDR_SIZE];
   int result,ld_backup,ldf_backup,lfd_backup;

   // keep a backup copy of the current state
   memcpy(rom_backup, onewire_app_vars.ROM_NO, OWI_ADDR_SIZE);

   ld_backup = onewire_app_vars.lastDiscrepancy;
   ldf_backup = onewire_app_vars.fLastDeviceFlag;
   lfd_backup = onewire_app_vars.lastFamilyDiscrepancy;

   // set search to find the same device
   onewire_app_vars.lastDiscrepancy = OWI_ADDR_LAST_BIT;
   onewire_app_vars.fLastDeviceFlag = FALSE;

   if (owi_search()) {
      // check if same device found
      result = TRUE;
      if (memcmp(rom_backup, onewire_app_vars.ROM_NO, OWI_ADDR_SIZE) != 0) {
         result = FALSE;
      }
   }
   else {
     result = FALSE;
   }

   // restore the search state 
   memcpy(onewire_app_vars.ROM_NO, rom_backup, OWI_ADDR_SIZE);

   onewire_app_vars.lastDiscrepancy = ld_backup;
   onewire_app_vars.fLastDeviceFlag = ldf_backup;
   onewire_app_vars.lastFamilyDiscrepancy = lfd_backup;

   // return the result of the verify
   return result;
}

//--------------------------------------------------------------------------
// Setup the search to find the device type 'family_code' on the next call
// to owi_next() if it is present.
//
void owi_targetSetup(unsigned char family_code)
{

   // set the search state to find SearchFamily type devices
   memset(onewire_app_vars.ROM_NO, 0, OWI_ADDR_SIZE);
   onewire_app_vars.ROM_NO[0] = family_code;
   onewire_app_vars.lastDiscrepancy = OWI_ADDR_LAST_BIT;
   onewire_app_vars.lastFamilyDiscrepancy = 0;
   onewire_app_vars.fLastDeviceFlag = FALSE;
}

//--------------------------------------------------------------------------
// Setup the search to skip the current device type on the next call
// to owi_next().
//
void owi_familySkipSetup()
{
   // set the Last discrepancy to last family discrepancy
   onewire_app_vars.lastDiscrepancy = onewire_app_vars.lastFamilyDiscrepancy;
   onewire_app_vars.lastFamilyDiscrepancy = 0;

   // check for end of list
   if (onewire_app_vars.lastDiscrepancy == 0) {
      onewire_app_vars.fLastDeviceFlag = TRUE;
   }
}

//--------------------------------------------------------------------------
// 1-Wire Functions to be implemented for a particular platform
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
// Reset the 1-Wire bus and return the presence of any device
// Return TRUE  : device present
//        FALSE : no device present
//
BOOLEAN owi_reset()
{
   dn_error_t           dnErr;
   dn_ow_ioctl_detect_t dn_ow_ioctl_detect;
   
   dnErr = dn_ioctl(
      OWI_DEVICE,
      DN_IOCTL_1WIRE_DETECT,
      &dn_ow_ioctl_detect,
      sizeof(dn_ow_ioctl_detect_t)
   );
   if (dnErr!=DN_ERR_NONE) {
      dnm_ucli_printf("WARNING dn_ioctl() returns %d\r\n",dnErr);
      return FALSE;
   }
   return (dn_ow_ioctl_detect.slavePresent > 0);
}

//--------------------------------------------------------------------------
// Send 8 bits of data to the 1-Wire bus
//
void owi_writeByte(INT8U byte_value)
{
   dn_error_t           dnErr;

   dnErr = dn_write(
         OWI_DEVICE,
         (char*)&byte_value,
         sizeof(byte_value)
   );
   if (dnErr!=DN_ERR_NONE) {
      dnm_ucli_printf("WARNING dn_write() returns %d\r\n",dnErr);
   }
}

//--------------------------------------------------------------------------
// Send 1 bit of data to teh 1-Wire bus
//
void owi_writeBit(INT8U bit_value)
{
   dn_ow_ioctl_writebit_t  dn_ow_ioctl_writebit;
   dn_error_t              dnErr;
   
   dn_ow_ioctl_writebit.writeData = bit_value;
   dnErr = dn_ioctl(
      OWI_DEVICE,
      DN_IOCTL_1WIRE_WRITEBIT,
      &dn_ow_ioctl_writebit,
      sizeof(dn_ow_ioctl_writebit_t)
   );
   
   if (dnErr!=DN_ERR_NONE) {
      dnm_ucli_printf("WARNING dn_ioctl() returns %d\r\n",dnErr);
   }
   
}

//--------------------------------------------------------------------------
// Read 1 bit of data from the 1-Wire bus 
// Return 1 : bit read is 1
//        0 : bit read is 0
//
INT8U owi_readBit()
{
   INT8U                  readBit;
   dn_error_t             dnErr;
   
   readBit = 0;

   dnErr = dn_ioctl(
      OWI_DEVICE,
      DN_IOCTL_1WIRE_READBIT,
      &readBit,
      sizeof(readBit)
   );
   if (dnErr!=DN_ERR_NONE) {
      dnm_ucli_printf("WARNING dn_ioctl() returns %d\r\n",dnErr);
   }
   
   return readBit;
}



//--------------------------------------------------------------------------
// Calculate the CRC8 of the byte value provided with the current 
// global 'crc8' value. 
// Returns current global crc8 value
//
void owi_doCrc8(INT8U value)
{
   onewire_app_vars.crc8 = dscrc_table[onewire_app_vars.crc8 ^ value];
}

//--------------------------------------------------------------------------
// Reset these global variables used by 1-wire search
//
void owi_resetCounters()
{
   onewire_app_vars.lastDiscrepancy = 0;
   onewire_app_vars.fLastDeviceFlag = FALSE;
   onewire_app_vars.lastFamilyDiscrepancy = 0;
}

//=========================== helpers =========================================


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
