/* A runnable integration example, not a game port or an installer. */
#include <switch.h>
#include <stdio.h>
#include <sys/stat.h>
#include "remote_reload.h"
#include "reload_main.h"
#include "reload_project.h"
#include "reload_user_channel.h"

int main(int argc,char **argv)
{
    /* This return is safe: no application workers have been started. */
    if(game_reload_early_bridge())return 0;
    AppletStorage ordinary={0};
    /* The demo has no application-specific launch data; a real game reads it. */
    if(game_reload_take_user_channel(&ordinary))appletStorageClose(&ordinary);
    game_reload_configure(argc,argv); /* Records the main-thread owner. */
    consoleInit(NULL);
    mkdir("sdmc:/switch",0777);mkdir(NXDK_SD_ROOT,0777);
    if(R_SUCCEEDED(socketInitializeDefault())){
        nxlinkStdioForDebug(); /* Keep stdout on screen. */
        game_reload_start(NXDK_APP_NAME,"1.0.0");
    }
    printf("Switch Debug Kit: %s\nReceiver integration example\n"
           "SD: %s\nInstalled title: %016llX\n"
           "Reload: TCP28285, explicit --token required\n"
           "Title zero: RUN disabled; configure your own forwarder first.\n"
           "This screen is a demo, not the game. '+' exits.\n",
           NXDK_APP_NAME,NXDK_SD_ROOT,(unsigned long long)NXDK_TITLE_ID);
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    while(appletMainLoop()){
        game_reload_main_poll();
        padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;
        consoleUpdate(NULL);
    }
    /* Resident worker lifetime cannot outlive its mapped NRO. */
    game_reload_main_finish();
    consoleExit(NULL);return 0;
}
