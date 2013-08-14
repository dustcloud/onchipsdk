/*
Copyright (c) 2010, Dust Networks.  All rights reserved.
*/

#ifndef LOC_H
#define LOC_H

#include "dn_api_local.h"
#include "dn_api_common.h"
#include "dn_mesh.h"
#include "dn_typedef.h"
#include "dn_errno.h"
#include "dn_channel.h"
#include "dnm_cli.h"
#include "dnm_cli_util.h"


/**
\addtogroup module_dnm_local
\{
*/

//=========================== enums  ==========================================

/// Is passthorugh mode of operation enabled?
typedef enum {
   PASSTHROUGH_OFF = 0x0,         ///< Pass-through mode is disabled.
   PASSTHROUGH_ON  = 0x01         ///< Pass-through mode is enabled.
} passThrough_mode_t;

//=========================== structs =========================================

PACKED_START
typedef struct {
   dn_api_cmd_hdr_t    header;    ///< Network packet header.
   dn_api_loc_sendto_t locSendTo; ///< Local sendto command.
} loc_sendtoNW_t;
PACKED_STOP

//=========================== function pointers ===============================

/**
\brief Function pointer used for receive notification call back functions.

\param[in] rxFrame Pointer the received notification frame.
\param[in] length  Length of the received notification frame.
*/
typedef dn_error_t (*rxNotifCb_t)(dn_api_loc_notif_received_t *rxFrame, 
                            INT8U length) ;
 
/**
\brief Function pointer used for event notification call back functions.

\param[in]  events Pointer to the received events.
\param[out] rsp    Pointer to hold the response.

\return DN_ERR_NONE on success, DN_ERR_ERROR on failure.
*/
typedef dn_error_t (*eventNotifCb_t)(dn_api_loc_notif_events_t *events, 
                                     INT8U *rsp);

/**
\brief Function pointer used for the event handler, in pass through mode.

\param[in] alarms Alarms bit field.
\param[in] events Event bit field.

\return DN_ERR_NONE on success, DN_ERR_ERROR on failure.
*/
typedef dn_error_t (*passThroughEventNotifCb_t)(INT32U events, INT32U alarms);
 
/**
\brief Function pointer used for local notification call back functions.

\param[in]  notifId     Type of the local API notification.
\param[in]  notifBuffer Pointer the notification data.
\param[in]  buffLen     Length of the notification data.
\param[out] rsp         Pointer to hold the response.

\return DN_ERR_NONE on success, DN_ERR_ERROR on failure.
*/
typedef dn_error_t (*locNotifCb_t)(INT8U notfId, INT8U *notifBuffer, 
                                   INT8U buffLen, INT8U *rsp);


/**
\brief Function pointer used for pass through notifications call back
   functions.

\param[in]  notifBuffer Pointer to the notification frame.
\param[in]  buffLen     Length notification frame.
\param[out] rsp         Pointer to hold the response.

\return DN_ERR_NONE on success, DN_ERR_ERROR on failure.
*/
typedef dn_error_t (*passThroughNotifCb_t)(INT8U **pBuf, INT8U bufLen, INT8U *rsp);


/**
\brief Function pointer used for 'mote state verification' callback functions.

\param[out] rsp Pointer to hold the response.

\return DN_ERR_NONE on success, DN_ERR_ERROR on failure.
*/
typedef dn_error_t (*isOkToProcessNotifsCb_t)(void);

/**
\brief Function pointer used for time notification call back functions.

\param[in] rxFrame Pointer the time notification frame.
\param[in] length  Length of time notification frame.
*/
typedef dn_error_t (*timeNotifCb_t)(dn_api_loc_notif_time_t *rxFrame, 
                            INT8U length) ;

//=========================== prototypes ======================================

/** 
\name Local Interface API
\{
*/

void dnm_loc_processNotifications(void);
dn_error_t dnm_loc_init(passThrough_mode_t mode, dnm_cli_cont_t *cliContext, INT32S TraceFlag,
                        INT8U *pBuffer, INT8U buffLen);
dn_error_t dnm_loc_setParameterCmd(INT8U paramId, INT8U *payload, 
                                   INT8U length, INT8U *rc);
dn_error_t dnm_loc_getParameterCmd(INT8U paramId, INT8U *payload, 
                                   INT8U txPayloadLen, 
                                   INT8U *rxPayloadLen, INT8U *rc);
dn_error_t dnm_loc_joinCmd(INT8U *rc);
dn_error_t dnm_loc_disconnectCmd(INT8U *rc);
dn_error_t dnm_loc_resetCmd(INT8U *rc);
dn_error_t dnm_loc_lowPowerSleepCmd(INT8U *rc);
dn_error_t dnm_loc_testRadioTxCmd(INT8U typeParam, INT16U mask, INT8S power, 
                                  INT16U numRepeats, INT8U numSubtests, 
                                  dn_api_loc_testrftx_subtestparam_t * subTests,
                                  INT8U *rc);
dn_error_t dnm_loc_testRadioRxCmd(INT16U mask, INT16U durationRxTest, INT8U *rc);
dn_error_t dnm_loc_requestServiceCmd(dn_moteid_t destAddr,
                                     INT8U svcType, INT32U svcInfo,
                                     INT8U *rc);
dn_error_t dnm_loc_getAssignedServiceCmd(dn_moteid_t destAddr, INT8U svcType,
                                     dn_api_loc_rsp_get_service_t *svcRsp);
dn_error_t dnm_loc_openSocketCmd(INT8U protocol, INT8U *sockId, INT8U *rc);
dn_error_t dnm_loc_closeSocketCmd(INT8U sockId, INT8U *rc);
dn_error_t dnm_loc_bindSocketCmd(INT8U sockId, INT16U port, INT8U *rc);
dn_error_t dnm_loc_sendtoCmd(loc_sendtoNW_t *sendto,INT8U length, INT8U *rc);
dn_error_t dnm_loc_clearNVCmd(INT8U *rc);
dn_error_t dnm_loc_registerEventNotifCallback(eventNotifCb_t cb);
dn_error_t dnm_loc_registerRxNotifCallback(rxNotifCb_t cb);
dn_error_t dnm_loc_registerTimeNotifCallback(timeNotifCb_t cb);
dn_error_t dnm_loc_sendRaw(INT8U* payload, INT8U length, INT8U* rsp, INT8U *rspLen);
void dnm_loc_prepareNotifResponse(INT8U notifId, INT8U response);
dn_error_t dnm_loc_registerPassthroughEvNotifCallback(passThroughEventNotifCb_t cb);
dn_error_t dnm_loc_registerPassThroughNotifCallback(passThroughNotifCb_t cb);

/**
// end of Local Interface API
\} 
*/

/**
// end of module_dnm_local
\}
*/

#endif /* LOC_H */
