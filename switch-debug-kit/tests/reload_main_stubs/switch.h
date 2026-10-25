#ifndef RELOAD_MAIN_TEST_SWITCH_H
#define RELOAD_MAIN_TEST_SWITCH_H
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
typedef uint32_t Result;
typedef uint32_t Handle;
typedef struct{pthread_mutex_t mutex;pthread_cond_t condition;bool signal,auto_clear;}UEvent;
typedef struct{UEvent *event;}Waiter;
enum{Module_Libnx=345,LibnxError_BadInput=4,LibnxError_NotInitialized=2};
#define MAKERESULT(m,d) ((Result)((m)|((d)<<9)))
Handle threadGetCurHandle(void);
void ueventCreate(UEvent*,bool);
void ueventSignal(UEvent*);
Waiter waiterForUEvent(UEvent*);
Result waitSingle(Waiter,uint64_t);
#endif
