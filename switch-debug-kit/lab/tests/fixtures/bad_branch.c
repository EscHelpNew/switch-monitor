#include "lab_abi.h"
__asm__(".global lab_outside\n.set lab_outside, 0x100000\n");
__attribute__((section(".text.entry")))
int32_t lab_module_entry(const LabHostAPI*a,const LabRequest*b,LabReply*c)
{(void)a;(void)b;(void)c;__asm__ __volatile__("bl lab_outside":::"x30","memory");return 0;}
