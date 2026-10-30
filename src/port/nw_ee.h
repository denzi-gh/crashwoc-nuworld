/* EE kernel names renamed to nw_ee_* (src/shim/ee.c), since kernel32 exports CreateThread, ResumeThread,
 * TerminateThread and GetThreadId. */
#ifndef NUWORLD_PORT_NW_EE_H
#define NUWORLD_PORT_NW_EE_H

#define ChangeThreadPriority nw_ee_ChangeThreadPriority
#define CreateSema nw_ee_CreateSema
#define CreateThread nw_ee_CreateThread
#define DeleteThread nw_ee_DeleteThread
#define FlushCache nw_ee_FlushCache
#define GetThreadId nw_ee_GetThreadId
#define PollSema nw_ee_PollSema
#define ResumeThread nw_ee_ResumeThread
#define StartThread nw_ee_StartThread
#define TerminateThread nw_ee_TerminateThread
#define WaitSema nw_ee_WaitSema
#define iSignalSema nw_ee_iSignalSema
#define iSuspendThread nw_ee_iSuspendThread

#endif
