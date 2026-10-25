#include "reload_main.h"
#include <switch.h>
#include <stdatomic.h>
#include <string.h>

enum{IDLE,RESERVED,PENDING,RUNNING,DONE};
static UEvent wake,reply;
static Handle owner;
static atomic_int state,game_done,accepted_launch,resident;
static ReloadLaunch queued;
static Result launch_result;
extern Result game_reload_request_launch(const ReloadLaunch *,int *);
extern Result game_reload_request_exit(void);

void game_reload_main_init(void)
{
    owner=threadGetCurHandle();
    ueventCreate(&wake,true);ueventCreate(&reply,true);
}
int game_reload_main_pending(void)
{return atomic_load_explicit(&state,memory_order_acquire)!=IDLE||atomic_load(&accepted_launch);}
uint32_t game_reload_submit(const ReloadLaunch *record)
{
    if(!owner||threadGetCurHandle()==owner||atomic_load(&accepted_launch))
        return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    int expected=IDLE;
    if(!atomic_compare_exchange_strong(&state,&expected,RESERVED))
        return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    queued=*record;
    atomic_store_explicit(&state,PENDING,memory_order_release);ueventSignal(&wake);
    while(atomic_load_explicit(&state,memory_order_acquire)!=DONE)
        (void)waitSingle(waiterForUEvent(&reply),UINT64_MAX);
    Result result=launch_result;
    atomic_store_explicit(&state,IDLE,memory_order_release);
    return result;
}
void game_reload_main_poll(void)
{
    if(atomic_load_explicit(&state,memory_order_acquire)!=PENDING||threadGetCurHandle()!=owner)return;
    int expected=PENDING;
    if(!atomic_compare_exchange_strong(&state,&expected,RUNNING))return;
    ReloadLaunch record=queued;memset(&queued,0,sizeof queued);
    int accepted=0;
    launch_result=game_reload_request_launch(&record,&accepted);
    memset(&record,0,sizeof record);
    /* Success never returns. If manager accepted the launch but Exit failed,
     * main must still not return to hbloader with a live network worker. */
    if(accepted)atomic_store(&accepted_launch,1);
    atomic_store_explicit(&state,DONE,memory_order_release);ueventSignal(&reply);
    if(accepted)for(;;)(void)waitSingle(waiterForUEvent(&wake),UINT64_MAX);
}
void game_reload_game_done(void)
{atomic_store_explicit(&game_done,1,memory_order_release);ueventSignal(&wake);}
void game_reload_wait_game(void)
{
    for(;;){
        game_reload_main_poll();
        if(atomic_load_explicit(&game_done,memory_order_acquire))return;
        (void)waitSingle(waiterForUEvent(&wake),UINT64_MAX);
    }
}

void game_reload_main_resident(void)
{atomic_store(&resident,1);}
void game_reload_main_finish(void)
{
    if(!atomic_load(&resident))return;
    /* Even a late RESERVED/PENDING job cannot cause an NRO loader return.
     * User exit may win over RUN; all workers retain process lifetime. */
    if(threadGetCurHandle()==owner){game_reload_main_poll();(void)game_reload_request_exit();}
    for(;;)(void)waitSingle(waiterForUEvent(&wake),UINT64_MAX);
}
