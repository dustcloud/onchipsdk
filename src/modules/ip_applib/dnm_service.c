/*
Copyright (c) 2010, Dust Networks.  All rights reserved.
*/

/********************************************************************
   Includes
 ********************************************************************/
#include "dnm_service.h"
#include "dnm_local.h"
#include "dn_api_common.h"

/********************************************************************
   Constants and Enumerations
 ********************************************************************/

#define BUF_UNUSED 0
#define BUF_USED   0xFF

/* network controller address */
#define NW_CONTROLLER_ID              0xFFFE

#define SM_TIMER_ENABLED             (INT8U)0x01
#define SM_TIMER_DISABLED            (INT8U)0x00

/********************************************************************
   Data Structures
 ********************************************************************/
/** @{name svcReq_t */
typedef struct {
   INT32U reqValue;                        /**< Holds the bandwidth request on per channel basis */
   INT8U fUsed;                          /**< Holds the availability information of the service module */
} svcReq_t;
/** @}end svcReq_t */

/** @{name sm_var_t */
typedef struct {
   svcReq_t svcRequest[MAX_SVC_REQ_BUF]; /**< Holds the service requests on a per channel basis */
   INT32U reqBandwidth;                     /**< Holds the requesting bandwidth */
   INT32U  allocSvcParam;                   /**< Holds the allocated bandwidth */
   INT8U timerStateFlag;                 /**< TBD - work around used instead of OSTmrStateGet() */
   dnm_cli_cont_t *cliCont;              /**< pointer to store the CLI context */
   INT32S cliTraceFlag;                  /**< CLI trace flag */
   INT8U svcRequestCounter;              /**< Service Request Counter */
} sm_var_t;
/** @}end sm_var_t */

/********************************************************************
        Variable Definitions
*********************************************************************/
static sm_var_t  sm_v;

/********************************************************************
   Function Prototypes
 ********************************************************************/
/* None */
/*******************************************************************
  Private Functions
 ********************************************************************/
/* None */

/********************************************************************
   Public Functions
 ********************************************************************/
/**
 * Initilizes the service module.
 *
 * @param cliContext - pointer to the application cli context
 * @param TraceFlag - trace flag allocated to the module
 * @return - none
 */
void dnm_sm_init(dnm_cli_cont_t *cliContext, INT32S TraceFlag)
{
   INT8U i;
   sm_v.cliCont = cliContext;
   sm_v.cliTraceFlag = TraceFlag;
   for(i=0; i < MAX_SVC_REQ_BUF; i++) {
      sm_v.svcRequest[i].fUsed = BUF_UNUSED;
      sm_v.svcRequest[i].reqValue = 0;
   }
   sm_v.reqBandwidth = 0;
   sm_v.allocSvcParam = 0;
   sm_v.timerStateFlag = SM_TIMER_DISABLED;
}

/**
 * Registers a channel for an entry in SM module.
 *
 * @param regId - OUT pointer to hold register Id
 * @return SM_NO_BUF_FREE - Failure, SM_ERR_OK - Success
 */
sm_error_t dnm_sm_registerChannel(INT8U *regId)
{
   sm_error_t status = SM_NO_BUF_FREE;
   INT8U i;
   for(i = 0; i < MAX_SVC_REQ_BUF; i++) {
      if(sm_v.svcRequest[i].fUsed == BUF_UNUSED){
         sm_v.svcRequest[i].reqValue = 0;
         sm_v.svcRequest[i].fUsed = BUF_USED;
         *regId = i;
         status = SM_ERR_OK;
         break;
      }
   }
   return status;
}

// private fct to this file
static sm_error_t call_dnm_loc_requestServiceCmd() {
   INT8U       status   = 0;
   dn_error_t  dn_error = DN_ERR_NONE;
   sm_error_t  ret      = SM_ERR_OK;

   dnm_cli_trace(sm_v.cliCont, sm_v.cliTraceFlag, "sm Tx srv req type=%d, bw = %d\n\r",
                 DN_API_SERVICE_TYPE_BW, sm_v.reqBandwidth);

   dn_error = dnm_loc_requestServiceCmd(NW_CONTROLLER_ID, DN_API_SERVICE_TYPE_BW,sm_v.reqBandwidth, &status);
   if((dn_error != DN_ERR_NONE)||(status != DN_API_RC_OK)) {
      ret = SM_RC_ERROR;
   }
   return(ret);
}

/**
 * Updates and requests for service param.
 * 
 * @param regId - SM register Id
 * @param bandwidth - latest service request param - bw in mS
 * @return SM_INVALID_BUF_ID - Failure, SM_ERR_OK - Success
 */
sm_error_t dnm_sm_updateAndRequestSvcParam(INT8U regId, INT32U bandwidth)
{
   sm_error_t  ret      = SM_ERR_OK;

   ret = dnm_sm_updateSvcParam(regId, bandwidth);
   if (ret == SM_ERR_OK) {
      ret = call_dnm_loc_requestServiceCmd();      
   }

   return(ret);
}

/**
 * Requests for aggregate service param.
 * 
 * @return SM_INVALID_BUF_ID - Failure, SM_ERR_OK - Success
 */
sm_error_t dnm_sm_requestSvcParam(void)
{
   sm_error_t  ret      = SM_ERR_OK;
   ret = call_dnm_loc_requestServiceCmd(); 
   return(ret);   
}

/**
 * Updates service param.
 * 
 * @param regId - SM register Id
 * @param bandwidth - latest service param - bw in mS
 * @return SM_INVALID_BUF_ID - Failure, SM_ERR_OK - Success
 */
sm_error_t dnm_sm_updateSvcParam(INT8U regId, INT32U bandwidth)
{
   sm_error_t  ret               = SM_SVC_PARAM_NOT_UPDATED;
   // packets per multiplier sec to avoid using floats !
   INT32U   multiplier        = 10000000;
   INT32U   bandwith_pps      = 0;
   INT32U   reg_id_pps        = 0;
   INT32U   total_pps         = 0;
   INT32U   tmp_pps           = 0;   

   if(regId >= MAX_SVC_REQ_BUF){
      return SM_INVALID_BUF_ID;
   }

   if(bandwidth) {
      bandwith_pps   = multiplier/bandwidth;
   }

   if(sm_v.svcRequest[regId].reqValue) {
      reg_id_pps   = multiplier/sm_v.svcRequest[regId].reqValue;
   }

   if(sm_v.reqBandwidth) {
      total_pps   = multiplier/sm_v.reqBandwidth;
   }   
   //dnm_cli_printf("dnm_sm_updateSvcParam] bandwith_pps<%d> reg_id_pps<%d> total_pps<%d>\r\n",bandwith_pps,reg_id_pps,total_pps);

   if(sm_v.svcRequest[regId].reqValue != bandwidth){
      tmp_pps = total_pps+bandwith_pps-reg_id_pps;
      //dnm_cli_printf("dnm_sm_updateSvcParam] tmp_pps<%d>\r\n",tmp_pps);
      if(tmp_pps == 0) {
         sm_v.reqBandwidth = 0;
      }
      else {
         sm_v.reqBandwidth = multiplier/tmp_pps;
      }
      sm_v.svcRequest[regId].reqValue = bandwidth;      
      ret = SM_ERR_OK;
   }
   
   return(ret);
}

/**
 * Service change notification received.
 *
 * @param - none
 * @return - none
 */
void dnm_sm_svcChanged(void)
{
   dn_error_t                    dn_error = DN_ERR_NONE;
   dn_api_loc_rsp_get_service_t  svcResp;
   dn_moteid_t                   destAddr = NW_CONTROLLER_ID;
   
   dnm_cli_trace(sm_v.cliCont, sm_v.cliTraceFlag, "sm rx srv change\n\r");
   dn_error = dnm_loc_getAssignedServiceCmd(destAddr, DN_API_SERVICE_TYPE_BW, &svcResp);
   if (dn_error == DN_ERR_NONE) {
      /* allocated bw is adequate - Please note : the allocated bw is adequate 
         only if it is less than or equal to the requested value */
      sm_v.allocSvcParam = svcResp.value;
      dnm_cli_trace(sm_v.cliCont, sm_v.cliTraceFlag, "sm read srv type=%d, bw=%u\n\r",
                    svcResp.type, svcResp.value);
      if(svcResp.type == DN_API_SERVICE_TYPE_BW){
         if(sm_v.reqBandwidth != 0) {
            if(sm_v.reqBandwidth <  svcResp.value) {
               call_dnm_loc_requestServiceCmd();             
            }            
         }
      }
   }
}

/**
 * Gets the allocated bandwidth.
 * Note : This is the bandwidth allocated by the manager upon a request
 * @return - allocated bandwidth
 */
INT32U dnm_sm_getAllocBandWidth(void)
{
   return sm_v.allocSvcParam;
}

/**
 * Gets the aggregated bandwidth.
 * Note : This is the aggregate bandwidth need of the mote
 * @return - allocated bandwidth
 */
INT32U dnm_sm_getAggrBandWidth(void)
{
   return sm_v.reqBandwidth;
}
