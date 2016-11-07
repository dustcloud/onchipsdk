/*
Copyright (c) 2013, Dust Networks.  All rights reserved.
*/

#include "dn_common.h"
#include "dn_exe_hdr.h"
#include "cli_task.h"
#include "loc_task.h"
#include "dnm_local.h"
#include "app_task_cfg.h"
#include "Ver.h"
#include "well_known_ports.h"

//=========================== defines =========================================
#define SERVICE_PORT                   WKP_USER_1
#define PAYLOAD_LENGTH                 10

//=========================== variables =======================================

typedef struct {
   OS_EVENT*       joinedSem;
   OS_EVENT*       serviceSem;
   OS_STK          serviceTaskStack[TASK_APP_SERVICE_STK_SIZE];
} service_app_vars_t;

service_app_vars_t service_app_vars;

//=========================== prototypes ======================================

static void serviceTask(void* unused);
void printService();

//=========================== initialization ==================================

/**
\brief This is the entry point for the application code.
*/
int p2_init(void) {
   INT8U           osErr;
   
   // create semaphore for loc_task to indicate when joined
   service_app_vars.joinedSem  = OSSemCreate(0);
   
   // create semaphore for loc_task to indicate when service granted
   service_app_vars.serviceSem = OSSemCreate(0);
   
   //===== initialize helper tasks
   
   cli_task_init(
      "service",                            // appName
      NULL                                  // cliCmds
   );
   loc_task_init(
      JOIN_YES,                             // fJoin
      NETID_NONE,                           // netId
      WKP_USER_1,                           // udpPort
      service_app_vars.joinedSem,           // joinedSem
      1000,                                 // bandwidth
      service_app_vars.serviceSem           // serviceSem
   );
   
   //===== initialize sendTask
   
   osErr = OSTaskCreateExt(
      serviceTask,
      (void *) 0,
      (OS_STK*) (&service_app_vars.serviceTaskStack[TASK_APP_SERVICE_STK_SIZE - 1]),
      TASK_APP_SERVICE_PRIORITY,
      TASK_APP_SERVICE_PRIORITY,
      (OS_STK*) service_app_vars.serviceTaskStack,
      TASK_APP_SERVICE_STK_SIZE,
      (void *) 0,
      OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR
   );
   ASSERT(osErr==OS_ERR_NONE);
   OSTaskNameSet(TASK_APP_SERVICE_PRIORITY, (INT8U*)TASK_APP_SERVICE_NAME, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   return 0;
}

static void serviceTask(void* unused) {
   INT8U      osErr;
   
   // wait for the loc_task to finish joining the network
   OSSemPend(service_app_vars.joinedSem, 0, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   // print
   dnm_ucli_printf("Done joining!\r\n");
   
   // wait for the loc_task to be granted service
   OSSemPend(service_app_vars.serviceSem, 0, &osErr);
   ASSERT(osErr==OS_ERR_NONE);
   
   // print
   dnm_ucli_printf("Service granted!\r\n");
   
   while (1) { // this is a task, it executes forever
      
      // retrieve current service
      printService();
      
      // wait a bit
      OSTimeDly(10000);
   }
}

void printService() {
   dn_error_t                     dnErr;
   dn_api_loc_rsp_get_service_t   currentService;
   
   // retrieve service information
   dnErr = dnm_loc_getAssignedServiceCmd(
      DN_MGR_SHORT_ADDR,       // destAddr
      DN_API_SERVICE_TYPE_BW,  // svcType
      &currentService          // svcRsp 
   );
   ASSERT(dnErr==DN_ERR_NONE && currentService.rc==DN_API_RC_OK);
   
   // print
   dnm_ucli_printf("Service:\r\n");
   dnm_ucli_printf("- dest:   0x%x\r\n",  currentService.dest);
   dnm_ucli_printf("- type:   %d\r\n",    currentService.type);
   dnm_ucli_printf("- status: %d\r\n",    currentService.status);
   dnm_ucli_printf("- value:  %d ms\r\n", currentService.value);
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
