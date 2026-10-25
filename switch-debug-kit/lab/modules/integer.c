#include "lab_abi.h"
__attribute__((section(".text.entry"),visibility("default")))
int32_t lab_module_entry(const LabHostAPI *api,const LabRequest *in,LabReply *out)
{
    if(!api||!in||!out||api->size!=sizeof *api||api->abi_version!=LAB_ABI_VERSION||in->size!=sizeof *in||in->abi_version!=LAB_ABI_VERSION||!api->should_cancel)return LAB_BAD_ABI;
    *out=(LabReply){sizeof *out,LAB_ABI_VERSION,LAB_OK,0,0,0};
    if(in->input_bytes!=16||in->output_capacity<8||!in->input||!in->output||in->iterations>LAB_MAX_ITERATIONS)return out->status=LAB_BAD_INPUT;
    uint64_t value=0,delta=0;for(unsigned i=0;i<8;i++){value|=(uint64_t)in->input[i]<<(8*i);delta|=(uint64_t)in->input[8+i]<<(8*i);}
    for(uint32_t n=0;n<in->iterations;n++){
        if(!(n&255u)&&api->should_cancel(api->context)){out->status=LAB_CANCELLED;break;}
        value+=delta;out->completed_iterations++;
    }
    for(unsigned i=0;i<8;i++)in->output[i]=(uint8_t)(value>>(8*i));
    out->checksum=value^out->completed_iterations;out->output_bytes=8;return out->status;
}
