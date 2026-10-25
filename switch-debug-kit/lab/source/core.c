#include "lab_core.h"
#include <string.h>
int lab_reply_validate(const LabReply*r,int32_t returned,uint32_t iterations,uint64_t output_capacity)
{return r&&r->size==sizeof *r&&r->abi_version==LAB_ABI_VERSION&&returned>=0&&returned<=LAB_BAD_BOUNDS&&r->status==(uint32_t)returned&&r->completed_iterations<=iterations&&r->output_bytes<=output_capacity;}
uint32_t lab_cpu_delta(uint64_t before,uint64_t after,uint32_t first_error,uint32_t last_error,uint64_t *delta)
{*delta=0;if(first_error)return first_error;if(last_error)return last_error;if(after<before)return UINT32_MAX;*delta=after-before;return 0;}
int lab_image_validate(const void *blob,size_t bytes,const uint8_t digest[32],LabImageHeader *out)
{
    if(!blob||!out||!digest||bytes<64||bytes>64+LAB_MAX_IMAGE)return 0;
    LabImageHeader h;memcpy(&h,blob,64);
    if(h.magic!=LAB_IMAGE_MAGIC||h.version!=LAB_IMAGE_VERSION||h.abi_version!=LAB_ABI_VERSION||h.flags!=LAB_IMAGE_PIC||h.reserved[0]||h.reserved[1]||h.image_bytes<4||h.image_bytes>LAB_MAX_IMAGE||bytes!=64u+h.image_bytes||h.entry_offset%4||h.entry_offset>h.image_bytes-4)return 0;
    unsigned different=0;for(unsigned i=0;i<32;i++)different|=h.image_sha256[i]^digest[i];
    if(different)return 0;*out=h;return 1;
}
int lab_token_equal(const char *expected,const char *given)
{
    if(!expected||!given)return 0;
    size_t n=strlen(expected),m=strlen(given);if(n<16||n>128||m>128)return 0;
    volatile unsigned different=(unsigned)(n^m);
    for(size_t i=0;i<n;i++)different|=(unsigned char)expected[i]^(i<m?(unsigned char)given[i]:0);
    return different==0;
}
static int decimal(const char *s,uint32_t maximum,uint32_t *out)
{
    if(!*s)return 0;uint32_t n=0;
    for(;*s;s++){if(*s<'0'||*s>'9')return 0;unsigned digit=(unsigned)(*s-'0');if(n>(maximum-digit)/10||digit>maximum)return 0;n=n*10+digit;}
    *out=n;return 1;
}
static int hex(unsigned char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
int lab_parse_command(const char *line,LabCommand *out)
{
    if(!line||!out||strlen(line)>255||!*line||*line==' ')return 0;
    char text[256];strcpy(text,line);char *words[7]={0};unsigned count=0;
    for(char *p=text;p;){if(count==7||!*p)return 0;words[count++]=p;char *space=strchr(p,' ');if(space){*space=0;p=space+1;}else p=NULL;}
    *out=(LabCommand){0};
    if(count==1){
        if(!strcmp(words[0],"STATUS"))out->kind=LAB_CMD_STATUS;
        else if(!strcmp(words[0],"RESULTS"))out->kind=LAB_CMD_RESULTS;
        else if(!strcmp(words[0],"CANCEL"))out->kind=LAB_CMD_CANCEL;
        else return 0;return 1;
    }
    if(count==3&&(!strcmp(words[0],"MODULE")||!strcmp(words[0],"INPUT"))){
        out->kind=!strcmp(words[0],"MODULE")?LAB_CMD_MODULE:LAB_CMD_INPUT;
        uint32_t maximum=out->kind==LAB_CMD_MODULE?64+LAB_MAX_IMAGE:LAB_MAX_INPUT;
        if(!decimal(words[1],maximum,&out->bytes)||!out->bytes||strlen(words[2])!=64)return 0;
        for(unsigned i=0;i<32;i++){int a=hex((unsigned char)words[2][2*i]),b=hex((unsigned char)words[2][2*i+1]);if(a<0||b<0)return 0;out->sha256[i]=(uint8_t)((a<<4)|b);}return 1;
    }
    if(count==5&&!strcmp(words[0],"RUN")){
        out->kind=LAB_CMD_RUN;
        return decimal(words[1],LAB_MAX_ITERATIONS,&out->iterations)&&out->iterations&&decimal(words[2],8,&out->warmups)&&decimal(words[3],LAB_MAX_TRIALS,&out->trials)&&out->trials&&decimal(words[4],LAB_MAX_BUDGET_MS,&out->budget_ms)&&out->budget_ms;
    }
    return 0;
}
