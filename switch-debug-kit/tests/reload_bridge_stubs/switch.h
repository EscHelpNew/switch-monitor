#ifndef NXDK_BRIDGE_STUB_H
#define NXDK_BRIDGE_STUB_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t Result;typedef int64_t s64;
typedef struct {int active;} AppletStorage;
enum{AppletLaunchParameterKind_UserChannel=1};
#define R_SUCCEEDED(x) ((x)==0)
#define R_FAILED(x) ((x)!=0)
Result appletPopLaunchParameter(AppletStorage*,int);
Result appletStorageGetSize(AppletStorage*,s64*);
Result appletStorageRead(AppletStorage*,s64,void*,size_t);
void appletStorageClose(AppletStorage*);
int envHasNextLoad(void);
Result envSetNextLoad(const char*,const char*);
#endif
