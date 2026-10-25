#include "lab_abi.h"
static volatile uint64_t state __attribute__((section(".mutable_state")))=1;
__attribute__((section(".text.entry")))
int32_t lab_module_entry(const LabHostAPI*a,const LabRequest*b,LabReply*c)
{(void)a;(void)b;(void)c;state++;return(int32_t)state;}
