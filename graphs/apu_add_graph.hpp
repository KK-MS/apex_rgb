/*****************************************************************************
*
* NXP Confidential Proprietary
*
* Copyright (c) 2014-2016 Freescale Semiconductor
* Copyright 2017-2018 NXP
* All Rights Reserved
*
******************************************************************************
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
#ifndef APUADDGRAPH_HPP
#define APUADDGRAPH_HPP

/****************************************************************************
* Includes
****************************************************************************/
#ifdef APEX2_EMULATE
#include "acf_lib.hpp"
using namespace APEX2;
#else
#include "acf_graph.hpp"
#endif

// Graph string definitions
#include "apu_add_graph_names.h"

//Kernel includes
#include <arithmetic_acf.h>

/****************************************************************************
* Class definition
****************************************************************************/
class apu_add_graph : public ACF_Graph
{
public:
  apu_add_graph()
    : ACF_Graph()
  {
    XREGISTER_ACF_KERNEL(ADD_08u_K); // this works only if the corresponding KERNEL_INFO structure has been defined with: extKernelInfoDecl(ADD_08u_K);
  }

  void Create()
  {
    //set identifier for graph
    SetIdentifier(ADD_GN);

    //add kernel(s)
    std::string add_kernel_instance("Add_kernel_0");
    AddKernel(add_kernel_instance, ADD_08u_KN);

    //add graph ports
    AddInputPort(ADD_GRAPH_INA);
    AddInputPort(ADD_GRAPH_INB);
    AddInputPort(ADD_GRAPH_INC);
    AddOutputPort(ADD_GRAPH_OUT);

    //specify connections
    Connect(GraphPort(ADD_GRAPH_INA), KernelPort(add_kernel_instance, INPUT_0));
    Connect(GraphPort(ADD_GRAPH_INB), KernelPort(add_kernel_instance, INPUT_1));
    Connect(GraphPort(ADD_GRAPH_INC), KernelPort(add_kernel_instance, INPUT_2));
    Connect(KernelPort(add_kernel_instance, OUTPUT_0), GraphPort(ADD_GRAPH_OUT));
  }
};


#endif /* APUADDGRAPH_HPP */
