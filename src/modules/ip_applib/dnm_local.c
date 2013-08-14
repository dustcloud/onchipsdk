/*
Copyright (c) 2010, Dust Networks.  All rights reserved.
*/

#include <string.h>
#include "dn_channel.h"
#include "dnm_local.h"
#include "dn_common.h"

//=========================== define ==========================================
#define REQ_BUF_CAST(var,type)      type*             ##var = (type*)(loc_v.ctrlCmdBuf  + sizeof(dn_api_cmd_hdr_t));
#define RSP_BUF_CAST(var,type)      type*             ##var = (type*)(loc_v.ctrlRespBuf + sizeof(dn_api_cmd_hdr_t));
#define REQ_HEADER_CAST(var)        dn_api_cmd_hdr_t* ##var = (dn_api_cmd_hdr_t*)(loc_v.ctrlCmdBuf);
#define RSP_HEADER_CAST(var)        dn_api_cmd_hdr_t* ##var = (dn_api_cmd_hdr_t*)(loc_v.ctrlRespBuf);

//=========================== variables =======================================

/// Variable local to the \ref module_dnm_local.
typedef struct {
   CH_DESC    ctrlChDesc;                                ///< Control channel descriptor.
   CH_DESC    notifChDesc;                               ///< Notification channel descriptor.
   INT8U      ctrlCmdBuf[DN_API_LOC_MAX_REQ_SIZE];       ///< Command to be sent over the control channel.
   INT8U      ctrlCmdBufLen;                             ///< Length on the command to be sent over the control channel, in bytes.
   INT8U      ctrlRespBuf[DN_API_LOC_MAX_RESP_SIZE];     ///< Response received over the control channel.
   INT8U      ctrlRespBufLen;                            ///< Length of the response received over the control channel, in bytes.
   INT8U*     notifRxBuf;                                ///< Notification received over the notification channel.
   INT8U      notifRxBufLen;                             ///< Length of the notification received over the notification channel, in bytes.
   INT8U      notifRespBuf[DN_API_LOC_MAX_RESP_SIZE];    ///< Notification response to be sent over the notifification channel.
   INT8U      notifRespBufLen;                           ///< Length of the notification response to be sent over the notifification channel, in bytes.
   eventNotifCb_t            eventNotifCb;               ///< Event notification call back function.
   locNotifCb_t              locNotifCb;                 ///< Notification call back function in pass through mode.
   rxNotifCb_t               rxNotifCb;                  ///< Neceive notification call back function.
   timeNotifCb_t             timeNotifCb;                ///< Time notification call back function.
   passThroughEventNotifCb_t passThroughEvNotifCb;       ///< Pass-through event notif call back function.
   passThroughNotifCb_t      passThroughNotifCb;         ///< Notification call back in pass-through mode.
   passThrough_mode_t        passThroughMode;            ///< Current pass-through mode.
   dnm_cli_cont_t*           cliCont;                    ///< CLI context.
   INT32S                    cliTraceFlag;               ///< CLI trace flag.
} loc_var_t;

static loc_var_t loc_v;

//=========================== prototype =======================================

//=========================== private =========================================

/**
\brief Process local notifications when the pass-through mode is OFF.
*/

void dnm_loc_processNotifications(void)
{
    INT8U                       cb_rsp = DN_API_RC_OK;
    INT32U                      rx_len = 0,msg_type;
    INT8U                       cmd_id;
    dn_api_cmd_hdr_t*           hdr;
    dn_api_loc_notif_events_t*  notif_event;   
    
    // read messages from notif. channel
    dn_readSyncMsg(loc_v.notifChDesc, loc_v.notifRxBuf, &rx_len,&msg_type, DN_API_LOC_MAX_NOTIF_SIZE, 0);
    loc_v.notifRxBufLen = (INT8U)rx_len;
    hdr                 = (dn_api_cmd_hdr_t *)loc_v.notifRxBuf;
    cmd_id              = hdr->cmdId;
    dnm_cli_traceDumpBlocking(loc_v.cliCont, loc_v.cliTraceFlag, loc_v.notifRxBuf,loc_v.notifRxBufLen, "locNotif RX:");    
   
    if((hdr->len == 0)||(rx_len == 0)) {
        // do something else??
        return;
    }
   
    if(loc_v.passThroughMode == PASSTHROUGH_OFF){
        switch(cmd_id) {
            case DN_API_LOC_NOTIF_EVENTS:
                if(loc_v.eventNotifCb != NULL) {
                    (*loc_v.eventNotifCb)((dn_api_loc_notif_events_t *)&loc_v.notifRxBuf[sizeof(dn_api_cmd_hdr_t)],&cb_rsp);         
                }
                break;
            case DN_API_LOC_NOTIF_RECEIVED:
                if(loc_v.rxNotifCb != NULL) {
                     dnm_cli_traceDumpBlocking(loc_v.cliCont, loc_v.cliTraceFlag, loc_v.notifRespBuf,loc_v.notifRespBufLen, "locNotif TX:");
                     (*loc_v.rxNotifCb)((dn_api_loc_notif_received_t *)&loc_v.notifRxBuf[sizeof(dn_api_cmd_hdr_t)],
                                        (INT8U)(loc_v.notifRxBufLen-sizeof(dn_api_cmd_hdr_t)));
                }
                break;
            case DN_API_LOC_NOTIF_TIME:
                if(loc_v.timeNotifCb != NULL) {
                    (*loc_v.timeNotifCb)((dn_api_loc_notif_time_t *)&loc_v.notifRxBuf[sizeof(dn_api_cmd_hdr_t)],
                    (INT8U)(loc_v.notifRxBufLen-sizeof(dn_api_cmd_hdr_t)));
                }
                break;
            default:
                break;
        }
    }
    else {
        if(cmd_id == DN_API_LOC_NOTIF_RECEIVED) {
            notif_event = (dn_api_loc_notif_events_t *)&loc_v.notifRxBuf[sizeof(dn_api_cmd_hdr_t)];
            if(loc_v.passThroughEvNotifCb != NULL) {
                (*loc_v.passThroughEvNotifCb)(ntohl(notif_event->events), ntohl(notif_event->alarms));
            }            
        }       
        if(loc_v.passThroughNotifCb != NULL) {
            (*loc_v.passThroughNotifCb)(&loc_v.notifRxBuf, loc_v.notifRxBufLen, &cb_rsp);
        }
    }

    // send reply
    dnm_loc_prepareNotifResponse(cmd_id, cb_rsp);
    loc_v.notifRespBufLen = sizeof(dn_api_empty_rsp_t);
    dn_sendReply(loc_v.notifChDesc,loc_v.notifRespBuf, loc_v.notifRespBufLen);       
    dnm_cli_traceDumpBlocking(loc_v.cliCont, loc_v.cliTraceFlag, loc_v.notifRespBuf,loc_v.notifRespBufLen, "locNotif TX:");
}

/**
\brief Processes simple commands; commands without any payload

\param[in]  Cmd The command to be invoked, e.g. DN_API_LOC_CMD_JOIN.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
static dn_error_t dnm_loc_processCmd(INT8U cmd,INT8U length)
{
    dn_error_t       rc;
    INT32U           rx_len = 0;
    REQ_HEADER_CAST(header_req)
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)

    if(cmd != DN_API_LOC_CMD_SEND_RAW) {
        header_req->cmdId    = cmd;
        header_req->len      = length;   
    }
    loc_v.ctrlCmdBufLen  = sizeof(dn_api_cmd_hdr_t) + length;
    dnm_cli_traceDumpBlocking(loc_v.cliCont, loc_v.cliTraceFlag, loc_v.ctrlCmdBuf, loc_v.ctrlCmdBufLen, "loc TX:");

    rc = dn_sendSyncMsg(loc_v.ctrlChDesc,
    loc_v.ctrlCmdBuf, loc_v.ctrlCmdBufLen, 
    DN_MSG_TYPE_NET_CTRL, loc_v.ctrlRespBuf, 
    DN_API_LOC_MAX_RESP_SIZE, &rx_len);
    loc_v.ctrlRespBufLen = (INT8U)rx_len;
    if (rc != DN_ERR_NONE) {
        dnm_cli_trace(loc_v.cliCont, loc_v.cliTraceFlag, "loc ERR: TX failed \n\r");
        return DN_ERR_ERROR;
    }
    dnm_cli_traceDumpBlocking(loc_v.cliCont, loc_v.cliTraceFlag, loc_v.ctrlRespBuf, loc_v.ctrlRespBufLen, "loc RX:");
    if(p_rsp->rc != DN_API_RC_OK){
        return DN_ERR_ERROR;
    }
    return DN_ERR_NONE;
}

//=========================== public ==========================================

/**
\brief Initialize this module.

This function verifies whether the control and notification channels have been
activated

\param[in] mode       Mode of operation.
\param[in] cliContext Pointer to the application cli context.
\param[in] TraceFlag  The trace flag to be used.
\param[in] pBuffer    Pointer to the buffers required for the local interface.
\param[in] buffLen    length  of the buffer passed.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_INVALID if either the control or the notification channel
hasn't been initialized.
*/
dn_error_t dnm_loc_init(passThrough_mode_t mode, dnm_cli_cont_t *cliContext, INT32S TraceFlag,
INT8U *pBuffer, INT8U buffLen) 
{
    dn_error_t rc;

    loc_v.passThroughMode   = mode;
    loc_v.cliCont           = cliContext;
    loc_v.cliTraceFlag      = TraceFlag;

    ASSERT(pBuffer != NULL && buffLen >= DN_API_LOC_MAX_NOTIF_SIZE);
    loc_v.notifRxBuf = pBuffer;

    rc = dn_getChannelDesc(DN_MSG_TYPE_NET_CTRL, &loc_v.ctrlChDesc);
    if (rc != DN_ERR_NONE){
        return rc;
    }

   rc = dn_getChannelDesc(DN_MSG_TYPE_NET_NOTIF, &loc_v.notifChDesc);
   if (rc != DN_ERR_NONE){
      return rc;
   }
   
   loc_v.passThroughMode = mode;
   return DN_ERR_NONE;
}

/**
\brief Set some configuration parameter.

\param[in]  paramId Identifier of the parameter to be set.
\param[in]  payload Pointer to the value to set the parameter to.
\param[in]  length  Length of the payload.
\param[out] rc      Location to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_setParameterCmd(INT8U paramId, INT8U *payload, 
INT8U length, INT8U *rc)
{
    dn_error_t  ret;
    REQ_BUF_CAST(p_setparam,dn_api_loc_setparam_t)
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)
    
    p_setparam->paramId = paramId;
    memcpy((void*)(&p_setparam->payload),(void*)(payload),length); 

    ret = dnm_loc_processCmd(DN_API_LOC_CMD_SETPARAM,sizeof(dn_api_loc_setparam_t) + length);

    if(p_setparam->paramId != paramId){
        dnm_cli_trace(loc_v.cliCont, loc_v.cliTraceFlag, "paramId mismatch\n\r");
        ret = DN_ERR_ERROR;
    }

    *rc = p_rsp->rc;
    return(ret);
}
 
/**
\brief Get some configuration parameter.

\param[in] paramId       Identifier for the parameter to be set.
\param[in,out] payload   Points to both the structure to pass with the 'GET'
command, and contains the result after the function returns.
\param[in] txPayloadLen  Number of bytes in the structure passed with the 'GET'
commmand.
\param[out] rxPayloadLen Length of the result, i.e. number of bytes written
back into the payload buffer.
\param[out] rc           Location to write the return code to.

\post After the function returns successfully, the location of <tt>payload</tt>
contains the following information:
- byte 0: the value of the return code.
- byte 1: the <tt>paramId</tt>.
- byte 2 and more: the value of the requested parameter.
You should therefore allocate two extra byte to the payload buffer for the 
return code and paramId. Similarly, the value of <tt>rxPayloadLen</tt>
should include those two bytes.

\note The <tt>payload</tt> parameter points to a buffer you have previously
allocated. You should allocate a buffer large enough to fit either the
structure sent to the function (<tt>txPayloadLen</tt> bytes), or the data 
written by this function (<tt>rxPayloadLen</tt> bytes). That is, when
allocating this buffer, it should be of size
<tt>max(txPayloadLen,rxPayloadLen)</tt> bytes.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_getParameterCmd(INT8U paramId, INT8U *payload, 
INT8U txPayloadLen, 
INT8U *rxPayloadLen, INT8U *rc)
{
    dn_error_t          ret;
    RSP_HEADER_CAST(header_rsp)
    REQ_BUF_CAST(p_getparam,dn_api_loc_getparam_t)
    RSP_BUF_CAST(p_getparam_rsp,dn_api_loc_rsp_getparam_t)
    
    p_getparam->paramId      = paramId;
   
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_GETPARAM,sizeof(dn_api_loc_getparam_t));

    *rc              = p_getparam_rsp->rc;
    *rxPayloadLen    = header_rsp->len;      
    if(*rxPayloadLen > 0){
        memcpy(payload, p_getparam_rsp, *rxPayloadLen);
    }
   
    if(p_getparam_rsp->paramId != paramId){
        dnm_cli_trace(loc_v.cliCont, loc_v.cliTraceFlag, "paramId mismatch\n\r");
        ret = DN_ERR_ERROR;
    }

    return(ret);
}

/**
\brief Have the mote join a network.

\param[out] rc Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_joinCmd(INT8U *rc)
{
    dn_error_t ret; 
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_JOIN,0);
    *rc = p_rsp->rc;
    return(ret);
}

/**
\brief Have the mote disconnect from the network its connected to.

\param[out] rc Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_disconnectCmd(INT8U *rc)
{
    dn_error_t ret; 
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_DISCONNECT,0);
    *rc = p_rsp->rc;
    return(ret);   
}

/**
\brief Reset the mote.

\param[out] rc Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_resetCmd(INT8U *rc)
{
    dn_error_t ret;   
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_RESET,0);
    *rc = p_rsp->rc;
    return(ret);   
}

/**
\brief Have the mote enter the low-power sleep mode.

\param[out] rc Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_lowPowerSleepCmd(INT8U *rc)
{
    dn_error_t ret;   
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_LOWPWRSLEEP,0);
    *rc = p_rsp->rc;
    return(ret);   
}

/**
\brief Clear the mote's non-volatile (NV) memory.

\param[out] rc Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_clearNVCmd(INT8U *rc)
{
    dn_error_t ret;   
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_CLEARNV,0);
    *rc = p_rsp->rc;
    return(ret);   
}

/**
\brief Send a packet into the network.

\param[in]  sendto  Pointer to the structure containing the packet and its
metadata.
\param[in]  length  Length of the payload, in bytes.
\param[out] rc      Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_sendtoCmd(loc_sendtoNW_t *sendto, INT8U length, INT8U *rc)
{
    dn_error_t                   ret;
    REQ_BUF_CAST(p_sendto,dn_api_loc_sendto_t)
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)
        
    memcpy((void*)(p_sendto),(void*)(&sendto->locSendTo),sizeof(dn_api_loc_sendto_t)+length); 
    p_sendto->destPort = htons(p_sendto->destPort);
    p_sendto->packetId = htons(p_sendto->packetId);
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_SENDTO,sizeof(dn_api_loc_sendto_t) + length);

    *rc = p_rsp->rc;
    return(ret);
}

/**
\brief Open a socket.

\param[in]  protocol Type of transport protocol for that socket.
\param[out] sockId   Location where to write the socket id to.
\param[out] rc       Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_openSocketCmd(INT8U protocol, INT8U *sockId, INT8U *rc)
{
    dn_error_t ret;
    REQ_BUF_CAST(p_open_socket,dn_api_loc_open_socket_t)
    RSP_BUF_CAST(p_open_socket_rsp,dn_api_loc_rsp_open_socket_t)

    p_open_socket->protocol  = protocol;

    ret = dnm_loc_processCmd(DN_API_LOC_CMD_OPEN_SOCKET,sizeof(dn_api_loc_open_socket_t));

    *rc      = p_open_socket_rsp->rc;
    *sockId  = p_open_socket_rsp->socketId;
    return(ret);      
}

/**
\brief Close a previously opened socket.

\param[in]  sockId Identifier of the socket to close.
\param[out] rc     Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_closeSocketCmd(INT8U sockId, INT8U *rc)
{
    dn_error_t ret;
    REQ_BUF_CAST(p_close_socket,dn_api_loc_close_socket_t)
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)

    p_close_socket->socketId  = sockId;

    ret = dnm_loc_processCmd(DN_API_LOC_CMD_CLOSE_SOCKET,sizeof(dn_api_loc_close_socket_t));
   
    *rc = p_rsp->rc;
    return(ret); 
}

/**
\brief Bind a socket to some port.

\param[in]  sockId The identifier of the socket.
\param[in]  port   The port to bind to.
\param[out] rc     Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_bindSocketCmd(INT8U sockId, INT16U port, INT8U *rc)
{  
    dn_error_t ret;
    REQ_BUF_CAST(p_bind_socket,dn_api_loc_bind_socket_t)
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)

    p_bind_socket->socketId  = sockId;
    p_bind_socket->port      = htons(port);

    ret = dnm_loc_processCmd(DN_API_LOC_CMD_BIND_SOCKET,sizeof(dn_api_loc_bind_socket_t));
   
    *rc = p_rsp->rc;
    return(ret); 
}

/**
\brief Request a service.

\param[in]  destAddr The address of the device to establish the service to.
\param[in]  svcType  The type of service to establish.
\param[in]  svcInfo  "value" of the service to request. The meaning of this 
parameter depends on the type of service.
\param[out] rc       Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_requestServiceCmd(dn_moteid_t destAddr,INT8U svcType, INT32U svcInfo,INT8U *rc)
{
    dn_error_t ret;
    REQ_BUF_CAST(p_service_request,dn_api_loc_svcrequest_t)
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)
    
    p_service_request->dest      = htons(destAddr);
    p_service_request->type      = svcType;
    p_service_request->value     = htonl(svcInfo);
   
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_SERVICE_REQUEST,sizeof(dn_api_loc_svcrequest_t));
   
    *rc = p_rsp->rc;
    return(ret); 
}

/**
\brief Get information about a particular service.

\param[in]  destAddr Address of the device the service of interest is
established to.
\param[in]  svcType  Type of the service of interest.
\param[out] svcRsp   Location to write the response structure to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_getAssignedServiceCmd(dn_moteid_t destAddr, INT8U svcType,dn_api_loc_rsp_get_service_t *svcRsp)
{
    dn_error_t ret;
    REQ_BUF_CAST(p_get_service,dn_api_loc_get_service_t)
    RSP_BUF_CAST(p_get_service_rsp,dn_api_loc_rsp_get_service_t)

    p_get_service->dest          = htons(destAddr);
    p_get_service->type          = svcType;
   
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_GET_SVC_INFO,sizeof(dn_api_loc_get_service_t));

    memcpy((void*)svcRsp,(void*)p_get_service_rsp,loc_v.ctrlRespBufLen);
    svcRsp->dest  = ntohs(svcRsp->dest);
    svcRsp->value = ntohl(svcRsp->value);

    return(ret); 
}

/**
\brief Have the mote perform a radio transmit test.

\param[in] type        Transmition type. Acceptable values are:
- #DN_API_RADIOTX_TYPE_CW for a continuous (unmodulated) wave.
- #DN_API_RADIOTX_TYPE_CM for a continuous modulated signal.
- #DN_API_RADIOTX_TYPE_PKT to send some number of packets.
\param[in] mask        Mask of channels (0-15) enabled for the test. Channel
0 (resp. 15) corresponds to 2.405GHz (resp. 2.480GHz), i.e. channel 15
(resp. 26) according to the IEEE802.15.4 numbering scheme. Bit 0 corresponds
to channel 0. For continuous wave and continuous modulation tests, enable
exactly one channel.
\param[in] power       Transmit power, in dB. Valid values are 0 and 8.
\param[in] numRepeats  Number of times to repeat the packet sequence
(0=repeat forever). Applies only to packet transmission tests.
\param[in] numSubtests Number of packets in each sequence. This parameter is
only used for packet test. Maximum allowed value is 10.
\param[in] subTests    Pointer to an array of numSubtests sequence 
definitions (up to 10) . This parameter is only used for packet test. Each
entry contains:
- the length of the packet (must be between 2 and 125 bytes).
- the delay between this packet at the next one, in microseconds.
\param[out] rc         Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_testRadioTxCmd(INT8U type, INT16U mask, INT8S power, 
                                  INT16U numRepeats, INT8U numSubtests, 
                                  dn_api_loc_testrftx_subtestparam_t * subTests,
                                  INT8U *rc)
{
    dn_error_t ret;
    INT8U i;
    REQ_BUF_CAST(p_radio_tx,dn_api_loc_testrftx_t)
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)

    p_radio_tx->type        = type;
    p_radio_tx->mask        = ntohs(mask);
    p_radio_tx->numRepeats  = ntohs(numRepeats);
    p_radio_tx->txPower     = power;
    p_radio_tx->numSubtests = numSubtests;
    memcpy((void*)&p_radio_tx->subtestParam,(void*)subTests,numSubtests * sizeof(dn_api_loc_testrftx_subtestparam_t));
    for(i=0; i<numSubtests; i++) {
      p_radio_tx->subtestParam[i].gap   = htons(p_radio_tx->subtestParam[i].gap);
    }
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_TESTRADIOTX,sizeof(dn_api_loc_testrftx_t) + numSubtests * sizeof(dn_api_loc_testrftx_subtestparam_t));
   
    *rc = p_rsp->rc;
    return(ret); 
}

/**
\brief Have the mote perform a radio receive test.

\param[in]  mask           Mask of channels (0-15) enabled for the test.
Channel 0 (resp. 15) corresponds to 2.405GHz (resp. 2.480GHz), i.e.
channel 15 (resp. 26) according to the IEEE802.15.4 numbering scheme. Bit 0
corresponds to channel 0. For continuous wave and continuous modulation
tests, only one channel should be enabled.
\param[in]  durationRxTest Duration of the Rx test, in seconds.
\param[out] rc             Location where to write the return code to.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_testRadioRxCmd(INT16U mask, INT16U durationRxTest, INT8U *rc)
{
    dn_error_t ret;
    REQ_BUF_CAST(p_radio_rx,dn_api_loc_testrfrx_t)
    RSP_BUF_CAST(p_rsp,dn_api_rc_rsp_t)

    p_radio_rx->mask             = htons(mask);
    p_radio_rx->timeSeconds      = htons(durationRxTest);
   
    ret = dnm_loc_processCmd(DN_API_LOC_CMD_TESTRADIORX,sizeof(dn_api_loc_testrfrx_t));
   
    *rc = p_rsp->rc;
    return(ret); 
}

/**
\brief Register a callback function for notification events.

\param[in] cb A pointer to the function to call.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_registerEventNotifCallback(eventNotifCb_t cb)
{
    if(cb != NULL) {
        loc_v.eventNotifCb = cb; 
        return DN_ERR_NONE;
    }
    else {
        return DN_ERR_ERROR;
    }
}

/**
\brief Register a callback function for events and alarms, in pass-through mode.

\param[in] cb A pointer to the function to call.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_registerPassThroughEvNotifCallback(passThroughEventNotifCb_t cb)
{
   if(cb != NULL) {
      loc_v.passThroughEvNotifCb = cb;
      return DN_ERR_NONE;
   }
   else {
      return DN_ERR_ERROR;
   }
}

/**
\brief Register a callback function for received frames, in passthrough mode.

\param[in] cb A pointer to the function to call.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_registerPassThroughNotifCallback(passThroughNotifCb_t cb)
{
   if(cb != NULL) {
      loc_v.passThroughNotifCb = cb;
      return DN_ERR_NONE;
   }
   else {
      return DN_ERR_ERROR;
   }
}


/**
\brief Register a callback function for received frames.

\param[in] cb A pointer to the function to call.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_registerRxNotifCallback(rxNotifCb_t cb)
{
    if(cb != NULL) {
        loc_v.rxNotifCb = cb; 
        return DN_ERR_NONE;
    }
    else {
        return DN_ERR_ERROR;
    }
}

/**
\brief Register a callback function for time notifications.

\param[in] cb A pointer to the function to call.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_registerTimeNotifCallback(timeNotifCb_t cb)
{
    if(cb != NULL) {
        loc_v.timeNotifCb = cb; 
        return DN_ERR_NONE;
    }
    else {
        return DN_ERR_ERROR;
    }
}


/**
\brief Send raw bytes into the network.

\param[in]     payload Pointer to the payload to send.
\param[in]     length  Number of bytes in the payload.
\param[out]    rsp     Location where the response will be written.
\param[in,out] rspLen  A pointer to the size of the response buffer. This
function will modify this value; after this function returns, it contains
the size of the received response, i.e. the number of byte written to the
buf.

\return #DN_ERR_NONE if the function completes successfully.
\return #DN_ERR_ERROR if the function can not be completed successfully.
*/
dn_error_t dnm_loc_sendRaw(INT8U* payload, INT8U length, INT8U* rsp, INT8U *rspLen)
{
    dn_error_t ret;
    REQ_HEADER_CAST(header_req)
    RSP_HEADER_CAST(header_rsp)

    memcpy((void*)(header_req),(void*)(payload),length); 

    ret = dnm_loc_processCmd(DN_API_LOC_CMD_SEND_RAW,length - sizeof(dn_api_cmd_hdr_t));

    memcpy((void*)rsp, (void*)header_rsp, loc_v.ctrlRespBufLen);
    *rspLen      = loc_v.ctrlRespBufLen;     
    return(ret); 
}

/**
\brief Prepare the response sent to a notification.
 
\param[in] notifId  Identifier of the notification.
\param[in] response Response code to answer.
*/
void dnm_loc_prepareNotifResponse(INT8U notifId, INT8U response)
{
   dn_api_empty_rsp_t *rsp;
   rsp = (dn_api_empty_rsp_t*)loc_v.notifRespBuf;
   rsp->hdr.cmdId = notifId;
   rsp->hdr.len = sizeof(rsp->rc);
   rsp->rc = response;
}
