#include "reload_main.h"
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static pthread_t main_thread;static atomic_uint calls,waits,main_waits,main_returned;static ReloadLaunch seen;static int accepted_case;
Handle threadGetCurHandle(void){return pthread_equal(pthread_self(),main_thread)?1:2;}
void ueventCreate(UEvent*e,bool clear){CHECK(!pthread_mutex_init(&e->mutex,NULL));CHECK(!pthread_cond_init(&e->condition,NULL));e->signal=false;e->auto_clear=clear;}
void ueventSignal(UEvent*e){pthread_mutex_lock(&e->mutex);e->signal=true;pthread_cond_broadcast(&e->condition);pthread_mutex_unlock(&e->mutex);}
Waiter waiterForUEvent(UEvent*e){return (Waiter){e};}
Result waitSingle(Waiter w,uint64_t timeout){CHECK(timeout==UINT64_MAX);atomic_fetch_add(&waits,1);if(pthread_equal(pthread_self(),main_thread))atomic_fetch_add(&main_waits,1);pthread_mutex_lock(&w.event->mutex);while(!w.event->signal)pthread_cond_wait(&w.event->condition,&w.event->mutex);if(w.event->auto_clear)w.event->signal=false;pthread_mutex_unlock(&w.event->mutex);return 0;}
Result game_reload_request_launch(const ReloadLaunch*r,int*accepted)
{CHECK(pthread_equal(pthread_self(),main_thread));*accepted=accepted_case;memcpy(&seen,r,sizeof seen);atomic_fetch_add(&calls,1);return 0xCAFE;}
static void *submit(void*);
static atomic_int exit_called;
Result game_reload_request_exit(void){CHECK(pthread_equal(pthread_self(),main_thread));atomic_store(&exit_called,1);return 0xCAFE;}
static void *late_submit(void*x){
    (void)x;while(!atomic_load(&exit_called)){struct timespec t={0,1000000};nanosleep(&t,NULL);}
    /* Reserve/queue after the final poll, while the main exit IPC has failed. */
    game_reload_game_done();
    pthread_t queued_worker;CHECK(!pthread_create(&queued_worker,NULL,submit,NULL));
    for(unsigned i=0;(!game_reload_main_pending()||!atomic_load(&main_waits))&&i<2000;i++){struct timespec t={0,1000000};nanosleep(&t,NULL);}
    CHECK(game_reload_main_pending()&&atomic_load(&main_waits)&&!atomic_load(&main_returned));
    puts("reload main: late RUN during failed resident exit cannot return to loader PASS");exit(0);
    return NULL;
}
static ReloadLaunch packet;static unsigned reply;
static void *submit(void*x){
    (void)x;reply=game_reload_submit(&packet);
    if(accepted_case){
        CHECK(reply==0xCAFE&&game_reload_main_pending());CHECK(game_reload_submit(&packet)!=0);
        for(unsigned i=0;!atomic_load(&main_waits)&&!atomic_load(&main_returned)&&i<2000;i++){struct timespec t={0,1000000};nanosleep(&t,NULL);}
        CHECK(atomic_load(&main_waits)>0&&!atomic_load(&main_returned));
        puts("reload main: accepted launch with Exit error cannot return to loader PASS");exit(0);
    }
    game_reload_game_done();return NULL;
}
static void *wrong_owner(void*x){(void)x;game_reload_main_poll();return NULL;}
static void *finish(void*x){(void)x;struct timespec t={0,20000000};nanosleep(&t,NULL);game_reload_game_done();return NULL;}
int main(int argc,char**argv)
{
    CHECK(argc==2);main_thread=pthread_self();game_reload_main_init();
    accepted_case=!strcmp(argv[1],"accepted");
    if(!strcmp(argv[1],"finish")){
        game_reload_main_finish(); /* no resident: ordinary loader return permitted */
        game_reload_main_resident();pthread_t thread;CHECK(!pthread_create(&thread,NULL,late_submit,NULL));
        game_reload_main_finish();atomic_store(&main_returned,1);CHECK(0);
    }
    if(!strcmp(argv[1],"idle")){
        pthread_t thread;CHECK(!pthread_create(&thread,NULL,finish,NULL));game_reload_wait_game();pthread_join(thread,NULL);
        CHECK(atomic_load(&waits)>=1&&atomic_load(&calls)==0);puts("reload main: idle GAME wait blocks on event PASS");return 0;
    }
    memcpy(packet.magic,"APPRLD2",8);packet.target=RELOAD_LAB;memset(packet.token,'a',64);strcpy(packet.host,"192.168.1.2");
    CHECK(game_reload_submit(&packet)!=0); /* main must not submit+wait itself */
    pthread_t worker;CHECK(!pthread_create(&worker,NULL,submit,NULL));
    for(unsigned i=0;!game_reload_main_pending()&&i<2000;i++){struct timespec t={0,1000000};nanosleep(&t,NULL);}
    CHECK(game_reload_main_pending()&&atomic_load(&calls)==0);
    if(accepted_case){game_reload_main_poll();atomic_store(&main_returned,1);pthread_join(worker,NULL);CHECK(0);}
    pthread_t other;CHECK(!pthread_create(&other,NULL,wrong_owner,NULL));pthread_join(other,NULL);CHECK(atomic_load(&calls)==0);
    game_reload_wait_game();pthread_join(worker,NULL);
    CHECK(reply==0xCAFE&&atomic_load(&calls)==1&&!memcmp(&packet,&seen,sizeof seen));
    CHECK(!game_reload_main_pending());puts("reload main: queued worker, main-only SDK launch, full packet, error recovery PASS");return 0;
}
