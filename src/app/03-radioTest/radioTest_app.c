/**
Copyright (c) 2015-16, Dust Networks.  All rights reserved.

This program implements 3 CLI commands to demonstrate the built-in radio testing functions.

Transmit test:
   Syntax: testtx <testType> <chanMask> <power> <repeatCnt> <stationId> <numSubtests> {<pkLen><delay>
      testType:    0 = PKT, 1 = CM, 2 = CW, 3 = PKCCA
      chanMask:    Channel mask is in hex, e.g. 0x0001 selects channel 0 (IEEE ch 11)
      power:       Transmit power in dBm
   The following only apply to PKT and PKCCA tests:
      stationId:   1-byte ID Used to filter packets from this mote on the receiving mote. 
      repeatCnt:   Number of times to repeat the sequence of subtests.
      numSubtests: 1-10 different sequences can be defined. For each sequence, the following are needed
      pkLen:       length of the packet (0-125 bytes)
      gap:         interpacket delay in us (up to 65565). Must be > pkLen * 32 us.
 
Receive test:
   Syntax: testrx <chanMask> <time> <stationID>
      chanMask:    Channel mask is in hex, e.g. 0x0001 selects channel 0 (IEEE ch 11)
      time:        listen time, in seconds
      stationId:   1-byte ID used to filter packets from a specific mote. 0 = do not filter. Applies to PKT and PKCCA tests. 
                   Will be set to 0 if ommitted.

View test statistics
   Syntax: stat

**/

// SDK includes
#include "dn_common.h"
#include "dn_api_common.h"
#include "dn_system.h"
#include "dn_gpio.h"
#include "dn_api_param.h"
#include "dn_exe_hdr.h"
#include "cli_task.h"
#include "loc_task.h"
#include "well_known_ports.h"
#include "Ver.h"

// project includes
#include "app_task_cfg.h"

// C includes
#include "string.h"
#include "stdio.h"

//=========================== definitions =====================================
#define MAX_SUBTESTS            10  // maximum number of transmit subtest descriptors
#define DEFAULT_REPS            100 // packets
//=========================== variables =======================================

typedef struct {
   OS_STK                    radioTestTaskStack[TASK_APP_RADIOTEST_STK_SIZE];
} radiotest_app_vars_t;

radiotest_app_vars_t rt_app_v;

// the packed version of this struct (dn_api_loc_testrftx_subtestparam_t) causes compiler 
// warnings when used in an array since elements may be at odd addresses
// note that the gap must be larger than pkLen * 32 microseconds
typedef struct {
   INT8U           pkLen;                        ///< Length of the packet to send. You must use a length between 2 and 125 bytes.
   INT16U          gap;                          ///< Delay between this packet and the next one, in micro-seconds.
} subtestparam_t;
   
//=========================== prototypes ======================================

//====tasks
static void radioTestTask(void* unused);

//=== Command Line Interface (CLI) handlers =======
dn_error_t cli_reset(const char* arg, INT32U len);
dn_error_t cli_radioTestTx(const char* arg, INT32U len);
dn_error_t cli_radioTestRx(const char* arg, INT32U len);
dn_error_t cli_radioTestStat(const char* arg, INT32U len);

//==== helpers
INT8U idleCheck(void);
void transmitTest(INT8U testType, INT16U chanMask, INT8S power, INT8U station, INT16U repCnt, INT8U numSubtests, subtestparam_t *subtests);
void receiveTest(INT16U chanMask, INT16U time, INT8U station);

//=========================== const  ==============================================
// Note - help strings for user defined CLI commands don't work in stack 1.0.3.28
const dnm_ucli_cmdDef_t cliCmdDefs[] = {
  {&cli_reset,                      "reset",          "reset",                                  DN_CLI_ACCESS_LOGIN }, 
  {&cli_radioTestTx,                "testtx",         "call with no args for syntax",           DN_CLI_ACCESS_LOGIN },
  {&cli_radioTestRx,                "testrx",         "testrx <chanMask> <time> <stationID>",   DN_CLI_ACCESS_LOGIN },
  {&cli_radioTestStat,              "stat",           "stat",                                   DN_CLI_ACCESS_LOGIN },
  {NULL,                            NULL,             NULL,                                     DN_CLI_ACCESS_NONE  },
};

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   INT8U           osErr;
   
   //===== initialize helper tasks
   
   cli_task_init(
      "radioTest",                          // appName
      cliCmdDefs                            // cliCmds
   );
   loc_task_init(
      JOIN_NO,                              // fJoin
      NETID_NONE,                           // netId
      UDPPORT_NONE,                         // udpPort
      NULL,                                 // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
 
   //===== initialize radioTestTask
   
   osErr = OSTaskCreateExt(
      radioTestTask,
      (void *) 0,
      (OS_STK*) (&rt_app_v.radioTestTaskStack[TASK_APP_RADIOTEST_STK_SIZE - 1]),
      TASK_APP_RADIOTEST_PRIORITY,
      TASK_APP_RADIOTEST_PRIORITY,
      (OS_STK*) rt_app_v.radioTestTaskStack,
      TASK_APP_RADIOTEST_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_RADIOTEST_PRIORITY, (INT8U*)TASK_APP_RADIOTEST_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

//========== CLI handler functions : These get called when a command is entered on the CLI =========
//===== CLI command to reset the mote
// @param arg = a string of arguments passed to the CLI command
// @param len = the length of the argument string
//
// @return  returns an error if dnm_loc_resetCmd fails
dn_error_t cli_reset(const char* arg, INT32U len){
   INT8U      rc;
   
   dnm_ucli_printf("Resetting...\r\n\n");
   
   // send reset to stack
   dnm_loc_resetCmd(&rc);
   ASSERT(rc == DN_API_RC_OK);
   
   return(DN_ERR_NONE);
}

//===== CLI command to invoke radioTest as transmitter
// @param arg = a string of arguments passed to the CLI command
// @param len = the length of the argument string
//
// @return  returns an error if incorrect arguments, otherwise DN_ERR_NONE
dn_error_t cli_radioTestTx(const char* arg, INT32U len){
   subtestparam_t                       subtests[MAX_SUBTESTS];                 // An array of subtest tuples
   INT8U                                testType=DN_API_RADIOTX_TYPE_PKT;       // One of PKT, CM, CW, or PKCCA - see API guide for details
   INT16U                               chanMask =0x0001;                       // A 16-bit channel mask, supplied in hex
   INT8S                                power = 8;                              // The transmit power, in dBm
   INT8U                                station = 255;                          // An 8-bit station ID to separate concurrent tests
   INT16U                               repCnt = DEFAULT_REPS;                  // the number of times the sequence(s) are to be repeated
   INT8U                                numSubtests = 1;                        // The number of subtests defined (1-10)
   INT8S                                length = -1;                            // The number of arguments read by sscanf
   char                                 *usageStr1;
   char                                 *usageStr2;
   
   usageStr1 = "Usage: testtx <testType> <chanMask> <power> <stationId> <repeatCnt> ... \r\n";
   usageStr2 = "... <numSubtests> {<pkLen> <delay> ...}\r\n";
   
   if(idleCheck()) {    
      // parse arguments. Syntax: testtx <testType> <chanMask> <power> <stationId> <repeatCnt> <numSubtests> {<pkLen><delay>...}
      // Note: This parser may behave incorrectly when presented with incorrectly formatted input
      length = sscanf(arg, "%hhu 0x%hx %hhd %hhu %hu %hhu %hhu %hu %hhu %hu %hhu %hu %hhu %hu %hhu %hu %hhu %hu %hhu %hu %hhu %hu %hhu %hu %hhu %hu",
                        &testType, &chanMask, &power, &station, &repCnt, &numSubtests,
                        &(subtests[0].pkLen), &(subtests[0].gap), &(subtests[1].pkLen), &(subtests[1].gap), &(subtests[2].pkLen), &(subtests[2].gap),
                        &(subtests[3].pkLen), &(subtests[3].gap), &(subtests[4].pkLen), &(subtests[4].gap), &(subtests[5].pkLen), &(subtests[5].gap),
                        &(subtests[6].pkLen), &(subtests[6].gap), &(subtests[7].pkLen), &(subtests[7].gap), &(subtests[8].pkLen), &(subtests[8].gap),
                        &(subtests[9].pkLen), &(subtests[9].gap));
      if(length == -1){
         dnm_ucli_printf("%s", usageStr1);
         dnm_ucli_printf("%s", usageStr2);
      }
      else{
         switch(testType) {
            // requesting a packet test, need at least 8 parameters
            case DN_API_RADIOTX_TYPE_PKT:    
            case DN_API_RADIOTX_TYPE_PKCCA:
               if (length < 8) {
                  // too few parameters - display help text
                  dnm_ucli_printf("Usage for packet (0) and PKCCA (3) tests: \r\n");
                  dnm_ucli_printf("%s", usageStr1);
                  dnm_ucli_printf("%s", usageStr2);
               }
               else if(((length - 6) != 2 * numSubtests)){
                     dnm_ucli_printf("Error: incorrect number of subtests are defined\r\n");
                  }
                  else {
                     dnm_ucli_printf("Test started...\r\n");
                     transmitTest(testType, chanMask, power, station, repCnt, numSubtests, subtests);
                  }     
            break;
            // requesting a CM or CW test, need 3 parameters
            case DN_API_RADIOTX_TYPE_CM:
            case DN_API_RADIOTX_TYPE_CW:
                  if(length != 3){
                     dnm_ucli_printf("Usage for CM (1) and CW (2) tests: testtx <testType> <chanMask> <power>\r\n");
                  }   
                  else {
                     dnm_ucli_printf("Continuous test started. You must reset the mote to start another test.\r\n");
                     transmitTest(testType, chanMask, power, 0 /*station*/, 0 /*repCnt*/, 0/*numSubtests*/, (void *)0L/*subtests*/);
                  }   
            break;
            //invalid test type   
            default:  
               dnm_ucli_printf("Error: invalid test type\r\n");
               dnm_ucli_printf("%s", usageStr1);
               dnm_ucli_printf("%s", usageStr2);
            break;
         }
      }
   }
   else {
      //print error message
      dnm_ucli_printf("Error: Can only run radio tests when mote is idle\r\n");
   }
   
   return(DN_ERR_NONE);
}

//===== CLI command to invoke radioTest as receiver
// @param arg = a string of arguments passed to the CLI command
// @param len = the length of the argument string
//
// @return  returns an error if incorrect arguments, otherwise DN_ERR_NONE
dn_error_t cli_radioTestRx(const char* arg, INT32U len){
   INT16U       chanMask;               // A 16-bit channel mask, supplied in hex
   INT16U       time;                   // Test time in seconds
   INT8U        station;                // An 8-bit station ID to separate concurrent tests
   int          length;                 // The number of arguments read by sscanf
   
   if(idleCheck()) {    
      // parse radiotest arguments - expects 3 parameters
      length = sscanf(arg, "0x%hx %hu %hhu", &chanMask, &time, &station);
       
      if (length == 3){
         // no station ID
         if(station == 0){
            dnm_ucli_printf("stationID = 0, accepting any\r\n"); 
         }
         receiveTest(chanMask, time, station);   
      }
      else {      
         dnm_ucli_printf("Usage: testrx <chanMask> <time (s)> <stationId>\r\n");
         dnm_ucli_printf("Channel mask is in hex, e.g. 0x0001 selects channel 0 (IEEE ch 11)\r\n");
      }
   }
   else {
      //print error message
      dnm_ucli_printf("Error: Can only run rftest when mote is idle.\r\n");
   }
   
   return(DN_ERR_NONE);
}

//===== CLI command to retrieve test statistics for receiver
// @param arg = a string of arguments passed to the CLI command
// @param len = the length of the argument string
//
// @return  returns DN_ERR_NONE
dn_error_t cli_radioTestStat(const char* arg, INT32U len){
   dn_error_t                   dnErr;                                          // Error code returned by getParameter command
   INT8U                        rc;                                             // response code for specific parameters used
   INT8U                        statusBuf[sizeof(dn_api_rsp_get_rfrxstats_t)];  // buffer to hold getParameter response
   INT8U                        respLen;                                        // size of getParameter response
   dn_api_rsp_get_rfrxstats_t*  results;                                        // the radio stats
   
   if(idleCheck()) {    
      dnErr = dnm_loc_getParameterCmd(
                                   DN_API_PARAM_TESTRADIORXSTATS,
                                   statusBuf,
                                   0,
                                   &respLen,
                                   &rc 
                                 );	
   
      ASSERT(dnErr==DN_ERR_NONE);                                  // something bad happened
      ASSERT(respLen == sizeof(dn_api_rsp_get_rfrxstats_t));       // returned data is not what we expected
   
      if(rc != DN_ERR_NONE ){
         switch (rc){       
            case DN_API_RC_BUSY:
               dnm_ucli_printf("Error: A test is already in progress...\r\n");
               break;
            default:
               dnm_ucli_printf("Error RC = %d\r\n", rc);
            }
      }    
      else{
            results = (dn_api_rsp_get_rfrxstats_t*)(&statusBuf[0]);
            dnm_ucli_printf("Radio Test Statistics:\r\n");
            dnm_ucli_printf("  OkCnt   : %d\r\n", ntohs(results->rxOkCnt));
            dnm_ucli_printf("  FailCnt : %d\r\n", ntohs(results->rxFailCnt));
      }
   }
   else {
      dnm_ucli_printf("Stats retrieved before a test has completed may not be meaningful\r\n");
   }
   
   return(dnErr);

}

//========================== helper functions ====================

// checks to see if mote is in idle state
INT8U idleCheck(void){
   
   dn_error_t                   dnErr;                                          // Error code for the getParameter call
   INT8U                        statusBuf[sizeof(dn_api_rsp_get_motestatus_t)]; // Buffer to receive getParam reply
   dn_api_rsp_get_motestatus_t  *currentStatus;                                 // Struct containing Mote's status
   INT8U                        respLen;                                        // Length of the getParam reply
   INT8U                        rc;                                             // Response code for the specific parameter requested
   
   // are we in IDLE?  (in this app yes, since we don't join, but need to check in general)
   dnErr = dnm_loc_getParameterCmd(
                                   DN_API_PARAM_MOTESTATUS,
                                   statusBuf,
                                   0,
                                   &respLen,
                                   &rc
                                   );
   
   currentStatus = (dn_api_rsp_get_motestatus_t*)(&statusBuf[0]);
   
   if((dnErr == DN_ERR_NONE) && (rc == DN_ERR_NONE)){
      if((currentStatus->state != DN_API_ST_IDLE) && (currentStatus->state != DN_API_ST_RADIOTEST)){
         return(FALSE);
      }
      else {
         return(TRUE);
      }
   } 
   
   // if we couldn't get the mote status, assume it's not safe to do rftest since something's seriously wrong
   return(FALSE);
   
}
// starts a trasmit test and parses RC
void transmitTest(INT8U testType, INT16U chanMask, INT8S power, INT8U station, INT16U repCnt,  INT8U numSubtests, subtestparam_t *subtests){
   
   dn_error_t                           dnErr;                          // Error code for the API call
   INT8U                                rc;                             // response code for the specific parameters used
   dn_api_loc_testrftx_subtestparam_t   tx_subtests[MAX_SUBTESTS];
   INT8U                                i;
   
   // copy passed subtests into packed struct array expected by testRadioTx command
   ASSERT(numSubtests < MAX_SUBTESTS);
   for(i=0; i< numSubtests; i++){    
      tx_subtests[i].pkLen = subtests[i].pkLen;
      tx_subtests[i].gap = subtests[i].gap;
   }
                   
   dnErr =  dnm_loc_testRadioTxCmd(
                                    testType,       // Type of test - pkt, cw, cm, or pkcca
                                    chanMask,       // channel mask
                                    power,          // transmit power in dBm
                                    station,        // station ID
                                    repCnt,         // repeat count
                                    numSubtests,    // number of subtests
                                    tx_subtests,    // pointer to array of subtests
                                    &rc             // response code
                                    );
   ASSERT(dnErr == DN_ERR_NONE);                   // something really bad happened 
   
   if(rc != DN_ERR_NONE ){
      switch (rc){
         case DN_API_RC_INVALID_VALUE:
            dnm_ucli_printf("Error: a parameter has an invalid value\r\n", rc);
            break;       
         case DN_API_RC_BUSY:
            dnm_ucli_printf("Error: A test is in progress...\r\n");
            break;
         default:
            dnm_ucli_printf("Error RC = %d\r\n", rc);
            break;
      }  
   }        
}

// starts a receive test and parses errors

void receiveTest(INT16U chanMask, INT16U time, INT8U station){
    
   dn_error_t   dnErr;                  // Error code for the API call
   INT8U        rc;                     // response code for the specific parameters used

   
   dnErr =  dnm_loc_testRadioRxCmd ( 
                                    chanMask,           // 16-bit channel mask
                                    time,               // seconds
                                    station,            // accept any station
                                    &rc 
                                    );
   ASSERT(dnErr == DN_ERR_NONE);               // something really bad happened
            
   if(rc != DN_ERR_NONE ){
      switch (rc){ 
         case DN_API_RC_INVALID_VALUE:
            dnm_ucli_printf("Error: a parameter has an invalid value\r\n");
            break;        
         case DN_API_RC_BUSY:
            dnm_ucli_printf("Error: A test is in progress...\r\n");
            break;
         default:
            dnm_ucli_printf("Error RC = %d\r\n", rc);
      }         
   }
}

//========================== idle task ====================
//
//   This task does nothing but wait

static void radioTestTask(void* unused) {
   
   OSTimeDly(SECOND);
   
   while (1) { // this is a task, it executes forever
     
      // wait a bit
      OSTimeDly(SECOND);
   }
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
