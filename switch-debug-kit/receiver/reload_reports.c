#include "reload_reports.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
typedef struct{char name[128];time_t time;}Report;
int reload_trace(const char *path,const char *stage,unsigned result,int reset)
{
    FILE*f=fopen(path,reset?"wb":"ab");if(!f)return 0;
    int ok=fprintf(f,"%s rc=%08X\n",stage,result)>=0;
    if(fclose(f))ok=0;return ok;
}
static int newer(const Report*a,const Report*b)
{return !b->name[0]||a->time>b->time||(a->time==b->time&&strcmp(a->name,b->name)>0);}
size_t reload_crash_reports(char *out,size_t capacity,const char *root)
{
    if(!out||!capacity||!root)return 0;out[0]=0;Report best[2]={0};
    DIR*d=opendir(root);
    if(d){struct dirent*e;unsigned scanned=0;
        while((e=readdir(d))&&scanned++<4096){
            size_t n=strlen(e->d_name);if(n<5||n>=sizeof best[0].name||strcmp(e->d_name+n-4,".log"))continue;
            int valid=1;for(size_t i=0;i<n;i++){unsigned char c=e->d_name[i];if(!isalnum(c)&&c!='_'&&c!='-'&&c!='.')valid=0;}
            if(!valid)continue;char path[768];int len=snprintf(path,sizeof path,"%s/%s",root,e->d_name);struct stat st;
            if(len<0||(size_t)len>=sizeof path||stat(path,&st)||!S_ISREG(st.st_mode))continue;
            Report next={0};strcpy(next.name,e->d_name);next.time=st.st_mtime;
            if(newer(&next,&best[0])){best[1]=best[0];best[0]=next;}else if(newer(&next,&best[1]))best[1]=next;
        }closedir(d);
    }
    size_t used=0;
    for(unsigned i=0;i<2&&best[i].name[0]&&used+1<capacity;i++){
        char path[768];snprintf(path,sizeof path,"%s/%s",root,best[i].name);
        int n=snprintf(out+used,capacity-used,"\n== %s ==\n",best[i].name);
        if(n<0)break;if((size_t)n>=capacity-used){used=capacity-1;break;}used+=(size_t)n;
        FILE*f=fopen(path,"rb");if(f){size_t room=capacity-used-1;if(room>32768)room=32768;used+=fread(out+used,1,room,f);fclose(f);}
        out[used]=0;
    }
    if(!best[0].name[0]){int n=snprintf(out,capacity,"No crash reports in fixed Atmosphere directory.\n");used=n<0?0:(size_t)n<capacity?(size_t)n:capacity-1;}
    out[used]=0;return used;
}
