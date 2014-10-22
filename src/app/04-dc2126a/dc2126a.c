/**
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "string.h"
// devices and services
#include "dn_system.h"
#include "dn_fs.h"
#include "dn_spi.h"
#include "dn_gpio.h"
#include "dn_adc.h"
#include "dn_exe_hdr.h"
// application helpers
#include "cli_task.h"
#include "loc_task.h"
#include "well_known_ports.h"
// application configuration
#include "app_task_cfg.h"
#include "Ver.h"

//=========================== definitions =====================================

//===== configuration

#define APP_CONFIG_FILENAME       "2dc2126.cfg"            // file name holding configuration

#define DFLT_REPORTPERIOD         60000                    // in ms
#define DFLT_BRIDGESETTLINGTIME   100                      // in ms
#define DFLC_LDOONTIME            250                      // in ms

//===== pins

#define PIN_LOW                   0x00
#define PIN_HIGH                  0x01

// SPI
#define PIN_SCK                   DN_GPIO_PIN_9_DEV_ID
#define PIN_MOSI                  DN_GPIO_PIN_10_DEV_ID
#define PIN_MISO                  DN_GPIO_PIN_11_DEV_ID
#define PIN_SS                    DN_GPIO_PIN_12_DEV_ID

// LDO
#define PIN_LDO                   DN_GPIO_PIN_21_DEV_ID    // DP2

//===== timeout & durations

// inhibit radio can take up to 7.25ms to go into effect
#define DELAY_INHIBIT_RADIO       8                        // in ms

#define DELAY_CHANGE_FREQ         1                        // in ms
#define DELAY_CONVERSION_TIME_MAX 150                      // in ms

//===== communication

#define CMDID_BASE                0x2484                   // lowest command ID
#define BASE_ERROR_VALUE          0x0BAD                   // lowest error code

//=========================== structs & enums =================================

//===== command IDs

enum {
   CMDID_GET_CONFIG,              ///< Get the current configuration.
   CMDID_SET_CONFIG               ///< Set the configuration.
} rx_cmdId;

enum {
   CMDID_CONFIGURATION,           ///< Configuration.
   CMDID_REPORT                   ///< Report.
} tx_cmdId;

//===== error codes

enum {
   ERR_NO_SERVICE,                ///< No service available.
   ERR_NOT_ENOUGH_BW,             ///< Not enough bandwidth.
} tx_errorCode;

//===== message formats

PACKED_START

typedef struct{
   INT16U cmdId;                  ///< Command identifier.
   INT32U reportPeriod;           ///< Report period, in ms.
   INT32U bridgeSettlingTime;     ///< Bridge settling duration, in ms.
   INT32U ldoOnTime;              ///< LDO on duration, in ms.
} configuration_ht;

typedef struct{
   INT16U cmdId;                  ///< Command identifier.
   INT32U temperature;            ///< Temperature reading.
   INT16U adcValue;               ///< ADC reading.
} report_ht;

typedef struct{
   INT16U errorCode;              ///< Error code.
   INT32U bw;                     ///< Maximum available bandwidth
} error_ht;

PACKED_STOP

//=========================== variables =======================================

typedef struct{
   INT32U reportPeriod;           ///< Report period, in ms.
   INT32U bridgeSettlingTime;     ///< Bridge settling duration, in ms.
   INT32U ldoOnTime;              ///< LDO on duration, in ms.
} app_cfg_t;

typedef struct {
   // admin
   OS_EVENT*       joinedSem;          ///< Posted when mote has joined the network.
   app_cfg_t       app_cfg;
   // report task
   OS_STK          reportTaskStack[TASK_APP_REPORT_STK_SIZE];
   OS_TMR*         reportTimer;        ///< Triggers transmissions of report.
   OS_EVENT*       reportSem;          ///< Posted when time to send a new report.
   INT32U          numReportsSent;     ///< Number of reports sent so far.
   // LDO pin task
   OS_STK          ldoPinTaskStack[TASK_APP_LDOPIN_STK_SIZE];
   OS_EVENT*       ldoSem;
} app_vars_t;

app_vars_t app_vars;

//=========================== prototypes ======================================

//===== report task

static void   reportTask(void* unused);
void          reportTimer_cb(void* pTimer, void *pArgs);
void          sendReport(INT32U temperature,INT16U adcVal);

//===== LDO pin task

static void   ldoPinTask(void* unused);

//===== network interaction

dn_error_t    rxNotif(dn_api_loc_notif_received_t* rxFrame, INT8U length);
void          sendConfiguration();
void          sendError(INT16U ErrorCode, INT32U BW);

//===== helpers

// configuration file
void          loadConfigFile();
void          syncToConfigFile();
void          printConfiguration();

// LTC2484
void          ltc2484_setup();
INT32U        ltc2484_read();

// SPI
INT8U         spiOpen();
INT32U        spiRead();
INT8U         spiClose();

// GPIO
void          gpioSetMode(INT8U pin,INT8U mode);
void          gpioWrite(INT8U pin,INT8U value);

// power source ADC
void          openPowerSourceADC();
INT16U        readPowerSourceADC();

// radio inhibit
void          inhibitRadio();
void          enableRadio();

//=========================== initialization ==================================

int p2_init(void){
   INT8U           osErr;

   // create semaphores
   app_vars.joinedSem   = OSSemCreate(0);   
   app_vars.reportSem   = OSSemCreate(1);   // "1" so mote sends report as soon as operational
   app_vars.ldoSem      = OSSemCreate(0);
   
   //==== initialize helper tasks
   cli_task_init(
      "DC2126a",                            // appName
      NULL                                  // cliCmds
   );
   
   loc_task_init(
      JOIN_YES,                             // fJoin
      NULL,                                 // netId
      WKP_DC2126A,                          // udpPort
      app_vars.joinedSem,                   // joinedSem
      BANDWIDTH_NONE,                       // bandwidth
      NULL                                  // serviceSem
   );
   
   //===== register a callback to receive packets
   dnm_loc_registerRxNotifCallback(rxNotif);
   
   //===== create report task
   osErr = OSTaskCreateExt(
      reportTask,
      (void *) 0,
      (OS_STK*) (&app_vars.reportTaskStack[TASK_APP_REPORT_STK_SIZE-1]),
      TASK_APP_REPORT_PRIORITY,
      TASK_APP_REPORT_PRIORITY,
      (OS_STK*) app_vars.reportTaskStack,
      TASK_APP_REPORT_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   
   OSTaskNameSet(
      TASK_APP_REPORT_PRIORITY, 
      (INT8U*)TASK_APP_REPORT_NAME, 
      &osErr
   );
   ASSERT(osErr == OS_ERR_NONE);
   
   //===== create LDO pin task
   osErr = OSTaskCreateExt(
      ldoPinTask,
      (void *) 0,
      (OS_STK*) (&app_vars.ldoPinTaskStack[TASK_APP_LDOPIN_STK_SIZE-1]),
      TASK_APP_LDOPIN_PRIORITY,
      TASK_APP_LDOPIN_PRIORITY,
      (OS_STK*) app_vars.ldoPinTaskStack,
      TASK_APP_LDOPIN_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   
   OSTaskNameSet(
      TASK_APP_LDOPIN_PRIORITY, 
      (INT8U*)TASK_APP_LDOPIN_NAME, 
      &osErr
   );
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== report task =====================================

static void reportTask(void* unused){
   INT8U                     osErr;
   INT32U                    ltc2484Val;
   INT16U                    powerSourceAdcVal;

   // load configuration
   loadConfigFile();
   printConfiguration();
   
   // wait for the loc_task to finish joining the network
   OSSemPend(app_vars.joinedSem, 0, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   // initialize ltc2484 internal variables
   ltc2484_setup();

   // open ADC device
   openPowerSourceADC();

   // create report timer
   app_vars.reportTimer = OSTmrCreate(
      app_vars.app_cfg.reportPeriod,        // dly
      app_vars.app_cfg.reportPeriod,        // period
      OS_TMR_OPT_PERIODIC,                  // opt
      (OS_TMR_CALLBACK)&reportTimer_cb,     // callback
      NULL,                                 // callback_arg
      NULL,                                 // pname
      &osErr                                // perr
   );
   ASSERT(osErr==OS_ERR_NONE);
   
   // start report timer
   OSTmrStart(app_vars.reportTimer, &osErr);
   ASSERT (osErr == OS_ERR_NONE);
   
   while (1) { // this is a task, it executes forever
      
      // wait for new event
      OSSemPend(app_vars.reportSem, 0, &osErr);
      
      // read the power source
      powerSourceAdcVal = readPowerSourceADC();
      
      // inhibit the Radio
      inhibitRadio();
      OSTimeDly(DELAY_INHIBIT_RADIO);
      
      // power up reference
      OSSemPost(app_vars.ldoSem);
      
      // wait for bridge settling time
      OSTimeDly(app_vars.app_cfg.bridgeSettlingTime);
      
      // read temperature value
      ltc2484Val        = ltc2484_read();
      
      // release the radio
      enableRadio();
      
      // send report
      sendReport(
         ltc2484Val,
         powerSourceAdcVal
      );
   }
}

void reportTimer_cb(void* pTimer, void *pArgs){
   OSSemPost(app_vars.reportSem);
}

void sendReport(INT32U ltc2484Val,INT16U powerSourceAdcVal){
   dn_error_t      dnErr;
   INT8U           osErr;
   loc_sendtoNW_t* pkToSend;
   INT8U           rc;
   INT8U           pkBuf[sizeof(loc_sendtoNW_t)+sizeof(report_ht)];
   report_ht       payload;
   INT16U          cmdId;
   
   // prepare packet to send
   pkToSend = (loc_sendtoNW_t*)pkBuf;
   pkToSend->locSendTo.socketId          = loc_getSocketId();
   pkToSend->locSendTo.destAddr          = DN_MGR_IPV6_MULTICAST_ADDR;
   pkToSend->locSendTo.destPort          = WKP_DC2126A;
   pkToSend->locSendTo.serviceType       = DN_API_SERVICE_TYPE_BW;   
   pkToSend->locSendTo.priority          = DN_API_PRIORITY_MED;   
   pkToSend->locSendTo.packetId          = 0xFFFF; // 0xFFFF=no notification
   
   // create payload
   cmdId = CMDID_BASE+CMDID_REPORT;
   payload.cmdId             = htons(cmdId);
   payload.temperature       = htonl(ltc2484Val);
   payload.adcValue          = htons(powerSourceAdcVal);
   
   // insert payload into packet
   memcpy(pkToSend->locSendTo.payload,&payload,sizeof(report_ht));
   
   // send packet
   dnErr = dnm_loc_sendtoCmd(
      pkToSend,
      sizeof(report_ht),
      &rc
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
   // print
   if (rc==DN_API_RC_OK) {
      dnm_ucli_printf("Message %d sent\r\n",++app_vars.numReportsSent);
   } else {
      dnm_ucli_printf("rc = 0x%02x\r\n",rc);
   }
}

//=========================== LDO pin task ====================================

static void ldoPinTask(void* unused) {
   INT8U       osErr;
   
   // configure DP2 as a output pin
   gpioSetMode(PIN_LDO,DN_IOCTL_GPIO_CFG_OUTPUT);
   
   // set pin low (default state)
   gpioWrite(PIN_LDO,PIN_LOW);
   
   while (1) { // this is a task, it executes forever
      
      // wait for ldoSem (blocking)
      OSSemPend(app_vars.ldoSem, 0, &osErr);
      
      // set LDO pin high
      gpioWrite(PIN_LDO,PIN_HIGH);
      
      // wait for LDO to settle
      OSTimeDly(app_vars.app_cfg.ldoOnTime);
      
      // set LDO pin low
      gpioWrite(PIN_LDO,PIN_LOW);
   }
}

//=========================== network interaction =============================

dn_error_t rxNotif(dn_api_loc_notif_received_t* rxFrame, INT8U length) {
   INT8U                          osErr;
   INT8U                          payloadLen;
   INT16U                         cmdId;
   dn_error_t                     dnErr;
   dn_api_loc_rsp_get_service_t   currentService;
   configuration_ht*              setConfigCmd;
   INT32U                         reportPeriod;
   INT32U                         bridgeSettlingTime;
   INT32U                         ldoOnTime;
   
   // calc data size
   payloadLen = length-sizeof(dn_api_loc_notif_received_t);
   
   // load msg structure
   memcpy(&cmdId,rxFrame->data,sizeof(cmdId));
   
   cmdId = ntohs(cmdId) - CMDID_BASE;
   
   if       (cmdId == CMDID_GET_CONFIG){
      // send current configuration
     
      dnm_ucli_printf("GET configuration\r\n"); 
     
      // verify length
      if (payloadLen!=sizeof(cmdId)) {
         dnm_ucli_printf("ERROR: wrong length\r\n");
         return DN_ERR_ERROR;
      }
      
      // print
      
      printConfiguration();
      
      // send configuration
      sendConfiguration();
   
   } else if (cmdId == CMDID_SET_CONFIG) {
      // set configuration
      
      dnm_ucli_printf("SET configuration\r\n");
      
      // verify length
      if (payloadLen!=sizeof(configuration_ht)) {
         dnm_ucli_printf("ERROR: wrong length\r\n");
         return DN_ERR_ERROR;
      }
      
      // parse payload
      setConfigCmd           = (configuration_ht*)rxFrame->data;
      reportPeriod           = ntohl(setConfigCmd->reportPeriod);
      bridgeSettlingTime     = ntohl(setConfigCmd->bridgeSettlingTime);
      ldoOnTime              = ntohl(setConfigCmd->ldoOnTime);
      
      // print
      dnm_ucli_printf("- reportPeriod:          %d\r\n",reportPeriod);
      dnm_ucli_printf("- bridgeSettlingTime:    %d\r\n",bridgeSettlingTime);
      dnm_ucli_printf("- ldoOnTime:             %d\r\n",ldoOnTime);
      
      // retrieve service information
      dnErr = dnm_loc_getAssignedServiceCmd(
         DN_MGR_SHORT_ADDR,       // destAddr
         DN_API_SERVICE_TYPE_BW,  // svcType
         &currentService          // svcRsp 
      );
      if (currentService.rc==DN_API_RC_NOT_FOUND){
         dnm_ucli_printf("No service information available yet.\r\n");  
         sendError(ERR_NO_SERVICE,0);
         return dnErr;
      } else{
         ASSERT(currentService.rc==DN_API_RC_OK);
      }
      
      // apply new configuration, if application
      if (reportPeriod>=currentService.value){
         // enough bandwidth: configuration valid.
         
         // store new configuration
         app_vars.app_cfg.reportPeriod           = reportPeriod;
         app_vars.app_cfg.bridgeSettlingTime     = bridgeSettlingTime;
         app_vars.app_cfg.ldoOnTime              = ldoOnTime;
         syncToConfigFile();
         
         // modify timer
         OSTmrStop(app_vars.reportTimer,OS_TMR_OPT_NONE,NULL,&osErr);
         ASSERT (osErr == OS_ERR_NONE);
         
         app_vars.reportTimer->OSTmrDly     = reportPeriod;
         app_vars.reportTimer->OSTmrPeriod  = reportPeriod;
         
         OSTmrStart(app_vars.reportTimer, &osErr);
         ASSERT (osErr == OS_ERR_NONE);
      
      } else {
         // NOT enough bandwidth: SET command fails.
         
         dnm_ucli_printf("SET config failed. Minimum reportPeriod %d\r\n.",currentService.value);
         sendError(ERR_NOT_ENOUGH_BW,currentService.value);  
      }
   }
   
   return DN_ERR_NONE;
}

void sendConfiguration(){
   dn_error_t           dnErr;
   loc_sendtoNW_t*      pkToSend;
   INT8U                rc;
   INT8U                pkBuf[sizeof(loc_sendtoNW_t)+sizeof(configuration_ht)];
   configuration_ht     payload;
   INT16U               cmdId;
   
   // prepare packet to send
   pkToSend = (loc_sendtoNW_t*)pkBuf;
   pkToSend->locSendTo.socketId        = loc_getSocketId();
   pkToSend->locSendTo.destAddr        = DN_MGR_IPV6_MULTICAST_ADDR;
   pkToSend->locSendTo.destPort        = WKP_DC2126A;
   pkToSend->locSendTo.serviceType     = DN_API_SERVICE_TYPE_BW;   
   pkToSend->locSendTo.priority        = DN_API_PRIORITY_MED;   
   pkToSend->locSendTo.packetId        = 0xFFFF; // 0xFFFF=no notification
   
   // create payload
   cmdId                               = CMDID_BASE+CMDID_CONFIGURATION;
   payload.cmdId                       = htons(cmdId);
   payload.reportPeriod                = ntohl(app_vars.app_cfg.reportPeriod);
   payload.bridgeSettlingTime          = ntohl(app_vars.app_cfg.bridgeSettlingTime);
   payload.ldoOnTime                   = ntohl(app_vars.app_cfg.ldoOnTime);

   // insert payload into packet
   memcpy(pkToSend->locSendTo.payload,&payload,sizeof(configuration_ht));
    
   // send packet
   dnErr = dnm_loc_sendtoCmd(
      pkToSend,
      sizeof(configuration_ht),
      &rc
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
#ifdef DEBUG   
   printConfiguration();
   //Print operation result
   if (rc==DN_API_RC_OK) {
      dnm_ucli_printf("Configuration reported\r\n");
   } else {
      dnm_ucli_printf("rc = 0x%02x\r\n",rc);
   }
#endif
}

void sendError(INT16U ErrorCode,INT32U BW){
   dn_error_t      dnErr;
   INT8U           pkBuf[sizeof(loc_sendtoNW_t)+sizeof(error_ht)];
   loc_sendtoNW_t* pkToSend;
   INT8U           rc;
   error_ht     payload;
   
   // prepare packet to send
   pkToSend = (loc_sendtoNW_t*)pkBuf;
   pkToSend->locSendTo.socketId        = loc_getSocketId();
   pkToSend->locSendTo.destAddr        = DN_MGR_IPV6_MULTICAST_ADDR;
   pkToSend->locSendTo.destPort        = WKP_DC2126A;
   pkToSend->locSendTo.serviceType     = DN_API_SERVICE_TYPE_BW;   
   pkToSend->locSendTo.priority        = DN_API_PRIORITY_MED;   
   pkToSend->locSendTo.packetId        = 0xFFFF; // 0xFFFF=no notification
   
   // create payload
   payload.errorCode   = htons(BASE_ERROR_VALUE+ErrorCode);
   payload.bw          = htonl(BW);
   
   memcpy(pkToSend->locSendTo.payload,&payload, sizeof(error_ht));
   
   // send packet
   dnErr = dnm_loc_sendtoCmd(
      pkToSend,
      sizeof(error_ht),
      &rc
   );
   ASSERT(dnErr==DN_ERR_NONE);
   
#ifdef DEBUG   
   //Print operation result
   if (rc==DN_API_RC_OK) {
      dnm_ucli_printf("Error reported\r\n");
   } else {
      dnm_ucli_printf("rc = 0x%02x\r\n",rc);
   }
#endif
}

//=========================== helpers =========================================

//===== configuration file

void loadConfigFile(){
   dn_error_t        dnErr;
   dn_fs_handle_t    configFileHandle;
   
   configFileHandle = dn_fs_find(APP_CONFIG_FILENAME);
   
   if (configFileHandle>=0) {
      // file found: read it
      
      // open file
      configFileHandle = dn_fs_open(
         APP_CONFIG_FILENAME,
         DN_FS_OPT_CREATE,
         sizeof(app_cfg_t),
         DN_FS_MODE_OTH_RW
      );
      ASSERT(configFileHandle >= 0);
      
      // read file
      dnErr = dn_fs_read(
         configFileHandle,
         0, // offset
         (INT8U*)&(app_vars.app_cfg),
         sizeof(app_cfg_t)
      );
      ASSERT(dnErr>=0);
       
      // close file
      dn_fs_close(configFileHandle);
   
   } else {
      // file not found: create it
      
      // prepare file content
      app_vars.app_cfg.reportPeriod              = DFLT_REPORTPERIOD;
      app_vars.app_cfg.bridgeSettlingTime        = DFLT_BRIDGESETTLINGTIME;
      app_vars.app_cfg.ldoOnTime                 = DFLC_LDOONTIME;

      // create file
      dnm_ucli_printf("Create config file\r\n");
      configFileHandle = dn_fs_open(
         APP_CONFIG_FILENAME,
         DN_FS_OPT_CREATE,
         sizeof(app_cfg_t),
         DN_FS_MODE_SHADOW
      );
      ASSERT(configFileHandle>=0);
      
      // write file
      dnErr = dn_fs_write(
         configFileHandle,
         0, // offset
         (INT8U*)&(app_vars.app_cfg),
         sizeof(app_cfg_t)
      );
      ASSERT(dnErr >= 0);
      
      // close file
      dn_fs_close(configFileHandle);
   }
}

void syncToConfigFile() {
   dn_error_t          dnErr;
   dn_fs_handle_t      configFileHandle;
   
   // open file
   configFileHandle = dn_fs_open(
      APP_CONFIG_FILENAME,
      DN_FS_OPT_CREATE,
      sizeof(app_cfg_t),
      DN_FS_MODE_OTH_RW
   );
   ASSERT(configFileHandle >= 0);
   
   // write file
   dnErr = dn_fs_write(
      configFileHandle,
      0, // offset
      (INT8U*)&(app_vars.app_cfg),
      sizeof(app_cfg_t)
   );
   ASSERT(dnErr >= 0);
   
   // close file
   dn_fs_close(configFileHandle);
}

void printConfiguration() {
   dnm_ucli_printf("Current configuration:\r\n");
   dnm_ucli_printf(" - reportPeriod:         %d\r\n",
      app_vars.app_cfg.reportPeriod
   );
   dnm_ucli_printf(" - bridgeSettlingTime:   %d\r\n",
      app_vars.app_cfg.bridgeSettlingTime
   );
   dnm_ucli_printf(" - ldoOnTime:            %d\r\n",
      app_vars.app_cfg.ldoOnTime
   );
}

//===== LTC2484

void ltc2484_setup() {
   
   // configure pin mode
   gpioSetMode(PIN_SS,   DN_IOCTL_GPIO_CFG_OUTPUT);
   gpioSetMode(PIN_SCK,  DN_IOCTL_GPIO_CFG_OUTPUT);
   gpioSetMode(PIN_MOSI, DN_IOCTL_GPIO_CFG_OUTPUT);
   gpioSetMode(PIN_MISO, DN_IOCTL_GPIO_CFG_INPUT);
   
   // default pin value
   gpioWrite(PIN_SS,     PIN_HIGH);
   gpioWrite(PIN_SCK,    PIN_HIGH);
   gpioWrite(PIN_MOSI,   PIN_LOW);
}

INT32U ltc2484_read() {
    
   INT32U ltc2484Raw;
    
   // initialize state
   ltc2484Raw = 0;
   gpioWrite(PIN_SCK,   PIN_HIGH);
   gpioWrite(PIN_SS,    PIN_HIGH);
   OSTimeDly(DELAY_CHANGE_FREQ);
    
   // clear stale data and start conversion
   gpioWrite(PIN_SS,    PIN_LOW);
   OSTimeDly(DELAY_CHANGE_FREQ);
   gpioWrite(PIN_SCK,   PIN_LOW);
   OSTimeDly(DELAY_CHANGE_FREQ);
   gpioWrite(PIN_SS,    PIN_HIGH);
    
   // wait for conversion to finish
   gpioWrite(PIN_SCK,   PIN_LOW);    
   OSTimeDly(DELAY_CHANGE_FREQ);
   gpioWrite(PIN_SS,    PIN_LOW);
   OSTimeDly(DELAY_CONVERSION_TIME_MAX);
   
   // read temperature value
   ltc2484Raw = spiRead();
   
   // end state
   gpioWrite(PIN_SS, PIN_HIGH);

#ifdef DEBUG
   INT16U bits28to19;
   INT16U bits18to10;
   
   bits28to19 = (0x01FF&(ltc2484Raw>>19));
   bits18to10 = (0x01FF&(ltc2484Raw>>10));
   
   dnm_ucli_printf("bit 28 thru 19 value: 0x%02x\r\nbit 18 thru 10 value: 0x%02x\r\n",
      bits28to19,
      bits18to10
   );
#endif
   
   return ltc2484Raw;
}

//===== SPI

INT8U spiOpen() {
   
   dn_spi_open_args_t           spiOpenArgs;
   int                          err;   
   
   spiOpenArgs.maxTransactionLenForCPHA_1 = 0;
   // open the SPI device
   err = dn_open(
      DN_SPI_DEV_ID,
      &spiOpenArgs,
      sizeof(spiOpenArgs)
   );
   if ((err < DN_ERR_NONE) && (err != DN_ERR_STATE)) {
      dnm_ucli_printf("unable to open SPI device, error %d\n\r",err);
      return 1;
   }
   
   return 0;
}

INT32U spiRead() {
   INT32U temperatureRaw;
   dn_ioctl_spi_transfer_t      spiTransfer;
   INT8U spiTxBuffer[]={0x0,0x0,0x0,0x0};
   INT8U spiRxBuffer[]={0x0,0x0,0x0,0x0};
   int   err;   
   INT8U res;
   
   res = spiOpen();
   if(res){
      return 1;
   }
   
   // initialize spi communication parameters
   spiTransfer.txData             = spiTxBuffer;
   spiTransfer.rxData             = spiRxBuffer;
   spiTransfer.transactionLen     = sizeof(spiTxBuffer);
   spiTransfer.numSamples         = 1;
   spiTransfer.startDelay         = 0;
   spiTransfer.clockPolarity      = DN_SPI_CPOL_0;
   spiTransfer.clockPhase         = DN_SPI_CPHA_0;
   spiTransfer.bitOrder           = DN_SPI_MSB_FIRST;
   spiTransfer.slaveSelect        = DN_SPIM_SS_0n;
   spiTransfer.clockDivider       = DN_SPI_CLKDIV_2;
   
   // Start read
   err = dn_ioctl(
      DN_SPI_DEV_ID,
      DN_IOCTL_SPI_TRANSFER,
      &spiTransfer,
      sizeof(spiTransfer)
   );
   if (err < DN_ERR_NONE) {
      dnm_ucli_printf("Unable to communicate over SPI, err=%d\r\n",err);
      return 1;
   }
   
   //Get temperature value
   memcpy(&temperatureRaw,spiRxBuffer,sizeof(spiTxBuffer));
   temperatureRaw=htonl(temperatureRaw);
   
   res= spiClose();
   
   if(res){
      return 1;
   }
   
   return temperatureRaw;
}

INT8U spiClose() {
   int   err;
   // close the SPI device
   err = dn_close(DN_SPI_DEV_ID);
   if ((err < DN_ERR_NONE) && (err != DN_ERR_STATE)) {
      dnm_ucli_printf("unable to close SPI device, error %d\n\r",err);
      return 1;
   }
   
   return 0;
}

//===== GPIO

void gpioSetMode(INT8U pin,INT8U mode) {
   dn_error_t              dnErr;
   dnErr = dn_open(
      pin,
      NULL,
      0
   );
   
   ASSERT(dnErr==DN_ERR_NONE);
   
   if(!DN_IOCTL_GPIO_CFG_INPUT){
     dn_gpio_ioctl_cfg_out_t gpioOutCfg; 
     gpioOutCfg.initialLevel = 0x00;
     dnErr = dn_ioctl(
        pin,
        mode,
        &gpioOutCfg,
        sizeof(gpioOutCfg)
     );
   }else{
     dn_gpio_ioctl_cfg_in_t   gpioInCfg;
     gpioInCfg.pullMode = DN_GPIO_PULL_DOWN;
     dnErr = dn_ioctl(
        pin,
        mode,
        &gpioInCfg,
        sizeof(gpioInCfg)
     );
     ASSERT(dnErr==DN_ERR_NONE);
   }
   ASSERT(dnErr==DN_ERR_NONE);
}

void gpioWrite(INT8U pin,INT8U value) {
   dn_error_t  dnErr;
   dnErr = dn_write(
      pin,              // device
      &value,               // buf
      sizeof(value)         // len
   );
   
   ASSERT(dnErr==DN_ERR_NONE);
}


//===== power source ADC

void openPowerSourceADC() {
   dn_adc_drv_open_args_t    openArgs;   
   dn_error_t                dnErr;
    
   // open ADC channel
   openArgs.rdacOffset  = 0;
   openArgs.vgaGain     = 0;
   openArgs.fBypassVga  = 1;
   dnErr = dn_open(
      DN_ADC_AI_0_DEV_ID,         // device
      &openArgs,                  // args
      sizeof(openArgs)            // argLen 
   );
   ASSERT(dnErr==DN_ERR_NONE);
}

INT16U readPowerSourceADC() {
   int                       numBytesRead;
   INT16U                    adcVal;
   
   // read ADC value
   numBytesRead = dn_read(
      DN_ADC_AI_0_DEV_ID ,        // device
      &adcVal,                    // buf
      sizeof(adcVal)              // bufSize 
   );
   ASSERT(numBytesRead == sizeof(adcVal));
   
#ifdef DEBUG   
   // Console print
   dnm_ucli_printf("adcVal=%d\r\n",adcVal);
#endif
   
   return adcVal;
}

//===== radio inhibit

void inhibitRadio() {
   // this is a stub
}

void enableRadio() {
   // this is a stub
}

//=============================================================================
//=========================== install a kernel header =========================
//=============================================================================

/**
 A kernel header is a set of bytes prepended to the actual binary image of this
 application. Thus header is needed for your application to start running.
 */

DN_CREATE_EXE_HDR(DN_VENDOR_ID_NOT_SET,
                  DN_APP_ID_NOT_SET,
                  VER_MAJOR,
                  VER_MINOR,
                  VER_PATCH,
                  VER_BUILD);
