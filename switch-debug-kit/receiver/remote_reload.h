#ifndef GAME_REMOTE_RELOAD_H
#define GAME_REMOTE_RELOAD_H
/* Opt-in development service. Call early_bridge before app workers or sockets;
 * a positive result means return from main to the loader. configure precedes
 * socket initialization, start follows it. No calls from frame/audio paths. */
#ifdef __cplusplus
extern "C" {
#endif
int game_reload_early_bridge(void);
void game_reload_configure(int argc,char **argv);
void game_reload_start(const char *application,const char *version);
#ifdef __cplusplus
}
#endif
#endif
