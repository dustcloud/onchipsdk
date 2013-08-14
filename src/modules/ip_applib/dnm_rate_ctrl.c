/*
Copyright (c) 2010, Dust Networks.  All rights reserved.
*/

/********************************************************************
   Includes
 ********************************************************************/
#include "dnm_rate_ctrl.h"
#include "dnm_local.h"

/********************************************************************
   Constants and Enumerations
 ********************************************************************/
#define BUF_UNUSED 0
#define BUF_USED   0xFF

/********************************************************************
   Data Structures
 ********************************************************************/
/** @{name rcm_var_t */
typedef struct {
   INT8U penaltyCount; /**< Holds the penalty counts on a per channel basis */
   INT8U fUsed;        /**< Used to denote the availability of buffer */
   INT8U skipCount;    /**< Holds the skip count on per channel basis */
} rcm_var_t;
/** @}end rcm_var_t */

/********************************************************************
        Variable Definitions
*********************************************************************/
static rcm_var_t  rcm_v[MAX_PENALTY_COUNT_BUF];
/* pointer to store the CLI context */
dnm_cli_cont_t *rcmCliCont;
/* CLI trace flag */
INT32S rcmCliTraceFlag;

/********************************************************************
   Function Prototypes
 ********************************************************************/
/* None */
/*******************************************************************
  Private Functions
 ********************************************************************/
/**
 * Initilizes penalty count buffers as unused.
 * 
 * @param cliContext - pointer to the application cli context
 * @param TraceFlag - trace flag allocated to the module
 * @return - none
 */
void dnm_rcm_init(dnm_cli_cont_t *cliContext, INT32S TraceFlag)
{
   INT8U i;
   rcmCliCont = cliContext;
   rcmCliTraceFlag = TraceFlag;
   for(i=0; i < MAX_PENALTY_COUNT_BUF; i++){
      rcm_v[i].fUsed = BUF_UNUSED;
   }
}

/**
 * Registers channel for an entry in RCM module.
 *
 * @param regId - OUT pointer to hold register Id
 * @return - RCM_NO_BUF_FREE - Failure, RCM_ERR_OK - Success
 */
rcm_error_t dnm_rcm_registerChannel(INT8U *regId)
{
   rcm_error_t status = RCM_NO_BUF_FREE;
   INT8U i;
   for(i = 0; i < MAX_PENALTY_COUNT_BUF; i++) {
      if(rcm_v[i].fUsed == BUF_UNUSED){
         rcm_v[i].penaltyCount = 0;
         rcm_v[i].skipCount = 0;
         rcm_v[i].fUsed = BUF_USED;
         *regId = i;
         status = RCM_ERR_OK;
         break;
      }
   }
   return status;
}

/**
 * \brief Request to transmit a frame.
 * 
 * \param regId            [in] RCM register Id
 * \param payload          [in] Pointer to the first byte of the payload
 * \param payloadSize      [in] Number of bytes in the payload
 * \param rateControlState [in] TODO
 *
 * \todo Document rateControlState
 *
 * \return RCM_NO_BUF_FREE Failure
 * \return RCM_ERR_OK      Success
 */
rcm_error_t dnm_rcm_transmitFrame(INT8U regId, INT8U* payload, INT8U payloadSize,
                                  INT8U rateControlState)
{
   dn_error_t locErr;
   INT8U status = 0;
   
   if(regId >= MAX_PENALTY_COUNT_BUF){
      return RCM_INVALID_BUF_ID;
   }
   
   if (rateControlState == RATE_CTRL_ON) {
      /* skip the frame? */
      if(rcm_v[regId].skipCount > 0) {
         rcm_v[regId].skipCount--;
         return RCM_RC_TX_FAILED;
      }
   }
   /* the destination address and destination port of the notifications is to be
    * retrieved in the from main module */
   locErr = dnm_loc_sendtoCmd((loc_sendtoNW_t*)payload, payloadSize, &status);
   /* Transmission failed */
   if((locErr != DN_ERR_NONE) || (status != DN_API_RC_OK)) {
      if (rateControlState == RATE_CTRL_ON) {
         /* Increment penalty count and restore skipcount to penalty count */
         /* Clamp penalty count to MAX_PENALTY_COUNT */
         if(rcm_v[regId].penaltyCount >= MAX_PENALTY_COUNT){
            rcm_v[regId].penaltyCount =  MAX_PENALTY_COUNT;  
         }
         else {
            rcm_v[regId].penaltyCount++;
         }
         rcm_v[regId].skipCount = rcm_v[regId].penaltyCount;
         /* Did transmission fail due to negative ack ? */
         if(status != DN_API_RC_OK) {
            dnm_cli_trace(rcmCliCont, rcmCliTraceFlag,
                          "rc Tx req eng = %d, rejected, nack, pen = %d\n\r", regId,
                          rcm_v[regId].penaltyCount);
         }
         else {
            dnm_cli_trace(rcmCliCont, rcmCliTraceFlag,
                          "rc Tx req eng = %d, rejected, ack, pen = %d\n\r", regId,
                          rcm_v[regId].penaltyCount);
         }
      }
      else {
         dnm_cli_trace(rcmCliCont, rcmCliTraceFlag,
                       "rc=OFF, eng = %d, tx=failed\n\r", regId);
      }
      /* return errors;*/
      return RCM_RC_TX_FAILED;
   }
   /* Transmission succeeded */
   else {
      if (rateControlState == RATE_CTRL_ON) {
         /* Decrement penalty count and restore skipcount to penalty count */
         if(rcm_v[regId].penaltyCount > 0) {
            rcm_v[regId].penaltyCount--;
            rcm_v[regId].skipCount = rcm_v[regId].penaltyCount;
         }
         dnm_cli_trace(rcmCliCont, rcmCliTraceFlag,
                       "rc Tx req eng = %d, accepted, ack, pen = %d\n\r", regId,
                       rcm_v[regId].penaltyCount);
      }
      else {
         dnm_cli_trace(rcmCliCont, rcmCliTraceFlag,
                       "rc=OFF, eng = %d, tx=success\n\r", regId);
      }
      return RCM_ERR_OK;
   }
}

