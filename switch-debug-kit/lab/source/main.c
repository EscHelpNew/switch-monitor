/* Trusted-LAN native-code test appliance, deliberately independent of GAME. */
#include <switch.h>
#include "lab_core.h"
#include "sleep_policy.h"
#include "remote_reload.h"
#include "reload_main.h"
#include "reload_project.h"
#include "reload_user_channel.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LAB_PORT 28284
#define OUTPUT_BYTES 65536u
typedef struct {
    uint64_t inner_ticks,outer_ticks,cpu_ticks,control_before,status_before,control_after,status_after;
    uint32_t core_before,core_after,cpu_result,fp_match,valid_reply,warmup;int32_t returned;LabReply reply;uint8_t output[32],output_sha256[32];
} Record;
static struct {
    Jit jit;int loaded,worker_live;Thread worker;LabEntry entry;
    uint8_t *input,*working,*scratch,*output;size_t input_bytes;
    uint8_t module_sha[32],input_sha[32];LabCommand job;
    atomic_uint state,cancel;uint64_t deadline,frequency;unsigned records,failed;
    Record record[LAB_MAX_TRIALS],empty;uint32_t clocks_before[3],clocks_after[3];
} lab;
static uint64_t ticks(void){__asm__ __volatile__("isb":::"memory");uint64_t t=armGetSystemTick();__asm__ __volatile__("":::"memory");return t;}
static uint64_t fpcr(void){uint64_t v;__asm__ __volatile__("mrs %0,fpcr":"=r"(v));return v;}
static uint64_t fpsr(void){uint64_t v;__asm__ __volatile__("mrs %0,fpsr":"=r"(v));return v;}
static void fp_restore(uint64_t control,uint64_t status){__asm__ __volatile__("msr fpcr,%0\n\tisb\n\tmsr fpsr,%1"::"r"(control),"r"(status):"memory");}
static uint32_t cancelled(void *unused){(void)unused;return atomic_load_explicit(&lab.cancel,memory_order_relaxed)||ticks()>=lab.deadline;}
static Result cpu_ticks(uint64_t *value)
{
    *value=0;if(!envIsSyscallHinted(0x29))return 1;
    Handle h=threadGetCurHandle();if(!h||h==CUR_THREAD_HANDLE)return 2;
    Result rc=svcGetInfo(value,InfoType_ThreadTickCount,h,UINT64_MAX);
    if(R_FAILED(rc))rc=svcGetInfo(value,InfoType_ThreadTickCountDeprecated,h,UINT64_MAX);
    return rc;
}
static uint32_t core(void){return envIsSyscallHinted(0x10)?svcGetCurrentProcessorNumber():UINT32_MAX;}
static void clocks(uint32_t out[3])
{
    memset(out,0,12);
    if(hosversionAtLeast(8,0,0)){
        if(R_FAILED(clkrstInitialize()))return;
        const PcvModuleId ids[]={PcvModuleId_CpuBus,PcvModuleId_GPU,PcvModuleId_EMC};
        for(unsigned i=0;i<3;i++){ClkrstSession s;if(R_SUCCEEDED(clkrstOpenSession(&s,ids[i],3))){clkrstGetClockRate(&s,&out[i]);clkrstCloseSession(&s);}}clkrstExit();
    }else{
        if(R_FAILED(pcvInitialize()))return;
        const PcvModule ids[]={PcvModule_CpuBus,PcvModule_GPU,PcvModule_EMC};
        for(unsigned i=0;i<3;i++)pcvGetClockRate(ids[i],&out[i]);pcvExit();
    }
}
static void measure(Record *r,int empty)
{
    memset(r,0,sizeof *r);memcpy(lab.working,lab.input,lab.input_bytes);memset(lab.scratch,0,LAB_SCRATCH_BYTES);memset(lab.output,0,OUTPUT_BYTES);
    LabHostAPI api={sizeof api,LAB_ABI_VERSION,cancelled,NULL};
    LabRequest request={sizeof request,LAB_ABI_VERSION,lab.working,lab.input_bytes,lab.scratch,LAB_SCRATCH_BYTES,lab.output,OUTPUT_BYTES,lab.job.iterations,0,0};
    r->reply=(LabReply){sizeof(LabReply),LAB_ABI_VERSION,0,0,0,0};
    r->control_before=fpcr();r->status_before=fpsr();
    uint64_t cpu0=0,cpu1=0,outer0=ticks();Result rc0=cpu_ticks(&cpu0);r->core_before=core();uint64_t inner0=ticks();
    if(!empty)r->returned=lab.entry(&api,&request,&r->reply);
    uint64_t inner1=ticks();r->core_after=core();Result rc1=cpu_ticks(&cpu1);uint64_t outer1=ticks();
    r->inner_ticks=inner1-inner0;r->outer_ticks=outer1-outer0;r->cpu_result=lab_cpu_delta(cpu0,cpu1,rc0,rc1,&r->cpu_ticks);
    r->control_after=fpcr();r->status_after=fpsr();r->fp_match=r->control_before==r->control_after&&r->status_before==r->status_after;
    fp_restore(r->control_before,r->status_before);
    if(!empty){memcpy(r->output,lab.output,sizeof r->output);r->valid_reply=lab_reply_validate(&r->reply,r->returned,lab.job.iterations,OUTPUT_BYTES);if(r->valid_reply)sha256CalculateHash(r->output_sha256,lab.output,r->reply.output_bytes);}
}
static void worker(void *unused)
{
    (void)unused;uint64_t saved_control=fpcr(),saved_status=fpsr();lab.records=lab.failed=0;clocks(lab.clocks_before);
    lab.deadline=ticks()+lab.frequency*lab.job.budget_ms/1000;measure(&lab.empty,1);
    for(unsigned i=0;i<lab.job.warmups&&!cancelled(NULL);i++){Record warm;measure(&warm,0);if(warm.returned||warm.reply.status||!warm.valid_reply||!warm.fp_match){warm.warmup=1;lab.record[lab.records++]=warm;lab.failed=1;break;}}
    for(unsigned i=0;i<lab.job.trials&&!lab.failed&&!cancelled(NULL);i++){
        Record *r=&lab.record[lab.records++];measure(r,0);
        if(r->returned||r->reply.status||!r->valid_reply||!r->fp_match){lab.failed=1;break;}
    }
    clocks(lab.clocks_after);fp_restore(saved_control,saved_status);atomic_store_explicit(&lab.state,2,memory_order_release);
}
static void join_worker(void){if(lab.worker_live&&atomic_load_explicit(&lab.state,memory_order_acquire)!=1){threadWaitForExit(&lab.worker);threadClose(&lab.worker);lab.worker_live=0;}}
static int socket_io_until(int fd,void *buffer,size_t bytes,int writing,uint64_t end)
{
    uint8_t *p=buffer;
    while(bytes){
        if(ticks()>=end)return 0;fd_set set;FD_ZERO(&set);FD_SET(fd,&set);struct timeval tv={0,100000};
        int ready=select(fd+1,writing?NULL:&set,writing?&set:NULL,NULL,&tv);
        if(ready<0){if(errno==EINTR)continue;return 0;}if(!ready)continue;
        int n=writing?send(fd,p,bytes>65536?65536:bytes,MSG_NOSIGNAL):recv(fd,p,bytes>65536?65536:bytes,0);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;if(n<=0)return 0;p+=n;bytes-=(size_t)n;
    }return 1;
}
static int socket_io(int fd,void *buffer,size_t bytes,int writing){return socket_io_until(fd,buffer,bytes,writing,ticks()+lab.frequency*5);}
static int line(int fd,char *out,size_t capacity)
{
    uint64_t end=ticks()+lab.frequency*5;
    for(size_t i=0;i<capacity;i++){char c;if(!socket_io_until(fd,&c,1,0,end))return 0;if(c=='\n'){out[i]=0;return 1;}if(c<' '||c>'~')return 0;out[i]=c;}return 0;
}
static void error(int fd,const char *name){char text[96];snprintf(text,sizeof text,"ERR %s\n",name);socket_io(fd,text,strlen(text),1);}
static void json(int fd,const char *text){char header[40];snprintf(header,sizeof header,"OK %zu\n",strlen(text));if(socket_io(fd,header,strlen(header),1))socket_io(fd,(void*)text,strlen(text),1);}
static char results[32768];
static void report(int fd,int full)
{
    char module_sha[65],input_sha[65];for(unsigned i=0;i<32;i++){snprintf(module_sha+2*i,3,"%02x",lab.module_sha[i]);snprintf(input_sha+2*i,3,"%02x",lab.input_sha[i]);}
    unsigned state=atomic_load_explicit(&lab.state,memory_order_acquire);size_t used=(size_t)snprintf(results,sizeof results,"{\"abi\":1,\"state\":%u,\"module\":%d,\"module_sha256\":\"%s\",\"input_sha256\":\"%s\",\"input_bytes\":%zu,\"counter_hz\":%llu,\"applet_type\":%u,\"hos_version\":%u,\"jit_type\":%u,\"syscall_cpu\":%u,\"records\":%u,\"failed\":%u",state,lab.loaded,module_sha,input_sha,lab.input_bytes,(unsigned long long)lab.frequency,(unsigned)appletGetAppletType(),hosversionGet(),lab.loaded?(unsigned)lab.jit.type:UINT32_MAX,envIsSyscallHinted(0x29),state==2?lab.records:0,state==2?lab.failed:0);
    if(full&&state==2){
        used+=(size_t)snprintf(results+used,sizeof results-used,",\"clocks_before\":[%u,%u,%u],\"clocks_after\":[%u,%u,%u],\"empty\":{\"wall\":%llu,\"cpu\":%llu,\"cpu_result\":%u},\"trials\":[",lab.clocks_before[0],lab.clocks_before[1],lab.clocks_before[2],lab.clocks_after[0],lab.clocks_after[1],lab.clocks_after[2],(unsigned long long)lab.empty.inner_ticks,(unsigned long long)lab.empty.cpu_ticks,lab.empty.cpu_result);
        for(unsigned i=0;i<lab.records;i++){
            if(used+2048>=sizeof results){error(fd,"report_bounds");return;}
            Record *r=&lab.record[i];char hex[65],sha[65];for(unsigned j=0;j<32;j++){snprintf(hex+2*j,3,"%02x",r->output[j]);snprintf(sha+2*j,3,"%02x",r->output_sha256[j]);}
            used+=(size_t)snprintf(results+used,sizeof results-used,"%s{\"wall\":%llu,\"outer\":%llu,\"cpu\":%llu,\"cpu_result\":%u,\"core\":[%u,%u],\"returned\":%d,\"status\":%u,\"valid_reply\":%u,\"warmup\":%u,\"completed\":%u,\"checksum\":%llu,\"output_bytes\":%llu,\"output_prefix\":\"%s\",\"output_sha256\":\"%s\",\"fp_match\":%u,\"fpcr_before\":%llu,\"fpsr_before\":%llu,\"fpcr_after\":%llu,\"fpsr_after\":%llu}",i?",":"",(unsigned long long)r->inner_ticks,(unsigned long long)r->outer_ticks,(unsigned long long)r->cpu_ticks,r->cpu_result,r->core_before,r->core_after,r->returned,r->reply.status,r->valid_reply,r->warmup,r->reply.completed_iterations,(unsigned long long)r->reply.checksum,(unsigned long long)r->reply.output_bytes,hex,sha,r->fp_match,(unsigned long long)r->control_before,(unsigned long long)r->status_before,(unsigned long long)r->control_after,(unsigned long long)r->status_after);
        }used+=(size_t)snprintf(results+used,sizeof results-used,"]");
    }
    if(used+2<sizeof results){strcpy(results+used,"}");json(fd,results);}else error(fd,"report_bounds");
}
static void connection(int fd,const char *token)
{
    char request[256];if(!line(fd,request,sizeof request)||strncmp(request,"AUTH ",5)||!lab_token_equal(token,request+5)){error(fd,"auth");return;}
    char auth[]="OK AUTH\n";if(!socket_io(fd,auth,sizeof auth-1,1)||!line(fd,request,sizeof request))return;
    LabCommand command;if(!lab_parse_command(request,&command)){error(fd,"command");return;}
    if(command.kind==LAB_CMD_STATUS){report(fd,0);return;}if(command.kind==LAB_CMD_RESULTS){report(fd,1);return;}
    if(command.kind==LAB_CMD_CANCEL){atomic_store(&lab.cancel,1);json(fd,"{\"cancel_requested\":true}");return;}
    if(atomic_load_explicit(&lab.state,memory_order_acquire)==1){error(fd,"busy");return;}join_worker();
    if(command.kind==LAB_CMD_RUN){
        if(!lab.loaded||!lab.input_bytes){error(fd,"missing_module_or_input");return;}
        lab.job=command;atomic_store(&lab.cancel,0);atomic_store(&lab.state,1);
        Result rc=threadCreate(&lab.worker,worker,NULL,NULL,256*1024,0x2c,-2);
        if(R_SUCCEEDED(rc)){lab.worker_live=1;rc=threadStart(&lab.worker);}
        if(R_FAILED(rc)){atomic_store(&lab.state,0);if(lab.worker_live){threadClose(&lab.worker);lab.worker_live=0;}error(fd,"worker_unavailable");return;}
        json(fd,"{\"started\":true}");return;
    }
    uint8_t *blob=malloc(command.bytes);if(!blob){error(fd,"allocation");return;}
    if(!socket_io(fd,blob,command.bytes,0)){free(blob);return;}
    uint8_t digest[32];sha256CalculateHash(digest,blob,command.bytes);
    if(memcmp(digest,command.sha256,32)){free(blob);error(fd,"sha256");return;}
    if(command.kind==LAB_CMD_INPUT){
        uint8_t *working=malloc(command.bytes);if(!working){free(blob);error(fd,"allocation");return;}
        free(lab.input);free(lab.working);lab.input=blob;lab.working=working;lab.input_bytes=command.bytes;memcpy(lab.input_sha,digest,32);atomic_store(&lab.state,0);json(fd,"{\"input_loaded\":true}");return;
    }
    LabImageHeader header;uint8_t image_sha[32];
    if(command.bytes<64){free(blob);error(fd,"image_header");return;}
    sha256CalculateHash(image_sha,blob+64,command.bytes-64);
    if(!lab_image_validate(blob,command.bytes,image_sha,&header)){free(blob);error(fd,"image_header");return;}
    Jit next;memset(&next,0,sizeof next);Result rc=jitCreate(&next,(header.image_bytes+4095u)&~4095u);int created=R_SUCCEEDED(rc);
    if(R_SUCCEEDED(rc)){rc=jitTransitionToWritable(&next);if(R_SUCCEEDED(rc)){memset(jitGetRwAddr(&next),0,next.size);memcpy(jitGetRwAddr(&next),blob+64,header.image_bytes);rc=jitTransitionToExecutable(&next);}}
    free(blob);if(R_FAILED(rc)){if(created)jitClose(&next);error(fd,"jit_unavailable");return;}
    if(lab.loaded&&R_FAILED(jitClose(&lab.jit))){lab.loaded=0;jitClose(&next);error(fd,"old_jit_close");return;}
    lab.jit=next;lab.entry=(LabEntry)((uint8_t*)jitGetRxAddr(&lab.jit)+header.entry_offset);lab.loaded=1;memcpy(lab.module_sha,digest,32);atomic_store(&lab.state,0);json(fd,"{\"module_loaded\":true}");
}
static int load_token(char token[129],int argc,char **argv)
{
    const char *path=NXDK_SD_ROOT "/token.txt",*value=NULL;
    for(int i=1;i<argc;i++){if(!strncmp(argv[i],"--token=",8))value=argv[i]+8;else if(!strncmp(argv[i],"--token-file=",13))path=argv[i]+13;}
    if(value){if(strlen(value)>128)return 0;strcpy(token,value);}else{FILE*f=fopen(path,"rb");if(!f)return 0;size_t n=fread(token,1,128,f);int extra=fgetc(f);fclose(f);if(extra!=EOF)return 0;token[n]=0;token[strcspn(token,"\r\n")]=0;}
    size_t n=strlen(token);if(n<16||n>128)return 0;for(size_t i=0;i<n;i++)if(token[i]<'!'||token[i]>'~')return 0;return 1;
}
int main(int argc,char **argv)
{
    if(game_reload_early_bridge())return 0;
    AppletStorage ordinary={0};
    if(game_reload_take_user_channel(&ordinary))appletStorageClose(&ordinary);
    game_reload_configure(argc,argv);
    LabSleepGuard sleep_guard={0};lab_sleep_begin(&sleep_guard);
    consoleInit(NULL);padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
    lab.frequency=armGetSystemTickFreq();lab.scratch=malloc(LAB_SCRATCH_BYTES);lab.output=malloc(OUTPUT_BYTES);char token[129]={0};int listener=-1;
    if(load_token(token,argc,argv)&&lab.frequency&&lab.scratch&&lab.output&&R_SUCCEEDED(socketInitializeDefault())){
        /* stdout remains the framebuffer console; stderr is the nxlink log.
         * Redirecting both streams made every on-screen printf disappear. */
        nxlinkStdioForDebug();game_reload_start("ARM lab","1.0.0");
        listener=socket(AF_INET,SOCK_STREAM,0);int one=1;if(listener>=0)setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
        struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(LAB_PORT),.sin_addr={.s_addr=INADDR_ANY}};
        if(listener>=0&&(bind(listener,(struct sockaddr*)&address,sizeof address)||listen(listener,1))){close(listener);listener=-1;}
        if(listener>=0)fcntl(listener,F_SETFL,O_NONBLOCK);
    }
    printf("Switch ARM lab - standalone resident test host\nABI1 port%u / %s\nTrusted native modules only; cooperative cancellation.\nNo game renderer/audio. '+' exits when idle.\nAuto-sleep:%s\n",LAB_PORT,listener>=0?"authenticated listener ready":"unavailable (token/network/allocation)",sleep_guard.saved?"disabled for this application":"service unavailable");
    fprintf(stderr,"[LAB] v1.0.0 screen=stdout log=stderr listener=%s\n",listener>=0?"ready":"unavailable");
    unsigned previous=UINT32_MAX;
    while(appletMainLoop()){
        game_reload_main_poll();
        padUpdate(&pad);unsigned state=atomic_load_explicit(&lab.state,memory_order_acquire);
        if(state!=previous){printf("State:%s\n",state==1?"running":state==2?"results ready":"idle");previous=state;}
        if(padGetButtonsDown(&pad)&HidNpadButton_Plus){if(state!=1)break;atomic_store(&lab.cancel,1);printf("Cancel requested; waiting for cooperative module.\n");}
        if(listener>=0){int fd=accept(listener,NULL,NULL);if(fd>=0){fcntl(fd,F_SETFL,O_NONBLOCK);connection(fd,token);close(fd);}}
        consoleUpdate(NULL);
    }
    game_reload_main_finish();
    if(atomic_load(&lab.state)==1){atomic_store(&lab.cancel,1);threadWaitForExit(&lab.worker);}join_worker();if(lab.loaded)jitClose(&lab.jit);
    if(listener>=0){close(listener);socketExit();}free(lab.input);free(lab.working);free(lab.scratch);free(lab.output);memset(token,0,sizeof token);lab_sleep_end(&sleep_guard);consoleExit(NULL);return 0;
}
