/*
Copyright (c) 2010, Dust Networks.  All rights reserved.
*/

#ifndef SM_H
#define SM_H

/**
 * The Service Module is responsible for aggregation of service requests from 
 * internal data engines into a single service request to the controller.<br>
 * 
 * The module has following features:<br>
 * - Sends up-to date, aggregated service requests to controller.
 * - Keeps track of currently required and assigned bandwidth.
 *
 * To use service module, application must follow the following steps:<br>
 * - Initialize service module with \ref dnm_sm_init by providing a pointer to \ref dnm_cli_cont_t and trace flag associated
 *   with the service module.
 * - Invoke \ref dnm_sm_registerChannel inorder to register each channel of the data engine with service module.
 * - Invoke \ref dnm_sm_updateSvcParam inorder to update the network controller for the bandwidth.
 * - Invoke \ref dnm_sm_svcChanged whenever a service changed event is received. Internally it updates itself with the 
 *   allocated bandwidth by the network controller and re-requests if the bandwidth is not adequate.
 * - Invoke \ref dnm_sm_getAllocBandWidth to get the current network bandwidth.
 */
 

/** @defgroup SM_API_Module Service Module
 *  @{
 */

/********************************************************************
                                Includes
 ********************************************************************/
#include "dn_typedef.h"
#include "dn_errno.h"
#include "dn_channel.h"
#include "dnm_cli.h"
#include "dnm_cli_util.h"

/********************************************************************
                          Constants and Enumerations
 ********************************************************************/


/** @name SM Errors */
typedef enum{
   SM_ERR_OK = 0,           /**< Request succeeded */
   SM_NO_BUF_FREE,          /**< No buffers free */
   SM_INVALID_BUF_ID,       /**< invalid buffer id */
   SM_RC_ERROR,             /**< unknown error */
   SM_SVC_PARAM_NOT_UPDATED, /**< Service parameter was not updated */
}sm_error_t;
/** @}end SM Errors */

/********************************************************************
                          Customizable Macros
 ********************************************************************/

#define MAX_SVC_REQ_BUF (20) /**< \hideinitializer The maximum number of modules
                              supported by the SM. */

 /*******************************************************************
                          Variable definitions
 ********************************************************************/
 /* None */

/********************************************************************
                            Data Structures
 ********************************************************************/

/********************************************************************
                          Function Prototypes
 ********************************************************************/
/** @name Service Module API
@{ */

// Initilizes the service module.
void dnm_sm_init(dnm_cli_cont_t *cliContext, INT32S TraceFlag);

// Registers a channel for an entry in SM module.
sm_error_t dnm_sm_registerChannel(INT8U *RegId);

// Updates service  param.
sm_error_t dnm_sm_updateSvcParam(INT8U regId, INT32U bandwidth);

// Updates and requests for service param.
sm_error_t dnm_sm_updateAndRequestSvcParam(INT8U regId, INT32U bandwidth);

// Requests for aggregate service param.
sm_error_t dnm_sm_requestSvcParam(void);

// Service change notification received.
void dnm_sm_svcChanged(void);

// Gets the allocated bandwidth.
INT32U dnm_sm_getAllocBandWidth(void);

// Gets the aggregated bandwidth.
INT32U dnm_sm_getAggrBandWidth(void);

/** @} Service Module API */

/** @}  (for @defgroup SM_API_Module) */

#endif /* SM_H */
