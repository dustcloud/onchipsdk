/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#ifndef __OCFDK_LOC_TASK_H
#define __OCFDK_LOC_TASK_H

#include "dn_common.h"
#include "dnm_cli.h"
#include "dnm_local.h"
#include "dn_mesh.h"

//============================ defines ========================================

#define BANDWIDTH_NONE       0x00000000

#define NETID_NONE           0x00
#define UDPPORT_NONE         0x00

#define JOIN_NO              0x00
#define JOIN_YES             0x01

// notification task
#define TASK_APP_LOCNOTIF_NAME         "locNotif"     ///< Name of the notification task.
#define TASK_APP_LOCNOTIF_PRIORITY     50             ///< Priority and ID of the notification task.
#define TASK_APP_LOCNOTIF_STK_SIZE     256            ///< Stack size of the notification task.

// control task
#define TASK_APP_LOCCTRL_NAME          "locCtrl"      ///< Name of the control task.
#define TASK_APP_LOCCTRL_PRIORITY      51             ///< Priority and ID of the control task.
#define TASK_APP_LOCCTRL_STK_SIZE      256            ///< Stack size of the control task.

//============================ typedef ========================================

/// Callback function for "packet received" notifications.
typedef dn_error_t loc_notifReceiveFunc_t(dn_api_loc_notif_received_t* rxFrame, INT8U length);

//=========================== prototypes ======================================

void loc_task_init(
   dnm_cli_cont_t*      cliContext,
   INT8U                fJoin,
   dn_netid_t           netId,
   INT16U               udpPort,
   OS_EVENT*            joinedSem,
   INT32U               bandwidth,
   OS_EVENT*            serviceSem
);

INT8U loc_getSocketId();

#endif
