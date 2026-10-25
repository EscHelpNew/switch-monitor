#ifndef GAME_RELOAD_MAIN_H
#define GAME_RELOAD_MAIN_H
#include "reload_core.h"
#ifdef __cplusplus
extern "C" {
#endif
void game_reload_main_init(void);
void game_reload_main_poll(void);
void game_reload_main_resident(void);
/* Before app cleanup: never return to a loader with resident workers. */
void game_reload_main_finish(void);
int game_reload_main_pending(void);
/* Submit belongs to the network worker; launch belongs to main_poll only. */
uint32_t game_reload_submit(const ReloadLaunch *);
void game_reload_game_done(void);
void game_reload_wait_game(void);
#ifdef __cplusplus
}
#endif
#endif
