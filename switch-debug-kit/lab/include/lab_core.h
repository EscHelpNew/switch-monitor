#ifndef GAME_ARM_LAB_CORE_H
#define GAME_ARM_LAB_CORE_H
#include "lab_abi.h"
typedef enum{LAB_CMD_STATUS,LAB_CMD_MODULE,LAB_CMD_INPUT,LAB_CMD_RUN,LAB_CMD_RESULTS,LAB_CMD_CANCEL}LabCommandKind;
typedef struct{LabCommandKind kind;uint32_t bytes,iterations,warmups,trials,budget_ms;uint8_t sha256[32];}LabCommand;
int lab_image_validate(const void *blob,size_t bytes,const uint8_t digest[32],LabImageHeader *out);
int lab_parse_command(const char *line,LabCommand *out);
int lab_token_equal(const char *expected,const char *given);
int lab_reply_validate(const LabReply*,int32_t returned,uint32_t iterations,uint64_t output_capacity);
uint32_t lab_cpu_delta(uint64_t before,uint64_t after,uint32_t first_error,uint32_t last_error,uint64_t *delta);
#endif
