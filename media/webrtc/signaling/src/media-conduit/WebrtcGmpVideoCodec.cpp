/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nspr.h"

#include <iostream>
#include <vector>


#if defined(XP_WIN)
	#include <stdio.h>
	#define snprintf _snprintf 
#else
	#include <sys/time.h>
#endif


#include <mozilla/Scoped.h>
#include "VideoConduit.h"
#include "AudioConduit.h"

#include "video_engine/include/vie_external_codec.h"

#include "runnable_utils.h"

#include "mozIGeckoMediaPluginService.h"
#include "nsContentCID.h"
#include "nsServiceManagerUtils.h"

#include "gmp-video-host.h"
#include "gmp-video-frame-i420.h"
#include "gmp-video-frame-encoded.h"

#include "WebrtcGmpVideoCodec.h"

namespace mozilla {

void WebrtcGmpFrameStats::FrameIn() {
  if (!frames_in_)
    std::cerr << "FIRST FRAME" << std::endl;

  ++frames_in_;
  time_t now = time(0);

  if (now == last_time_)
    return;

  if (!(frames_in_ % 30)) {
    std::cerr << type_ << ": " << now << " Frame count "
              << frames_in_
              << "(" << (frames_in_ / (now - start_time_)) << "/"
              << (30 / (now - last_time_)) << ")"
              << " -- " << frames_out_ << std::endl;
    last_time_ = now;
  }
}

// Encoder.
WebrtcGmpVideoEncoder::WebrtcGmpVideoEncoder() :
    mps_(nullptr),
    gmp_thread_(nullptr),
    gmp_(nullptr),
    host_(nullptr),
    callback_(nullptr),
    stats_("WebrtcGmpVideoEncoder"),
    stampers_() {}

static int WebrtcFrameTypeToGmpFrameType(webrtc::VideoFrameType in,
                                         GMPVideoFrameType *out) {
  switch(in) {
    case webrtc::kKeyFrame:
      *out = kGMPKeyFrame;
      break;
    case webrtc::kDeltaFrame:
      *out = kGMPDeltaFrame;
      break;
    case webrtc::kGoldenFrame:
      *out = kGMPGoldenFrame;
      break;
    case webrtc::kAltRefFrame:
      *out = kGMPAltRefFrame;
      break;
    case webrtc::kSkipFrame:
      *out = kGMPSkipFrame;
      break;
    default:
      MOZ_CRASH();
      return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

static int GmpFrameTypeToWebrtcFrameType(GMPVideoFrameType in,
                                         webrtc::VideoFrameType *out) {
  switch(in) {
    case kGMPKeyFrame:
      *out = webrtc::kKeyFrame;
      break;
      *out = webrtc::kDeltaFrame;
    case kGMPDeltaFrame:
      break;
    case kGMPGoldenFrame:
      *out = webrtc::kGoldenFrame;
      break;
    case kGMPAltRefFrame:
      *out = webrtc::kAltRefFrame;
      break;
    case kGMPSkipFrame:
      *out = webrtc::kSkipFrame;
      break;
    default:
      MOZ_CRASH();
      return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}


int32_t WebrtcGmpVideoEncoder::InitEncode(
    const webrtc::VideoCodec* codecSettings,
    int32_t numberOfCores,
    uint32_t maxPayloadSize) {
  mps_ = do_GetService("@mozilla.org/gecko-media-plugin-service;1");

  if (!mps_) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  nsresult rv = mps_->GetThread(&gmp_thread_);
  if (NS_FAILED(rv))
    return WEBRTC_VIDEO_CODEC_ERROR;

  int32_t ret;
  RUN_ON_THREAD(gmp_thread_,
                WrapRunnableRet(this,
                                &WebrtcGmpVideoEncoder::InitEncode_g,
                                codecSettings,
                                numberOfCores,
                                maxPayloadSize,
                                &ret),
                NS_DISPATCH_SYNC);

  return ret;
}

int32_t WebrtcGmpVideoEncoder::InitEncode_g(
    const webrtc::VideoCodec* codecSettings,
    int32_t numberOfCores,
    uint32_t maxPayloadSize) {
  GMPVideoHost* host = nullptr;
  GMPVideoEncoder* gmp = nullptr;

  nsresult rv = mps_->GetGMPVideoEncoderVP8(&host, &gmp);
  if (NS_FAILED(rv))
    return WEBRTC_VIDEO_CODEC_ERROR;

  gmp_ = gmp;
  host_ = host;

  if (!gmp)
    return WEBRTC_VIDEO_CODEC_ERROR;
  if (!host)
    return WEBRTC_VIDEO_CODEC_ERROR;

  // TODO(ekr@rtfm.com): transfer settings from codecSettings to codec.
  GMPVideoCodec codec;
  memset(&codec, 0, sizeof(codec));

  codec.mWidth = codecSettings->width;
  codec.mHeight = codecSettings->height;
  codec.mStartBitrate = codecSettings->startBitrate;
  codec.mMinBitrate = codecSettings->minBitrate;
  codec.mMaxBitrate = codecSettings->maxBitrate;
  codec.mMaxFramerate = codecSettings->maxFramerate;

  GMPVideoErr err = gmp_->InitEncode(codec, this, 1, maxPayloadSize);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}


int32_t WebrtcGmpVideoEncoder::Encode(
    const webrtc::I420VideoFrame& inputImage,
    const webrtc::CodecSpecificInfo* codecSpecificInfo,
    const std::vector<webrtc::VideoFrameType>* frame_types) {
  stats_.FrameIn();
  char tmp[40];
  snprintf(tmp, sizeof(tmp), "CTX %p Frame %llu", this, stats_.frames_in());
  stampers_.push(new TimeStamper(tmp));
  stampers_.back()->Stamp("Encode enter");


  int32_t ret;
  RUN_ON_THREAD(gmp_thread_,
                WrapRunnableRet(this,
                                &WebrtcGmpVideoEncoder::Encode_g,
                                &inputImage,
                                codecSpecificInfo,
                                frame_types,
                                &ret),
                NS_DISPATCH_SYNC);

  stampers_.back()->Stamp("Encode leave");
  return ret;
}


 int32_t WebrtcGmpVideoEncoder::Encode_g(const webrtc::I420VideoFrame* inputImage,
                                         const webrtc::CodecSpecificInfo* codecSpecificInfo,
                                         const std::vector<webrtc::VideoFrameType>* frame_types) {
  GMPVideoFrame* ftmp = nullptr;
  stampers_.back()->Stamp("Encode_m enter");
  // Translate the image.
  GMPVideoErr err = host_->CreateFrame(kGMPI420VideoFrame, &ftmp);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  GMPVideoi420Frame* frame = static_cast<GMPVideoi420Frame*>(ftmp);

  err = frame->CreateFrame(inputImage->allocated_size(webrtc::kYPlane),
                           inputImage->buffer(webrtc::kYPlane),
                           inputImage->allocated_size(webrtc::kUPlane),
                           inputImage->buffer(webrtc::kUPlane),
                           inputImage->allocated_size(webrtc::kVPlane),
                           inputImage->buffer(webrtc::kVPlane),
                           inputImage->width(), inputImage->height(),
                           inputImage->stride(webrtc::kYPlane),
                           inputImage->stride(webrtc::kUPlane),
                           inputImage->stride(webrtc::kVPlane));
  if (err != GMPVideoNoErr) {
    return err;
  }
  frame->SetTimestamp(inputImage->timestamp());
  frame->SetRenderTime_ms(inputImage->render_time_ms());

  // TODO(ekr@rtfm.com): actually translate this.
  GMPCodecSpecificInfo info;
  memset(&info, 0, sizeof(info));

  std::vector<GMPVideoFrameType> gmp_frame_types;
  for (auto it = frame_types->begin(); it != frame_types->end(); ++it) {
    GMPVideoFrameType ft;

    int32_t ret = WebrtcFrameTypeToGmpFrameType(*it, &ft);
    if (ret != WEBRTC_VIDEO_CODEC_OK)
      return ret;

    gmp_frame_types.push_back(ft);
  }

  err = gmp_->Encode(frame, info, &gmp_frame_types);
  if (err != GMPVideoNoErr) {
    return err;
  }

  stampers_.back()->Stamp("Encode_m leave");
  return WEBRTC_VIDEO_CODEC_OK;
}



int32_t WebrtcGmpVideoEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
  callback_ = callback;

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t WebrtcGmpVideoEncoder::Release() {
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t WebrtcGmpVideoEncoder::SetChannelParameters(uint32_t packetLoss,
						     int rtt) {
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t WebrtcGmpVideoEncoder::SetRates(uint32_t newBitRate,
                                        uint32_t frameRate) {
  int32_t ret;
  RUN_ON_THREAD(gmp_thread_,
                WrapRunnableRet(this,
                                &WebrtcGmpVideoEncoder::SetRates_g,
                                newBitRate,
                                frameRate,
                                &ret),
                NS_DISPATCH_SYNC);

  return ret;
}

int32_t WebrtcGmpVideoEncoder::SetRates_g(uint32_t newBitRate,
                                          uint32_t frameRate) {
  GMPVideoErr err = gmp_->SetRates(newBitRate, frameRate);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

// GMPEncoderCallback virtual functions.
void WebrtcGmpVideoEncoder::Encoded(GMPVideoEncodedFrame* aEncodedFrame,
                                    const GMPCodecSpecificInfo& aCodecSpecificInfo) {
  TimeStamper* ts = stampers_.front();
  stampers_.pop();

  ts->Stamp("Encoded enter");

  webrtc::EncodedImage image(aEncodedFrame->Buffer(), aEncodedFrame->AllocatedSize(),
                             aEncodedFrame->Size());

  image._encodedWidth = aEncodedFrame->EncodedWidth();
  image._encodedHeight = aEncodedFrame->EncodedHeight();
  image._timeStamp = aEncodedFrame->TimeStamp();

  webrtc::VideoFrameType ft;
  int32_t ret = GmpFrameTypeToWebrtcFrameType(aEncodedFrame->FrameType(),
                                              &ft);
  if (ret != WEBRTC_VIDEO_CODEC_OK)
    return;
  image._frameType = ft;
  image._completeFrame = true;

  callback_->Encoded(image, nullptr, nullptr);
  stats_.FrameOut();
  ts->Stamp("Encoded leave");
  //  ts->Dump();
  delete ts;
  aEncodedFrame->Destroy();
}


// Decoder.
WebrtcGmpVideoDecoder::WebrtcGmpVideoDecoder() :
    mps_(nullptr),
    gmp_thread_(nullptr),
    gmp_(nullptr),
    host_(nullptr),
    callback_(nullptr),
    stats_("WebrtcGmpVideoDecoder") {}

int32_t WebrtcGmpVideoDecoder::InitDecode(
    const webrtc::VideoCodec* codecSettings,
    int32_t numberOfCores) {
  mps_ = do_GetService("@mozilla.org/gecko-media-plugin-service;1");

  if (!mps_) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  nsresult rv = mps_->GetThread(&gmp_thread_);
  if (NS_FAILED(rv))
    return WEBRTC_VIDEO_CODEC_ERROR;

  int32_t ret;
  RUN_ON_THREAD(gmp_thread_,
                WrapRunnableRet(this,
                                &WebrtcGmpVideoDecoder::InitDecode_g,
                                codecSettings,
                                numberOfCores,
                                &ret),
                NS_DISPATCH_SYNC);

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t WebrtcGmpVideoDecoder::InitDecode_g(
    const webrtc::VideoCodec* codecSettings,
    int32_t numberOfCores) {
  GMPVideoHost* host = nullptr;
  GMPVideoDecoder* gmp = nullptr;

  nsresult rv = mps_->GetGMPVideoDecoderVP8(&host, &gmp);
  if (NS_FAILED(rv))
    return WEBRTC_VIDEO_CODEC_ERROR;

  gmp_ = gmp;
  host_ = host;

  if (!gmp)
    return WEBRTC_VIDEO_CODEC_ERROR;
  if (!host)
    return WEBRTC_VIDEO_CODEC_ERROR;

  // TODO(ekr@rtfm.com): transfer settings from codecSettings to codec.
  GMPVideoCodec codec;
  memset(&codec, 0, sizeof(codec));

  GMPVideoErr err = gmp_->InitDecode(codec, this, 1);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t WebrtcGmpVideoDecoder::Decode(
    const webrtc::EncodedImage& inputImage,
    bool missingFrames,
    const webrtc::RTPFragmentationHeader* fragmentation,
    const webrtc::CodecSpecificInfo*
    codecSpecificInfo,
    int64_t renderTimeMs) {
  stats_.FrameIn();

  int32_t ret;

  RUN_ON_THREAD(gmp_thread_,
                WrapRunnableRet(this,
                                &WebrtcGmpVideoDecoder::Decode_g,
                                inputImage,
                                missingFrames,
                                fragmentation,
                                codecSpecificInfo,
                                renderTimeMs,
                                &ret),
                NS_DISPATCH_SYNC);

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t WebrtcGmpVideoDecoder::Decode_g(
    const webrtc::EncodedImage& inputImage,
    bool missingFrames,
    const webrtc::RTPFragmentationHeader* fragmentation,
    const webrtc::CodecSpecificInfo*
    codecSpecificInfo,
    int64_t renderTimeMs) {

  GMPVideoFrame* ftmp = nullptr;
  GMPVideoErr err = host_->CreateFrame(kGMPEncodedVideoFrame, &ftmp);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  GMPVideoEncodedFrame* frame = static_cast<GMPVideoEncodedFrame*>(ftmp);
  err = frame->CreateEmptyFrame(inputImage._length);
  if (err != GMPVideoNoErr) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  memcpy(frame->Buffer(), inputImage._buffer, frame->Size());

  frame->SetEncodedWidth(inputImage._encodedWidth);
  frame->SetEncodedHeight(inputImage._encodedHeight);
  frame->SetTimeStamp(inputImage._timeStamp);
  frame->SetCompleteFrame(inputImage._completeFrame);

  GMPVideoFrameType ft;
  int32_t ret = WebrtcFrameTypeToGmpFrameType(inputImage._frameType, &ft);
  if (ret != WEBRTC_VIDEO_CODEC_OK)
    return ret;

  // TODO(ekr@rtfm.com): Fill in
  GMPCodecSpecificInfo info;
  memset(&info, 0, sizeof(info));

  err = gmp_->Decode(frame, missingFrames, info, renderTimeMs);

  if (err != GMPVideoNoErr)
    return WEBRTC_VIDEO_CODEC_ERROR;

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t WebrtcGmpVideoDecoder::RegisterDecodeCompleteCallback(
    webrtc::DecodedImageCallback* callback) {
  callback_ = callback;

  return WEBRTC_VIDEO_CODEC_OK;
}


int32_t WebrtcGmpVideoDecoder::Release() {
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t WebrtcGmpVideoDecoder::Reset() {
  return WEBRTC_VIDEO_CODEC_OK;
}

void WebrtcGmpVideoDecoder::Decoded(GMPVideoi420Frame* aDecodedFrame) {
  webrtc::I420VideoFrame image;
  int ret = image.CreateFrame(aDecodedFrame->AllocatedSize(kGMPYPlane),
                              aDecodedFrame->Buffer(kGMPYPlane),
                              aDecodedFrame->AllocatedSize(kGMPUPlane),
                              aDecodedFrame->Buffer(kGMPUPlane),
                              aDecodedFrame->AllocatedSize(kGMPVPlane),
                              aDecodedFrame->Buffer(kGMPVPlane),
                              aDecodedFrame->Width(),
                              aDecodedFrame->Height(),
                              aDecodedFrame->Stride(kGMPYPlane),
                              aDecodedFrame->Stride(kGMPUPlane),
                              aDecodedFrame->Stride(kGMPVPlane));
  if (ret != 0)
    return;
  image.set_timestamp(aDecodedFrame->Timestamp());
  image.set_render_time_ms(0);

  callback_->Decoded(image);
  stats_.FrameOut();
  aDecodedFrame->Destroy();
}

}
