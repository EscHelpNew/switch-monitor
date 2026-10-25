#include "reload_core.h"
#include "launcher.h"
#include <switch.h>

/* Request the specific installed GAME application, not the application that
 * originally hosted hbmenu. The latter may be a completely different game.
 * libnx transfers/closes the storage in appletRequestLaunchApplication.
 * Only accepted launch and managed self-exit requests permit handoff. */
Result game_reload_request_launch(const ReloadLaunch *record,int *accepted)
{
    *accepted=0;
    if(!NXDK_TITLE_ID)return MAKERESULT(Module_Libnx,LibnxError_NotInitialized);
    Service *self=appletGetServiceSession_SelfController();
    if(!serviceIsActive(self))return MAKERESULT(Module_Libnx,LibnxError_NotInitialized);
    /* Current-title restart is safe only for the actual GAME host. Unlike
     * CreateApplicationAndRequestToStart + Exit (079), it does not intentionally
     * replace the foreground application with a newly requested background one.
     * Never use the development Jump API from a foreign hbmenu host. */
    u64 title=0;
    Result identity=svcGetInfo(&title,InfoType_ProgramId,CUR_PROCESS_HANDLE,0);
    if(R_FAILED(identity))return identity;
    if(title==GAME_NX_HOME_TITLE_ID){
        Result rc=appletRestartProgram(record,sizeof *record);
        if(R_FAILED(rc))return rc;
        *accepted=1;
        for(;;)svcSleepThread(1000000000);
    }
    /* First bootstrap from a foreign title needs the explicit GAME launch.
     * Horizon may leave it Background until the user opens GAME from HOME. */
    AppletStorage storage={0};
    Result rc=appletCreateStorage(&storage,sizeof *record);
    if(R_FAILED(rc))return rc;
    rc=appletStorageWrite(&storage,0,record,sizeof *record);
    if(R_FAILED(rc)){appletStorageClose(&storage);return rc;}
    rc=appletRequestLaunchApplication(GAME_NX_HOME_TITLE_ID,&storage);
    if(R_FAILED(rc))return rc;
    *accepted=1;
    /* libnx's normal managed exit uses this exact ISelfController::Exit(0)
     * command. Use the manager path rather than raw svcExitProcess; the077
     * raw-exit experiment did not yield a verified target launch.
     * Do not run libc/SDL teardown concurrently with live game workers. */
    serviceAssumeDomain(self);
    rc=serviceDispatch(self,0);
    if(R_SUCCEEDED(rc))for(;;)svcSleepThread(1000000000);
    /* A returned self-exit error does not cancel the accepted launch above. */
    return rc;
}

Result game_reload_request_exit(void)
{
    Service *self=appletGetServiceSession_SelfController();
    if(!serviceIsActive(self))return MAKERESULT(Module_Libnx,LibnxError_NotInitialized);
    serviceAssumeDomain(self);
    return serviceDispatch(self,0);
}
