#include "reload_core.h"
#include "launcher.h"
#include <switch.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int stage,fail_at,closed,exited;static ReloadLaunch captured;static jmp_buf exit_point;
static int can_jump;
static uint64_t actual_title=0x0500000000001234ULL;
static Result info_result;static unsigned restarts;
static Service self={1};
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);return 1;}}while(0)
Result svcGetInfo(u64 *out,int type,Handle handle,uint64_t index)
{if(type!=InfoType_ProgramId||handle!=CUR_PROCESS_HANDLE||index)return 995;*out=actual_title;return info_result;}
Result appletRestartProgram(const void*data,size_t n)
{restarts++;if(n!=sizeof captured)return 994;memcpy(&captured,data,n);return fail_at==5?105:0;}
Result appletCreateStorage(AppletStorage*s,int64_t n){stage=1;s->active=1;return n==128?(fail_at==1?101:0):999;}
Result appletStorageWrite(AppletStorage*s,int64_t offset,const void*data,size_t n)
{if(!s->active||offset||n!=128)return 998;stage=2;memcpy(&captured,data,n);return fail_at==2?102:0;}
void appletStorageClose(AppletStorage*s){if(s->active){closed++;s->active=0;}}
Result appletRequestLaunchApplication(uint64_t title,AppletStorage*s)
{stage=3;if(title!=0x0500000000001234ULL||!s->active)return 997;appletStorageClose(s);return fail_at==3?103:0;}
void svcExitProcess(void){exited++;fprintf(stderr,"FAIL: raw kernel exit abandons managed application handoff\n");exit(2);}
Service *appletGetServiceSession_SelfController(void){return &self;}
int serviceIsActive(Service*s){return s->active;}
void serviceAssumeDomain(Service*s){if(s!=&self)stage=99;}
Result serviceDispatch(Service*s,uint32_t command){stage=4;if(s!=&self||command!=0)return 996;return fail_at==4?104:0;}
void svcSleepThread(int64_t ns){if(ns!=1000000000)stage=98;if(!can_jump){fprintf(stderr,"FAIL unexpected accepted launch before identity/error checks\n");exit(2);}longjmp(exit_point,1);}
extern Result game_reload_request_launch(const ReloadLaunch*,int*);
int main(void)
{
    ReloadLaunch record={0};memcpy(record.magic,"APPRLD2",8);record.target=RELOAD_GAME;
#if NXDK_TITLE_ID == 0
    int refused=-1;Result disabled=game_reload_request_launch(&record,&refused);
    CHECK(disabled!=0&&refused==0&&!stage&&!restarts&&!exited);
    puts("reload launch: unconfigured title zero refuses before any system call PASS");return 0;
#endif
    memset(record.token,'a',64);strcpy(record.host,"192.168.1.2");
    /* Only positively identified own title may use current-program restart.
     * Error must return, never fall through to another launch method. */
    static int accepted;accepted=-1;info_result=106;
    CHECK(game_reload_request_launch(&record,&accepted)==106&&accepted==0&&!restarts&&!stage);
    info_result=0;fail_at=5;
    CHECK(game_reload_request_launch(&record,&accepted)==105&&accepted==0&&restarts==1&&!stage);
    fail_at=0;restarts=0;
    can_jump=1;
    if(!setjmp(exit_point)){game_reload_request_launch(&record,&accepted);CHECK(0);}
    can_jump=0;
    CHECK(restarts==1&&accepted==1&&!stage&&!closed&&!exited&&!memcmp(&captured,&record,sizeof record));
    actual_title=0x010018100cd46000ULL;restarts=0;

    for(int fail=1;fail<=4;fail++){
        fail_at=fail;stage=closed=exited=0;
        int accepted=-1;Result rc=game_reload_request_launch(&record,&accepted);
        CHECK(rc==(unsigned)(100+fail)&&stage==fail&&exited==0);
        CHECK(accepted==(fail==4));
        CHECK(closed==(fail==1?0:1));
    }
    fail_at=0;stage=closed=exited=0;
    accepted=-1;
    can_jump=1;
    if(!setjmp(exit_point)){game_reload_request_launch(&record,&accepted);CHECK(0);}
    CHECK(stage==4&&closed==1&&exited==0&&!restarts);CHECK(!memcmp(&captured,&record,sizeof record));
    puts("reload launch: explicit GAME title, full packet, failure retains process, managed exit only PASS");return 0;
}
