#include <switch.h>
#include "remote_reload.h"
#include "reload_user_channel.h"
#include "reload_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL bridge line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static int available,closed,loads;static s64 payload_size;static ReloadLaunch packet;
Result appletPopLaunchParameter(AppletStorage*s,int kind){CHECK(kind==AppletLaunchParameterKind_UserChannel);if(!available)return 1;available=0;s->active=1;return 0;}
Result appletStorageGetSize(AppletStorage*s,s64*n){CHECK(s->active);*n=payload_size;return 0;}
Result appletStorageRead(AppletStorage*s,s64 offset,void*out,size_t n){CHECK(s->active&&!offset&&n==sizeof packet);memcpy(out,&packet,n);return 0;}
void appletStorageClose(AppletStorage*s){CHECK(s->active);s->active=0;closed++;}
int envHasNextLoad(void){return 1;}
Result envSetNextLoad(const char*path,const char*args){CHECK(strstr(path,"game.nro")&&strstr(args,"--game-reload-image="));loads++;return 0;}
int game_reload_image_digest(unsigned target,unsigned char sha[32]){CHECK(target==RELOAD_GAME);memset(sha,0,32);return 1;}
int reload_trace(const char*p,const char*stage,unsigned rc,int reset){(void)p;(void)stage;(void)rc;(void)reset;return 1;}
int main(void){
 AppletStorage ordinary={0};CHECK(!game_reload_early_bridge());CHECK(!game_reload_take_user_channel(&ordinary)&&!closed);
 available=1;payload_size=8;CHECK(!game_reload_early_bridge());CHECK(!closed&&!loads);
 CHECK(game_reload_take_user_channel(&ordinary)&&ordinary.active);s64 n=0;CHECK(!appletStorageGetSize(&ordinary,&n)&&n==8);appletStorageClose(&ordinary);CHECK(!game_reload_take_user_channel(&ordinary));
 available=1;payload_size=128;memset(&packet,0,sizeof packet);CHECK(!game_reload_early_bridge());CHECK(closed==1);CHECK(game_reload_take_user_channel(&ordinary));appletStorageClose(&ordinary);
 available=1;memcpy(packet.magic,"APPRLD2",8);packet.target=RELOAD_GAME;memset(packet.token,'a',64);strcpy(packet.host,"192.168.1.2");CHECK(game_reload_early_bridge()==1);CHECK(closed==3&&loads==1&&!game_reload_take_user_channel(&ordinary));
 puts("early bridge: ordinary launch storage preserved, exact kit packet consumed, no lost UserChannel PASS");return 0;
}
