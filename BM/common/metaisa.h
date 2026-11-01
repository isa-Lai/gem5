#ifndef METAISA_H
#define METAISA_H

#include <stdint.h>

#define write_csr(csr, value)                                                 \
    asm volatile("csrrw x0, %0, %1" ::"i"(csr), "r"((uint64_t)(value)))

#define RESERVED 0
enum
{
    INIT_NOT_LINKED = 0,
    INIT_LINKED = 1
};
enum
{
    END_NOT_LINKED = 0,
    END_LINKED = 1
};
enum
{
    LOOP1_ID = 0,
    LOOP2_ID = 1,
    STREAM1_ID = 2,
    STREAM2_ID = 3
};
enum
{
    LOOP = 0,
    DIR_STREAM = 1,
    INDIR_STREAM = 2,
    BRANCH = 3,
    PTR_CHASE = 4
};
enum
{
    INVALID = 0,
    VALID = 1
};
enum
{
    INACTIVE = 0,
    ACTIVE = 1
};

typedef struct __attribute__((__packed__))
{
    uint8_t parentLoopId : 6;
    uint8_t initValLinked : 1;
    uint8_t endValLinked : 1;
    uint32_t initVal;
    uint32_t endVal;
    uint16_t inc;
    uint32_t headerPCOffset;
} MISA_LoopDesc_t;

typedef struct __attribute__((__packed__))
{
    uint8_t loopDescId : 6;
    uint8_t linked : 1;
    uint8_t reserved : 1;
    uint64_t baseAddr;
    uint16_t stride;
    uint16_t streamAttr;
    uint16_t reserved_;
} MISA_DirStreamDesc_t;

typedef struct __attribute__((__packed__))
{
    uint8_t parentStreamId : 6;
    uint8_t linked : 1;
    uint8_t reserved : 1;
    uint64_t baseAddr;
    uint16_t stride;
    uint16_t streamAttr;
    uint16_t reserved_;
} MISA_IndirStreamDesc_t;

typedef struct __attribute__((__packed__))
{
    uint8_t type : 6;
    uint8_t valid : 1;
    uint8_t active : 1;
    union
    {
        MISA_LoopDesc_t loopDesc;
        MISA_DirStreamDesc_t streamDesc;
        MISA_IndirStreamDesc_t indirDesc;
    } descInfo;
} MISA_Desc_t;

#endif // METAISA_H
