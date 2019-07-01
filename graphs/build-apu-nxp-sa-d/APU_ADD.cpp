#include <icp_datatype.h>
#include <acf_common.h>
#include <acf_utilities.h>
#include <acf_hwutilities.h>
#include <acf_memmgmt.h>
#include <linked_list_utilities.h> //for linked list entry sizes
#include <mcdma_hal.h>
#include <mce_hal.h>
#include <seq_hal.h>
#include <stio_hal.h>
#include <acf_cbmanager.hpp>
#include <acf_odprodpattern.hpp>
#include <util.h>

//global values that can be used by advanced kernels to perform init/reduction operations
extern int16_t gsACF_TILE_WIDTH_IN_CHUNKS;
extern int16_t gsACF_FIRST_CUID;
extern int16_t gsACF_NUM_INPUT_ITERATIONS;
extern int16_t gsACF_CU_ARRAY_WIDTH;

//profiling stuff
#if ((1 == ACF_SIM_NATIVE) || (1 == ACF_SIM_ISS))
#define RESET_PERF_REG //empty
#define QUERY_PERF_REG 0
#else
#include <apex_642cl_map.h>
#include <apex_642cl_dmem_fifo_map_type.h>
#define RESET_PERF_REG (*((volatile unsigned int *)(APEX_642CL_DMEM_FIFO_0_PERFORMANCE)) = 0)
#define QUERY_PERF_REG (*((volatile unsigned int *)(APEX_642CL_DMEM_FIFO_0_PERFORMANCE)))
#endif
volatile static struct ApuPerfInfoStruct gPerfInfo;
static int32_t gProbeStart    = 0;
static int32_t gProbeA        = 0;
static int32_t gProbeB        = 0;

//kernel function signatures
void add_08u(kernel_io_desc, kernel_io_desc, kernel_io_desc, kernel_io_desc);

//global input info for DMA+STREAM from XMEM to CMEM (populated/initialized by the host)
static acf_io_desc gAcfInputDescArray[2]; //[0]=Template_Demo_InputA [1]=Template_Demo_InputB 

//global input info for static input DMA xfer (populated/initialized by the host)
static acf_io_desc gAcfStaticInputDescArray[1]; //[0]=Template_Demo_InputC 

//global output info for STREAM+DMA from CMEM to XMEM (populated/initialized by the host)
static acf_io_desc gAcfOutputDescArray[1]; //[0]=Template_Demo_Output 

//global linked list memory allocation (must be 128-bit aligned)
const int gInputLinkedListMemSize = LLE_SIZE_2D_DMA_XMEM_CMEM*2 + LLE_SIZE_2D_DMA_MEM_MEM*0;
DECLARE_BYTE_ALIGNED(static int, gAcfInputLinkedListMem[gInputLinkedListMemSize*2], 16); //*2 for double buffering 
const int gOutputLinkedListMemSize = LLE_SIZE_2D_DMA_XMEM_CMEM*1 + LLE_SIZE_2D_DMA_MEM_MEM*0;
DECLARE_BYTE_ALIGNED(static int, gAcfOutputLinkedListMem[gOutputLinkedListMemSize*2], 16); //*2 for double buffering 

//allocate global array of local memory descriptors
static ACF_LocalMemDesc gLocalMemArray[4];

//allocate memory for linked list entry database(s)
static acf_llentry_desc gInputLinkedListEntryDatabase[2];
static acf_llentry_desc gOutputLinkedListEntryDatabase[1];

//allocate global array of advanced transfer info that is associated (by index) with gLocalMemArray
static vec16u gvBytesPushedIntoFifoArray[1];  //not used
static uint32_t* gPtrToFinalFifoSizeArray[1]; //not used

//the physical DMEM base address from the DMA's perspective (i.e. the host/fpga perspective)
extern int gsDmemBaseAddrForDma;

//allocate global array of kernel_io_desc
static kernel_io_desc gAcfKernelIoDescArray[4];

//grouping of IOs into logical transfers (managing transfers with multiple DMA)
static int8_t gNumLogicalTransfersCmemIn = 0;
static int8_t gNumLogicalTransfersDmemIn = 0;
static int8_t gNumLogicalTransfersCmemOut = 0;
static int8_t gNumLogicalTransfersDmemOut = 0;
static int8_t gNumLogicalTransfersCmemIn_MCE = 0;
static struct transfer gLogicalTransfersCmemIn[2];
static struct transfer* gLogicalTransfersDmemIn = 0;
static struct transfer gLogicalTransfersCmemOut[1];
static struct transfer* gLogicalTransfersDmemOut = 0;
static struct transfer* gLogicalTransfersCmemIn_MCE = 0;

volatile static int gNumInputIterations = 0;
volatile static int gInputIter          = 0;
volatile static int gProcIter           = 0;
volatile static int gInputIterMce       = 0;

int APU_ADD(int* const lpParams, int const lNumParams)
{
   RESET_PERF_REG;

   gProbeStart = QUERY_PERF_REG;

   int lRetVal = ACF_SUCCESS;

   //---------------------------
   //unpack parameters from host
   //---------------------------
   int lNumInputIterations          = lpParams[0];
   int lInputTileWidthInChunks      = lpParams[1];
   int lCoreDataCuOffset            = lpParams[2];
   int lOutputTileWidthInChunks     = lpParams[3];
   int lNumOutputIterations         = lpParams[4];
   int lOutputSkip                  = lpParams[5]; //number of output tiles to skip (related to ROI processing)
   int lLastInputTileWidthInChunks  = lpParams[6];
   int lLastOutputTileWidthInChunks = lpParams[7];
   gsDmemBaseAddrForDma             = lpParams[8];
   int lCmemHeapBaseAddr            = lpParams[9];
   int lCmemHeapSizeInbytes         = lpParams[10];
   int lDmemHeapBaseAddr            = lpParams[11];
   int lDmemHeapSizeInBytes         = lpParams[12];
   int lClover389Flag               = lpParams[13];
   gsACF_CU_ARRAY_WIDTH             = lpParams[14];
   gNumInputIterations              = lNumInputIterations; //for debugging

   //only make last tile width adjustment if it differs from previous tiles
   int lLastInputTileWidthInChunks_tmp = (lLastInputTileWidthInChunks == lInputTileWidthInChunks) ? -1 : lLastInputTileWidthInChunks;
   int lLastOutputTileWidthInChunks_tmp = (lLastOutputTileWidthInChunks == lOutputTileWidthInChunks) ? -1 : lLastOutputTileWidthInChunks;

   acf_res_info* lpResInfo = (acf_res_info*)(&lpParams[16]); //HW resources
   int lApexId = lpResInfo->apexId;

   memcpy_int(&lpParams[32], (int*)gAcfInputDescArray, 2*14);
   memcpy_int(&lpParams[60], (int*)gAcfStaticInputDescArray, 1*14);
   memcpy_int(&lpParams[74], (int*)gAcfOutputDescArray, 1*14);
   int lScenarioIterAdjust = lpParams[88];
   acf_scenario_buffer_data* lpScenarioBufferData = (acf_scenario_buffer_data*)&lpParams[89];
   acf_scenario_kernel_data* lpScenarioKernelData = (acf_scenario_kernel_data*)&lpParams[105];

   //***********************************************
   //initialization---------------------------------
   //***********************************************

   //sequencer is required for generating the 'final' interrupt that indicates to the host that execution has completed
   do
   {
   ACF_InitSeqRegPtr(lApexId); //initialize global static pointer to sequencer ONCE for use by subsequent ACF_ seq hal calls
   seq_hal_enable_seq_apu(lApexId, lpResInfo->apuId, lpResInfo->procIdSeqRes);
   seq_hal_set_rsc_counter(lApexId, lpResInfo->zeroSeqRes_Done, ((1 << 16) - 1)); //initialize to -1

   gsACF_TILE_WIDTH_IN_CHUNKS = lInputTileWidthInChunks;
   gsACF_FIRST_CUID           = lCoreDataCuOffset;
   gsACF_NUM_INPUT_ITERATIONS = lNumInputIterations;

   int lApuCuOffset = 0; //0 if APU0, 32 if APU1 (this is for the stream dma 'start cu' field
   if (lpResInfo->apuId == 1) lApuCuOffset = 32;

   if (ACF_OrgLogXfer_CmemOnly(lpResInfo->numDmaChIn, lpResInfo->numStreamInChForDma, 2, &gNumLogicalTransfersCmemIn, gLogicalTransfersCmemIn) != 0)
   {
      lRetVal = ACF_HW_RESOURCE_ERROR;
      break;//goto EARLY_EXIT;
   }

   if (ACF_OrgLogXfer_CmemOnly(lpResInfo->numDmaChOut, lpResInfo->numStreamOutChForDma, 1, &gNumLogicalTransfersCmemOut, gLogicalTransfersCmemOut) != 0)
   {
      lRetVal = ACF_HW_RESOURCE_ERROR;
      break;//goto EARLY_EXIT;
   }

   InitVecHeap((vec08s*)(uint32_t)lCmemHeapBaseAddr, lCmemHeapSizeInbytes);
   InitSclHeap((int8_t*)(uint32_t)lDmemHeapBaseAddr, lDmemHeapSizeInBytes);

   //initialize array of local memory descriptors
   int lLmError = 0;

   //Memory associated with the following ports:
   //Producer: graph port 'Template_Demo_InputA'
   //Consumer kernel 'Add_kernel_0' port 'INPUT_0'
   lLmError += LmInit(&gLocalMemArray[0], &lpScenarioBufferData[0], icp::DATATYPE_08U, 1, 1, -1, 0, 0, 0, 0, 1, 0);

   //Memory associated with the following ports:
   //Producer: kernel 'Add_kernel_0' port 'OUTPUT_0'
   //Consumer: graph port 'Template_Demo_Output'
   lLmError += LmInit(&gLocalMemArray[1], &lpScenarioBufferData[1], icp::DATATYPE_08U, 1, 1, -1, 0, 0, 0, 0, 1, 0);

   //Memory associated with the following ports:
   //Producer: graph port 'Template_Demo_InputB'
   //Consumer kernel 'Add_kernel_0' port 'INPUT_1'
   lLmError += LmInit(&gLocalMemArray[2], &lpScenarioBufferData[2], icp::DATATYPE_08U, 1, 1, -1, 0, 0, 0, 0, 1, 0);

   //Memory associated with the following ports:
   //Producer: graph port 'Template_Demo_InputC'
   //Consumer kernel 'Add_kernel_0' port 'INPUT_2'
   lLmError += LmInit(&gLocalMemArray[3], &lpScenarioBufferData[3], icp::DATATYPE_08U, 1, 1, -1, 0, 0, 0, 0, 0, 0);

   if (lLmError != 0)
   {
      lRetVal = ACF_MEMORY_ALLOCATION_ERROR;
      break;//goto EARLY_EXIT;
   }

#if (1 == ACF_SIM_NATIVE)
   SimLmInitAdjustment(gLocalMemArray, 4, lCmemHeapBaseAddr);
#endif

   //initialize global array of kernel_io_desc
   IoDescInit(&gAcfKernelIoDescArray[0], &gLocalMemArray[0], 0); //associated with kernel add_08u port INPUT_0
   IoDescInit(&gAcfKernelIoDescArray[1], &gLocalMemArray[2], 2); //associated with kernel add_08u port INPUT_1
   IoDescInit(&gAcfKernelIoDescArray[2], &gLocalMemArray[3], 3); //associated with kernel add_08u port INPUT_2
   IoDescInit(&gAcfKernelIoDescArray[3], &gLocalMemArray[1], 1); //associated with kernel add_08u port OUTPUT_0

   //allocate and initialize linked list buffer arrays (each LL is double buffered)
   int* lInputLLBufArray[2]  = {&gAcfInputLinkedListMem[0],  &gAcfInputLinkedListMem[gInputLinkedListMemSize]};
   int* lOutputLLBufArray[2] = {&gAcfOutputLinkedListMem[0], &gAcfOutputLinkedListMem[gOutputLinkedListMemSize]};

   //allocate and initialize linked list database entries (associates each ll entry with a lm desc and an io desc)
   gInputLinkedListEntryDatabase[0].pIoDesc = &gAcfInputDescArray[0];
   gInputLinkedListEntryDatabase[0].pLmDesc = &gLocalMemArray[0];
   gInputLinkedListEntryDatabase[0].size = LLE_SIZE_2D_DMA_XMEM_CMEM;
   gInputLinkedListEntryDatabase[0].pEntry[0] = lInputLLBufArray[0] + 0*LLE_SIZE_2D_DMA_XMEM_CMEM;
   gInputLinkedListEntryDatabase[0].pEntry[1] = lInputLLBufArray[1] + 0*LLE_SIZE_2D_DMA_XMEM_CMEM;
   gInputLinkedListEntryDatabase[0]._framework_cbindex = 0;
   gInputLinkedListEntryDatabase[1].pIoDesc = &gAcfInputDescArray[1];
   gInputLinkedListEntryDatabase[1].pLmDesc = &gLocalMemArray[2];
   gInputLinkedListEntryDatabase[1].size = LLE_SIZE_2D_DMA_XMEM_CMEM;
   gInputLinkedListEntryDatabase[1].pEntry[0] = lInputLLBufArray[0] + 1*LLE_SIZE_2D_DMA_XMEM_CMEM;
   gInputLinkedListEntryDatabase[1].pEntry[1] = lInputLLBufArray[1] + 1*LLE_SIZE_2D_DMA_XMEM_CMEM;
   gInputLinkedListEntryDatabase[1]._framework_cbindex = 0;

   gOutputLinkedListEntryDatabase[0].pIoDesc = &gAcfOutputDescArray[0];
   gOutputLinkedListEntryDatabase[0].pLmDesc = &gLocalMemArray[1];
   gOutputLinkedListEntryDatabase[0].size = LLE_SIZE_2D_DMA_XMEM_CMEM;
   gOutputLinkedListEntryDatabase[0].pEntry[0] = lOutputLLBufArray[0] + 0*LLE_SIZE_2D_DMA_XMEM_CMEM;
   gOutputLinkedListEntryDatabase[0].pEntry[1] = lOutputLLBufArray[1] + 0*LLE_SIZE_2D_DMA_XMEM_CMEM;
   gOutputLinkedListEntryDatabase[0]._framework_cbindex = lOutputSkip%gLocalMemArray[1].numBuffers;

   //perform DMA transfers associated with STATIC region initialization 
   ACF_XFER_DMEM_XMEM(lApexId, gAcfStaticInputDescArray[0], gLocalMemArray[3], lpResInfo->dmaChIn[0], 1, lClover389Flag); //associated with port Template_Demo_InputC

   //-----------------------
   //initialize linked lists
   //-----------------------

   int lLlOffset = 0;

   lLlOffset = 0;
   for (int i=0; i<2; i++)
   {
      ACF_LLE_2DTransferConfig_XMEM_CMEM(
                lInputLLBufArray[0] + lLlOffset,                        //linked list entry address
                gInputLinkedListEntryDatabase[i].pLmDesc->e0,           //e0
                gInputLinkedListEntryDatabase[i].pIoDesc->physAddr,     //source address
                RetStreamInPeriphId(lpResInfo->streamInChForDma[0]),    //destination address (***dummy - will be reconfigured)
                gInputLinkedListEntryDatabase[i].pIoDesc->nd_span.height, //span
                gInputLinkedListEntryDatabase[i].pIoDesc->nd_size.width,//width
                lInputTileWidthInChunks,                                //input tile width in chunks
                gInputLinkedListEntryDatabase[i].pLmDesc->chunkWidth,   //chunk width
                gInputLinkedListEntryDatabase[i].pLmDesc->bufferHeight, //chunk height
                gInputLinkedListEntryDatabase[i].pLmDesc->chunkSpan,    //chunk span
                0xBEEF,                                                 //cmem address (***dummy - will be reconfigured)
                lApuCuOffset);                                          //start CU
      lLlOffset += LLE_SIZE_2D_DMA_XMEM_CMEM;
   }
   memcpy_int(lInputLLBufArray[0], lInputLLBufArray[1], gInputLinkedListMemSize);

   lLlOffset = 0;
   for (int i=0; i<1; i++)
   {
      ACF_LLE_2DTransferConfig_XMEM_CMEM(
                lOutputLLBufArray[0] + lLlOffset,                        //linked list entry address
                gOutputLinkedListEntryDatabase[i].pLmDesc->e0,           //e0
                RetStreamOutPeriphId(lpResInfo->streamOutChForDma[0]),   //source address (***dummy - will be reconfigured)
                gOutputLinkedListEntryDatabase[i].pIoDesc->physAddr,     //destination address
                gOutputLinkedListEntryDatabase[i].pIoDesc->nd_span.height, //span
                gOutputLinkedListEntryDatabase[i].pIoDesc->nd_size.width,//width
                lOutputTileWidthInChunks,                                //output tile width in chunks
                gOutputLinkedListEntryDatabase[i].pLmDesc->chunkWidth,   //chunk width
                gOutputLinkedListEntryDatabase[i].pLmDesc->chunkHeight,  //chunk height
                gOutputLinkedListEntryDatabase[i].pLmDesc->chunkSpan,    //chunk span
                0xBEEF,                                                  //cmem address (***dummy - will be reconfigured)
                lApuCuOffset + lCoreDataCuOffset);                       //start CU
      lLlOffset += LLE_SIZE_2D_DMA_XMEM_CMEM;
   }
   memcpy_int(lOutputLLBufArray[0], lOutputLLBufArray[1], gOutputLinkedListMemSize);

   ConfigureLogicalTransfersDma(lApexId, gNumLogicalTransfersCmemIn, gLogicalTransfersCmemIn, gInputLinkedListEntryDatabase,
                                &lpResInfo->dmaChIn[0], &lpResInfo->streamInChForDma[0], 0);
   ConfigureLogicalTransfersDma(lApexId, gNumLogicalTransfersCmemOut, gLogicalTransfersCmemOut, gOutputLinkedListEntryDatabase,
                                &lpResInfo->dmaChOut[0], &lpResInfo->streamOutChForDma[0], 1);
   //local variables
   int lInputIter = 0;
   int lInputLLBufIndex = 0;
   int lInputTransferArray[2];
   int lInputDmaProcDecFlag = 1;
   int lOutputLLBufIndex = 0;
   int lOutputTransferArray[1];
   int lProcessingProcDecFlag = 1;
   int lProcIter = 0;
   int lLaunchDelay = 0;
   int lOutputDelay = 0;
   int lWriteDelay = 0;
   int lNumIterations = lNumInputIterations + lScenarioIterAdjust;
   int lLastFlag = 0;

   //----------------------------------------------------------
   //initialize sequencer resources
   //----------------------------------------------------------

   int lFixSeqResIdx = 0; //used to move through the lFixedSeqRes array 
   int lSEQ_RESOURCE_DMA_INPUT_DATABUF_AVAIL_DMA = lpResInfo->fixedSeqRes[lFixSeqResIdx++]; //incremented by ACP when processing has completed for an input iteration

   //-----dma + stream in-----
   for (int i=0; i<gNumLogicalTransfersCmemIn; i++)
   {
      gLogicalTransfersCmemIn[i].seqResDescAvailDma = lpResInfo->fixedSeqRes[lFixSeqResIdx++];
      gLogicalTransfersCmemIn[i].seqResBufAvailApu = lpResInfo->fixedSeqRes[lFixSeqResIdx++];
   }

   //-----stream out + dma-----
   for (int i=0; i<gNumLogicalTransfersCmemOut; i++)
   {
      gLogicalTransfersCmemOut[i].seqResDescAvailDma = lpResInfo->fixedSeqRes[lFixSeqResIdx++];
      gLogicalTransfersCmemOut[i].seqResBufAvailApu = lpResInfo->fixedSeqRes[lFixSeqResIdx++];
   }

   if (lFixSeqResIdx > lpResInfo->numFixedSeqRes)
   {
      lRetVal = ACF_HW_RESOURCE_ERROR;
      break;//goto EARLY_EXIT;
   }

   seq_hal_set_rsc_counter(lApexId, lSEQ_RESOURCE_DMA_INPUT_DATABUF_AVAIL_DMA, 2);

   //-----dma + stream in-----
   for (int i=0; i<gNumLogicalTransfersCmemIn; i++)
   {
      seq_hal_set_dma_dec(lApexId, lpResInfo->dmaChIn[i], (1ul << gLogicalTransfersCmemIn[i].seqResDescAvailDma));
      seq_hal_set_dma_inc(lApexId, lpResInfo->dmaChIn[i], (1ul << gLogicalTransfersCmemIn[i].seqResBufAvailApu));
      seq_hal_set_rsc_counter(lApexId, gLogicalTransfersCmemIn[i].seqResDescAvailDma, 0);
      seq_hal_set_rsc_counter(lApexId, gLogicalTransfersCmemIn[i].seqResBufAvailApu,   0);
   }

   //-----stream out + dma-----
   for (int i=0; i<gNumLogicalTransfersCmemOut; i++)
   {
      seq_hal_set_dma_dec(lApexId, lpResInfo->dmaChOut[i], (1ul << gLogicalTransfersCmemOut[i].seqResDescAvailDma));
      seq_hal_set_dma_inc(lApexId, lpResInfo->dmaChOut[i], (1ul << gLogicalTransfersCmemOut[i].seqResBufAvailApu));
      seq_hal_set_rsc_counter(lApexId, gLogicalTransfersCmemOut[i].seqResDescAvailDma, 0);
      seq_hal_set_rsc_counter(lApexId, gLogicalTransfersCmemOut[i].seqResBufAvailApu, 2);
   }

   int lScbMask = 0;
   //-----dma + stream in-----
   for (int i=0; i<gNumLogicalTransfersCmemIn; i++)
   {
      lScbMask |= (1ul << lpResInfo->dmaChIn[i]);
   }
   //-----stream out + dma-----
   for (int i=0; i<gNumLogicalTransfersCmemOut; i++)
   {
      lScbMask |= (1ul << lpResInfo->dmaChOut[i]);
   }
   seq_hal_scb_enable_apu(lApexId, lpResInfo->apuId, lScbMask);

   //ensure interrupts associated with procid lpResInfo->procIntSeqRes[0] (dma in), lpResInfo->procIntSeqRes[1] (mce in), and lpResInfo->procIntSeqRes[2] (processing & dma out)
   //are cleared and unmasked
   int lProcIntMask = ((1<<lpResInfo->procIntSeqRes[0]) | (1<<lpResInfo->procIntSeqRes[1]) | (1<<lpResInfo->procIntSeqRes[2]));
   seq_hal_clear_interrupts(lApexId, lProcIntMask);
   seq_hal_mask_interrupts_apu(lApexId, lpResInfo->apuId, ~lProcIntMask);

   gProbeA = QUERY_PERF_REG; //probe for end of 'initialization' section
   gPerfInfo.init = gProbeA - gProbeStart;

   //----------------------------------------------------------
   //main execution loop
   //----------------------------------------------------------

   while (lProcIter < lNumIterations || lInputIter < lNumIterations)
   {
      //interrupt associated with DMA input
      if ((lInputIter < lNumIterations) && (1 == lInputDmaProcDecFlag))
      {
         ACF_seq_hal_proc_dec_rscmask(lpResInfo->procIntSeqRes[0], 1<<lSEQ_RESOURCE_DMA_INPUT_DATABUF_AVAIL_DMA);
         lInputDmaProcDecFlag = 0;
      }

      //no 'iterative' MCE input ...

      //interrupt associated with processing and output
      if ((lProcIter < lNumIterations) && (1 == lProcessingProcDecFlag))
      {
         int lProcDecMask = 0;
         for (int i=0; i<gNumLogicalTransfersCmemIn; i++)
            lProcDecMask |= (1<<(gLogicalTransfersCmemIn[i].seqResBufAvailApu));
         for (int i=0; i<gNumLogicalTransfersCmemOut; i++)
            lProcDecMask |= (1<<(gLogicalTransfersCmemOut[i].seqResBufAvailApu));
         ACF_seq_hal_proc_dec_rscmask(lpResInfo->procIntSeqRes[2], lProcDecMask);
         lProcessingProcDecFlag = 0;
      }

      gProbeA = QUERY_PERF_REG; //probe for idle start

      ACF_WAIT(32768); //this will 'wait' until an interrupt occurs (or the timer runs out)
#if ((1 == ACF_SIM_NATIVE) || (1 == ACF_SIM_ISS))
      AdvanceSimulation(lApexId);
#endif

      gProbeB = QUERY_PERF_REG; //probe for idle end
      gPerfInfo.idle += gProbeB - gProbeA;

      uint32_t isrval = ACF_seq_hal_query_interrupts() & lProcIntMask; //capture state of relevant interrupts

      if (isrval) //if any of the relevant interrupts have occurred
      {
         ACF_seq_hal_clear_interrupts(isrval); //clear the interrupts that occurred

         //**********************************************
         //input (DMA)
         //**********************************************

         if (isrval & (0x1<<lpResInfo->procIntSeqRes[0])) //input DMA
         {
            lInputDmaProcDecFlag = 1;

            //determine input DMA transfers that will take place during iteration 'lInputIter'
            lLaunchDelay = lpScenarioBufferData[0].writeDelay; //input dma launch delay
            lInputTransferArray[0] = (lInputIter >= lLaunchDelay) && (lInputIter < (lLaunchDelay + lNumInputIterations)) ? (lInputIter - lLaunchDelay) : -1; //input transfer associated with port 'Template_Demo_InputA'
            lLaunchDelay = lpScenarioBufferData[2].writeDelay; //input dma launch delay
            lInputTransferArray[1] = (lInputIter >= lLaunchDelay) && (lInputIter < (lLaunchDelay + lNumInputIterations)) ? (lInputIter - lLaunchDelay) : -1; //input transfer associated with port 'Template_Demo_InputB'
            lLastFlag = (lInputIter == lNumIterations-1) ? 1 : 0;

            //Generate DMA input linked list
            int* lpLinkedList = lInputLLBufArray[lInputLLBufIndex];

            ACF_AppendLLE_XMEM_CMEM_IN(lpLinkedList, lInputTransferArray, 2, gInputLinkedListEntryDatabase, lNumInputIterations, lLastInputTileWidthInChunks_tmp, LLE_SIZE_2D_DMA_XMEM_CMEM);
            lpLinkedList += 48;

            for (int i=0; i<gNumLogicalTransfersCmemIn; i++)
            {
               ACF_LleSetLast(gLogicalTransfersCmemIn[i].lastEntryPtr[lInputLLBufIndex], lLastFlag);
               ACF_seq_hal_proc_inc_rscmask(lpResInfo->procIdSeqRes, 1<<gLogicalTransfersCmemIn[i].seqResDescAvailDma); //kick transfer
            }

            lInputLLBufIndex = ((0 == lInputLLBufIndex) ? 1 : 0); //toggle 
            lInputIter ++;
            gInputIter = lInputIter; //for debugging
         }

         //**********************************************
         //processing and output
         //**********************************************

         if (isrval & (0x1<<lpResInfo->procIntSeqRes[2])) //processing and output
         {
            lProcessingProcDecFlag = 1;

            //<processing> -------------------------------------------------------------------

            gProbeA = QUERY_PERF_REG; //probe for processing start

            //add_08u
            lLaunchDelay = lpScenarioKernelData[0].executionStartDelay;
            lOutputDelay = lpScenarioKernelData[0].extraIterToSatisfyOd;
            if (lProcIter >= lLaunchDelay && lProcIter < (lNumInputIterations + lLaunchDelay + lOutputDelay))
            {
               int16_t lLocalIter = lProcIter - lLaunchDelay;
               ACF_UpdateTileFlags(lLocalIter, lNumInputIterations);
               add_08u(
                  *_RETIODESC_VEC08(&gLocalMemArray[0], &gAcfKernelIoDescArray[0]),
                  *_RETIODESC_VEC08(&gLocalMemArray[2], &gAcfKernelIoDescArray[1]),
                  *_RETIODESC_SCL(&gLocalMemArray[3], &gAcfKernelIoDescArray[2]),
                  *_RETIODESC_VEC08(&gLocalMemArray[1], &gAcfKernelIoDescArray[3]));
            }

            gProbeB = QUERY_PERF_REG; //probe for processing end
            gPerfInfo.processing += gProbeB - gProbeA;

            //</processing> ------------------------------------------------------------------

            ACF_seq_hal_proc_inc_rscmask(lpResInfo->procIdSeqRes, 1<<lSEQ_RESOURCE_DMA_INPUT_DATABUF_AVAIL_DMA);

            //<output> -----------------------------------------------------------------------

            //setup and kick output transfer associated with iteration 'lProcIter'; if there isn't one, increment resources to keep the loop going

            //determine output transfers that should be scheduled during iteration 'lProcIter' (if any)
            lLaunchDelay = lpScenarioBufferData[1].writeDelay; //dma launch delay
            lOutputTransferArray[0] = (lProcIter >= (lLaunchDelay + lOutputSkip) && lProcIter < (lLaunchDelay + lOutputSkip + lNumOutputIterations)) ? (lProcIter - lLaunchDelay) : -1; //output transfer associated with port 'Template_Demo_Output'
            lLastFlag = (lProcIter == lNumIterations-1) ? 1 : 0;

            //Generate DMA output linked list
            int* lpLinkedList = lOutputLLBufArray[lOutputLLBufIndex];

            ACF_AppendLLE_XMEM_CMEM_OUT(lpLinkedList, lOutputTransferArray, 1, gOutputLinkedListEntryDatabase, -lOutputSkip, lNumOutputIterations, lLastOutputTileWidthInChunks_tmp, LLE_SIZE_2D_DMA_XMEM_CMEM);
            lpLinkedList += 24;

            for (int i=0; i<gNumLogicalTransfersCmemOut; i++)
            {
               ACF_LleSetLast(gLogicalTransfersCmemOut[i].lastEntryPtr[lOutputLLBufIndex], lLastFlag);
               ACF_seq_hal_proc_inc_rscmask(lpResInfo->procIdSeqRes, 1<<gLogicalTransfersCmemOut[i].seqResDescAvailDma); //kick transfer
            }

            lOutputLLBufIndex = ((0 == lOutputLLBufIndex) ? 1 : 0); //toggle 


            //</output> ----------------------------------------------------------------------

            lProcIter ++;
            gProcIter = lProcIter; //for debugging
         }
      }
   }

   //----------------------------------------------------------
   //wait for output transfers to complete and clean up
   //----------------------------------------------------------

   //wait for all output transfers to complete
   int lProcDecMask = 0;
   for (int i=0; i<gNumLogicalTransfersCmemOut; i++)
      lProcDecMask |= (1<<(gLogicalTransfersCmemOut[i].seqResBufAvailApu));
   ACF_seq_hal_proc_dec_rscmask(lpResInfo->procIntSeqRes[0], lProcDecMask);
   while(seq_hal_query_interrupt(lApexId, lpResInfo->procIntSeqRes[0]) == 0)
   {
      gProbeA = QUERY_PERF_REG; //probe for idle start

      ACF_WAIT(32768);
#if ((1 == ACF_SIM_NATIVE) || (1 == ACF_SIM_ISS))
      AdvanceSimulation(lApexId);
#endif

      gProbeB = QUERY_PERF_REG; //probe for idle end
      gPerfInfo.idle += gProbeB - gProbeA;
   }
   seq_hal_clear_interrupt(lApexId, lpResInfo->procIntSeqRes[0]);
   ACF_seq_hal_proc_dec_rscmask(lpResInfo->procIntSeqRes[0], lProcDecMask);
   while(seq_hal_query_interrupt(lApexId, lpResInfo->procIntSeqRes[0]) == 0)
   {
      gProbeA = QUERY_PERF_REG; //probe for idle start

      ACF_WAIT(32768);
#if ((1 == ACF_SIM_NATIVE) || (1 == ACF_SIM_ISS))
      AdvanceSimulation(lApexId);
#endif

      gProbeB = QUERY_PERF_REG; //probe for idle end
      gPerfInfo.idle += gProbeB - gProbeA;
   }
   seq_hal_clear_interrupt(lApexId, lpResInfo->procIntSeqRes[0]);

   seq_hal_scb_disable_apu(lApexId, lpResInfo->apuId, lScbMask);

   for (int i=0; i<gNumLogicalTransfersCmemIn; i++)
   {
      mcdma_hal_interruptclear_ch_done(lApexId, lpResInfo->dmaChIn[i]);
      stio_hal_interruptclear_sti_done(lApexId, lpResInfo->streamInChForDma[i]);
   }
   for (int i=0; i<gNumLogicalTransfersCmemOut; i++)
   {
      mcdma_hal_interruptclear_ch_done(lApexId, lpResInfo->dmaChOut[i]);
      stio_hal_interruptclear_sto_done(lApexId, lpResInfo->streamOutChForDma[i]);
   }
   seq_hal_mask_interrupts_apu(lApexId, lpResInfo->apuId, lProcIntMask);


   }
   while(false);

   //signal interrupt to indicate to host that the program has completed
   ACF_seq_hal_proc_inc_rscmask(lpResInfo->procIdSeqRes, 1<<lpResInfo->zeroSeqRes_Done);
   seq_hal_disable_seq_apu(lApexId, lpResInfo->apuId, lpResInfo->procIdSeqRes);

   gProbeA = QUERY_PERF_REG; //probe for total
   gPerfInfo.total = gProbeA - gProbeStart;

   return lRetVal;
}

#if (1 == ACF_SIM_NATIVE)
//create a wrapper for acf native sim use (otherwise function pointer name clashes with class name)
int APU_ADD_wrapper(void* lpParams)
{
   return APU_ADD((int* const)lpParams, 0); //number of params is not used
}
#endif

