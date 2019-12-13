/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef SOUNDTRIGGERENGINECAPICNN_H
#define SOUNDTRIGGERENGINECAPICNN_H

#include "capi_v2.h"
#include "capi_v2_extn.h"

#include "SoundTriggerEngine.h"
#include "QalRingBuffer.h"

class Stream;

class SoundTriggerEngineCapiCnn : public SoundTriggerEngine
{
 public:
    SoundTriggerEngineCapiCnn(Stream *s, uint32_t id, uint32_t stage_id,
                              QalRingBufferReader **reader,
                              std::shared_ptr<QalRingBuffer> buffer);
    ~SoundTriggerEngineCapiCnn();
    int32_t LoadSoundModel(Stream *s, uint8_t *data,
                           uint32_t data_size) override;
    int32_t UnloadSoundModel(Stream *s) override;
    int32_t StartRecognition(Stream *s) override;
    int32_t StopBuffering(Stream *s) override;
    int32_t StopRecognition(Stream *s) override;
    int32_t UpdateConfLevels(
        Stream *s,
        struct qal_st_recognition_config *config,
        uint8_t *conf_levels,
        uint32_t num_conf_levels) override;
    void SetDetected(bool detected) override;

    // Functions with no-op
    int32_t Open(Stream *s) {return 0;}
    int32_t Close(Stream *s) {return 0;}
    int32_t Prepare(Stream *s) {return 0;}
    int32_t SetConfig(Stream * s, configType type, int tag) {return 0;}
    int32_t getParameters(uint32_t param_id, void **payload) {return 0;}
    int32_t ConnectSessionDevice(
        Stream* stream_handle,
        qal_stream_type_t stream_type,
        std::shared_ptr<Device> device_to_connect) {return 0;}
    int32_t DisconnectSessionDevice(
        Stream* stream_handle,
        qal_stream_type_t stream_type,
        std::shared_ptr<Device> device_to_disconnect) {return 0;}
    int32_t UpdateBufConfig(uint32_t hist_buffer_duration,
                            uint32_t pre_roll_duration) {return 0;}
    void SetCaptureRequested(bool is_requested) {}

 protected:
    int32_t PrepareSoundEngine() {return 0;}
    int32_t StartSoundEngine();
    int32_t StopSoundEngine();
    int32_t StartDetection();
    static void BufferThreadLoop(SoundTriggerEngineCapiCnn *cnn_engine);

    capi_v2_t *capi_handle_;
    void* capi_lib_handle_;
    capi_v2_init_f  capi_init_;

    bool processing_started_;
    bool keyword_detected_;
    int32_t confidence_threshold_;
    uint32_t buffer_size_;
    /*
     * externally to allow engine to know where
     * it can stop and start processing
     */
    uint32_t buffer_start_;
    uint32_t buffer_end_;
    uint64_t kw_start_timestamp_;  // input from 1st stage
    uint64_t kw_end_timestamp_;
    uint32_t bytes_processed_;
    uint32_t kw_start_idx_;
    uint32_t kw_end_idx_;
    uint32_t confidence_score_;
};

#endif  // SOUNDTRIGGERENGINECAPICNN_H