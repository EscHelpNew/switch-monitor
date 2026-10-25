#include "reload_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h>
#include <bcrypt.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static int file_hash(const char *path,unsigned char out[32])
{
    BCRYPT_ALG_HANDLE alg=NULL; BCRYPT_HASH_HANDLE hash=NULL;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0;
    if(ok)ok=BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)>=0;
    unsigned char b[1024];size_t n;
    while(ok && (n=fread(b,1,sizeof b,f)))ok=BCryptHashData(hash,b,(ULONG)n,0)>=0;
    if(ferror(f))ok=0;
    if(ok)ok=BCryptFinishHash(hash,out,32,0)>=0;
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
    fclose(f);return ok;
}
static void le32(unsigned char *p,unsigned v){for(int i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static int write_image(const char *path,unsigned char tag)
{
    unsigned char b[0x3000]={0};memcpy(b+0x10,"NRO0",4);le32(b+0x18,sizeof b);
    le32(b+0x24,0x1000);le32(b+0x28,0x1000);le32(b+0x2c,0x1000);
    le32(b+0x30,0x2000);le32(b+0x34,0x1000);b[0x100]=tag;
    FILE *f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(b,1,sizeof b,f)==sizeof b;return fclose(f)==0&&ok;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);ReloadCommand c;
    CHECK(reload_parse("STATUS",&c)&&c.kind==RELOAD_STATUS);
    CHECK(reload_parse("CRASH",&c)&&c.kind==RELOAD_CRASH);
    CHECK(reload_parse("RUN GAME",&c)&&c.target==RELOAD_GAME);
    CHECK(reload_parse("RUN LAB",&c)&&c.target==RELOAD_LAB);
    CHECK(!reload_parse("RUN BRIDGE",&c));CHECK(!reload_parse("RUN ../save",&c));
    CHECK(!reload_parse("STATUS extra",&c));CHECK(!reload_parse("RUN GAME x",&c));
    const char *sha="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    char line[256];snprintf(line,sizeof line,"PUT GAME 12288 %s",sha);
    CHECK(reload_parse(line,&c)&&c.bytes==12288&&c.target==RELOAD_GAME&&c.sha256[0]==1);
    const char *bad[]={"PUT GAME -1 x","PUT GAME 0 x","PUT GAME 67108865 x","PUT GAME 99999999999999999999999999 x","PUT GAME 12288 short","PUT BAD 12288 x","PUT GAME 12288 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef x"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)CHECK(!reload_parse(bad[i],&c));
    CHECK(reload_token_valid(sha));CHECK(!reload_token_valid("abc"));CHECK(reload_token_equal(sha,sha));
    CHECK(!reload_token_equal(sha,"1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    ReloadLaunch launch={0};memcpy(launch.magic,"APPRLD2",8);launch.target=RELOAD_LAB;
    strcpy(launch.token,sha);strcpy(launch.host,"192.168.1.1");CHECK(reload_launch_valid(&launch));
    strcpy(launch.host,"1.2.3.4 x");CHECK(!reload_launch_valid(&launch));
    strcpy(launch.host,"256.2.3.4");CHECK(!reload_launch_valid(&launch));
    strcpy(launch.host,"192.168.1.1");launch.target=RELOAD_BRIDGE;CHECK(!reload_launch_valid(&launch));
    unsigned char header[128]={0};CHECK(!reload_nro_valid(header,sizeof header,12288));
    memcpy(header+0x10,"NRO0",4);le32(header+0x18,0x3000);le32(header+0x24,0x1000);
    le32(header+0x28,0x1000);le32(header+0x2c,0x1000);le32(header+0x30,0x2000);le32(header+0x34,0x1000);
    CHECK(reload_nro_valid(header,sizeof header,12288));CHECK(!reload_nro_valid(header,sizeof header,12287));
    le32(header+0x28,0xfffff000);CHECK(!reload_nro_valid(header,sizeof header,12288));
    le32(header+0x28,0x1000);le32(header+0x38,0xffffffff);CHECK(!reload_nro_valid(header,sizeof header,12288));
    char path[512],tmp[512];CHECK(reload_path(path,sizeof path,argv[1],RELOAD_GAME,0));
    CHECK(reload_path(tmp,sizeof tmp,argv[1],RELOAD_GAME,1));CHECK(!reload_path(tmp,3,argv[1],RELOAD_GAME,1));
    CHECK(!reload_path(tmp,sizeof tmp,argv[1],99,1));CHECK(reload_path(tmp,sizeof tmp,argv[1],RELOAD_GAME,1));
    CHECK(write_image(path,'A')&&write_image(tmp,'B'));
    CHECK(!reload_bridge_valid(path));FILE *bridge=fopen(path,"r+b");CHECK(bridge);
    CHECK(fseek(bridge,0x1100,SEEK_SET)==0);CHECK(fwrite(RELOAD_BRIDGE_MARKER,1,sizeof(RELOAD_BRIDGE_MARKER),bridge)==sizeof(RELOAD_BRIDGE_MARKER));
    CHECK(fclose(bridge)==0);CHECK(reload_bridge_valid(path));
    unsigned char old[32],next[32],actual[32];CHECK(file_hash(path,old)&&file_hash(tmp,next));
    CHECK(!reload_publish(argv[1],RELOAD_GAME,12288,old,file_hash));CHECK(file_hash(path,actual)&&!memcmp(old,actual,32));
    CHECK(!reload_publish(argv[1],RELOAD_GAME,12289,next,file_hash));
    CHECK(reload_publish(argv[1],RELOAD_GAME,12288,next,file_hash));CHECK(file_hash(path,actual)&&!memcmp(next,actual,32));
    CHECK(!reload_publish(argv[1],RELOAD_GAME,12288,next,file_hash)); /* missing temp */
    CHECK(write_image(tmp,'C'));CHECK(file_hash(tmp,next));CHECK(reload_publish(argv[1],RELOAD_GAME,12288,next,file_hash));
    CHECK(file_hash(path,actual)&&!memcmp(next,actual,32));
    CHECK(write_image(tmp,'X'));FILE*f=fopen(tmp,"r+b");CHECK(f);CHECK(fseek(f,0x10,SEEK_SET)==0);CHECK(fwrite("BAD!",1,4,f)==4);CHECK(fclose(f)==0);
    CHECK(file_hash(tmp,next));CHECK(!reload_publish(argv[1],RELOAD_GAME,12288,next,file_hash));
    puts("reload core: parser, bounded NRO, token, failed update and replacement PASS");return 0;
}
