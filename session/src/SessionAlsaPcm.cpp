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


 #define LOG_TAG "SessionAlsaPcm"

#include "SessionAlsaPcm.h"
#include "SessionAlsaUtils.h"
#include "Stream.h"
#include "ResourceManager.h"
#include <agm_api.h>
#include <sstream>
#include <string>
#include "detection_cmn_api.h"
#include "audio_dam_buffer_api.h"
#include "apm_api.h"

SessionAlsaPcm::SessionAlsaPcm(std::shared_ptr<ResourceManager> Rm)
{
   rm = Rm;
   builder = new PayloadBuilder();
   customPayload = NULL;
   customPayloadSize = 0;
   pcm = NULL;
   pcmRx = NULL;
   pcmTx = NULL;
   mState = SESSION_IDLE;
   isECRefSet = false;
}

SessionAlsaPcm::~SessionAlsaPcm()
{
   delete builder;

}


int SessionAlsaPcm::prepare(Stream * s)
{
   return 0;
}

int SessionAlsaPcm::open(Stream * s)
{
    int status = -EINVAL;
    struct qal_stream_attributes sAttr;
    std::vector<std::shared_ptr<Device>> associatedDevices;

    status = s->getStreamAttributes(&sAttr);
    if(0 != status) {
        QAL_ERR(LOG_TAG,"%s: getStreamAttributes Failed \n", __func__);
        return status;
    }

    status = s->getAssociatedDevices(associatedDevices);
    if(0 != status) {
        QAL_ERR(LOG_TAG,"%s: getAssociatedDevices Failed \n", __func__);
        return status;
    }

    rm->getBackEndNames(associatedDevices, rxAifBackEnds, txAifBackEnds);
    if (rxAifBackEnds.empty() && txAifBackEnds.empty()) {
        QAL_ERR(LOG_TAG, "no backend specified for this stream");
        return status;

    }
    if(sAttr.direction == QAL_AUDIO_INPUT) {
        pcmDevIds = rm->allocateFrontEndIds(sAttr, 0);
    } else if (sAttr.direction == QAL_AUDIO_OUTPUT) {
        pcmDevIds = rm->allocateFrontEndIds(sAttr, 0);
    } else {
        pcmDevRxIds = rm->allocateFrontEndIds(sAttr, RXLOOPBACK);
        pcmDevTxIds = rm->allocateFrontEndIds(sAttr, TXLOOPBACK);
    }
    status = rm->getAudioMixer(&mixer);
    if (status) {
        QAL_ERR(LOG_TAG,"mixer error");
        return status;
    }
    switch(sAttr.direction) {
        case QAL_AUDIO_INPUT:
            status = SessionAlsaUtils::open(s, rm, pcmDevIds, txAifBackEnds);
            if (status) {
                QAL_ERR(LOG_TAG, "session alsa open failed with %d", status);
                rm->freeFrontEndIds(pcmDevIds, sAttr, 0);
            }
            break;
        case QAL_AUDIO_OUTPUT:
            status = SessionAlsaUtils::open(s, rm, pcmDevIds, rxAifBackEnds);
            if (status) {
                QAL_ERR(LOG_TAG, "session alsa open failed with %d", status);
                rm->freeFrontEndIds(pcmDevIds, sAttr, 0);
                break;
            }
            status = SessionAlsaUtils::getModuleInstanceId(mixer, pcmDevIds.at(0),
                    rxAifBackEnds[0].second.data(), false, STREAM_SPR, &spr_miid);
            if (0 != status) {
                QAL_ERR(LOG_TAG, "Failed to get tag info %x, status = %d", STREAM_SPR, status);
                status = 0; //TODO: add this to some policy in qal
            }
            break;
        case QAL_AUDIO_INPUT | QAL_AUDIO_OUTPUT:
            status = SessionAlsaUtils::open(s, rm, pcmDevRxIds, pcmDevTxIds,
                    rxAifBackEnds, txAifBackEnds);
            if (status) {
                QAL_ERR(LOG_TAG, "session alsa open failed with %d", status);
                rm->freeFrontEndIds(pcmDevRxIds, sAttr, RXLOOPBACK);
                rm->freeFrontEndIds(pcmDevTxIds, sAttr, TXLOOPBACK);
            }
            break;
        default:
            QAL_ERR(LOG_TAG,"unsupported direction");
            break;
    }
    return status;
}

int SessionAlsaPcm::setTKV(Stream * s, configType type, effect_qal_payload_t *payload)
{
    return 0;
}

int SessionAlsaPcm::setConfig(Stream * s, configType type, uint32_t tag1,
        uint32_t tag2, uint32_t tag3)
{
    int status = 0;
    uint32_t tagsent = 0;
    struct agm_tag_config* tagConfig = nullptr;
    std::ostringstream tagCntrlName;
    char const *stream = "PCM";
    const char *setParamTagControl = "setParamTag";
    struct mixer_ctl *ctl = nullptr;
    uint32_t tkv_size = 0;

    switch (type) {
        case MODULE:
            tkv.clear();
            if (tag1)
                builder->populateTagKeyVector(s, tkv, tag1, &tagsent);
            if (tag2)
                builder->populateTagKeyVector(s, tkv, tag2, &tagsent);
            if (tag3)
                builder->populateTagKeyVector(s, tkv, tag3, &tagsent);

            if (tkv.size() == 0) {
                status = -EINVAL;
                goto exit;
            }
            tagConfig = (struct agm_tag_config*)malloc (sizeof(struct agm_tag_config) +
                            (tkv.size() * sizeof(agm_key_value)));
            if(!tagConfig) {
                status = -ENOMEM;
                goto exit;
            }
            status = SessionAlsaUtils::getTagMetadata(tagsent, tkv, tagConfig);
            if (0 != status) {
                goto exit;
            }
            tagCntrlName << stream << pcmDevIds.at(0) << " " << setParamTagControl;
            ctl = mixer_get_ctl_by_name(mixer, tagCntrlName.str().data());
            if (!ctl) {
                QAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", tagCntrlName.str().data());
                return -ENOENT;
            }

            tkv_size = tkv.size() * sizeof(struct agm_key_value);
            status = mixer_ctl_set_array(ctl, tagConfig, sizeof(struct agm_tag_config) + tkv_size);
            if (status != 0) {
                QAL_ERR(LOG_TAG,"failed to set the tag calibration %d", status);
                goto exit;
            }
            ctl = NULL;
            tkv.clear();
            break;
        default:
            status = 0;
            break;
    }

exit:
    return status;
}

int SessionAlsaPcm::setConfig(Stream * s, configType type, int tag)
{
    int status = 0;
    uint32_t tagsent;
    struct agm_tag_config* tagConfig;
    const char *setParamTagControl = "setParamTag";
    const char *stream = "PCM";
    const char *setCalibrationControl = "setCalibration";
    struct mixer_ctl *ctl;
    struct agm_cal_config *calConfig;
    std::ostringstream tagCntrlName;
    std::ostringstream calCntrlName;
    int tkv_size = 0;
    int ckv_size = 0;

    switch (type) {
        case MODULE:
            tkv.clear();
            status = builder->populateTagKeyVector(s, tkv, tag, &tagsent);
            if (0 != status) {
                QAL_ERR(LOG_TAG,"%s: Failed to set the tag configuration\n", __func__);
                goto exit;
            }

            if (tkv.size() == 0) {
                status = -EINVAL;
                goto exit;
            }

            tagConfig = (struct agm_tag_config*)malloc (sizeof(struct agm_tag_config) +
                            (tkv.size() * sizeof(agm_key_value)));

            if(!tagConfig) {
                status = -EINVAL;
                goto exit;
            }

            status = SessionAlsaUtils::getTagMetadata(tagsent, tkv, tagConfig);
            if (0 != status) {
                goto exit;
            }
            tagCntrlName<<stream<<pcmDevIds.at(0)<<" "<<setParamTagControl;
            ctl = mixer_get_ctl_by_name(mixer, tagCntrlName.str().data());
            if (!ctl) {
                QAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", tagCntrlName.str().data());
                return -ENOENT;
            }

            tkv_size = tkv.size()*sizeof(struct agm_key_value);
            status = mixer_ctl_set_array(ctl, tagConfig, sizeof(struct agm_tag_config) + tkv_size);
            if (status != 0) {
                QAL_ERR(LOG_TAG,"failed to set the tag calibration %d", status);
                goto exit;
            }
            ctl = NULL;
            tkv.clear();
            break;
            //todo calibration
        case CALIBRATION:
            status = builder->populateCalKeyVector(s, ckv, tag);
            if (0 != status) {
                QAL_ERR(LOG_TAG,"%s: Failed to set the calibration data\n", __func__);
                goto exit;
            }


            if (ckv.size() == 0) {
                status = -EINVAL;
                goto exit;
            }


            calConfig = (struct agm_cal_config*)malloc (sizeof(struct agm_cal_config) +
                            (ckv.size() * sizeof(agm_key_value)));

            if(!calConfig) {
                status = -EINVAL;
                goto exit;
            }

            status = SessionAlsaUtils::getCalMetadata(ckv, calConfig);
            calCntrlName<<stream<<pcmDevIds.at(0)<<" "<<setCalibrationControl;
            ctl = mixer_get_ctl_by_name(mixer, calCntrlName.str().data());
            if (!ctl) {
                QAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", calCntrlName.str().data());
                return -ENOENT;
            }
            ckv_size = ckv.size()*sizeof(struct agm_key_value);
            status = mixer_ctl_set_array(ctl, calConfig, sizeof(struct agm_cal_config) + ckv_size);
            if (status != 0) {
                QAL_ERR(LOG_TAG,"failed to set the tag calibration %d", status);
                goto exit;
            }
            ctl = NULL;
            ckv.clear();
            break;
        default:
            QAL_ERR(LOG_TAG,"%s: invalid type ", __func__);
            status = -EINVAL;
            goto exit;
    }

exit:
    QAL_DBG(LOG_TAG,"%s: exit status:%d ", __func__, status);
    return status;
}
/*
int SessionAlsaPcm::getConfig(Stream * s)
{
   return 0;
}
*/
int SessionAlsaPcm::start(Stream * s)
{
    struct pcm_config config;
    struct qal_stream_attributes sAttr;
    int32_t status = 0;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    struct qal_device dAttr;
    uint32_t ch_tag = 0, bitwidth_tag = 16, mfc_sr_tag = 0;
    struct sessionToPayloadParam deviceData;
    struct sessionToPayloadParam streamData;
    uint8_t* payload = NULL;
    size_t payloadSize = 0;
    uint32_t miid;

    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        QAL_ERR(LOG_TAG,"stream get attributes failed");
        return status;
    }

    if (mState == SESSION_IDLE) {
        s->getBufInfo(&in_buf_size,&in_buf_count,&out_buf_size,&out_buf_count);
        memset(&config, 0, sizeof(config));

        if (sAttr.direction == QAL_AUDIO_INPUT) {
            config.rate = sAttr.in_media_config.sample_rate;
            if (sAttr.in_media_config.bit_width == 32)
                config.format = PCM_FORMAT_S32_LE;
            else if (sAttr.in_media_config.bit_width == 24)
                config.format = PCM_FORMAT_S24_3LE;
            else if (sAttr.in_media_config.bit_width == 16)
                config.format = PCM_FORMAT_S16_LE;
            config.channels = sAttr.in_media_config.ch_info->channels;
            config.period_size = in_buf_size;
            config.period_count = in_buf_count;
        } else {
            config.rate = sAttr.out_media_config.sample_rate;
            if (sAttr.out_media_config.bit_width == 32)
                config.format = PCM_FORMAT_S32_LE;
            else if (sAttr.out_media_config.bit_width == 24)
                config.format = PCM_FORMAT_S24_3LE;
            else if (sAttr.out_media_config.bit_width == 16)
                config.format = PCM_FORMAT_S16_LE;
            config.channels = sAttr.out_media_config.ch_info->channels;
            config.period_size = out_buf_size;
            config.period_count = out_buf_count;
        }
        config.start_threshold = 0;
        config.stop_threshold = 0;
        config.silence_threshold = 0;

        switch(sAttr.direction) {
            case QAL_AUDIO_INPUT:
                if (sAttr.type == QAL_STREAM_VOICE_UI) {
                    SessionAlsaUtils::setMixerParameter(mixer, pcmDevIds.at(0), false, customPayload, customPayloadSize);
                }
 
                pcm = pcm_open(rm->getSndCard(), pcmDevIds.at(0), PCM_IN, &config);
                if (!pcm) {
                    QAL_ERR(LOG_TAG, "pcm open failed");
                    return -EINVAL;
                }

                if (!pcm_is_ready(pcm)) {
                    QAL_ERR(LOG_TAG, "pcm open not ready");
                    return -EINVAL;
                }
                break;
            case QAL_AUDIO_OUTPUT:
                pcm = pcm_open(rm->getSndCard(), pcmDevIds.at(0), PCM_OUT, &config);
                if (!pcm) {
                    QAL_ERR(LOG_TAG, "pcm open failed");
                    return -EINVAL;
                }

                if (!pcm_is_ready(pcm)) {
                    QAL_ERR(LOG_TAG, "pcm open not ready");
                    return -EINVAL;
                }
                break;
            case QAL_AUDIO_INPUT | QAL_AUDIO_OUTPUT:
                pcmRx = pcm_open(rm->getSndCard(), pcmDevRxIds.at(0), PCM_OUT, &config);
                if (!pcmRx) {
                    QAL_ERR(LOG_TAG, "pcm-rx open failed");
                    return -EINVAL;
                }

                if (!pcm_is_ready(pcmRx)) {
                    QAL_ERR(LOG_TAG, "pcm-rx open not ready");
                    return -EINVAL;
                }
                status = pcm_start(pcmRx);
                if (status) {
                    QAL_ERR(LOG_TAG, "pcm_start rx failed %d", status);
                }
                pcmTx = pcm_open(rm->getSndCard(), pcmDevTxIds.at(0), PCM_IN, &config);
                if (!pcmTx) {
                    QAL_ERR(LOG_TAG, "pcm-tx open failed");
                    return -EINVAL;
                }

                if (!pcm_is_ready(pcmTx)) {
                    QAL_ERR(LOG_TAG, "pcm-tx open not ready");
                    return -EINVAL;
                }
                status = pcm_start(pcmTx);
                if (status) {
                    QAL_ERR(LOG_TAG, "pcm_start tx failed %d", status);
                }
                break;
        }
        mState = SESSION_OPENED;
    }
    if (sAttr.type == QAL_STREAM_VOICE_UI) {
        SessionAlsaUtils::registerMixerEvent(mixer, pcmDevIds.at(0),
                txAifBackEnds[0].second.data(), false, DEVICE_SVA, true);
    }

    checkAndConfigConcurrency(s);


    switch(sAttr.direction) {
        case QAL_AUDIO_INPUT:
            if (sAttr.type != QAL_STREAM_VOICE_UI) {
                /* Get MFC MIID and configure to match to stream config */
                /* This has to be done after sending all mixer controls and before connect */
                status = SessionAlsaUtils::getModuleInstanceId(mixer, pcmDevIds.at(0),
                                                               txAifBackEnds[0].second.data(),
                                                               false, TAG_STREAM_MFC_SR, &miid);
                if (status != 0) {
                    QAL_ERR(LOG_TAG,"getModuleInstanceId failed");
                    return status;
                }
                QAL_ERR(LOG_TAG, "miid : %x id = %d, data %s\n", miid,
                        pcmDevIds.at(0), txAifBackEnds[0].second.data());
                streamData.bitWidth = sAttr.in_media_config.bit_width;
                streamData.sampleRate = sAttr.in_media_config.sample_rate;
                streamData.numChannel = sAttr.in_media_config.ch_info->channels;
                builder->payloadMFCConfig(&payload, &payloadSize, miid, &streamData);
                status = SessionAlsaUtils::setMixerParameter(mixer, pcmDevIds.at(0), false,
                                                             payload, payloadSize);
                if (status != 0) {
                    QAL_ERR(LOG_TAG,"setMixerParameter failed");
                    return status;
                }
            }
            status = pcm_start(pcm);
            if (status) {
                QAL_ERR(LOG_TAG, "pcm_start failed %d", status);
            }
            break;
        case QAL_AUDIO_OUTPUT:
            status = s->getAssociatedDevices(associatedDevices);
            if(0 != status) {
                QAL_ERR(LOG_TAG,"%s: getAssociatedDevices Failed \n", __func__);
                return status;
            }
            for (int i = 0; i < associatedDevices.size();i++) {
                status = associatedDevices[i]->getDeviceAtrributes(&dAttr);
                if(0 != status) {
                    QAL_ERR(LOG_TAG,"%s: getAssociatedDevices Failed \n", __func__);
                    return status;
                }
                /* Get PSPD MFC MIID and configure to match to device config */
                /* This has to be done after sending all mixer controls and before connect */
                status = SessionAlsaUtils::getModuleInstanceId(mixer, pcmDevIds.at(0),
                                                               rxAifBackEnds[i].second.data(),
                                                               false, TAG_DEVICE_MFC_SR, &miid);
                if (status != 0) {
                    QAL_ERR(LOG_TAG,"getModuleInstanceId failed");
                    return status;
                }
                QAL_DBG(LOG_TAG, "miid : %x id = %d, data %s, dev id = %d\n", miid,
                        pcmDevIds.at(0), rxAifBackEnds[i].second.data(), dAttr.id);
                deviceData.bitWidth = dAttr.config.bit_width;
                deviceData.sampleRate = dAttr.config.sample_rate;
                deviceData.numChannel = dAttr.config.ch_info->channels;
                builder->payloadMFCConfig(&payload, &payloadSize, miid, &deviceData);
                status = SessionAlsaUtils::setMixerParameter(mixer, pcmDevIds.at(0), false,
                                                             payload, payloadSize);
                if (status != 0) {
                    QAL_ERR(LOG_TAG,"setMixerParameter failed");
                    return status;
                }
            }
            //status = pcm_prepare(pcm);
            //if (status) {
            //    QAL_ERR(LOG_TAG, "pcm_prepare failed %d", status);
            //}
            status = pcm_start(pcm);
            if (status) {
                QAL_ERR(LOG_TAG, "pcm_start failed %d", status);
            }
            break;
        case QAL_AUDIO_INPUT | QAL_AUDIO_OUTPUT:
            break;
    }
    mState = SESSION_STARTED;

    if (sAttr.type == QAL_STREAM_VOICE_UI) {
        threadHandler = std::thread(SessionAlsaPcm::eventWaitThreadLoop, (void *)mixer, this);
        if (!threadHandler.joinable()) {
            QAL_ERR(LOG_TAG, "Failed to create threadHandler");
            status = -EINVAL;
        }
    }
    return status;
}

int SessionAlsaPcm::stop(Stream * s)
{
    int status = 0;
    struct qal_stream_attributes sAttr;
    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        QAL_ERR(LOG_TAG,"stream get attributes failed");
        return status;
    }
    switch(sAttr.direction) {
        case QAL_AUDIO_INPUT:
        case QAL_AUDIO_OUTPUT:
            if (pcm && isActive()) {
                status = pcm_stop(pcm);
                if (status) {
                    QAL_ERR(LOG_TAG, "pcm_stop failed %d", status);
                }
            }
            break;
        case QAL_AUDIO_INPUT | QAL_AUDIO_OUTPUT:
            if (pcmRx && isActive()) {
                status = pcm_stop(pcmRx);
                if (status) {
                    QAL_ERR(LOG_TAG, "pcm_stop - rx failed %d", status);
                }
            }
            if (pcmTx && isActive()) {
                status = pcm_stop(pcmTx);
                if (status) {
                    QAL_ERR(LOG_TAG, "pcm_stop - tx failed %d", status);
                }
            }
            break;
    }
    mState = SESSION_STOPPED;

    if (sAttr.type == QAL_STREAM_VOICE_UI) {
        threadHandler.join();
        QAL_DBG(LOG_TAG, "threadHandler joined");
        SessionAlsaUtils::registerMixerEvent(mixer, pcmDevIds.at(0), txAifBackEnds[0].second.data(),
                false, DEVICE_SVA, false);
    }
    return status;
}

int SessionAlsaPcm::close(Stream * s)
{
    int status = 0;
    struct qal_stream_attributes sAttr;
    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        QAL_ERR(LOG_TAG,"stream get attributes failed");
        return status;
    }
    switch(sAttr.direction) {
        case QAL_AUDIO_INPUT:
        case QAL_AUDIO_OUTPUT:
            if (pcm)
                status = pcm_close(pcm);
            if (status) {
                QAL_ERR(LOG_TAG, "pcm_close failed %d", status);
            }
            rm->freeFrontEndIds(pcmDevIds, sAttr, 0);
            pcm = NULL;
            break;
        case QAL_AUDIO_INPUT | QAL_AUDIO_OUTPUT:
            if (pcmRx)
                status = pcm_close(pcmRx);
            if (status) {
                QAL_ERR(LOG_TAG, "pcm_close - rx failed %d", status);
            }
            if (pcmTx)
                status = pcm_close(pcmTx);
            if (status) {
               QAL_ERR(LOG_TAG, "pcm_close - tx failed %d", status);
            }
            pcmRx = NULL;
            pcmTx = NULL;
            break;
    }

    mState = SESSION_IDLE;

    if (customPayload) {
        free(customPayload);
        customPayload = NULL;
        customPayloadSize = 0;
    }
exit:
    return status;
}

int SessionAlsaPcm::disconnectSessionDevice(Stream *streamHandle,
        qal_stream_type_t streamType, std::shared_ptr<Device> deviceToDisconnect)
{
    std::vector<std::shared_ptr<Device>> deviceList;
    struct qal_device dAttr;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEndsToDisconnect;
    std::vector<std::pair<int32_t, std::string>> txAifBackEndsToDisconnect;
    int32_t status = 0;

    deviceList.push_back(deviceToDisconnect);
    rm->getBackEndNames(deviceList, rxAifBackEndsToDisconnect,
            txAifBackEndsToDisconnect);
    deviceToDisconnect->getDeviceAtrributes(&dAttr);

    if (!rxAifBackEndsToDisconnect.empty())
        status = SessionAlsaUtils::disconnectSessionDevice(streamHandle, streamType, rm,
            dAttr, pcmDevIds, rxAifBackEndsToDisconnect);

    if (!txAifBackEndsToDisconnect.empty())
        status = SessionAlsaUtils::disconnectSessionDevice(streamHandle, streamType, rm,
            dAttr, pcmDevIds, txAifBackEndsToDisconnect);

    return status;
}

int SessionAlsaPcm::connectSessionDevice(Stream* streamHandle, qal_stream_type_t streamType,
        std::shared_ptr<Device> deviceToConnect)
{
    std::vector<std::shared_ptr<Device>> deviceList;
    struct qal_device dAttr;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEndsToConnect;
    std::vector<std::pair<int32_t, std::string>> txAifBackEndsToConnect;
    int32_t status = 0;

    deviceList.push_back(deviceToConnect);
    rm->getBackEndNames(deviceList, rxAifBackEndsToConnect,
            txAifBackEndsToConnect);
    deviceToConnect->getDeviceAtrributes(&dAttr);

    if (!rxAifBackEndsToConnect.empty())
        status = SessionAlsaUtils::connectSessionDevice(streamHandle, streamType, rm,
            dAttr, pcmDevIds, rxAifBackEndsToConnect);

    if (!txAifBackEndsToConnect.empty())
        status = SessionAlsaUtils::connectSessionDevice(streamHandle, streamType, rm,
            dAttr, pcmDevIds, txAifBackEndsToConnect);

    return status;
}

int SessionAlsaPcm::read(Stream *s, int tag, struct qal_buffer *buf, int * size)
{
    int status = 0, bytesRead = 0, bytesToRead = 0, offset = 0, pcmReadSize = 0;
    uint64_t timestamp = 0;
    const char *control = "bufTimestamp";
    const char *stream = "PCM";
    struct mixer_ctl *ctl;
    std::ostringstream CntrlName;
    struct qal_stream_attributes sAttr;

    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        QAL_ERR(LOG_TAG,"stream get attributes failed");
        return status;
    }
    while (1) {
        offset = bytesRead + buf->offset;
        bytesToRead = buf->size - offset;
        if (!bytesToRead)
            break;
        if ((bytesToRead / in_buf_size) >= 1)
            pcmReadSize = in_buf_size;
        else
            pcmReadSize = bytesToRead;
        void *data = buf->buffer;
        data = static_cast<char*>(data) + offset;
        status =  pcm_read(pcm, data,  pcmReadSize);
        if ((0 != status) || (pcmReadSize == 0)) {
            QAL_ERR(LOG_TAG,"%s: Failed to read data %d bytes read %d", __func__, status, pcmReadSize);
            break;
        }

        if (!bytesRead && buf->ts &&
            (sAttr.type == QAL_STREAM_VOICE_UI)) {
            CntrlName << stream << pcmDevIds.at(0) << " " << control;
            ctl = mixer_get_ctl_by_name(mixer, CntrlName.str().data());
            if (!ctl) {
                QAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", CntrlName.str().data());
                status = -ENOENT;
                goto exit;
            }

            status = mixer_ctl_get_array(ctl, (void *)&timestamp, sizeof(uint64_t));
            if (0 != status) {
                QAL_ERR(LOG_TAG, "Get timestamp failed, status = %d", status);
                goto exit;
            }

            buf->ts->tv_sec = timestamp / 1000000;
            buf->ts->tv_nsec = (timestamp - buf->ts->tv_sec * 1000000) * 1000;
            QAL_VERBOSE(LOG_TAG, "Timestamp %llu, tv_sec = %ld, tv_nsec = %ld",
                        timestamp, buf->ts->tv_sec, buf->ts->tv_nsec);
        }
        bytesRead += pcmReadSize;
    }
exit:
    *size = bytesRead;
    QAL_DBG(LOG_TAG,"%s: exit bytesRead:%d status:%d ", __func__, bytesRead, status);
    return status;
}

int SessionAlsaPcm::write(Stream *s, int tag, struct qal_buffer *buf, int * size,
                          int flag)
{
    int status = 0, bytesWritten = 0, bytesRemaining = 0, offset = 0;
    uint32_t sizeWritten = 0;
    QAL_DBG(LOG_TAG,"%s: enter buf:%p tag:%d flag:%d", __func__, buf, tag, flag);

    void *data = nullptr;
    struct gsl_buff gslBuff;
    gslBuff.timestamp = (uint64_t) buf->ts;

    bytesRemaining = buf->size;

    while ((bytesRemaining / out_buf_size) > 1) {
        offset = bytesWritten + buf->offset;
        data = buf->buffer;
        data = static_cast<char *>(data) + offset;
        sizeWritten = out_buf_size;  //initialize 0
        if(pcm && !isActive()) {
            status = pcm_start(pcm);
            if (status) {
                QAL_ERR(LOG_TAG, "pcm_start failed %d", status);
                return -EINVAL; 
            }
            mState = SESSION_STARTED;
        }
        status = pcm_write(pcm, data, sizeWritten);
        if (0 != status) {
            QAL_ERR(LOG_TAG,"%s: Failed to write the data to gsl", __func__);
            return status;
        }
        bytesWritten += sizeWritten;
        bytesRemaining -= sizeWritten;
    }
    offset = bytesWritten + buf->offset;
    sizeWritten = bytesRemaining;
    data = buf->buffer;
    if(pcm && !isActive()) {
        status = pcm_start(pcm);
        if (status) {
            QAL_ERR(LOG_TAG, "pcm_start failed %d", status);
            return -EINVAL; 
        }
        mState = SESSION_STARTED;
    }
    data = static_cast<char *>(data) + offset;
    status = pcm_write(pcm, data, sizeWritten);
    if (status != 0) {
        QAL_ERR(LOG_TAG,"Error! pcm_write failed");
        return status;
    }
    bytesWritten += sizeWritten;
    *size = bytesWritten;
    return status;
}

int SessionAlsaPcm::readBufferInit(Stream * /*streamHandle*/, size_t /*noOfBuf*/, size_t /*bufSize*/,
                                   int /*flag*/)
{
    return 0;
}
int SessionAlsaPcm::writeBufferInit(Stream * /*streamHandle*/, size_t /*noOfBuf*/, size_t /*bufSize*/,
                                    int /*flag*/)
{
    return 0;
}

int SessionAlsaPcm::setParameters(Stream *streamHandle, int /*tagId*/, uint32_t param_id, void *payload)
{
    int status = 0;
    int device = pcmDevIds.at(0);
    uint8_t* paramData = NULL;
    size_t paramSize = 0;
    uint32_t miid = 0;

    QAL_DBG(LOG_TAG, "Enter.");
    switch (param_id) {
        case PARAM_ID_DETECTION_ENGINE_SOUND_MODEL:
        {
            struct qal_st_sound_model *pSoundModel = NULL;
            pSoundModel = (struct qal_st_sound_model *)payload;
            status = SessionAlsaUtils::getModuleInstanceId(mixer, device,
                    txAifBackEnds[0].second.data(), false, DEVICE_SVA, &miid);
            if (status) {
                QAL_ERR(LOG_TAG, "Failed to get tage info %x, status = %d", DEVICE_SVA, status);
                goto exit;
            }
            builder->payloadSVASoundModel(&paramData, &paramSize, miid, pSoundModel);
            break;
        }
        case PARAM_ID_DETECTION_ENGINE_CONFIG_VOICE_WAKEUP:
        {
            struct detection_engine_config_voice_wakeup *pWakeUpConfig = NULL;
            pWakeUpConfig = (struct detection_engine_config_voice_wakeup *)payload;
            status = SessionAlsaUtils::getModuleInstanceId(mixer, device,
                    txAifBackEnds[0].second.data(), false, DEVICE_SVA, &miid);
            if (status) {
                QAL_ERR(LOG_TAG, "Failed to get tage info %x, status = %d", DEVICE_SVA, status);
                goto exit;
            }
            builder->payloadSVAWakeUpConfig(&paramData, &paramSize, miid, pWakeUpConfig);
            break;
        }
        case PARAM_ID_DETECTION_ENGINE_GENERIC_EVENT_CFG:
        {
            struct detection_engine_generic_event_cfg *pEventConfig = NULL;
            pEventConfig = (struct detection_engine_generic_event_cfg *)payload;
            // set custom config for detection event
            status = SessionAlsaUtils::getModuleInstanceId(mixer, device,
                    txAifBackEnds[0].second.data(), false, DEVICE_SVA, &miid);
            if (status) {
                QAL_ERR(LOG_TAG, "Failed to get tage info %x, status = %d", DEVICE_SVA, status);
                goto exit;
            }
            builder->payloadSVAEventConfig(&paramData, &paramSize, miid, pEventConfig);
            break;
        }
        case PARAM_ID_VOICE_WAKEUP_BUFFERING_CONFIG:
        {
            struct detection_engine_voice_wakeup_buffer_config *pWakeUpBufConfig = NULL;
            pWakeUpBufConfig = (struct detection_engine_voice_wakeup_buffer_config *)payload;
            status = SessionAlsaUtils::getModuleInstanceId(mixer, device,
                    txAifBackEnds[0].second.data(), false, DEVICE_SVA, &miid);
            if (status) {
                QAL_ERR(LOG_TAG, "Failed to get tage info %x, status = %d", DEVICE_SVA, status);
                goto exit;
            }
            builder->payloadSVAWakeUpBufferConfig(&paramData, &paramSize, miid, pWakeUpBufConfig);
            break;
        }
        case PARAM_ID_AUDIO_DAM_DOWNSTREAM_SETUP_DURATION:
        {
            struct audio_dam_downstream_setup_duration *pSetupDuration = NULL;
            pSetupDuration = (struct audio_dam_downstream_setup_duration *)payload;
            status = SessionAlsaUtils::getModuleInstanceId(mixer, device,
                    txAifBackEnds[0].second.data(), false, DEVICE_ADAM, &miid);
            if (status) {
                QAL_ERR(LOG_TAG, "Failed to get tage info %x, status = %d", DEVICE_ADAM, status);
                goto exit;
            }
            builder->payloadSVAStreamSetupDuration(&paramData, &paramSize, miid, pSetupDuration);
            break;
        }
        case PARAM_ID_DETECTION_ENGINE_RESET:
        {
            status = SessionAlsaUtils::getModuleInstanceId(mixer, device,
                    txAifBackEnds[0].second.data(), false, DEVICE_SVA, &miid);
            if (status) {
                QAL_ERR(LOG_TAG, "Failed to get tage info %x, status = %d", DEVICE_SVA, status);
                goto exit;
            }
            builder->payloadSVAEngineReset(&paramData, &paramSize, miid);
            status = SessionAlsaUtils::setMixerParameter(mixer, pcmDevIds.at(0), false, paramData, paramSize);
            if (status) {
                QAL_ERR(LOG_TAG, "Failed to set mixer param, status = %d", status);
                goto exit;
            }
            break;
        }
        default:
            status = -EINVAL;
            QAL_ERR(LOG_TAG, "Unsupported param id %u status %d", param_id, status);
            goto exit;
    }

    if (!paramData) {
        status = -ENOMEM;
        QAL_ERR(LOG_TAG, "failed to get payload status %d", status);
        goto exit;
    }

    QAL_VERBOSE(LOG_TAG, "%x - payload and %d size", paramData , paramSize);

    if (param_id != PARAM_ID_DETECTION_ENGINE_RESET) {
        if (!customPayloadSize) {
            customPayload = (uint8_t *)calloc(1, paramSize);
        } else {
            customPayload = (uint8_t *)realloc(customPayload, customPayloadSize + paramSize);
        }

        if (!customPayload) {
            status = -ENOMEM;
            QAL_ERR(LOG_TAG, "failed to allocate memory for custom payload");
            goto free_payload;
        }

        memcpy((uint8_t *)customPayload + customPayloadSize, paramData, paramSize);
        customPayloadSize += paramSize;
        QAL_INFO(LOG_TAG, "customPayloadSize = %d", customPayloadSize);
    }

    QAL_DBG(LOG_TAG, "Exit. status %d", status);
free_payload :
    free(paramData);
exit:
    return status;
}

void SessionAlsaPcm::eventWaitThreadLoop(void *context, SessionAlsaPcm *session)
{
    struct mixer *mixer = (struct mixer *)context;
    int ret = 0;
    struct snd_ctl_event mixer_event = {0};

    QAL_VERBOSE(LOG_TAG, "subscribing for event");
    mixer_subscribe_events(mixer, 1);

    while (1) {
        QAL_VERBOSE(LOG_TAG, "going to wait for event");
        // TODO: set timeout here to avoid stuck during stop
        // Better if AGM side can provide one event indicating stop
        ret = mixer_wait_event(mixer, 1000);
        QAL_VERBOSE(LOG_TAG, "mixer_wait_event returns %d", ret);
        if (ret <= 0) {
            QAL_DBG(LOG_TAG, "mixer_wait_event err! ret = %d", ret);
        } else if (ret > 0) {
            ret = mixer_read_event(mixer, &mixer_event);
            if (ret >= 0) {
                QAL_INFO(LOG_TAG, "Event Received %s", mixer_event.data.elem.id.name);
                ret = session->handleMixerEvent(mixer, (char *)mixer_event.data.elem.id.name);
            } else {
                QAL_DBG(LOG_TAG, "mixer_read failed, ret = %d", ret);
            }
        }
        if (!session->isActive()) {
            QAL_VERBOSE(LOG_TAG, "Exit thread, isActive = %d", session->isActive());
            break;
        }
    }
    QAL_VERBOSE(LOG_TAG, "unsubscribing for event");
    mixer_subscribe_events(mixer, 0);
}

int SessionAlsaPcm::handleMixerEvent(struct mixer *mixer, char *mixer_str)
{
    struct mixer_ctl *ctl = nullptr;
    char *buf = nullptr;
    unsigned int num_values;
    int status = 0;
    struct agm_event_cb_params *params;
    Stream *s = nullptr;
    uint32_t event_id = 0;
    uint32_t *event_data = nullptr;

    QAL_DBG(LOG_TAG, "Enter");
    ctl = mixer_get_ctl_by_name(mixer, mixer_str);
    if (!ctl) {
        QAL_ERR(LOG_TAG, "Invalid mixer control: %s", mixer_str);
        status = -EINVAL;
        goto exit;
    }

    num_values = mixer_ctl_get_num_values(ctl);
    QAL_VERBOSE(LOG_TAG, "num_values: %d", num_values);
    buf = (char *)calloc(1, num_values);
    if (!buf) {
        QAL_ERR(LOG_TAG, "Failed to allocate buf");
        status = -ENOMEM;
        goto exit;
    }

    status = mixer_ctl_get_array(ctl, buf, num_values);
    if (status < 0) {
        QAL_ERR(LOG_TAG, "Failed to mixer_ctl_get_array");
        goto exit;
    }

    params = (struct agm_event_cb_params *)buf;
    QAL_DBG(LOG_TAG, "source module id %x, event id %d, payload size %d",
            params->source_module_id, params->event_id,
            params->event_payload_size);

    if (!params->source_module_id || !params->event_payload_size) {
        QAL_ERR(LOG_TAG, "Invalid source module id or payload size");
        goto exit;
    }

    event_id = params->event_id;
    event_data = (uint32_t *)(params->event_payload);

    if (!sessionCb) {
        status = -EINVAL;
        QAL_ERR(LOG_TAG, "Invalid session callback");
        goto exit;
    }
    sessionCb(cbCookie, event_id, (void *)event_data);

exit:
    if (buf)
        free(buf);
    QAL_DBG(LOG_TAG, "Exit, status %d", status);

    return status;
}

void SessionAlsaPcm::checkAndConfigConcurrency(Stream *s)
{
    int32_t status = 0;
    std::shared_ptr<Device> rxDevice = nullptr;
    std::shared_ptr<Device> txDevice = nullptr;
    struct qal_stream_attributes sAttr;
    std::vector <Stream *> activeStreams;
    qal_stream_type_t txStreamType;
    std::vector <std::shared_ptr<Device>> activeDevices;
    std::vector <std::shared_ptr<Device>> deviceList;
    std::vector <std::shared_ptr<Device>> rxDeviceList;
    std::vector <std::string> backendNames;

    // get stream attributes
    status = s->getStreamAttributes(&sAttr);
    if (status != 0) {
        QAL_ERR(LOG_TAG,"stream get attributes failed");
        return;
    }

    // get associated device list
    status = s->getAssociatedDevices(deviceList);
    if (0 != status) {
        QAL_ERR(LOG_TAG, "Failed to get associated device, status %d", status);
        return;
    }

    // get all active devices from rm and
    // determine Rx and Tx for concurrency usecase
    rm->getActiveDevices(activeDevices);
    for (int i = 0; i < activeDevices.size(); i++) {
        int deviceId = activeDevices[i]->getSndDeviceId();
        if ((deviceId >= QAL_DEVICE_OUT_HANDSET && deviceId <= QAL_DEVICE_OUT_PROXY) &&
            sAttr.direction == QAL_AUDIO_INPUT) {
            rxDevice = activeDevices[i];
            for (int j = 0; j < deviceList.size(); j++) {
                std::shared_ptr<Device> dev = deviceList[j];
                if (dev->getSndDeviceId() >= QAL_DEVICE_IN_HANDSET_MIC &&
                    dev->getSndDeviceId() <= QAL_DEVICE_IN_TRI_MIC)
                    txDevice = dev;
            }
        }

        if (deviceId >= QAL_DEVICE_IN_HANDSET_MIC &&
            deviceId <= QAL_DEVICE_IN_TRI_MIC &&
            sAttr.direction == QAL_AUDIO_OUTPUT) {
            txDevice = activeDevices[i];
            for (int j = 0; j < deviceList.size(); j++) {
                std::shared_ptr<Device> dev = deviceList[j];
                //if ((deviceId == QAL_DEVICE_OUT_SPEAKER) || (deviceId == QAL_DEVICE_OUT_WIRED_HEADPHONE)) {
                if (deviceId >= QAL_DEVICE_OUT_HANDSET && deviceId <= QAL_DEVICE_OUT_PROXY) {
                    rxDevice = dev;
                    break;
                }
            }
        }
    }

    if (!rxDevice || !txDevice) {
        if (isECRefSet && sAttr.type == QAL_STREAM_VOICE_UI) {
            status = SessionAlsaUtils::setECRefPath(mixer, pcmDevIds.at(0), false, "ZERO");
            if (status)
                QAL_ERR(LOG_TAG, "Failed to disable EC ref path, status %d", status);
        }
        QAL_ERR(LOG_TAG, "No need to handle for concurrency");
        return;
    }

    QAL_DBG(LOG_TAG, "rx device %d, tx device %d", rxDevice->getSndDeviceId(), txDevice->getSndDeviceId());
    // determine concurrency usecase
    for (int i = 0; i < deviceList.size(); i++) {
        std::shared_ptr<Device> dev = deviceList[i];
        if (dev == rxDevice) {
            rm->getActiveStream(txDevice, activeStreams);
            for (int j = 0; j < activeStreams.size(); j++) {
                activeStreams[j]->getStreamType(&txStreamType);
            }
        }
        else if (dev == txDevice)
            s->getStreamType(&txStreamType);
        else {
            QAL_ERR(LOG_TAG, "Concurrency usecase exists, not related to current stream");
            return;
        }
    }
    QAL_DBG(LOG_TAG, "tx stream type = %d", txStreamType);
    // TODO: use table to map types/devices to key values
    if (txStreamType == QAL_STREAM_VOICE_UI) {
        rxDeviceList.push_back(rxDevice);
        backendNames = rm->getBackEndNames(rxDeviceList);
        status = SessionAlsaUtils::setECRefPath(mixer, pcmDevIds.at(0), false, backendNames[0].c_str());
        if (status) {
            QAL_ERR(LOG_TAG, "Failed to set EC ref path, status %d", status);
        } else {
            isECRefSet = true;
        }
    }
}

int SessionAlsaPcm::getParameters(Stream *s, int tagId, uint32_t param_id, void **payload)
{
    int status = 0;
    uint8_t *ptr = NULL;
    uint8_t *config = NULL;
    uint8_t *payloadData = NULL;
    size_t payloadSize = 0;
    size_t configSize = 0;
    int device = pcmDevIds.at(0);
    uint32_t miid = 0;
    const char *control = "getParam";
    const char *stream = "PCM";
    struct mixer_ctl *ctl;
    std::ostringstream CntrlName;
    QAL_DBG(LOG_TAG, "Enter.");

    CntrlName << stream << pcmDevIds.at(0) << " " << control;
    ctl = mixer_get_ctl_by_name(mixer, CntrlName.str().data());
    if (!ctl) {
        QAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", CntrlName.str().data());
        status = -ENOENT;
        goto exit;
    }

    if (!rxAifBackEnds.empty()) { /** search in RX GKV */
        status = SessionAlsaUtils::getModuleInstanceId(mixer, device, rxAifBackEnds[0].second.data(),
                false, tagId, &miid);
        if (status) /** if not found, reset miid to 0 again */
            miid = 0;
    }
    
    if (!txAifBackEnds.empty()) { /** search in TX GKV */
        status = SessionAlsaUtils::getModuleInstanceId(mixer, device, txAifBackEnds[0].second.data(),
                false, tagId, &miid);
        if (status)
            miid = 0;
    }

    if (miid == 0) {
        QAL_ERR(LOG_TAG, "failed to look for module with tagID 0x%x", tagId);
        status = -EINVAL;
        goto exit;
    }


    switch (param_id) {
        case QAL_PARAM_ID_DIRECTION_OF_ARRIVAL:
        {
            configSize = sizeof(struct ffv_doa_tracking_monitor_t);
            builder->payloadDOAInfo(&payloadData, &payloadSize, miid);
            break;
        }
        default:
            status = EINVAL;
            QAL_ERR(LOG_TAG, "Unsupported param id %u status %d", param_id, status);
            goto exit;
    }

    status = mixer_ctl_set_array(ctl, payloadData, payloadSize);
    if (0 != status) {
        QAL_ERR(LOG_TAG, "Set custom config failed, status = %d", status);
        goto exit;
    }

    status = mixer_ctl_get_array(ctl, payloadData, payloadSize);
    if (0 != status) {
        QAL_ERR(LOG_TAG, "Get custom config failed, status = %d", status);
        goto exit;
    }

    ptr = (uint8_t *)payloadData + sizeof(struct apm_module_param_data_t);
    config = (uint8_t *)calloc(1, configSize);
    if (!config) {
        QAL_ERR(LOG_TAG, "Failed to allocate memory for config");
        status = -ENOMEM;
        goto exit;
    }

    casa_osal_memcpy(config, configSize, ptr, configSize);
    *payload = (void *)config;


exit:
    if(payloadData)
        free(payloadData);
    QAL_DBG(LOG_TAG, "Exit. status %d", status);
    return status;
}

int SessionAlsaPcm::registerCallBack(session_callback cb, void *cookie)
{
    sessionCb = cb;
    cbCookie = cookie;
    return 0;
}

int SessionAlsaPcm::getTimestamp(struct qal_session_time *stime)
{
    int status = 0;
    status = SessionAlsaUtils::getTimestamp(mixer, false, pcmDevIds, spr_miid, stime);
    if (0 != status) {
       QAL_ERR(LOG_TAG, "getTimestamp failed status = %d", status);
       return status;
    }
    return status;
}
int SessionAlsaPcm::drain(qal_drain_type_t type)
{
    return 0;
}

int SessionAlsaPcm::flush()
{
    int status = 0;

    if (!pcm) {
        QAL_ERR(LOG_TAG, "Pcm is invalid");
        return -EINVAL;
    }
    QAL_VERBOSE(LOG_TAG,"Enter flush\n");
    if (pcm && isActive()) {
        status = pcm_stop(pcm);

        if (!status)
            mState = SESSION_STOPPED;
    }

    QAL_VERBOSE(LOG_TAG,"status %d\n", status);

    return status;
}

bool SessionAlsaPcm::isActive()
{
    QAL_VERBOSE(LOG_TAG, "state = %d", mState);
    return mState == SESSION_STARTED;
}
