/**
Copyright (c) 2014, Dust Networks.  All rights reserved.

\brief Fast Fourier Transform (FFT) Sample Application

This sample application shows you how to repetetive sampling over SPI, and
do a Fast Fourier Transform (FFT) over the gathered samples.

This sample application was written for the "Triple Axis Accelerometer
Breakout" board is the STMicro LIS331HH three axis linear accelerometer,
available at https://www.sparkfun.com/products/10345.

The following table indicates how to connect to the two boards"

SEN-10345 PIN    DC9003 PIN
   GND              GND
   INT2             DP1
   INT1             DP0
   SDA              SPIM_MOSI
   SCL              SPIM_SCK
   SA0              SPIM_MISO
   CS               SPIM_SS_1n
   VCC              VSUPPLY

To run the sample application:
- make sure your DC9003 board is programmed with this application, and turned
  off (POWER switch in the OFF position)
- connect the DC9003 to the SEN-10345 boards following the table above
- connect the DC9003 to a DC9006 interface card
- connect the DC9006 interface card to a computer over a USB cable, and start
  a serial terminal on the CLI port.
- power on the D9003
- on the CLI interface, enter the command "sample". This will cause the DC9006
  to collect 1024 samples from the z-axis of the LIS331HH, at 1kHz. The
  accelearation values are printed to the CLI interface, and also stored in
  internal memory.
- on the CLI interface, enter the command "fft". This cause the sample
  application to calculate a Fast Fourier Transform over the collected samples.
  Note: the FFT operation overwrites the source data. The calling code must
  copy the data beforehand if it wishes to preserve it.

This software is designed to play nicely in a low-power mote environment as
opposed to optimizing for real-time performance.
*/

#include "dn_common.h"
#include "string.h"
#include "stdio.h"
#include "math.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dn_system.h"
#include "dn_api_param.h"
#include "dnm_local.h"
#include "dn_uart.h"
#include "dn_gpio.h"
#include "dn_spi.h"
#include "app_task_cfg.h"
#include "dn_exe_hdr.h"
#include "Ver.h"
#include "dn_time.h"

//=========================== defines =========================================

#define LOG2PTS              10                // 2^n samples
#define POINTS               (1<<LOG2PTS)      // Number of samples
#define SPI_BUFFER_LENGTH    7
#define TWOPI                6.2831853

#define MIN_STACK_VER_MAJOR  1
#define MIN_STACK_VER_MINOR  2
#define MIN_STACK_VER_PATCH  1
#define MIN_STACK_VER_BUILD  19

//===== Macros

#define SWAP(a,b) real_temp=(a);(a)=(b);(b)=real_temp

//=========================== prototypes ======================================

//===== CLI handlers
dn_error_t cli_sampleHandler(INT8U* arg, INT32U len);
dn_error_t cli_fftHandler(INT8U* arg, INT32U len);

//===== tasks
static void sampleTask(void* unused);

//===== helpers
void   spi_TxRx(INT8U len);
void   fft(void);
INT16S scaledMult(INT16S a, INT16S b);
INT16U mag(INT16S a, INT16S b);

//=========================== const ===========================================

const dnm_ucli_cmdDef_t cliCmdDefs[] = {
   {&cli_sampleHandler,      "sample", "sample", DN_CLI_ACCESS_LOGIN},
   {&cli_fftHandler,         "fft",    "fft",    DN_CLI_ACCESS_LOGIN},
   {NULL,                    NULL,     NULL,     0},
};

//=========================== variables =======================================

typedef struct {
   OS_STK                    sampleTaskStack[TASK_APP_SAMPLE_STK_SIZE];
   OS_EVENT*                 sampleNowSem;
   dn_ioctl_spi_transfer_t   spiTransfer;
   INT8U                     spiTxBuffer[SPI_BUFFER_LENGTH];
   INT16S                    real[3*POINTS/2]; 
   INT16S                    imaginary[POINTS];
} fft_vars_t;

fft_vars_t fft_v;

//=========================== Initialization ==================================

/**
\brief This is the entry point in the application code.
*/
int p2_init(void) {
   INT8U osErr;
   
   //==== initialize local variables
   memset(&fft_v,0x00,sizeof(fft_v));
   
   // create semaphore
   fft_v.sampleNowSem = OSSemCreate(0);     // by default, don't start sampling
   
   //==== initialize helper tasks
   cli_task_init(
      "fft",                      // appName
      &cliCmdDefs                 // cliCmds
   );
   loc_task_init(
      JOIN_NO,                    // fJoin
      NETID_NONE,                 // netId
      UDPPORT_NONE,               // udpPort
      NULL,                       // joinedSem
      BANDWIDTH_NONE,             // bandwidth
      NULL                        // serviceSem
   );
   
   //==== create the sample task
   osErr  = OSTaskCreateExt(
      sampleTask,
      (void *)0,
      (OS_STK*)(&fft_v.sampleTaskStack[TASK_APP_SAMPLE_STK_SIZE-1]),
      TASK_APP_SAMPLE_PRIORITY,
      TASK_APP_SAMPLE_PRIORITY,
      (OS_STK*)fft_v.sampleTaskStack,
      TASK_APP_SAMPLE_STK_SIZE,
      (void *)0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr == OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_SAMPLE_PRIORITY, (INT8U*)TASK_APP_SAMPLE_NAME, &osErr);
   ASSERT(osErr == OS_ERR_NONE);
   
   return 0;
}

//=========================== CLI handlers ====================================

dn_error_t cli_sampleHandler(INT8U* arg, INT32U len) {
   INT8U osErr;
   
   // unlock the sampleTask
   osErr = OSSemPost(fft_v.sampleNowSem);
   ASSERT(osErr==OS_ERR_NONE);
   
   return DN_ERR_NONE;
}

dn_error_t cli_fftHandler(INT8U* arg, INT32U len) {
   INT16U       i;
   INT64U       startTime;
   INT64U       finishTime;
   
   // perform the FFT transformation (and measure the time it takes)
   dnm_ucli_printf("Performing FFT... ");
   dn_getSystemTime(&startTime);
   fft();
   dn_getSystemTime(&finishTime);
   dnm_ucli_printf("done.\r\n\r\n");
   
   // print how long FFT took
   dnm_ucli_printf("FFT done in %d 32kHz ticks\r\n\r\n",(INT32U)(finishTime - startTime));
   
   // print FFT result
   for (i=0; i<POINTS; i+=1) {
      //dnm_ucli_printf("%04d: (%d, %d)\r\n", i, fft_v.real[i], fft_v.imaginary[i]);
      dnm_ucli_printf("fft %04d: %d\r\n", i, mag(fft_v.real[i], fft_v.imaginary[i]));
   }
   
   return DN_ERR_NONE;
}

//=========================== Tasks ===========================================

static void sampleTask(void* unused) {
   INT8U                          osErr;
   dn_error_t                     dnErr;
   INT8U                          rc;
   INT8U                          moteInfoBuf[2+sizeof(dn_api_rsp_get_moteinfo_t)];
   dn_api_rsp_get_moteinfo_t*     moteInfo;
   INT8U                          respLen;
   dn_spi_open_args_t             spiOpenArgs;
   dn_ioctl_spi_transfer_t        spiTransfer;
   INT16U                         i;
   INT8U*                         readBuffer;
   
   // set the CLI user level
   dnm_ucli_changeAccessLevel(DN_CLI_ACCESS_USER);
   
   // other setup here
   OSTimeDly(1000);
   
   //===== verify stack version
   
   // retrieve the stack version
   dnErr = dnm_loc_getParameterCmd(
      DN_API_PARAM_MOTEINFO,                // paramId
      moteInfoBuf,                          // payload
      0,                                    // txPayloadLen
      &respLen,                             // rxPayloadLen
      &rc                                   // rc
   );
   ASSERT(dnErr==DN_ERR_NONE);
   ASSERT(rc==DN_ERR_NONE);
   
   // check stack version
   moteInfo = (dn_api_rsp_get_moteinfo_t*)(&moteInfoBuf[0]);
   dnm_ucli_printf(
      "\r\nYou are using stack version %d.%d.%d.%d\r\n",
      moteInfo->swVer.major,
      moteInfo->swVer.minor,
      moteInfo->swVer.patch,
      htons(moteInfo->swVer.build)
   );
   dnm_ucli_printf(
      "Minimal stack version       %d.%d.%d.%d\r\n",
      MIN_STACK_VER_MAJOR,
      MIN_STACK_VER_MINOR,
      MIN_STACK_VER_PATCH,
      MIN_STACK_VER_BUILD
   );
   if (
      moteInfo->swVer.major        >  MIN_STACK_VER_MAJOR ||
      moteInfo->swVer.minor        >  MIN_STACK_VER_MINOR ||
      moteInfo->swVer.patch        >  MIN_STACK_VER_PATCH ||
      htons(moteInfo->swVer.build) >= MIN_STACK_VER_BUILD
   ) {
      dnm_ucli_printf("PASS\r\n\r\n");
   } else {
      while(1) {
         dnm_ucli_printf("FAIL. Aborting execution. Please update your stack.\r\n");
         // dummy infinite wait
         OSSemPend(fft_v.sampleNowSem, 0, &osErr);
         ASSERT(osErr==OS_ERR_NONE);
      }
   }
   
   //===== initialize SPI
   // open the SPI device
   spiOpenArgs.maxTransactionLenForCPHA_1   = 0;
   dnErr = dn_open(
      DN_SPI_DEV_ID,
      &spiOpenArgs,
      sizeof(spiOpenArgs)
   );
   ASSERT((dnErr == DN_ERR_NONE) || (dnErr == DN_ERR_STATE));
   
   // initialize spi communication parameters
   fft_v.spiTransfer.txData                 = fft_v.spiTxBuffer;
   fft_v.spiTransfer.rxData                 = (INT8U *)fft_v.real;
   fft_v.spiTransfer.transactionLen         = sizeof(fft_v.spiTxBuffer);
   fft_v.spiTransfer.slaveSelect            = DN_SPIM_SS_1n;
   fft_v.spiTransfer.startDelay             = 0;
   fft_v.spiTransfer.bitOrder               = DN_SPI_MSB_FIRST;
   fft_v.spiTransfer.clockPolarity          = DN_SPI_CPOL_0;
   fft_v.spiTransfer.clockPhase             = DN_SPI_CPHA_0;
   fft_v.spiTransfer.clockDivider           = DN_SPI_CLKDIV_16;
   fft_v.spiTransfer.numSamples             = 1;
     
   // config accelerometer
   fft_v.spiTxBuffer[0]                     = 0x20; // write to register 0x20 (CTRL_REG1)
   fft_v.spiTxBuffer[1]                     = 0x3C; // normal mode, 1000Hz, z-enabled
   spi_TxRx(2);
   
   OSTimeDly(100);
   
   fft_v.spiTxBuffer[0]                     = 0x21; // write to register 0x21 (CTRL_REG2)
   fft_v.spiTxBuffer[1]                     = 0x00; // HP filter ON (cuttoff@8MHz) = 0x60, HPfilter OFF = 0x00
   spi_TxRx(2);
   
   OSTimeDly(100);
   
   fft_v.spiTxBuffer[0]                     = 0x23; // write to register 0x23 (CTRL_REG4)
   //fft_v.spiTxBuffer[1]                     = 0xC0; // Block data update, Little-endian data, +/-6g
   fft_v.spiTxBuffer[1]                     = 0x80; // Block data update, Big-endian data, +/-6g
   spi_TxRx(2);
   
   OSTimeDly(100);
   
   // set up for multiple samples
   fft_v.spiTransfer.numSamples             = POINTS;
   fft_v.spiTransfer.samplePeriod           = 1000;  //uS, i.e. 1 KHz
   
   while(1) { // this is a task, it executes forever
      
      // wait for the semaphore to be released
      OSSemPend(fft_v.sampleNowSem, 0, &osErr);
      ASSERT(osErr==OS_ERR_NONE);
      
      dnm_ucli_printf("Sampling SPI...");
      
      readBuffer = (INT8U*)fft_v.real;
      
      // Impulse dataset for testing ---
      /*
      //impulse at 1
      readBuffer[0] = 0;
      readBuffer[1] = 0;
      readBuffer[2] = 0xFF;
      readBuffer[3] = 0x7F;
      */
      // --- end impulse dataset
      
      // Real dataset from SPI accelerometer ---
      // initialize spi communication parameters
      fft_v.spiTransfer.txData            = fft_v.spiTxBuffer;
      fft_v.spiTransfer.rxData            = (INT8U *)fft_v.real;
      fft_v.spiTransfer.transactionLen    = sizeof(fft_v.spiTxBuffer);
      fft_v.spiTransfer.slaveSelect       = DN_SPIM_SS_1n;
      fft_v.spiTransfer.startDelay        = 0;
      fft_v.spiTransfer.bitOrder          = DN_SPI_MSB_FIRST;
      fft_v.spiTransfer.clockPolarity     = DN_SPI_CPOL_0;
      fft_v.spiTransfer.clockPhase        = DN_SPI_CPHA_0;
      fft_v.spiTransfer.clockDivider      = DN_SPI_CLKDIV_16;
      fft_v.spiTransfer.numSamples        = POINTS;
      fft_v.spiTransfer.samplePeriod      = 1000;  // in us, 1000 == 1 kHz
      
      //=== Z
      fft_v.spiTxBuffer[0]                = 0xec; // read bit | consecutive measure bit | 0x2c = 0xec (Z)
      fft_v.spiTxBuffer[1]                = 0x00;
      fft_v.spiTxBuffer[2]                = 0x00;
      spi_TxRx(3);
      
      // remove the dummy bytes from the receive buffer
      readBuffer[0]                       = readBuffer[1];
      readBuffer[1]                       = readBuffer[2];
      for (i=1; i<POINTS; i++) {
        readBuffer[2*i]                   = readBuffer[3*i+1];
        readBuffer[2*i+1]                 = readBuffer[3*i+2];
      }
      // --- end real dataset
      
      dnm_ucli_printf(" done.\r\n\r\n");
      
      // print the sampled points
      for (i=0; i<POINTS; i++) {
         dnm_ucli_printf("sample %04d: %d\r\n", i, fft_v.real[i]);
      }
   }
}

//=========================== helpers =========================================

/**
\brief Single transaction over SPI.

\pre fft_v.spiTransfer needs to be set up before calling this function.

\param[in] len Number of bytes in the SPI transaction.
*/
void spi_TxRx(INT8U len) {
   dn_error_t      dnErr;
   
   fft_v.spiTransfer.transactionLen = len;
   dnErr = dn_ioctl(
      DN_SPI_DEV_ID,
      DN_IOCTL_SPI_TRANSFER,
      &fft_v.spiTransfer,
      sizeof(fft_v.spiTransfer)
   );
   
   if (dnErr < DN_ERR_NONE) {
      dnm_ucli_printf("Unable to communicate over SPI, err=%d\r\n",dnErr);
   }
}

/**
\brief Perform a Fast Fourier Transform (FFT).

FFT done on the data in real[n], with the freq and phase data returned in
real[n] and imaginary[n].

\post This function overwrites the real dataset. You will need to copy the
   sample data if you want to preserve it.
*/
void fft(void) {
   INT16S mr,i,j,l,k,istep,count,shift;
   INT16S qr,qi,real_temp,imag_temp,wr,wi;
   float  fTemp;
   
   mr    = 0;
   
   // decimation in time - re-order data
   for(count=1; count<=POINTS-1; ++count) {
      l = POINTS;
      do {
         l >>= 1;
      } while(mr+l > POINTS-1);
      mr = (mr & (l-1)) + l;
      
      if(mr <= count) continue;
      SWAP(fft_v.real[count], fft_v.real[mr]);
    
      // normally here we'd swap complex array, but since it's uniformly 0, we don't need to
   }
   
   k = LOG2PTS-1;
   
   l = 1;
   while(l < POINTS) {
      
      // fixed scaling, for proper normalization. There will be log2(n) passes,
      // so this results in an overall factor of 1/n, distributed to maximize
      // arithmetic accuracy
      shift = 1;
      istep = l << 1;
      for(count=0; count<l; ++count) {
         j = count << k;
         
         fTemp          = sinf((j + POINTS/4) * TWOPI/POINTS)* 32767.0;
         wr             = (INT16S) fTemp;
         fTemp          = -sinf(j * TWOPI/POINTS * 1.0) * 32767.0;
         wi             = (INT16S) fTemp;
         
         if (shift) {
            wr >>= 1;
            wi >>= 1;
         }
         for(i=count; i<POINTS; i+=istep) {
            j = i + l;
            real_temp   = scaledMult(wr,fft_v.real[j]) - scaledMult(wi,fft_v.imaginary[j]);
            imag_temp   = scaledMult(wr,fft_v.imaginary[j]) + scaledMult(wi,fft_v.real[j]);
            qr          = fft_v.real[i];
            qi          = fft_v.imaginary[i];
            if (shift) {
               qr >>= 1;
               qi >>= 1;
            }
            fft_v.real[j]         = qr - real_temp;
            fft_v.imaginary[j]    = qi - imag_temp;
            fft_v.real[i]         = qr + real_temp;
            fft_v.imaginary[i]    = qi + imag_temp;
         }
      }
      --k;
      l = istep;
   }
}

/**
\brief Fixed-point multiplication

\param[in] a First multiplier.
\param[in] b Second multiplier.

\return Calculated value of a*b.
*/
INT16S scaledMult(INT16S a, INT16S b) {
   return ((long)(a) * (long)(b))>>15;  //scaled 16-bit multiply
}

/**
\brief Magnitude of complex vector.

\param[in] a First coordinate of the vector.
\param[in] b Second coordinate of the vector.

\return Calculated magnitude, i.e. ||(a,b)||.
*/
INT16U mag(INT16S a, INT16S b){
   float returnVal;
   
   returnVal = sqrt((float)pow(a,2) + (float)pow(b,2));
   
   return ((INT16U)returnVal);
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
