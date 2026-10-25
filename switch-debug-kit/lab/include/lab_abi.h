#ifndef GAME_ARM_LAB_ABI_H
#define GAME_ARM_LAB_ABI_H
#include <stddef.h>
#include <stdint.h>
#define LAB_ABI_VERSION 1u
#define LAB_IMAGE_MAGIC UINT32_C(0x3142414c) /* LAB1, little endian */
#define LAB_IMAGE_VERSION 1u
#define LAB_IMAGE_PIC 1u
#define LAB_MAX_IMAGE (1024u*1024u)
#define LAB_MAX_INPUT (16u*1024u*1024u)
#define LAB_SCRATCH_BYTES (4u*1024u*1024u)
#define LAB_MAX_ITERATIONS 1000000u
#define LAB_MAX_TRIALS 32u
#define LAB_MAX_BUDGET_MS 2000u
typedef struct {
    uint32_t magic,version,abi_version,image_bytes;
    uint32_t entry_offset,flags,reserved[2];
    uint8_t image_sha256[32];
} LabImageHeader;
typedef struct {
    uint32_t size,abi_version;
    uint32_t (*should_cancel)(void *context);
    void *context;
} LabHostAPI;
typedef struct {
    uint32_t size,abi_version;
    const uint8_t *input;uint64_t input_bytes;
    uint8_t *scratch;uint64_t scratch_bytes;
    uint8_t *output;uint64_t output_capacity;
    uint32_t iterations,flags;uint64_t seed;
} LabRequest;
typedef struct {
    uint32_t size,abi_version,status,completed_iterations;
    uint64_t checksum,output_bytes;
} LabReply;
typedef int32_t (*LabEntry)(const LabHostAPI *,const LabRequest *,LabReply *);
enum {LAB_OK=0,LAB_BAD_ABI=1,LAB_BAD_INPUT=2,LAB_CANCELLED=3,LAB_BAD_BOUNDS=4};
_Static_assert(sizeof(LabImageHeader)==64,"image wire ABI");
_Static_assert(sizeof(void*)==8,"ARM64 or64-bit host required");
_Static_assert(sizeof(LabHostAPI)==24&&sizeof(LabRequest)==72&&sizeof(LabReply)==32,"module ABI sizes");
#endif
