#include <apu_microkernel.h>

int APU_ADD(int * const lpParams, int const lNumParams);

volatile KERNEL_INFO DMEM_KERNEL_DB[2] =
{
   {(int32_t)(&APU_ADD), "APU_ADD"},
   {(int32_t)0xFFFFFFFF, ""}
};

 int32_t MK_KERNEL_DB[1] =
{
   (int32_t)(&APU_ADD),
};
