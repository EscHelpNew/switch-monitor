#include "reload_reports.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);return 1;}}while(0)
static int put(const char*root,const char*name,const char*body)
{char path[1024];snprintf(path,sizeof path,"%s/%s",root,name);FILE*f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(body,1,strlen(body),f)==strlen(body);return fclose(f)==0&&ok;}
int main(int argc,char**argv)
{
    CHECK(argc==2);char out[256];size_t n=reload_crash_reports(out,sizeof out,argv[1]);CHECK(n&&strstr(out,"No crash reports"));
    char trace[1024];snprintf(trace,sizeof trace,"%s/trace.txt",argv[1]);
    CHECK(reload_trace(trace,"record",0,1));CHECK(reload_trace(trace,"next",123,0));
    FILE*tf=fopen(trace,"rb");CHECK(tf);size_t tn=fread(out,1,sizeof out-1,tf);out[tn]=0;fclose(tf);
    CHECK(strstr(out,"record rc=00000000")&&strstr(out,"next rc=0000007B"));
    CHECK(reload_trace(trace,"reset",0,1));tf=fopen(trace,"rb");CHECK(tf);tn=fread(out,1,sizeof out-1,tf);out[tn]=0;fclose(tf);CHECK(!strstr(out,"record")&&strstr(out,"reset"));
    CHECK(put(argv[1],"1.log","oldest\n"));CHECK(put(argv[1],"2.log","Process: ARM lab\n"));CHECK(put(argv[1],"3.log","Exception: data abort\n"));CHECK(put(argv[1],"9.bin","must not read"));
    n=reload_crash_reports(out,sizeof out,argv[1]);CHECK(n<sizeof out&&out[n]==0);CHECK(strstr(out,"2.log")&&strstr(out,"3.log"));CHECK(!strstr(out,"oldest")&&!strstr(out,"must not read"));
    char tiny[7];memset(tiny,'x',sizeof tiny);n=reload_crash_reports(tiny,6,argv[1]);CHECK(n<=5&&tiny[6]=='x'&&tiny[n]==0);
    puts("reload crash reports: latest two text reports, binary excluded, bounded output PASS");return 0;
}
