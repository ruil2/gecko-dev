/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 * This file contains the WEBRTC dummy H264 wrapper implementation
 *
 */

#include "webrtc/modules/video_coding/codecs/h264/dummy_h264_impl.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#include "webrtc/common.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"
#include "webrtc/modules/interface/module_common_types.h"
#include "webrtc/system_wrappers/interface/trace.h"
#include "webrtc/system_wrappers/interface/tick_util.h"
#include "webrtc/system_wrappers/interface/trace_event.h"

namespace webrtc {

H264Encoder* H264Encoder::Create() {
  return new DummyH264EncoderImpl();
}

DummyH264EncoderImpl::DummyH264EncoderImpl()
    : encoded_complete_callback_(NULL) {
}

DummyH264EncoderImpl::~DummyH264EncoderImpl() {
  Release();
}

int DummyH264EncoderImpl::Release() {
  return WEBRTC_VIDEO_CODEC_OK;
}

int DummyH264EncoderImpl::SetRates(uint32_t new_bitrate_kbit,
                             uint32_t new_framerate) {
  WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
               "DummyH264EncoderImpl::SetRates(%d, %d)", new_bitrate_kbit, new_framerate);

  return WEBRTC_VIDEO_CODEC_OK;
}

int DummyH264EncoderImpl::InitEncode(const VideoCodec* inst,
                               int number_of_cores,
                               uint32_t /*max_payload_size*/) {

  return WEBRTC_VIDEO_CODEC_OK;
}

int DummyH264EncoderImpl::Encode(const I420VideoFrame& input_image,
                           const CodecSpecificInfo* codec_specific_info,
                           const std::vector<VideoFrameType>* frame_types) {
  return WEBRTC_VIDEO_CODEC_OK;
}

int DummyH264EncoderImpl::RegisterEncodeCompleteCallback(
    EncodedImageCallback* callback) {
  encoded_complete_callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int DummyH264EncoderImpl::SetChannelParameters(uint32_t /*packet_loss*/, int rtt) {
  // ffs
  return WEBRTC_VIDEO_CODEC_OK;
}

int DummyH264EncoderImpl::UpdateCodecFrameSize(const I420VideoFrame& input_image) {
  // ffs
  return WEBRTC_VIDEO_CODEC_OK;
}

H264Decoder* H264Decoder::Create() {
  return new DummyH264DecoderImpl();
}

DummyH264DecoderImpl::DummyH264DecoderImpl()
    : decode_complete_callback_(NULL) {
}

DummyH264DecoderImpl::~DummyH264DecoderImpl() {
  Release();
}

int DummyH264DecoderImpl::Reset() {
  return WEBRTC_VIDEO_CODEC_OK;
}

int DummyH264DecoderImpl::InitDecode(const VideoCodec* inst, int number_of_cores) {
  return WEBRTC_VIDEO_CODEC_OK;
}

int DummyH264DecoderImpl::Decode(const EncodedImage& input_image,
                           bool missing_frames,
                           const RTPFragmentationHeader* fragmentation,
                           const CodecSpecificInfo* codec_specific_info,
                           int64_t /*render_time_ms*/) {
  return WEBRTC_VIDEO_CODEC_OK;
}

int DummyH264DecoderImpl::RegisterDecodeCompleteCallback(
    DecodedImageCallback* callback) {
  decode_complete_callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int DummyH264DecoderImpl::Release() {
  return WEBRTC_VIDEO_CODEC_OK;
}

VideoDecoder* DummyH264DecoderImpl::Copy() {
  // Create a new VideoDecoder object
  DummyH264DecoderImpl *copy = new DummyH264DecoderImpl;

  return static_cast<VideoDecoder*>(copy);
}

}  // namespace webrtc
