#include "remote_reload.h"
#include "reload_user_channel.h"
#include "reload_core.h"
#include "reload_reports.h"
#include "reload_project.h"
#include <stdio.h>
#include <string.h>
#define ROOT NXDK_SD_ROOT
#define TRACE ROOT "/reload-bridge.log"
extern int game_reload_image_digest(unsigned,unsigned char[32]);
static AppletStorage unclaimed;
static int has_unclaimed;
int game_reload_take_user_channel(AppletStorage *out)
{
    if(!out||!has_unclaimed)return 0;
    *out=unclaimed;memset(&unclaimed,0,sizeof unclaimed);has_unclaimed=0;return 1;
}
int game_reload_early_bridge(void)
{
    if(has_unclaimed)return 0;
    AppletStorage storage={0};ReloadLaunch launch={0};s64 size=0;
    if(R_FAILED(appletPopLaunchParameter(&storage,AppletLaunchParameterKind_UserChannel)))return 0;
    Result rc=appletStorageGetSize(&storage,&size);
    if(R_SUCCEEDED(rc)&&size==(s64)sizeof launch)rc=appletStorageRead(&storage,0,&launch,sizeof launch);
    /* Preserve ownership of ordinary application startup data. The game takes
     * the original storage object after this helper; no byte copy/truncation. */
    if(R_FAILED(rc)||size!=(s64)sizeof launch||memcmp(launch.magic,"APPRLD2",8)){
        unclaimed=storage;has_unclaimed=1;return 0;
    }
    appletStorageClose(&storage);
    (void)reload_trace(TRACE,"bridge_received_record",rc,1);
    if(!reload_launch_valid(&launch)||!envHasNextLoad()){(void)reload_trace(TRACE,"invalid_record_or_loader",1,0);fprintf(stderr,"[RELOAD] Invalid launch record or loader unavailable\n");return 1;}
    unsigned char actual[32];
    if(!game_reload_image_digest(launch.target,actual)||memcmp(actual,launch.sha256,32)){(void)reload_trace(TRACE,"image_verification_failed",1,0);fprintf(stderr,"[RELOAD] Target verification failed\n");return 1;}
    (void)reload_trace(TRACE,"target_digest_verified",0,0);
    char path[512],args[1024],hex[65];reload_path(path,sizeof path,ROOT,launch.target,0);
    for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",actual[i]);
    snprintf(args,sizeof args,"%s --game-remote=%s --token=%s --game-log-host=%s --game-reload-image=%s",path,launch.token,launch.token,launch.host,hex);
    rc=envSetNextLoad(path,args);memset(args,0,sizeof args);memset(&launch,0,sizeof launch);
    (void)reload_trace(TRACE,"next_load_set",rc,0);
    if(R_FAILED(rc))fprintf(stderr,"[RELOAD] Loader handoff failed: %08X\n",rc);
    return 1; /* No app workers or private mappings exist; loader return is safe. */
}
