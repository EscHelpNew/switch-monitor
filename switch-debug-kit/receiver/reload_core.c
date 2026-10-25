#include "reload_core.h"
#include "reload_project.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static int hex(unsigned char c)
{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
int reload_token_valid(const char *s)
{if(!s||strlen(s)!=64)return 0;for(unsigned i=0;i<64;i++)if(hex((unsigned char)s[i])<0)return 0;return 1;}
int reload_token_equal(const char *a,const char *b)
{if(!reload_token_valid(a)||!reload_token_valid(b))return 0;unsigned diff=0;for(unsigned i=0;i<64;i++)diff|=(unsigned char)a[i]^(unsigned char)b[i];return diff==0;}
int reload_launch_valid(const ReloadLaunch *p)
{
    _Static_assert(sizeof(ReloadLaunch)==128,"version2 launch ABI");
    if(!p||memcmp(p->magic,"APPRLD2",8)||p->target<RELOAD_GAME||p->target>RELOAD_LAB
       ||p->token[64]||!reload_token_valid(p->token)||!memchr(p->host,0,sizeof p->host))return 0;
    unsigned a,b,c,d;char extra;
    return sscanf(p->host,"%u.%u.%u.%u%c",&a,&b,&c,&d,&extra)==4&&a<=255&&b<=255&&c<=255&&d<=255;
}
static unsigned target(const char *s)
{return !strcmp(s,"BRIDGE")?RELOAD_BRIDGE:!strcmp(s,"GAME")?RELOAD_GAME:!strcmp(s,"LAB")?RELOAD_LAB:0;}
int reload_parse(const char *s,ReloadCommand *out)
{
    if(!s||!out||strlen(s)>255)return 0;ReloadCommand c={0};
    if(!strcmp(s,"STATUS")){c.kind=RELOAD_STATUS;*out=c;return 1;}
    if(!strcmp(s,"CRASH")){c.kind=RELOAD_CRASH;*out=c;return 1;}
    char name[8],amount[24],sha[65],extra;
    if(!strncmp(s,"RUN ",4)&&sscanf(s+4,"%7s %c",name,&extra)==1){c.target=target(name);if(c.target<RELOAD_GAME)return 0;c.kind=RELOAD_RUN;*out=c;return 1;}
    if(strncmp(s,"PUT ",4)||sscanf(s+4,"%7s %23s %64s %c",name,amount,sha,&extra)!=3)return 0;
    c.target=target(name);if(!c.target||!reload_token_valid(sha))return 0;
    uint64_t n=0;for(size_t i=0;amount[i];i++){if(amount[i]<'0'||amount[i]>'9')return 0;n=n*10+(unsigned)(amount[i]-'0');if(n>RELOAD_MAX_BYTES)return 0;}
    if(n<128)return 0;c.kind=RELOAD_PUT;c.bytes=(size_t)n;
    for(unsigned i=0;i<32;i++)c.sha256[i]=(unsigned char)(hex(sha[i*2])*16+hex(sha[i*2+1]));
    *out=c;return 1;
}
static uint32_t le32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int reload_nro_valid(const unsigned char *h,size_t n,size_t file_bytes)
{
    if(!h||n<128||file_bytes<128||file_bytes>RELOAD_MAX_BYTES||memcmp(h+0x10,"NRO0",4))return 0;
    uint32_t image=le32(h+0x18),end=0;
    if(image<4096||image>file_bytes||(image&4095)||le32(h+0x38)>512u*1024u*1024u)return 0;
    for(unsigned i=0;i<3;i++){
        uint32_t offset=le32(h+0x20+i*8),bytes=le32(h+0x24+i*8);
        if(offset!=end||(offset&4095)||(bytes&4095)||offset>image||bytes>image-offset)return 0;
        if(i==0&&bytes<4096)return 0;end=offset+bytes;
    }
    return end==image;
}
int reload_path(char *out,size_t capacity,const char *root,unsigned t,int temp)
{
    const char *name=t==RELOAD_BRIDGE?NXDK_BRIDGE_FILE:t==RELOAD_GAME?NXDK_GAME_FILE:t==RELOAD_LAB?NXDK_LAB_FILE:NULL;
    if(!name||!out||!root||!capacity)return 0;int n=snprintf(out,capacity,"%s/%s%s",root,name,temp?".incoming":"");
    return n>=0&&(size_t)n<capacity;
}
int reload_bridge_valid(const char *path)
{
    struct stat st;unsigned char h[128],buffer[4096+sizeof(RELOAD_BRIDGE_MARKER)];
    if(stat(path,&st)||st.st_size<0)return 0;FILE*f=fopen(path,"rb");if(!f)return 0;
    size_t n=fread(h,1,sizeof h,f);int found=0;
    if(reload_nro_valid(h,n,(size_t)st.st_size)&&!fseek(f,(long)le32(h+0x28),SEEK_SET)){
        size_t left=le32(h+0x2c),carry=0;
        while(left&&!found){
            size_t chunk=left>4096?4096:left;size_t got=fread(buffer+carry,1,chunk,f);
            if(got!=chunk)break;left-=got;size_t total=carry+got;
            for(size_t i=0;i+sizeof(RELOAD_BRIDGE_MARKER)<=total;i++)
                if(!memcmp(buffer+i,RELOAD_BRIDGE_MARKER,sizeof(RELOAD_BRIDGE_MARKER))){found=1;break;}
            carry=total<sizeof(RELOAD_BRIDGE_MARKER)-1?total:sizeof(RELOAD_BRIDGE_MARKER)-1;
            memmove(buffer,buffer+total-carry,carry);
        }
    }
    if(fclose(f))found=0;return found;
}
int reload_publish(const char *root,unsigned t,size_t bytes,const unsigned char expected[32],ReloadFileHash hash)
{
    char final[512],temp[512],backup[544];unsigned char header[128],digest[32];struct stat st;
    if(!expected||!hash||!reload_path(final,sizeof final,root,t,0)||!reload_path(temp,sizeof temp,root,t,1))return 0;
    if(stat(temp,&st)||st.st_size<0||(uint64_t)st.st_size!=bytes)return 0;
    FILE*f=fopen(temp,"rb");if(!f)return 0;size_t n=fread(header,1,sizeof header,f);int closed=fclose(f)==0;
    if(!closed||!reload_nro_valid(header,n,bytes)||!hash(temp,digest)||memcmp(digest,expected,32))return 0;
    snprintf(backup,sizeof backup,"%s.previous",final);
    int existed=stat(final,&st)==0;
    if(existed){if(remove(backup)&&stat(backup,&st)==0)return 0;if(rename(final,backup))return 0;}
    if(rename(temp,final)){if(existed)(void)rename(backup,final);return 0;}
    return 1;
}
