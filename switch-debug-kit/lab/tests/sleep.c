#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t Result;
#define R_SUCCEEDED(rc) ((rc)==0)
static Result get_result,set_result;static bool prior,values[4];static unsigned calls,queries;
static Result appletIsAutoSleepDisabled(bool*out){queries++;if(!get_result)*out=prior;return get_result;}
static Result appletSetAutoSleepDisabled(bool value){values[calls++]=value;return set_result;}
#include "../source/sleep_policy.h"
static void run(bool old,Result get,Result set,unsigned expected){
    prior=old;get_result=get;set_result=set;calls=queries=0;LabSleepGuard guard={0};
    lab_sleep_begin(&guard);assert(queries==1);
    lab_sleep_end(&guard);assert(calls==expected);
    if(expected){assert(values[0]);if(expected==2)assert(values[1]==old);}
    lab_sleep_end(&guard);assert(calls==expected); /* restoration is idempotent */
}
int main(void){run(false,0,0,2);run(true,0,0,2);run(false,1,0,0);run(false,0,1,1);return 0;}
