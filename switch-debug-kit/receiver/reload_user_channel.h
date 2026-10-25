/* Use from the platform startup unit which already uses libnx. */
#ifndef NXDK_RELOAD_USER_CHANNEL_H
#define NXDK_RELOAD_USER_CHANNEL_H
#include <switch.h>
#ifdef __cplusplus
extern "C" {
#endif
/* After early_bridge returns zero, take the untouched non-kit launch storage.
 * On success the caller owns it and must appletStorageClose after use. */
int game_reload_take_user_channel(AppletStorage *out);
#ifdef __cplusplus
}
#endif
#endif
