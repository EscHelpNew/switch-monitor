/* Authenticated trusted-LAN developer uploads. Full application replacement
 * avoids returning to hbloader with live guest/GPU/audio threads and mappings. */
#include "remote_reload.h"
#include "reload_core.h"
#include "reload_reports.h"
#include "reload_main.h"
#include "launcher.h"
#include <switch.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROOT NXDK_SD_ROOT
#define PORT 28285
#define TRACE ROOT "/reload-bridge.log"
static char key[65],app_name[16],app_version[24],image_sha[65];
static Thread worker;
static int listener=-1,started;

static uint64_t now_ms(void)
{uint64_t t=armGetSystemTick(),f=armGetSystemTickFreq();return t/f*1000+(t%f)*1000/f;}
static int io(int fd,void *buffer,size_t bytes,int writing,uint64_t end)
{
    unsigned char *p=buffer;
    while(bytes){
        uint64_t now=now_ms();if(now>=end)return 0;uint64_t left=end-now;
        struct timeval tv={(long)(left/1000),(long)(left%1000)*1000};fd_set set;FD_ZERO(&set);FD_SET(fd,&set);
        int r=select(fd+1,writing?NULL:&set,writing?&set:NULL,NULL,&tv);
        if(r<0){if(errno==EINTR)continue;return 0;}if(!r)return 0;
        int n=writing?send(fd,p,bytes>32768?32768:bytes,MSG_NOSIGNAL):recv(fd,p,bytes>32768?32768:bytes,0);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
        if(n<=0)return 0;p+=n;bytes-=(size_t)n;
    }return 1;
}
static int reply(int fd,const char *s){return io(fd,(void*)s,strlen(s),1,now_ms()+5000);}
static int line(int fd,char out[256])
{
    uint64_t end=now_ms()+5000;
    for(unsigned i=0;i<255;i++){char c;if(!io(fd,&c,1,0,end))return 0;if(c=='\n'){out[i]=0;return 1;}if(c<' '||c>'~')return 0;out[i]=c;}return 0;
}
static int file_hash(const char *path,unsigned char out[32])
{
    FILE*f=fopen(path,"rb");if(!f)return 0;Sha256Context hash;sha256ContextCreate(&hash);
    unsigned char b[8192];size_t n,total=0;int ok=1;
    while((n=fread(b,1,sizeof b,f))){total+=n;if(total>RELOAD_MAX_BYTES){ok=0;break;}sha256ContextUpdate(&hash,b,n);}
    if(ferror(f))ok=0;if(fclose(f))ok=0;if(ok)sha256ContextGetHash(&hash,out);return ok;
}
int game_reload_image_digest(unsigned target,unsigned char sha[32])
{
    char path[512];unsigned char header[128];struct stat st;
    if(!reload_path(path,sizeof path,ROOT,target,0)||stat(path,&st)||st.st_size<0)return 0;
    FILE*f=fopen(path,"rb");if(!f)return 0;size_t n=fread(header,1,sizeof header,f);int ok=fclose(f)==0;
    return ok&&reload_nro_valid(header,n,(size_t)st.st_size)&&file_hash(path,sha);
}
/* This duplicates only the existing non-mutating identity check; the lab must
 * not link the game's installer or runtime. Never launch an arbitrary title. */
static int verified_title(void)
{
    if(!NXDK_TITLE_ID)return 0;
    AppletType type=appletGetAppletType();
    if((type!=AppletType_Application&&type!=AppletType_SystemApplication)||hosversionBefore(5,0,0))return 0;
    if(R_FAILED(nsInitialize()))return 0;bool installed=false;
    Result rc=nsIsAnyApplicationEntityInstalled(GAME_NX_HOME_TITLE_ID,&installed);
    NsApplicationControlData *data=NULL;u64 n=0;int ok=0;
    if(R_SUCCEEDED(rc)&&installed&&(data=calloc(1,sizeof *data))){
        rc=nsGetApplicationControlData(NsApplicationControlSource_Storage,GAME_NX_HOME_TITLE_ID,data,sizeof *data,&n);
        ok=R_SUCCEEDED(rc)&&n>=sizeof data->nacp&&data->nacp.presence_group_id==GAME_NX_HOME_TITLE_ID
            &&!strncmp(data->nacp.lang[0].name,GAME_NX_HOME_NAME,sizeof data->nacp.lang[0].name)
            &&!strncmp(data->nacp.lang[0].author,GAME_NX_HOME_AUTHOR,sizeof data->nacp.lang[0].author);
    }
    free(data);nsExit();return ok;
}
void game_reload_configure(int argc,char **argv)
{
    game_reload_main_init();
    const char *value=NULL,*host=NULL;int bad=0;
    for(int i=1;i<argc;i++){
        const char *s=NULL;
        if(!strncmp(argv[i],"--game-remote=",14))s=argv[i]+14;
        else if(!strncmp(argv[i],"--token=",8))s=argv[i]+8;
        else if(!strncmp(argv[i],"--game-log-host=",16))host=argv[i]+16;
        else if(!strncmp(argv[i],"--game-reload-image=",20)&&reload_token_valid(argv[i]+20))strcpy(image_sha,argv[i]+20);
        if(s){if(!reload_token_valid(s)||(value&&strcmp(value,s)))bad=1;value=s;}
    }
    if(!bad&&value)memcpy(key,value,65);
    struct in_addr address;if(key[0]&&host&&inet_pton(AF_INET,host,&address)==1)__nxlink_host=address;
}
static void store(int fd,const ReloadCommand *c)
{
    char temp[512];if(!reload_path(temp,sizeof temp,ROOT,c->target,1)){reply(fd,"ERR path\n");return;}
    mkdir("sdmc:/switch",0777);mkdir(ROOT,0777);FILE*f=fopen(temp,"wb");
    if(!f){reply(fd,"ERR storage\n");return;}
    int ok=reply(fd,"OK SEND\n");size_t left=c->bytes;unsigned char b[8192];uint64_t end=now_ms()+120000;
    while(ok&&left){size_t n=left>sizeof b?sizeof b:left;ok=io(fd,b,n,0,end)&&fwrite(b,1,n,f)==n;left-=n;}
    if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;
    if(ok&&c->target==RELOAD_BRIDGE)ok=reload_bridge_valid(temp);
    if(ok)ok=reload_publish(ROOT,c->target,c->bytes,c->sha256,file_hash);
    if(!ok){remove(temp);reply(fd,"ERR transfer_or_verification\n");return;}
    reply(fd,"OK STORED\n");
}
static void run(int fd,unsigned target)
{
    ReloadLaunch launch={0};unsigned char bridge_sha[32];
    if(!verified_title()||!reload_bridge_valid(ROOT "/" NXDK_BRIDGE_FILE)||!game_reload_image_digest(RELOAD_BRIDGE,bridge_sha)||!game_reload_image_digest(target,launch.sha256)){
        reply(fd,"ERR bridge_or_target_unavailable\n");return;
    }
    memcpy(launch.magic,"APPRLD2",8);launch.target=target;memcpy(launch.token,key,sizeof key);
    struct sockaddr_in peer={0};socklen_t peer_size=sizeof peer;
    /* The authenticated controller is the log receiver. nxlink --server is
     * optional during bootstrap, so its injected argv host may be absent. */
    if(getpeername(fd,(struct sockaddr*)&peer,&peer_size)||peer.sin_family!=AF_INET
       ||!inet_ntop(AF_INET,&peer.sin_addr,launch.host,sizeof launch.host)||!reload_launch_valid(&launch)){
        reply(fd,"ERR log_host\n");return;
    }
    if(!reply(fd,"OK RESTART_REQUESTED\n"))return;
    Result rc=game_reload_submit(&launch);
    memset(&launch,0,sizeof launch);
    if(R_FAILED(rc)){char text[64];snprintf(text,sizeof text,"ERR restart_%08X\n",rc);reply(fd,text);return;}
    /* Success never returns: the accepted system launch exits the process. */
}
static void connection(int fd)
{
    char request[256];if(!line(fd,request)||strncmp(request,"AUTH ",5)||!reload_token_equal(key,request+5)){reply(fd,"ERR auth\n");return;}
    if(!reply(fd,"OK AUTH\n")||!line(fd,request))return;ReloadCommand c;
    if(!reload_parse(request,&c)){reply(fd,"ERR command\n");return;}
    if(c.kind==RELOAD_CRASH){
        char *text=malloc(65536+1024);if(!text){reply(fd,"ERR allocation\n");return;}
        size_t bytes=0;FILE*f=fopen(TRACE,"rb");
        if(f){const char*title="== GAME reload bridge ==\n";bytes=strlen(title);memcpy(text,title,bytes);bytes+=fread(text+bytes,1,1024,f);fclose(f);}
        bytes+=reload_crash_reports(text+bytes,65536+1024-bytes,"sdmc:/atmosphere/crash_reports");
        char header[48];snprintf(header,sizeof header,"OK REPORT %zu\n",bytes);
        if(reply(fd,header))io(fd,text,bytes,1,now_ms()+15000);free(text);return;
    }
    if(c.kind==RELOAD_PUT){store(fd,&c);return;}if(c.kind==RELOAD_RUN){run(fd,c.target);return;}
    char text[384];snprintf(text,sizeof text,"{\"protocol\":2,\"app\":\"%s\",\"version\":\"%s\",\"image_sha256\":\"%s\",\"bridge_protocol\":\"%s\",\"bridge_compatible\":%s,\"title_verified\":%s}\n",app_name,app_version,image_sha,RELOAD_BRIDGE_MARKER,reload_bridge_valid(ROOT "/" NXDK_BRIDGE_FILE)?"true":"false",verified_title()?"true":"false");reply(fd,text);
}
static void service(void *unused)
{
    (void)unused;
    for(;;){int fd=accept(listener,NULL,NULL);if(fd<0){if(errno==EINTR)continue;break;}
        int flags=fcntl(fd,F_GETFL,0);if(flags>=0&&!fcntl(fd,F_SETFL,flags|O_NONBLOCK))connection(fd);close(fd);}
    close(listener);listener=-1;
}
void game_reload_start(const char *application,const char *version)
{
    if(!key[0]||started)return;snprintf(app_name,sizeof app_name,"%s",application);snprintf(app_version,sizeof app_version,"%s",version);
    listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener<0)return;int one=1;
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(PORT),.sin_addr={.s_addr=INADDR_ANY}};
    if(bind(listener,(struct sockaddr*)&address,sizeof address)||listen(listener,2)){close(listener);listener=-1;return;}
    Result rc=threadCreate(&worker,service,NULL,NULL,64*1024,0x30,-2);int created=R_SUCCEEDED(rc);
    if(created)rc=threadStart(&worker);
    if(R_FAILED(rc)){if(created)threadClose(&worker);close(listener);listener=-1;return;}started=1;game_reload_main_resident();
    if(image_sha[0])(void)reload_trace(TRACE,"target_service_started",0,0);
    /* Per-application opt-in development policy; no persistent OS setting. */
    (void)appletSetAutoSleepDisabled(true);
}
