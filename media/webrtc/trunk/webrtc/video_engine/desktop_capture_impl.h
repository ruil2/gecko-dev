/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_DESKTOP_CAPTURE_MAIN_SOURCE_DESKTOP_CAPTURE_IMPL_H_
#define WEBRTC_MODULES_DESKTOP_CAPTURE_MAIN_SOURCE_DESKTOP_CAPTURE_IMPL_H_

/*
 * video_capture_impl.h
 */

#include "webrtc/common_video/interface/i420_video_frame.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"
#include "webrtc/modules/desktop_capture/screen_capturer.h"
#include "webrtc/system_wrappers/interface/tick_util.h"
#include "webrtc/modules/video_capture/video_capture_config.h"
#include "webrtc/modules/desktop_capture/shared_memory.h"
#include "webrtc/system_wrappers/interface/thread_wrapper.h"
#include "webrtc/modules/desktop_capture/mouse_cursor_shape.h"

using namespace webrtc::videocapturemodule;
namespace webrtc
{
class CriticalSectionWrapper;
class VideoCaptureEncodeInterface;
//vagouzhou@gmail.com
//we reuse video engine pipeline for screen sharing.
//As video did , DesktopCaptureImpl will be proxy for screen sharing ,aslo follow video pipeline design 

    class DesktopCaptureImpl: public VideoCaptureModule,
                            public VideoCaptureExternal,
                            public ScreenCapturer::Callback ,
                            public ScreenCapturer::MouseShapeObserver
{
public:
	/* Create a screen capture modules object
	*/
	static VideoCaptureModule* Create(const int32_t process_id,const int32_t monitor_id);

    static DeviceInfo* CreateDeviceInfo(const int32_t id);

    int32_t Init(const int32_t process_id,const int32_t monitor_id);
    // Implements Module declared functions.
    virtual int32_t ChangeUniqueId(const int32_t id);

    //Call backs
    virtual int32_t RegisterCaptureDataCallback(VideoCaptureDataCallback& dataCallback);
    virtual int32_t DeRegisterCaptureDataCallback();
    virtual int32_t RegisterCaptureCallback(VideoCaptureFeedBack& callBack);
    virtual int32_t DeRegisterCaptureCallback();

    virtual int32_t SetCaptureDelay(int32_t delayMS);
    virtual int32_t CaptureDelay();
    virtual int32_t SetCaptureRotation(VideoCaptureRotation rotation);

    virtual int32_t EnableFrameRateCallback(const bool enable);
    virtual int32_t EnableNoPictureAlarm(const bool enable);

    virtual const char* CurrentDeviceName() const;

    // Module handling
    virtual int32_t TimeUntilNextProcess();
    virtual int32_t Process();

    // Implement VideoCaptureExternal
    // |capture_time| must be specified in the NTP time format in milliseconds.
    virtual int32_t IncomingFrame(uint8_t* videoFrame,
                                  int32_t videoFrameLength,
                                  const VideoCaptureCapability& frameInfo,
                                  int64_t captureTime = 0);
    virtual int32_t IncomingFrameI420(
        const VideoFrameI420& video_frame,
        int64_t captureTime = 0);

    // Platform dependent
    virtual int32_t StartCapture(const VideoCaptureCapability& capability);
    virtual int32_t StopCapture();
    virtual bool CaptureStarted();
    virtual int32_t CaptureSettings(VideoCaptureCapability& /*settings*/);
    VideoCaptureEncodeInterface* GetEncodeInterface(const VideoCodec& /*codec*/){return NULL;}

	//ScreenCapturer::Callback
	virtual SharedMemory* CreateSharedMemory(size_t size);
	virtual void OnCaptureCompleted(DesktopFrame* frame);

    //ScreenCapturer::MouseShapeObserver
    virtual void OnCursorShapeChanged(MouseCursorShape* cursor_shape);
    
protected:
    DesktopCaptureImpl(const int32_t id);
    virtual ~DesktopCaptureImpl();
    int32_t DeliverCapturedFrame(I420VideoFrame& captureFrame,
                                 int64_t capture_time);

    int32_t _id; // Module ID
    char* _deviceUniqueId; // current Device unique name;
    CriticalSectionWrapper& _apiCs;
    int32_t _captureDelay; // Current capture delay. May be changed of platform dependent parts.
    VideoCaptureCapability _requestedCapability; // Should be set by platform dependent code in StartCapture.
private:
    void UpdateFrameCount();
    uint32_t CalculateFrameRate(const TickTime& now);

    CriticalSectionWrapper& _callBackCs;

    TickTime _lastProcessTime; // last time the module process function was called.
    TickTime _lastFrameRateCallbackTime; // last time the frame rate callback function was called.
    bool _frameRateCallBack; // true if EnableFrameRateCallback
    bool _noPictureAlarmCallBack; //true if EnableNoPictureAlarm
    VideoCaptureAlarm _captureAlarm; // current value of the noPictureAlarm

    int32_t _setCaptureDelay; // The currently used capture delay
    VideoCaptureDataCallback* _dataCallBack;
    VideoCaptureFeedBack* _captureCallBack;

    TickTime _lastProcessFrameCount;
    TickTime _incomingFrameTimes[kFrameRateCountHistorySize];// timestamp for local captured frames
    VideoRotationMode _rotateFrame; //Set if the frame should be rotated by the capture module.

    I420VideoFrame _captureFrame;
    VideoFrame _capture_encoded_frame;

    // Used to make sure incoming timestamp is increasing for every frame.
    int64_t last_capture_time_;

    // Delta used for translating between NTP and internal timestamps.
    const int64_t delta_ntp_internal_ms_;
public:
    static bool Run(void*obj)
    {
        static_cast<DesktopCaptureImpl*>(obj)->process();
        return true;
    };
    void process();
protected:
    
private:
	//
	scoped_ptr<ScreenCapturer> screen_capturer_;
    ThreadWrapper&  screen_capturer_thread_;
    scoped_ptr<MouseCursorShape> cursor_shape_;
};

}  // namespace webrtc
#endif  // WEBRTC_MODULES_DESKTOP_CAPTURE_MAIN_SOURCE_DESKTOP_CAPTURE_IMPL_H_
