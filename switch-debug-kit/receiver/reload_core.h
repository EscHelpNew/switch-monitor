#ifndef GAME_RELOAD_CORE_H
#define GAME_RELOAD_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {RELOAD_BRIDGE=1,RELOAD_GAME=2,RELOAD_LAB=3};
enum {RELOAD_STATUS=1,RELOAD_PUT=2,RELOAD_RUN=3,RELOAD_CRASH=4};
#define RELOAD_MAX_BYTES (64u*1024u*1024u)
#define RELOAD_BRIDGE_MARKER "GAME_REMOTE_RELOAD_BRIDGE_V2"
typedef struct {unsigned kind,target;size_t bytes;unsigned char sha256[32];} ReloadCommand;
typedef struct {
    char magic[8];uint32_t target;char token[65],host[16];
    unsigned char sha256[32],reserved[3];
} ReloadLaunch;
int reload_launch_valid(const ReloadLaunch *);
typedef int (*ReloadFileHash)(const char *,unsigned char[32]);
int reload_parse(const char *,ReloadCommand *);
int reload_token_valid(const char *);
int reload_token_equal(const char *,const char *);
int reload_nro_valid(const unsigned char *,size_t,size_t);
int reload_bridge_valid(const char *);
int reload_path(char *,size_t,const char *,unsigned,int);
int reload_publish(const char *,unsigned,size_t,const unsigned char[32],ReloadFileHash);
#ifdef __cplusplus
}
#endif
#endif
