/*
Copyright (c) 2010, Dust Networks.  All rights reserved.
*/

#ifndef RCM_H
#define RCM_H

/**
 * Rate Control module allows in controlling the packet transmission rate.<br>
 * 
 * The module has the following features:<br>
 * - Allows the data engines to back-off packet output rates based on network conditions.
 * - Maintains packet failure rate of each data engine, which it uses inorder to back-off packets.
 * 
 * To use rate control module, application must follow the following steps:<br>
 * - Initialize rate control module with \ref dnm_rcm_init.
 * - Invoke \ref dnm_rcm_registerChannel inorder to register each channel of the data engine with rate control module.
 * - Invoke \ref dnm_rcm_transmitFrame inorder to transmit the packet.
 */

/** @defgroup RCM_API_Module Rate Control Module
 *  @{
 */

/********************************************************************
                                Includes
 ********************************************************************/
#include "dn_typedef.h"
#include "dn_errno.h"
#include "dn_channel.h"
#include "dnm_ucli.h"

/********************************************************************
                          Constants and Enumerations
 ********************************************************************/
/** @name RCM Errors */
typedef enum{
   RCM_ERR_OK = 0,         /**< Request succeeded */
   RCM_NO_BUF_FREE,        /**< No buffers free */
   RCM_INVALID_BUF_ID,     /**< invalid buffer id */
   RCM_RC_TX_FAILED,       /**< transmission failed */
   RCM_RC_ERROR,           /**< unknown error */
} rcm_error_t;
/** @}end RCM Errors */

/********************************************************************
                          Customizable Macros
 ********************************************************************/
#define   MAX_PENALTY_COUNT_BUF    (20)   /**< \hideinitializer The maximum 
                                          number of modules supported by the RCM. */

#define   MAX_PENALTY_COUNT        0xFF   /**< \hideinitializer The penalty 
                                          count needs to be clamped to a maximum
                                          value, otherwise while incrementing it 
                                          will reach 0xFF and roll-over to 0. */

#define   RATE_CTRL_ON              0u    /**< \hideinitializer rate control state = on */
#define   RATE_CTRL_OFF             1u    /**< \hideinitializer rate control state = off */
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
/** @name Rate Control Module API
@{ */

//Initilizes penalty count buffers as unused.
void dnm_rcm_init(void);

// Request to transmit a frame.
rcm_error_t dnm_rcm_transmitFrame(INT8U regId, INT8U* payload, INT8U payloadSize,
                                  INT8U rateControlState);

// Registers channel for an entry in RCM module.
rcm_error_t dnm_rcm_registerChannel(INT8U *regId);

// Enable/disable trace
void dnm_rcm_traceControl (INT8U traceFlag);
// Check if trace is enabled
BOOLEAN dnm_rcm_isTraceEnabled (void);

/** @} Rate Control Module API */

/** @}  (for @defgroup RCM_API_Module) */

#endif /* RCM_H */
