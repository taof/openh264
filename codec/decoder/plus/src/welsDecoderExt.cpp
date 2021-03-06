/*!
 * \copy
 *     Copyright (c)  2009-2013, Cisco Systems
 *     All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 *     are met:
 *
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in
 *          the documentation and/or other materials provided with the
 *          distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *     POSSIBILITY OF SUCH DAMAGE.
 *
 *
 *  welsDecoderExt.cpp
 *
 *  Abstract
 *      Cisco OpenH264 decoder extension utilization
 *
 *  History
 *      3/12/2009 Created
 *
 *
 ************************************************************************/
//#include <assert.h>
#include "welsDecoderExt.h"
#include "welsCodecTrace.h"
#include "codec_def.h"
#include "typedefs.h"
#include "mem_align.h"
#include "utils.h"

//#include "macros.h"
#include "decoder.h"

extern "C" {
#include "decoder_core.h"
#include "manage_dec_ref.h"
}
#include "error_code.h"
#include "crt_util_safe_x.h"	// Safe CRT routines like util for cross platforms
#include <time.h>
#if defined(_WIN32) /*&& defined(_DEBUG)*/

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif

namespace WelsDec {

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

/***************************************************************************
*	Description:
*			class CWelsDecoder constructor function, do initialization	and
*       alloc memory required
*
*	Input parameters: none
*
*	return: none
***************************************************************************/
CWelsDecoder::CWelsDecoder (void_t)
  :	m_pDecContext (NULL),
    m_pTrace (NULL) {
#ifdef OUTPUT_BIT_STREAM
  str_t chFileName[1024] = { 0 };  //for .264
  int iBufUsed = 0;
  int iBufLeft = 1023;

  str_t chFileNameSize[1024] = { 0 }; //for .len
  int iBufUsedSize = 0;
  int iBufLeftSize = 1023;
#endif//OUTPUT_BIT_STREAM 

  m_pTrace = CreateWelsTrace (Wels_Trace_Type);

  IWelsTrace::WelsVTrace (m_pTrace, IWelsTrace::WELS_LOG_INFO, "CWelsDecoder::CWelsDecoder() entry");


#ifdef OUTPUT_BIT_STREAM
  SWelsTime sCurTime;

  WelsGetTimeOfDay (&sCurTime);

  iBufUsed      += WelsSnprintf (chFileName,  iBufLeft,  "bs_0x%p_", (void_t*)this);
  iBufUsedSize += WelsSnprintf (chFileNameSize, iBufLeftSize, "size_0x%p_", (void_t*)this);

  iBufLeft -= iBufUsed;
  if (iBufLeft > iBufUsed) {
    iBufUsed += WelsStrftime (&chFileName[iBufUsed], iBufLeft, "%y%m%d%H%M%S", &sCurTime);
    iBufLeft -= iBufUsed;
  }

  iBufLeftSize -= iBufUsedSize;
  if (iBufLeftSize > iBufUsedSize) {
    iBufUsedSize += WelsStrftime (&chFileNameSize[iBufUsedSize], iBufLeftSize, "%y%m%d%H%M%S", &sCurTime);
    iBufLeftSize -= iBufUsedSize;
  }

  if (iBufLeft > iBufUsed) {
    iBufUsed += WelsSnprintf (&chFileName[iBufUsed], iBufLeft, ".%03.3u.264", WelsGetMillsecond (&sCurTime));
    iBufLeft -= iBufUsed;
  }

  if (iBufLeftSize > iBufUsedSize) {
    iBufUsedSize += WelsSnprintf (&chFileNameSize[iBufUsedSize], iBufLeftSize, ".%03.3u.len",
                                  WelsGetMillsecond (&sCurTime));
    iBufLeftSize -= iBufUsedSize;
  }


  m_pFBS = WelsFopen (chFileName, "wb");
  m_pFBSSize = WelsFopen (chFileNameSize, "wb");
#endif//OUTPUT_BIT_STREAM

}

/***************************************************************************
*	Description:
*			class CWelsDecoder destructor function, destroy allocced memory
*
*	Input parameters: none
*
*	return: none
***************************************************************************/
CWelsDecoder::~CWelsDecoder() {
  IWelsTrace::WelsVTrace (m_pTrace, IWelsTrace::WELS_LOG_INFO, "CWelsDecoder::~CWelsDecoder()");

  UninitDecoder();

#ifdef OUTPUT_BIT_STREAM
  if (m_pFBS) {
    WelsFclose (m_pFBS);
    m_pFBS = NULL;
  }
  if (m_pFBSSize) {
    WelsFclose (m_pFBSSize);
    m_pFBSSize = NULL;
  }
#endif//OUTPUT_BIT_STREAM

  if (NULL != m_pTrace) {
    delete m_pTrace;
    m_pTrace = NULL;
  }
}

long CWelsDecoder::Initialize (void_t* pParam, const INIT_TYPE keInitType) {
  if (pParam == NULL || keInitType != INIT_TYPE_PARAMETER_BASED) {
    IWelsTrace::WelsVTrace (m_pTrace, IWelsTrace::WELS_LOG_INFO, "CWelsDecoder::Initialize(), invalid input argument.");
    return cmInitParaError;
  }

  // H.264 decoder initialization,including memory allocation,then open it ready to decode
  InitDecoder();

  DecoderConfigParam (m_pDecContext, pParam);

  return cmResultSuccess;
}

long CWelsDecoder::Uninitialize() {
  UninitDecoder();

  return ERR_NONE;
}

void_t CWelsDecoder::UninitDecoder (void_t) {
  if (NULL == m_pDecContext)
    return;

  IWelsTrace::WelsVTrace (m_pTrace, IWelsTrace::WELS_LOG_INFO, "into CWelsDecoder::uninit_decoder()..");

  WelsEndDecoder (m_pDecContext);

  if (NULL != m_pDecContext) {
    WelsFree (m_pDecContext, "m_pDecContext");

    m_pDecContext	= NULL;
  }

  IWelsTrace::WelsVTrace (m_pTrace, IWelsTrace::WELS_LOG_INFO, "left CWelsDecoder::uninit_decoder()..");
}

// the return value of this function is not suitable, it need report failure info to upper layer.
void_t CWelsDecoder::InitDecoder (void_t) {
  IWelsTrace::WelsVTrace (m_pTrace, IWelsTrace::WELS_LOG_INFO, "CWelsDecoder::init_decoder()..");

  m_pDecContext	= (PWelsDecoderContext)WelsMalloc (sizeof (SWelsDecoderContext), "m_pDecContext");

  WelsInitDecoder (m_pDecContext, m_pTrace, IWelsTrace::WelsTrace);

  IWelsTrace::WelsVTrace (m_pTrace, IWelsTrace::WELS_LOG_INFO, "CWelsDecoder::init_decoder().. left");
}

/*
 * Set Option
 */
long CWelsDecoder::SetOption (DECODER_OPTION eOptID, void_t* pOption) {
  int iVal = 0;

  if (m_pDecContext == NULL)
    return dsInitialOptExpected;

  if (eOptID == DECODER_OPTION_DATAFORMAT) { // Set color space of decoding output frame
    if (pOption == NULL)
      return cmInitParaError;

    iVal = * ((int*)pOption);	// is_rgb

    return DecoderSetCsp (m_pDecContext, iVal);
  } else if (eOptID == DECODER_OPTION_END_OF_STREAM) { // Indicate bit-stream of the final frame to be decoded
    if (pOption == NULL)
      return cmInitParaError;

    iVal	= * ((int*)pOption);	// boolean value for whether enabled End Of Stream flag

    m_pDecContext->bEndOfStreamFlag	= iVal ? true : false;

    return cmResultSuccess;
  } else if (eOptID == DECODER_OPTION_MODE) {
    if (pOption == NULL)
      return cmInitParaError;

    iVal = * ((int*)pOption);

    m_pDecContext->iSetMode = iVal;
    if (iVal == SW_MODE) {
      m_pDecContext->iDecoderOutputProperty = BUFFER_HOST;
    } else {
#if !defined(__APPLE__)
      m_pDecContext->iDecoderOutputProperty = BUFFER_DEVICE;
#else
      m_pDecContext->iDecoderOutputProperty = BUFFER_HOST;//BUFFER_HOST;//BUFFER_DEVICE;
#endif

    }

    return cmResultSuccess;
  } else if (eOptID == DECODER_OPTION_OUTPUT_PROPERTY) {
    if (pOption == NULL)
      return cmInitParaError;

    iVal = * ((int*)pOption);
    if (m_pDecContext->iSetMode != SW_MODE)
      m_pDecContext->iDecoderOutputProperty = iVal;
  }


  return cmInitParaError;
}

/*
 *	Get Option
 */
long CWelsDecoder::GetOption (DECODER_OPTION eOptID, void_t* pOption) {
  int iVal = 0;

  if (m_pDecContext == NULL)
    return cmInitExpected;

  if (pOption == NULL)
    return cmInitParaError;

  if (DECODER_OPTION_DATAFORMAT == eOptID) {
    iVal = m_pDecContext->iOutputColorFormat;
    * ((int*)pOption)	= iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_END_OF_STREAM == eOptID) {
    iVal	= m_pDecContext->bEndOfStreamFlag;
    * ((int*)pOption)	= iVal;
    return cmResultSuccess;
  }
#ifdef LONG_TERM_REF
  else if (DECODER_OPTION_IDR_PIC_ID == eOptID) {
    iVal = m_pDecContext->uiCurIdrPicId;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_FRAME_NUM == eOptID) {
    iVal = m_pDecContext->iFrameNum;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_LTR_MARKING_FLAG == eOptID) {
    iVal = m_pDecContext->bCurAuContainLtrMarkSeFlag;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_LTR_MARKED_FRAME_NUM == eOptID) {
    iVal = m_pDecContext->iFrameNumOfAuMarkedLtr;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  }
#endif
  else if (DECODER_OPTION_VCL_NAL == eOptID) { //feedback whether or not have VCL NAL in current AU
    iVal = m_pDecContext->iFeedbackVclNalInAu;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_TEMPORAL_ID == eOptID) { //if have VCL NAL in current AU, then feedback the temporal ID
    iVal = m_pDecContext->iFeedbackTidInAu;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_MODE == eOptID) {
    if (pOption == NULL)
      return cmInitParaError;

    iVal = m_pDecContext->iSetMode;

    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_DEVICE_INFO == eOptID) {
    if (pOption == NULL)
      return cmInitParaError;

    return cmResultSuccess;
  }

  return cmInitParaError;
}

DECODING_STATE CWelsDecoder::DecodeFrame (const unsigned char* kpSrc,
    const int kiSrcLen,
    void_t** ppDst,
    SBufferInfo* pDstInfo) {
  if (kiSrcLen > 0 && kpSrc != NULL) {
#ifdef OUTPUT_BIT_STREAM
    if (m_pFBS) {
      WelsFwrite (kpSrc, sizeof (unsigned char), kiSrcLen, m_pFBS);
      WelsFflush (m_pFBS);
    }
    if (m_pFBSSize) {
      WelsFwrite (&kiSrcLen, sizeof (int), 1, m_pFBSSize);
      WelsFflush (m_pFBSSize);
    }
#endif//OUTPUT_BIT_STREAM
    m_pDecContext->bEndOfStreamFlag = false;
  } else {
    //For application MODE, the error detection should be added for safe.
    //But for CONSOLE MODE, when decoding LAST AU, kiSrcLen==0 && kpSrc==NULL.
    m_pDecContext->bEndOfStreamFlag = true;
  }

  ppDst[0] = ppDst[1] = ppDst[2] = NULL;
  m_pDecContext->iErrorCode             = dsErrorFree; //initialize at the starting of AU decoding.
  m_pDecContext->iFeedbackVclNalInAu = FEEDBACK_UNKNOWN_NAL; //initialize
  memset (pDstInfo, 0, sizeof (SBufferInfo));
  pDstInfo->eBufferProperty = (EBufferProperty)m_pDecContext->iDecoderOutputProperty;

#ifdef LONG_TERM_REF
  m_pDecContext->bReferenceLostAtT0Flag       = false; //initialize for LTR
  m_pDecContext->bCurAuContainLtrMarkSeFlag = false;
  m_pDecContext->iFrameNumOfAuMarkedLtr      = 0;
  m_pDecContext->iFrameNum                       = -1; //initialize
#endif

  m_pDecContext->iFeedbackTidInAu             = -1; //initialize

  WelsDecodeBs (m_pDecContext, kpSrc, kiSrcLen, (unsigned char**)ppDst,
                pDstInfo); //iErrorCode has been modified in this function

  pDstInfo->eWorkMode = (EDecodeMode)m_pDecContext->iDecoderMode;

  if (m_pDecContext->iErrorCode) {
    ENalUnitType eNalType =
      NAL_UNIT_UNSPEC_0;	//for NBR, IDR frames are expected to decode as followed if error decoding an IDR currently

    eNalType	= m_pDecContext->sCurNalHead.eNalUnitType;

    //for AVC bitstream (excluding AVC with temporal scalability, including TP), as long as error occur, SHOULD notify upper layer key frame loss.
    if ((IS_PARAM_SETS_NALS (eNalType) || NAL_UNIT_CODED_SLICE_IDR == eNalType) ||
        (VIDEO_BITSTREAM_AVC == m_pDecContext->eVideoType)) {
#ifdef LONG_TERM_REF
      m_pDecContext->bParamSetsLostFlag = true;
#else
      m_pDecContext->bReferenceLostAtT0Flag = true;
#endif
      ResetParameterSetsState (m_pDecContext);  //initial SPS&PPS ready flag
    }

    IWelsTrace::WelsVTrace (m_pTrace, IWelsTrace::WELS_LOG_INFO, "decode failed, failure type:%d \n",
                            m_pDecContext->iErrorCode);
    return (DECODING_STATE)m_pDecContext->iErrorCode;
  }

  return dsErrorFree;
}

DECODING_STATE CWelsDecoder::DecodeFrame (const unsigned char* kpSrc,
    const int kiSrcLen,
    unsigned char** ppDst,
    int* pStride,
    int& iWidth,
    int& iHeight) {
  DECODING_STATE eDecState = dsErrorFree;
  SBufferInfo    DstInfo;

  memset (&DstInfo, 0, sizeof (SBufferInfo));
  DstInfo.UsrData.sSystemBuffer.iStride[0] = pStride[0];
  DstInfo.UsrData.sSystemBuffer.iStride[1] = pStride[1];
  DstInfo.UsrData.sSystemBuffer.iWidth = iWidth;
  DstInfo.UsrData.sSystemBuffer.iHeight = iHeight;
  DstInfo.eBufferProperty = BUFFER_HOST;

  eDecState = DecodeFrame (kpSrc, kiSrcLen, (void_t**)ppDst, &DstInfo);
  if (eDecState == dsErrorFree) {
    pStride[0] = DstInfo.UsrData.sSystemBuffer.iStride[0];
    pStride[1] = DstInfo.UsrData.sSystemBuffer.iStride[1];
    iWidth     = DstInfo.UsrData.sSystemBuffer.iWidth;
    iHeight    = DstInfo.UsrData.sSystemBuffer.iHeight;
  }

  return eDecState;
}

DECODING_STATE CWelsDecoder::DecodeFrameEx (const unsigned char* kpSrc,
    const int kiSrcLen,
    unsigned char* pDst,
    int iDstStride,
    int& iDstLen,
    int& iWidth,
    int& iHeight,
    int& iColorFormat) {
  DECODING_STATE	 state = dsErrorFree;

  return state;
}


} // namespace WelsDec


using namespace WelsDec;

/* WINAPI is indeed in prefix due to sync to application layer callings!! */

/*
*	CreateDecoder
*	@return:	success in return 0, otherwise failed.
*/
long CreateDecoder (ISVCDecoder** ppDecoder) {

  if (NULL == ppDecoder) {
    return ERR_INVALID_PARAMETERS;
  }

  *ppDecoder	= new CWelsDecoder();

  if (NULL == *ppDecoder) {
    return ERR_MALLOC_FAILED;
  }

  return ERR_NONE;
}

/*
*	DestroyDecoder
*/
void_t DestroyDecoder (ISVCDecoder* pDecoder) {
  if (NULL != pDecoder) {
    delete (CWelsDecoder*)pDecoder;
    pDecoder = NULL;
  }
}
