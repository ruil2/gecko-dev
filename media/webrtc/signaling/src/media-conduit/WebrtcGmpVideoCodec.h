/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Class templates copied from WebRTC:
/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef WEBRTCGMPVIDEOCODEC_H_
#define WEBRTCGMPVIDEOCODEC_H_

#if !defined(XP_WIN)
#include <sys/time.h>
#endif

#include <iostream>
#include <queue>

#include "nsThreadUtils.h"
#include "mozilla/Mutex.h"

#include "mozIGeckoMediaPluginService.h"
#include "MediaConduitInterface.h"
#include "AudioConduit.h"
#include "VideoConduit.h"
#include "modules/video_coding/codecs/interface/video_codec_interface.h"

#include "gmp-video-host.h"
#include "gmp-video-encode.h"
#include "gmp-video-decode.h"
#include "gmp-video-frame-i420.h"
#include "gmp-video-frame-encoded.h"

#include "WebrtcGmpVideoCodec.h"

namespace mozilla {

class WebrtcGmpFrameStats {
 public:
  WebrtcGmpFrameStats(const char *type) :
      frames_in_(0),
      frames_out_(0),
      start_time_(time(0)),
      last_time_(start_time_),
      type_(type) {}

  void FrameIn();

  void FrameOut() {
    ++frames_out_;
  }

  uint64_t frames_in() const { return frames_in_; }

 private:
  uint64_t frames_in_;
  uint64_t frames_out_;
  time_t start_time_;
  time_t last_time_;
  const std::string type_;
};

struct TimeStampedEvent {
  TimeStampedEvent(const std::string& name) :
      name_(name) {
	#if defined(XP_WIN)
	
		#define EPOCHFILETIME (116444736000000000ULL)
		FILETIME ft;
		LARGE_INTEGER li;

		GetSystemTimeAsFileTime(&ft);
		li.LowPart = ft.dwLowDateTime;
		li.HighPart = ft.dwHighDateTime;
		time_ = (li.QuadPart - EPOCHFILETIME) * 100;

	#else

		struct timeval tv;
		gettimeofday(&tv, 0);
		time_ = tv.tv_sec;
		time_ *= 1000000;
		time_ += tv.tv_usec;
	
	#endif
  };

  const std::string name_;
  uint64_t time_;
};

class TimeStamper {
 public:
  TimeStamper(const char* name) :
      name_(name),
      stamps_() {}

  void Stamp(const char* event) {
    stamps_.push_back(new TimeStampedEvent(event));
  }

  void Dump() {
    uint64_t last = 0;
    std::cerr << "TIME RESULTS FOR = " << name_ << std::endl;

    for (auto a = stamps_.begin();  a != stamps_.end(); ++a) {
      if (!last)
        last = (*a)->time_;

      std::cerr << (*a)->name_ << ": " << (*a)->time_ << "("
                << (*a)->time_ - stamps_[0]->time_ << "/"
		<< (*a)->time_ - last << ")" << std::endl;
      last = (*a)->time_;
    }
  }

 private:
  const std::string name_;
  std::vector<TimeStampedEvent*> stamps_;
};


class WebrtcGmpVideoEncoder : public WebrtcVideoEncoder,
                              public GMPEncoderCallback {
 public:
  WebrtcGmpVideoEncoder();

  virtual ~WebrtcGmpVideoEncoder() {
  }

  // Implement VideoEncoder interface.
  virtual int32_t InitEncode(const webrtc::VideoCodec* codecSettings,
                                   int32_t numberOfCores,
                                   uint32_t maxPayloadSize);

  virtual int32_t Encode(const webrtc::I420VideoFrame& inputImage,
      const webrtc::CodecSpecificInfo* codecSpecificInfo,
      const std::vector<webrtc::VideoFrameType>* frame_types);

  virtual int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback);

  virtual int32_t Release();

  virtual int32_t SetChannelParameters(uint32_t packetLoss,
                                             int rtt);

  virtual int32_t SetRates(uint32_t newBitRate,
                           uint32_t frameRate);

  // GMPEncoderCallback virtual functions.
  virtual void Encoded(GMPVideoEncodedFrame* aEncodedFrame,
		       const GMPCodecSpecificInfo& aCodecSpecificInfo);


 private:
  virtual int32_t InitEncode_g(const webrtc::VideoCodec* codecSettings,
			       int32_t numberOfCores,
			       uint32_t maxPayloadSize);

  virtual int32_t Encode_g(const webrtc::I420VideoFrame* inputImage,
      const webrtc::CodecSpecificInfo* codecSpecificInfo,
      const std::vector<webrtc::VideoFrameType>* frame_types);

  virtual int32_t SetRates_g(uint32_t newBitRate,
                             uint32_t frameRate);

  nsCOMPtr<mozIGeckoMediaPluginService> mps_;
  nsIThread* gmp_thread_;
  GMPVideoEncoder* gmp_;
  GMPVideoHost* host_;
  webrtc::EncodedImageCallback* callback_;
  WebrtcGmpFrameStats stats_;
  std::queue<TimeStamper*> stampers_;
};


class WebrtcGmpVideoDecoder : public WebrtcVideoDecoder,
                              public GMPDecoderCallback {

 public:
  WebrtcGmpVideoDecoder();

  virtual ~WebrtcGmpVideoDecoder() {
  }

  // Implement VideoDecoder interface.
  virtual int32_t InitDecode(const webrtc::VideoCodec* codecSettings,
			     int32_t numberOfCores);
  virtual int32_t Decode(const webrtc::EncodedImage& inputImage,
			 bool missingFrames,
			 const webrtc::RTPFragmentationHeader* fragmentation,
			 const webrtc::CodecSpecificInfo*
			 codecSpecificInfo = NULL,
			 int64_t renderTimeMs = -1);
  virtual int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback* callback);

  virtual int32_t Release();

  virtual int32_t Reset();

  virtual void Decoded(GMPVideoi420Frame* aDecodedFrame);


  virtual void ReceivedDecodedReferenceFrame(const uint64_t aPictureId) {
    MOZ_CRASH();
  }

  virtual void ReceivedDecodedFrame(const uint64_t aPictureId) {
    MOZ_CRASH();
  }

  virtual void InputDataExhausted() {
    MOZ_CRASH();
  }

 private:
  virtual int32_t InitDecode_g(const webrtc::VideoCodec* codecSettings,
                               int32_t numberOfCores);

  virtual int32_t Decode_g(const webrtc::EncodedImage& inputImage,
                           bool missingFrames,
                           const webrtc::RTPFragmentationHeader* fragmentation,
                           const webrtc::CodecSpecificInfo* codecSpecificInfo,
                           int64_t renderTimeMs);

  nsCOMPtr<mozIGeckoMediaPluginService> mps_;
  nsIThread* gmp_thread_;
  GMPVideoDecoder* gmp_;
  GMPVideoHost* host_;
  webrtc::DecodedImageCallback* callback_;
  WebrtcGmpFrameStats stats_;
};

}

#endif
