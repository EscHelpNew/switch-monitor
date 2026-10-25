#include "lab_abi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <xmmintrin.h>
#include <stdlib.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int32_t lab_module_entry(const LabHostAPI*,const LabRequest*,LabReply*);
static unsigned polls,cancel_at;
static uint32_t stop(void*ctx){(void)ctx;return ++polls>=cancel_at;}
static uint64_t read64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
int main(void){
    uint8_t input[16]={7,0,0,0,0,0,0,0,3},output[16]={0},scratch[16]={0};
    LabHostAPI api={sizeof api,1,stop,NULL};LabRequest request={sizeof request,1,input,16,scratch,16,output,16,1000,0,0};LabReply result;
    unsigned saved=_mm_getcsr();_mm_setcsr(0x7fd5);polls=0;cancel_at=100;
    assert(lab_module_entry(&api,&request,&result)==LAB_OK&&result.completed_iterations==1000&&read64(output)==3007&&result.checksum==(3007^1000)&&result.output_bytes==8);
    assert(_mm_getcsr()==0x7fd5);
    polls=0;cancel_at=2;assert(lab_module_entry(&api,&request,&result)==LAB_CANCELLED&&result.completed_iterations==256&&read64(output)==775);
    request.abi_version=2;assert(lab_module_entry(&api,&request,&result)==LAB_BAD_ABI);request.abi_version=1;
    request.input_bytes=15;assert(lab_module_entry(&api,&request,&result)==LAB_BAD_INPUT&&result.status==LAB_BAD_INPUT);request.input_bytes=16;
    request.output_capacity=7;assert(lab_module_entry(&api,&request,&result)==LAB_BAD_INPUT);
    _mm_setcsr(saved);puts("Module native ABI/independent integer oracle/cancel/bounds/host CSR PASS");return 0;
}
