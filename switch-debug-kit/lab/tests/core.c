#include "lab_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    uint8_t blob[72]={0},hash[32]={1};LabImageHeader h={LAB_IMAGE_MAGIC,1,1,8,0,1,{0,0},{0}},out;
    memcpy(h.image_sha256,hash,32);memcpy(blob,&h,64);
    assert(lab_image_validate(blob,sizeof blob,hash,&out));
    for(unsigned n=0;n<64;n++)assert(!lab_image_validate(blob,n,hash,&out));
    blob[0]^=1;assert(!lab_image_validate(blob,sizeof blob,hash,&out));blob[0]^=1;
    for(unsigned field=1;field<8;field++){
        LabImageHeader bad=h;
        switch(field){case 1:bad.version=2;break;case 2:bad.abi_version=2;break;case 3:bad.image_bytes=LAB_MAX_IMAGE+1;break;case 4:bad.entry_offset=8;break;case 5:bad.flags=2;break;case 6:bad.reserved[0]=1;break;default:bad.reserved[1]=1;break;}
        memcpy(blob,&bad,64);assert(!lab_image_validate(blob,sizeof blob,hash,&out));
    }
    memcpy(blob,&h,64);blob[32]^=1;assert(!lab_image_validate(blob,sizeof blob,hash,&out));blob[32]^=1;
    h.entry_offset=1;memcpy(blob,&h,64);assert(!lab_image_validate(blob,sizeof blob,hash,&out));
    LabCommand c;
    const char*bad[]={"","STATUS x"," RUN 1 1 1 1","RUN 0 1 1 1","RUN 1 9 1 1","RUN 1 1 33 1","RUN 1 1 1 2001","RUN -1 0 1 1","RUN 4294967296 0 1 1","RUN 1 0 1 1 x","MODULE 1 nope","INPUT 16777217 0000000000000000000000000000000000000000000000000000000000000000"};
    for(unsigned i=0;i<sizeof bad/sizeof*bad;i++)assert(!lab_parse_command(bad[i],&c));
    assert(lab_parse_command("STATUS",&c)&&c.kind==LAB_CMD_STATUS);
    assert(lab_parse_command("RUN 1000 2 7 250",&c)&&c.kind==LAB_CMD_RUN&&c.iterations==1000&&c.warmups==2&&c.trials==7&&c.budget_ms==250);
    assert(lab_parse_command("MODULE 72 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",&c)&&c.bytes==72&&c.sha256[0]==1&&c.sha256[31]==0xef);
    assert(lab_token_equal("0123456789abcdef","0123456789abcdef"));
    assert(!lab_token_equal("0123456789abcdef","0123456789abcdee"));
    assert(!lab_token_equal("0123456789abcdef","0123456789abcdefx"));
    LabReply reply={sizeof reply,LAB_ABI_VERSION,LAB_OK,1000,0,8};
    assert(lab_reply_validate(&reply,0,1000,8));reply.status=LAB_BAD_INPUT;
    assert(!lab_reply_validate(&reply,0,1000,8));reply.status=LAB_OK;
    reply.output_bytes=9;assert(!lab_reply_validate(&reply,0,1000,8));reply.output_bytes=8;
    reply.completed_iterations=1001;assert(!lab_reply_validate(&reply,0,1000,8));
    uint64_t delta=99;assert(lab_cpu_delta(5,8,0,0,&delta)==0&&delta==3);
    assert(lab_cpu_delta(8,7,0,0,&delta)!=0&&delta==0);
    assert(lab_cpu_delta(5,8,123,0,&delta)==123&&delta==0);
    puts("Resident host portable envelope/parser/auth negative cases PASS");return 0;
}
