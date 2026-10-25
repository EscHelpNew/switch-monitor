#ifndef RELOAD_LAUNCH_STUB_SWITCH_H
#define RELOAD_LAUNCH_STUB_SWITCH_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t Result;
typedef uint64_t u64;
typedef uint32_t Handle;
enum{InfoType_ProgramId=18,CUR_PROCESS_HANDLE=0xFFFF8001};
Result svcGetInfo(u64*,int,Handle,uint64_t);
Result appletRestartProgram(const void*,size_t);
typedef struct{int active;}AppletStorage;
typedef struct{int active;}Service;
#define R_SUCCEEDED(x) ((x)==0)
#define R_FAILED(x) ((x)!=0)
enum{Module_Libnx=345,LibnxError_NotInitialized=2};
#define MAKERESULT(m,d) ((Result)((m)|((d)<<9)))
Result appletCreateStorage(AppletStorage*,int64_t);
Result appletStorageWrite(AppletStorage*,int64_t,const void*,size_t);
void appletStorageClose(AppletStorage*);
Result appletRequestLaunchApplication(uint64_t,AppletStorage*);
void svcExitProcess(void);
Service *appletGetServiceSession_SelfController(void);
int serviceIsActive(Service*);
void serviceAssumeDomain(Service*);
Result serviceDispatch(Service*,uint32_t);
void svcSleepThread(int64_t);
#endif
