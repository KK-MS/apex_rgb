/*****************************************************************************
*
* NXP Confidential Proprietary
*
* Copyright (c) 2018 NXP
* All Rights Reserved
*
*****************************************************************************
*
* THIS SOFTWARE IS PROVIDED BY NXP "AS IS" AND ANY EXPRESSED OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL NXP OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
* IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
* THE POSSIBILITY OF SUCH DAMAGE.
*
****************************************************************************/

/****************************************************************************
* Includes
****************************************************************************/
#include <inttypes.h>
#include <string.h>

// Utility function for SUMat/SMat
#include <umat_helper.h>
// Utility functions
#include <common_helpers.h>
#include <string>

// Name definitions used for the ACF graph, access to the port names
#include <apu_add_graph_names.h>

// ACF Process class definition
#ifdef APEX2_EMULATE
#include "acf_lib.hpp"
using namespace APEX2;
#include "apu_add_apu_process_desc.hpp"
REGISTER_PROCESS_TYPE(ADD_PI, apu_add_apu_process_desc)
#else
#include "apex.h"
#include str_header(ADD_PI, hpp)
#endif

/// LLL DISPLAY ---(
#if !defined(APEX2_EMULATE) && !defined(__INTEGRITY__)
#ifdef __STANDALONE__
#include "frame_output_dcu.h"
#else // #ifdef __STANDALONE__
#include "frame_output_v234fb.h"
#endif // else from #ifdef __STANDALONE__

#define CHNL_CNT io::IO_DATA_CH3
#define CHNL_CNT_GRAY io::IO_DATA_CH1 
#endif

#include <umat.hpp>
/// LLL DISPLAY ---)


#if 1 //def GRAPH_CONNECT_DEBUG
// BY Port ID
//ACF_Process_PortInfo* ACF_Process::RetPortInfo(std::string lPortIdentifier);

ACF_DataDesc TranslateVsdkUMatToAcfDataDesc(const vsdk::SUMat& lUmat);
#endif//def GRAPH_CONNECT_DEBUG  
/****************************************************************************
* Constant declarations
****************************************************************************/
// Supported resolutions and pixel formats for Frame_output:
// The FrameOutputV234 object always configures the FB in a double buffer
// scheme. It was tested
// to work well up to 1280x720, 24bpp. Attempts to use higher resolution (e.g.
// 1920x1080, 24bpp)
// might fail because of problems with allocation of contiguous buffers above
// certain size in Linux Kernel.(Reference:Frame_output_User_Guide.pdf.Page
// number:07)
const int cImageWidth  = 1024;
const int cImageHeight = 720;

/****************************************************************************
* Main function
* Note: This sample code highlights the steps for using a functionality.
*       To make the code more readable, error management has been simplified.
****************************************************************************/
#define INPUT_ROOT "./" //"data/common/"
int main(int argc, char** argv)
{
  // 0 success, not 0 error
  int lRetVal = 0;
  printf("Starting display using opencv and FB.\n");

  int lSrcWidth  = cImageWidth;
  int lSrcHeight = cImageHeight;

  char  inputImgName[255] = INPUT_ROOT "default.jpg";
  char *ptrName = inputImgName;

  if (argc < 1) { printf("Error: Provide input file\n"); return 0;}
  printf(" Taking %s as input file\n", argv[1]);
  ptrName = argv[1];

  vsdk::UMat c = cv::imread(ptrName, CV_LOAD_IMAGE_COLOR).getUMat(cv::ACCESS_READ);
  if(c.empty()) { printf("Cannot find the input image %s\n", ptrName); lRetVal = -1; return 0; }

  printf("\nFRAME OUTPUT V234 FB:--\n");

  io::FrameOutputV234Fb output(lSrcWidth, lSrcHeight, io::IO_DATA_DEPTH_08, CHNL_CNT); //DCU_BPP_YCbCr422);

  vsdk::UMat w = vsdk::UMat(lSrcHeight, lSrcWidth, VSDK_CV_8UC3);
  cv::Mat matOutput = w.getMat(cv::ACCESS_WRITE | OAL_USAGE_CACHED);

  printf("O1 size w:%d, h:%d, t:%d, c:%d\n", matOutput.cols, matOutput.rows, matOutput.type(), matOutput.channels());

  cv::Mat matResult = c.getMat(cv::ACCESS_READ  | OAL_USAGE_CACHED);

  // To display, make the resolution as to the FrameOutputV234Fb
  cv::Size umSize(lSrcWidth, lSrcHeight);
  cv::resize(matResult, matOutput, umSize);

  printf("R size w:%d, h:%d, t:%d, c:%d\n", matResult.cols, matResult.rows, matResult.type(), matResult.channels());
  printf("O size w:%d, h:%d, t:%d, c:%d\n", matOutput.cols, matOutput.rows, matOutput.type(), matOutput.channels());
  printf("W size w:%d, h:%d, t:%d, c:%d\n", w.cols, w.rows, w.type(), w.channels());

  printf("\nSAVE Result now: result.jpg\n");
  imwrite("/home/nxp/ws/result.jpg", matResult);

  printf("\nSAVE output now: output.jpg \n");
  imwrite("/home/nxp/ws/output.jpg", matOutput);

  // Put it on screen
  printf("\nOUTPUT now:--\n");
  output.PutFrame(w);// umInputImg); //err

  if(lRetVal != 0) {
    printf("Program Ended with Error 0x%X [ERROR]\n", lRetVal);
  }
  else {
    printf("Program Ended [SUCCESS]\n");
  }

  return lRetVal;
}
