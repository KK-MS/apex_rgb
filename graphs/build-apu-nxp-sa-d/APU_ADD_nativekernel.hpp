#ifndef _ACF_PROCESS_APU_APU_ADD_NATIVE_KERNEL_WRAPPER
#define _ACF_PROCESS_APU_APU_ADD_NATIVE_KERNEL_WRAPPER

#include <APU_ADD.hpp>

//structure encapsulating the information ACF requires for each I/O
typedef struct _acf_io_info {                                      
    int32_t      pBuf;   //CL framework will initialize this with a pointer to global memory (based on associated cl_mem entity)
    int32_t      width;  //width in unit of elements
    int32_t      height; //height in unit of elements
    int32_t      span;   //(2D only) the number of bytes required to jump from one line of bytes in memory to the 'next' line of bytes in memory
    icp::DATATYPE elementDataType; //element data type (icp::DATATYPE_08U, icp::DATATYPE_16S, etc.
    int32_t      elementDimX;     //element X dimension
    int32_t      elementDimY;     //element Y dimension
} acf_io_info;

/******************************************************
pArgs must point to an array with the following format:

acf_io_info MYKERNEL_args[4];

where:
   [0] corresponds to input 'Template_Demo_InputA'
   [1] corresponds to input 'Template_Demo_InputB'
   [2] corresponds to input 'Template_Demo_InputC'
   [3] corresponds to output 'Template_Demo_Output'

and 'pArgs = (void*)MYKERNEL_args;'
******************************************************/

static void APU_ADD_nativekernel(void* pArgs)
{
   acf_io_info* lpIo = 0;

   //Template_Demo_InputA
   ACF_DataDesc Template_Demo_InputA;
   lpIo = (acf_io_info*)((int32_t)pArgs+0*sizeof(acf_io_info));
   Template_Demo_InputA.Init2D(0, (void*)lpIo->pBuf, lpIo->nd_size.width, lpIo->nd_size.height, lpIo->nd_span.height, lpIo->elementDataType, lpIo->elementDimX, lpIo->elementDimY);

   //Template_Demo_InputB
   ACF_DataDesc Template_Demo_InputB;
   lpIo = (acf_io_info*)((int32_t)pArgs+1*sizeof(acf_io_info));
   Template_Demo_InputB.Init2D(0, (void*)lpIo->pBuf, lpIo->nd_size.width, lpIo->nd_size.height, lpIo->nd_span.height, lpIo->elementDataType, lpIo->elementDimX, lpIo->elementDimY);

   //Template_Demo_InputC
   ACF_DataDesc Template_Demo_InputC;
   lpIo = (acf_io_info*)((int32_t)pArgs+2*sizeof(acf_io_info));
   Template_Demo_InputC.Init2D(0, (void*)lpIo->pBuf, lpIo->nd_size.width, lpIo->nd_size.height, lpIo->nd_span.height, lpIo->elementDataType, lpIo->elementDimX, lpIo->elementDimY);

   //Template_Demo_Output
   ACF_DataDesc Template_Demo_Output;
   lpIo = (acf_io_info*)((int32_t)pArgs+3*sizeof(acf_io_info));
   Template_Demo_Output.Init2D(0, (void*)lpIo->pBuf, lpIo->nd_size.width, lpIo->nd_size.height, lpIo->nd_span.height, lpIo->elementDataType, lpIo->elementDimX, lpIo->elementDimY);

   APU_ADD lProcess;
   lProcess.Initialize();
   lProcess.ConnectIO("Template_Demo_InputA", Template_Demo_InputA);
   lProcess.ConnectIO("Template_Demo_InputB", Template_Demo_InputB);
   lProcess.ConnectIO("Template_Demo_InputC", Template_Demo_InputC);
   lProcess.ConnectIO("Template_Demo_Output", Template_Demo_Output);
   lProcess.Start();
   lProcess.Wait();
}

#endif //_ACF_PROCESS_APU_APU_ADD_NATIVE_KERNEL_WRAPPER
