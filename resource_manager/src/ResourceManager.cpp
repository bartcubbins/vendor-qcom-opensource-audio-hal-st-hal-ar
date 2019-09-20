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

#define LOG_TAG "ResourceManager"
#include "ResourceManager.h"
#include "Session.h"
#include "Device.h"
#include "Stream.h"
#include "StreamPCM.h"
#include "StreamCompress.h"
#include "StreamSoundTrigger.h"
#include "gsl_intf.h"
#include "SessionGsl.h"
#include "SessionQts.h"
#include "PayloadBuilder.h"
#include "SpeakerMic.h"
#include "Speaker.h"


#define MIXER_FILE_DELIMITER "_"
#define MIXER_FILE_EXT ".xml"
#define MIXER_XML_BASE_STRING "/etc/mixer_paths"
#define MIXER_XML_DEFAULT_PATH "/etc/mixer_paths_wsa.xml"
#define MIXER_PATH_MAX_LENGTH 100

#define MAX_SND_CARD 8
#define LOWLATENCY_PCM_DEVICE 15
#define DEEP_BUFFER_PCM_DEVICE 0
#define DEFAULT_ACDB_FILES "/etc/acdbdata/MTP/acdb_cal.acdb"
#define DEVICE_NAME_MAX_SIZE 128
// should be defined in qal_defs.h
#define QAL_DEVICE_MAX QAL_DEVICE_IN_PROXY+1

#define DEFAULT_BIT_WIDTH 16
#define DEFAULT_SAMPLE_RATE 48000
#define DEFAULT_CHANNELS 2
#define DEFAULT_FORMAT 0x00000000u
// TODO: double check and confirm actual
// values for max sessions number
#define MAX_SESSIONS_LOW_LATENCY 8
#define MAX_SESSIONS_DEEP_BUFFER 1
#define MAX_SESSIONS_COMPRESSED 10
#define MAX_SESSIONS_GENERIC 1
#define MAX_SESSIONS_VOICE_UI 2
#define XMLFILE "/etc/resourcemanager.xml"
#define GECKOXMLFILE "/etc/kvh2xml.xml"


/*
To be defined in detail, if GSL is defined,
pcm device id is directly related to device,
else using legacy design for alsa
*/
// Will update actual value when numbers got for VT

std::vector<std::pair<int32_t, std::string>> ResourceManager::deviceLinkName {
    {QAL_DEVICE_NONE,                     {std::string{ "none" }}},
    {QAL_DEVICE_OUT_EARPIECE,             {std::string{ "" }}},
    {QAL_DEVICE_OUT_SPEAKER,              {std::string{ "" }}},
    {QAL_DEVICE_OUT_WIRED_HEADSET,        {std::string{ "" }}},
    {QAL_DEVICE_OUT_WIRED_HEADPHONE,      {std::string{ "" }}},
    {QAL_DEVICE_OUT_LINE,                 {std::string{ "" }}},
    {QAL_DEVICE_OUT_BLUETOOTH_SCO,        {std::string{ "" }}},
    {QAL_DEVICE_OUT_BLUETOOTH_A2DP,       {std::string{ "" }}},
    {QAL_DEVICE_OUT_AUX_DIGITAL,          {std::string{ "" }}},
    {QAL_DEVICE_OUT_HDMI,                 {std::string{ "" }}},
    {QAL_DEVICE_OUT_USB_DEVICE,           {std::string{ "" }}},
    {QAL_DEVICE_OUT_USB_HEADSET,          {std::string{ "" }}},
    {QAL_DEVICE_OUT_SPDIF,                {std::string{ "" }}},
    {QAL_DEVICE_OUT_FM,                   {std::string{ "" }}},
    {QAL_DEVICE_OUT_AUX_LINE,             {std::string{ "" }}},
    {QAL_DEVICE_OUT_PROXY,                {std::string{ "" }}},

    {QAL_DEVICE_IN_HANDSET_MIC,           {std::string{ "tdm-pri" }}},
    {QAL_DEVICE_IN_SPEAKER_MIC,           {std::string{ "tdm-pri" }}},
    {QAL_DEVICE_IN_TRI_MIC,               {std::string{ "tdm-pri" }}},
    {QAL_DEVICE_IN_QUAD_MIC,              {std::string{ "" }}},
    {QAL_DEVICE_IN_EIGHT_MIC,             {std::string{ "" }}},
    {QAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET, {std::string{ "" }}},
    {QAL_DEVICE_IN_WIRED_HEADSET,         {std::string{ "" }}},
    {QAL_DEVICE_IN_AUX_DIGITAL,           {std::string{ "" }}},
    {QAL_DEVICE_IN_HDMI,                  {std::string{ "" }}},
    {QAL_DEVICE_IN_USB_ACCESSORY,         {std::string{ "" }}},
    {QAL_DEVICE_IN_USB_DEVICE,            {std::string{ "" }}},
    {QAL_DEVICE_IN_USB_HEADSET,           {std::string{ "" }}},
    {QAL_DEVICE_IN_FM_TUNER,              {std::string{ "" }}},
    {QAL_DEVICE_IN_LINE,                  {std::string{ "" }}},
    {QAL_DEVICE_IN_SPDIF,                 {std::string{ "" }}},
    {QAL_DEVICE_IN_PROXY,                 {std::string{ "" }}}
};

std::vector<std::pair<int32_t, int32_t>> ResourceManager::devicePcmId {
    {QAL_DEVICE_NONE,                     0},
    {QAL_DEVICE_OUT_EARPIECE,             0},
    {QAL_DEVICE_OUT_SPEAKER,              1},
    {QAL_DEVICE_OUT_WIRED_HEADSET,        0},
    {QAL_DEVICE_OUT_WIRED_HEADPHONE,      0},
    {QAL_DEVICE_OUT_LINE,                 0},
    {QAL_DEVICE_OUT_BLUETOOTH_SCO,        0},
    {QAL_DEVICE_OUT_BLUETOOTH_A2DP,       0},
    {QAL_DEVICE_OUT_AUX_DIGITAL,          0},
    {QAL_DEVICE_OUT_HDMI,                 0},
    {QAL_DEVICE_OUT_USB_DEVICE,           0},
    {QAL_DEVICE_OUT_USB_HEADSET,          0},
    {QAL_DEVICE_OUT_SPDIF,                0},
    {QAL_DEVICE_OUT_FM,                   0},
    {QAL_DEVICE_OUT_AUX_LINE,             0},
    {QAL_DEVICE_OUT_PROXY,                0},
    {QAL_DEVICE_IN_HANDSET_MIC,           0},
    {QAL_DEVICE_IN_SPEAKER_MIC,           0},
    {QAL_DEVICE_IN_TRI_MIC,               0},
    {QAL_DEVICE_IN_QUAD_MIC,              0},
    {QAL_DEVICE_IN_EIGHT_MIC,             0},
    {QAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET, 0},
    {QAL_DEVICE_IN_WIRED_HEADSET,         0},
    {QAL_DEVICE_IN_AUX_DIGITAL,           0},
    {QAL_DEVICE_IN_HDMI,                  0},
    {QAL_DEVICE_IN_USB_ACCESSORY,         0},
    {QAL_DEVICE_IN_USB_DEVICE,            0},
    {QAL_DEVICE_IN_USB_HEADSET,           0},
    {QAL_DEVICE_IN_FM_TUNER,              0},
    {QAL_DEVICE_IN_LINE,                  0},
    {QAL_DEVICE_IN_SPDIF,                 0},
    {QAL_DEVICE_IN_PROXY,                 0}
};

// To be defined in detail

std::vector<std::pair<int32_t, std::string>> ResourceManager::sndDeviceNameLUT {
    {QAL_DEVICE_NONE,                     {std::string{ "none" }}},
    {QAL_DEVICE_OUT_EARPIECE,             {std::string{ "" }}},
    {QAL_DEVICE_OUT_SPEAKER,              {std::string{ "" }}},
    {QAL_DEVICE_OUT_WIRED_HEADSET,        {std::string{ "" }}},
    {QAL_DEVICE_OUT_WIRED_HEADPHONE,      {std::string{ "" }}},
    {QAL_DEVICE_OUT_LINE,                 {std::string{ "" }}},
    {QAL_DEVICE_OUT_BLUETOOTH_SCO,        {std::string{ "" }}},
    {QAL_DEVICE_OUT_BLUETOOTH_A2DP,       {std::string{ "" }}},
    {QAL_DEVICE_OUT_AUX_DIGITAL,          {std::string{ "" }}},
    {QAL_DEVICE_OUT_HDMI,                 {std::string{ "" }}},
    {QAL_DEVICE_OUT_USB_DEVICE,           {std::string{ "" }}},
    {QAL_DEVICE_OUT_USB_HEADSET,          {std::string{ "" }}},
    {QAL_DEVICE_OUT_SPDIF,                {std::string{ "" }}},
    {QAL_DEVICE_OUT_FM,                   {std::string{ "" }}},
    {QAL_DEVICE_OUT_AUX_LINE,             {std::string{ "" }}},
    {QAL_DEVICE_OUT_PROXY,                {std::string{ "" }}},

    {QAL_DEVICE_IN_HANDSET_MIC,           {std::string{ "" }}},
    {QAL_DEVICE_IN_SPEAKER_MIC,           {std::string{ "" }}},
    {QAL_DEVICE_IN_TRI_MIC,               {std::string{ "" }}},
    {QAL_DEVICE_IN_QUAD_MIC,              {std::string{ "" }}},
    {QAL_DEVICE_IN_EIGHT_MIC,             {std::string{ "" }}},
    {QAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET, {std::string{ "" }}},
    {QAL_DEVICE_IN_WIRED_HEADSET,         {std::string{ "" }}},
    {QAL_DEVICE_IN_AUX_DIGITAL,           {std::string{ "" }}},
    {QAL_DEVICE_IN_HDMI,                  {std::string{ "" }}},
    {QAL_DEVICE_IN_USB_ACCESSORY,         {std::string{ "" }}},
    {QAL_DEVICE_IN_USB_DEVICE,            {std::string{ "" }}},
    {QAL_DEVICE_IN_USB_HEADSET,           {std::string{ "" }}},
    {QAL_DEVICE_IN_FM_TUNER,              {std::string{ "" }}},
    {QAL_DEVICE_IN_LINE,                  {std::string{ "" }}},
    {QAL_DEVICE_IN_SPDIF,                 {std::string{ "" }}},
    {QAL_DEVICE_IN_PROXY,                 {std::string{ "" }}}
};

const std::map<std::string, uint32_t> deviceIdLUT {
    {std::string{ "QAL_DEVICE_NONE" },                     QAL_DEVICE_NONE},
    {std::string{ "QAL_DEVICE_OUT_EARPIECE" },             QAL_DEVICE_OUT_EARPIECE},
    {std::string{ "QAL_DEVICE_OUT_SPEAKER" },              QAL_DEVICE_OUT_SPEAKER},
    {std::string{ "QAL_DEVICE_OUT_WIRED_HEADSET" },        QAL_DEVICE_OUT_WIRED_HEADSET},
    {std::string{ "QAL_DEVICE_OUT_WIRED_HEADPHONE" },      QAL_DEVICE_OUT_WIRED_HEADPHONE},
    {std::string{ "QAL_DEVICE_OUT_LINE" },                 QAL_DEVICE_OUT_LINE},
    {std::string{ "QAL_DEVICE_OUT_BLUETOOTH_SCO" },        QAL_DEVICE_OUT_BLUETOOTH_SCO},
    {std::string{ "QAL_DEVICE_OUT_BLUETOOTH_A2DP" },       QAL_DEVICE_OUT_BLUETOOTH_A2DP},
    {std::string{ "QAL_DEVICE_OUT_AUX_DIGITAL" },          QAL_DEVICE_OUT_AUX_DIGITAL},
    {std::string{ "QAL_DEVICE_OUT_HDMI" },                 QAL_DEVICE_OUT_HDMI},
    {std::string{ "QAL_DEVICE_OUT_USB_DEVICE" },           QAL_DEVICE_OUT_USB_DEVICE},
    {std::string{ "QAL_DEVICE_OUT_USB_HEADSET" },          QAL_DEVICE_OUT_USB_HEADSET},
    {std::string{ "QAL_DEVICE_OUT_SPDIF" },                QAL_DEVICE_OUT_SPDIF},
    {std::string{ "QAL_DEVICE_OUT_FM" },                   QAL_DEVICE_OUT_FM},
    {std::string{ "QAL_DEVICE_OUT_AUX_LINE" },             QAL_DEVICE_OUT_AUX_LINE},
    {std::string{ "QAL_DEVICE_OUT_PROXY" },                QAL_DEVICE_OUT_PROXY},
    {std::string{ "QAL_DEVICE_IN_HANDSET_MIC" },           QAL_DEVICE_IN_HANDSET_MIC},
    {std::string{ "QAL_DEVICE_IN_SPEAKER_MIC" },           QAL_DEVICE_IN_SPEAKER_MIC},
    {std::string{ "QAL_DEVICE_IN_TRI_MIC" },               QAL_DEVICE_IN_TRI_MIC},
    {std::string{ "QAL_DEVICE_IN_QUAD_MIC" },              QAL_DEVICE_IN_QUAD_MIC},
    {std::string{ "QAL_DEVICE_IN_EIGHT_MIC" },             QAL_DEVICE_IN_EIGHT_MIC},
    {std::string{ "QAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET" }, QAL_DEVICE_IN_BLUETOOTH_SCO_HEADSET},
    {std::string{ "QAL_DEVICE_IN_WIRED_HEADSET" },         QAL_DEVICE_IN_WIRED_HEADSET},
    {std::string{ "QAL_DEVICE_IN_AUX_DIGITAL" },           QAL_DEVICE_IN_AUX_DIGITAL},
    {std::string{ "QAL_DEVICE_IN_HDMI" },                  QAL_DEVICE_IN_HDMI},
    {std::string{ "QAL_DEVICE_IN_USB_ACCESSORY" },         QAL_DEVICE_IN_USB_ACCESSORY},
    {std::string{ "QAL_DEVICE_IN_USB_DEVICE" },            QAL_DEVICE_IN_USB_DEVICE},
    {std::string{ "QAL_DEVICE_IN_USB_HEADSET" },           QAL_DEVICE_IN_USB_HEADSET},
    {std::string{ "QAL_DEVICE_IN_FM_TUNER" },              QAL_DEVICE_IN_FM_TUNER},
    {std::string{ "QAL_DEVICE_IN_LINE" },                  QAL_DEVICE_IN_LINE},
    {std::string{ "QAL_DEVICE_IN_SPDIF" },                 QAL_DEVICE_IN_SPDIF},
    {std::string{ "QAL_DEVICE_IN_PROXY" },                 QAL_DEVICE_IN_PROXY}
};

std::shared_ptr<ResourceManager> ResourceManager::rm = nullptr;
std::vector <int> ResourceManager::streamTag = {0};
std::vector <int> ResourceManager::streamPpTag = {0};
std::vector <int> ResourceManager::mixerTag = {0};
std::vector <int> ResourceManager::devicePpTag = {0};
std::vector <int> ResourceManager::deviceTag = {0};
std::mutex ResourceManager::mutex;

ResourceManager::ResourceManager()
{
    QAL_INFO(LOG_TAG, "Enter.");
    int ret = 0;
    // TODO: set bOverwriteFlag to true by default
    // should we add api for client to set this value?
    bool bOverwriteFlag = true;
    int snd_card = -1;
    // Init audio_route and audio_mixer
    ret = ResourceManager::init_audio();
    if (ret) {
        QAL_ERR(LOG_TAG, "error in init audio route and audio mixer ret %d", ret);
    }
    //#ifdef CONFIG_GSL
    ret = SessionGsl::init(DEFAULT_ACDB_FILES);
    if (ret) {
        QAL_ERR(LOG_TAG, "session gsl init failed ret %d", ret);
    }


     //Initialize QTS
    SessionQts::init();

    //TODO: parse the tag and populate in the tags
    streamTag.clear();
    deviceTag.clear();
    ret = ResourceManager::XmlParser(GECKOXMLFILE);
    if (ret) {
        QAL_ERR(LOG_TAG, "error in gecko xml parsing ret %d", ret);
    }
    ret = ResourceManager::XmlParser(XMLFILE);
    if (ret) {
        QAL_ERR(LOG_TAG, "error in resource xml parsing ret %d", ret);
    }
    QAL_INFO(LOG_TAG, "Exit. ret %d", ret);
}

ResourceManager::~ResourceManager()
{

}

int ResourceManager::init_audio()
{
    int snd_card_num = 0;
    int ret = 0;
    char snd_macro[] = "snd";
    char *snd_card_name = NULL, *snd_card_name_t = NULL;
    char *snd_internal_name = NULL;
    char *tmp = NULL;
    char mixer_xml_file[MIXER_PATH_MAX_LENGTH] = {0};
    QAL_DBG(LOG_TAG, "Enter.");
    while (snd_card_num < MAX_SND_CARD) {
        audio_mixer = mixer_open(snd_card_num);
        if (!audio_mixer) {
            snd_card_num++;
            continue;
        } else {
            QAL_DBG(LOG_TAG, "mixer open success. snd_card_num = %d", snd_card_num);
            break;
        }
    }

    if (snd_card_num >= MAX_SND_CARD) {
        QAL_ERR(LOG_TAG, "audio mixer open failure");
        return -EINVAL;
    }

    snd_card_name = strdup(mixer_get_name(audio_mixer));
    if (!snd_card_name) {
        QAL_ERR(LOG_TAG, "failed to allocate memory for snd_card_name");
        mixer_close(audio_mixer);
        return -EINVAL;
    }

    snd_card_name_t = strdup(snd_card_name);
    snd_internal_name = strtok_r(snd_card_name_t, "-", &tmp);

    if (snd_internal_name != NULL)
        snd_internal_name = strtok_r(NULL, "-", &tmp);

    if (snd_internal_name != NULL) {
        strlcpy(mixer_xml_file, MIXER_XML_BASE_STRING, MIXER_PATH_MAX_LENGTH);
        ret = strcmp(snd_internal_name, snd_macro);
        if(ret == 0) {
            strlcat(mixer_xml_file, MIXER_FILE_EXT, MIXER_PATH_MAX_LENGTH);
        } else {
            strlcat(mixer_xml_file, MIXER_FILE_DELIMITER, MIXER_PATH_MAX_LENGTH);
            strlcat(mixer_xml_file, snd_internal_name, MIXER_PATH_MAX_LENGTH);
            strlcat(mixer_xml_file, MIXER_FILE_EXT, MIXER_PATH_MAX_LENGTH);
        }
    } else
        strlcpy(mixer_xml_file, MIXER_XML_DEFAULT_PATH, MIXER_PATH_MAX_LENGTH);

    audio_route = audio_route_init(snd_card_num, mixer_xml_file);
    QAL_INFO(LOG_TAG, "audio route %pK, mixer path %s", audio_route, mixer_xml_file);
    if (!audio_route) {
        QAL_ERR(LOG_TAG, "audio route init failed");
        mixer_close(audio_mixer);
        if (snd_card_name)
            free(snd_card_name);
        if (snd_card_name_t)
            free(snd_card_name_t);
        return -EINVAL;
    }
    // audio_route init success
    QAL_DBG(LOG_TAG, "Exit. audio route init success with card %d mixer path %s",
            snd_card_num, mixer_xml_file);
    snd_card = snd_card_num;
    return 0;
}

int ResourceManager::init()
{

}

bool ResourceManager::isStreamSupported(struct qal_stream_attributes *attributes,
                                        struct qal_device *devices, int no_of_devices)
{
    bool result = false;
    uint16_t channels, dev_channels;
    uint32_t samplerate, bitwidth;
    uint32_t dev_samplerate, dev_bitwidth, rc;
    size_t cur_sessions = 0;
    size_t max_sessions = 0;
    qal_audio_fmt_t format, dev_format;

    // check if stream type is supported
    // and new stream session is allowed
    qal_stream_type_t type = attributes->type;
    QAL_DBG(LOG_TAG, "Enter. type %d", type);
    switch (type) {
        case QAL_STREAM_VOICE_CALL_RX:
        case QAL_STREAM_VOICE_CALL_TX:
        case QAL_STREAM_VOICE_CALL_RX_TX:
        case QAL_STREAM_LOW_LATENCY:
        case QAL_STREAM_VOIP:
        case QAL_STREAM_VOIP_RX:
        case QAL_STREAM_VOIP_TX:
            cur_sessions = active_streams_ll.size();
            max_sessions = MAX_SESSIONS_LOW_LATENCY;
            break;
        case QAL_STREAM_DEEP_BUFFER:
            cur_sessions = active_streams_db.size();
            max_sessions = MAX_SESSIONS_DEEP_BUFFER;
            break;
        case QAL_STREAM_COMPRESSED:
            cur_sessions = active_streams_comp.size();
            max_sessions = MAX_SESSIONS_COMPRESSED;
            break;
        case QAL_STREAM_VOICE_CALL_MUSIC:
            break;
        case QAL_STREAM_GENERIC:
            cur_sessions = active_streams_ulla.size();
            max_sessions = MAX_SESSIONS_GENERIC;
            break;
        case QAL_STREAM_RAW:
        case QAL_STREAM_VOICE_ACTIVATION:
        case QAL_STREAM_VOICE_CALL:
        case QAL_STREAM_LOOPBACK:
        case QAL_STREAM_TRANSCODE:
        case QAL_STREAM_VOICE_UI:
            cur_sessions = active_streams_st.size();
            max_sessions = MAX_SESSIONS_VOICE_UI;
            break;
        default:
            QAL_ERR(LOG_TAG, "Invalid stream type = %d", type);
        return result;
    }
    if (cur_sessions == max_sessions) {
        QAL_ERR(LOG_TAG, "no new session allowed for stream %d", type);
        return result;
    }

    // check if param supported by audio configruation
    switch (type) {
        case QAL_STREAM_VOICE_CALL_RX:
        case QAL_STREAM_VOICE_CALL_TX:
        case QAL_STREAM_VOICE_CALL_RX_TX:
        case QAL_STREAM_LOW_LATENCY:
        case QAL_STREAM_VOIP:
        case QAL_STREAM_VOIP_RX:
        case QAL_STREAM_VOIP_TX:
            if (attributes->direction == QAL_AUDIO_INPUT) {
                channels = attributes->in_media_config.ch_info->channels;
                samplerate = attributes->in_media_config.sample_rate;
                bitwidth = attributes->in_media_config.bit_width;
            } else {
                channels = attributes->out_media_config.ch_info->channels;
                samplerate = attributes->out_media_config.sample_rate;
                bitwidth = attributes->out_media_config.bit_width;
        }
        rc = (StreamPCM::isBitWidthSupported(bitwidth) |
             StreamPCM::isSampleRateSupported(samplerate) |
             StreamPCM::isChannelSupported(channels));
        if (0 != rc) {
            QAL_ERR(LOG_TAG, "config not supported rc %d", rc);
            return result;
        }
        QAL_INFO(LOG_TAG, "config suppported");
        result = true;
        break;
    case QAL_STREAM_VOICE_UI:
        if (attributes->direction == QAL_AUDIO_INPUT) {
            channels = attributes->in_media_config.ch_info->channels;
            samplerate = attributes->in_media_config.sample_rate;
            bitwidth = attributes->in_media_config.bit_width;
        } else {
            channels = attributes->out_media_config.ch_info->channels;
            samplerate = attributes->out_media_config.sample_rate;
            bitwidth = attributes->out_media_config.bit_width;
        }
        rc = (StreamSoundTrigger::isBitWidthSupported(bitwidth) |
             StreamSoundTrigger::isSampleRateSupported(samplerate) |
             StreamSoundTrigger::isChannelSupported(channels));
        if (0 != rc) {
            QAL_ERR(LOG_TAG, "config not supported rc %d", rc);
            return result;
        }
        QAL_INFO(LOG_TAG, "config suppported");
        result = true;
        break;
    default:
        QAL_ERR(LOG_TAG, "unknown type");
        return false;
    }
    // check if param supported by any of the devices
    for (int i = 0; i < no_of_devices; i++) {
        rc = 0;
        dev_channels = devices[i].config.ch_info->channels;
        dev_samplerate = devices[i].config.sample_rate;
        dev_bitwidth = devices[i].config.bit_width;

        switch(devices[i].id) {
            case QAL_DEVICE_OUT_SPEAKER:
                rc = (Speaker::isBitWidthSupported(dev_bitwidth) |
                    Speaker::isSampleRateSupported(dev_samplerate) |
                    Speaker::isChannelSupported(dev_channels));
                break;
            case QAL_DEVICE_IN_HANDSET_MIC:
            case QAL_DEVICE_IN_SPEAKER_MIC:
            case QAL_DEVICE_IN_TRI_MIC:
            case QAL_DEVICE_IN_QUAD_MIC:
            case QAL_DEVICE_IN_EIGHT_MIC:
                rc = (SpeakerMic::isBitWidthSupported(dev_bitwidth) |
                      SpeakerMic::isSampleRateSupported(dev_samplerate) |
                      SpeakerMic::isChannelSupported(dev_channels));
                QAL_DBG(LOG_TAG, "device attributes rc = %d", rc);
                break;
            default:
                QAL_ERR(LOG_TAG, "unknown device id %d", devices[i].id);
            return false;
        }
        if (0 != rc) {
            QAL_ERR(LOG_TAG, "Attributes not supported by devices");
            result = false;
            return result;
        } else {
            result = true;
        }
    }
    QAL_DBG(LOG_TAG, "Exit. result %d", result);
    return result;
}

template <class T>
int registerstream(T s, std::vector<T> &streams)
{
    int ret = 0;
    streams.push_back(s);
    return ret;
}

int ResourceManager::registerStream(Stream *s)
{
    int ret = 0;
    qal_stream_type_t type;
    QAL_DBG(LOG_TAG, "Enter. stream %pK", s);
    ret = s->getStreamType(&type);
    if (0 != ret) {
        QAL_ERR(LOG_TAG, "getStreamType failed with status = %d", ret);
        return ret;
    }
    QAL_DBG(LOG_TAG, "stream type %d", type);
    mutex.lock();
    switch (type) {
        case QAL_STREAM_LOW_LATENCY:
        case QAL_STREAM_VOIP_RX:
        case QAL_STREAM_VOIP_TX:
        {
            StreamPCM* sPCM = dynamic_cast<StreamPCM*>(s);
            ret = registerstream(sPCM, active_streams_ll);
            break;
        }
        case QAL_STREAM_DEEP_BUFFER:
        {
            StreamPCM* sDB = dynamic_cast<StreamPCM*>(s);
            ret = registerstream(sDB, active_streams_db);
            break;
        }
        case QAL_STREAM_COMPRESSED:
        {
            StreamCompress* sComp = dynamic_cast<StreamCompress*>(s);
            ret = registerstream(sComp, active_streams_comp);
            break;
        }
        case QAL_STREAM_GENERIC:
        {
            StreamPCM* sULLA = dynamic_cast<StreamPCM*>(s);
            ret = registerstream(sULLA, active_streams_ulla);
            break;
        }
        case QAL_STREAM_VOICE_UI:
        {
            StreamSoundTrigger* sST = dynamic_cast<StreamSoundTrigger*>(s);
            ret = registerstream(sST, active_streams_st);
            break;
        }
        default:
            ret = -EINVAL;
            QAL_ERR(LOG_TAG, " Invalid stream type = %d ret %d", type, ret);
            break;
    }
    mutex.unlock();
    QAL_DBG(LOG_TAG, "Exit. ret %d", ret);
    return ret;
}

// template function to deregister stream
template <class T>
int deregisterstream(T s, std::vector<T> &streams)
{
    int ret = 0;
    typename std::vector<T>::iterator iter = std::find(streams.begin(), streams.end(), s);
    if (iter != streams.end())
        streams.erase(iter);
    else
        ret = -ENOENT;
    return ret;
}

int ResourceManager::deregisterStream(Stream *s)
{
    int ret = 0;
    qal_stream_type_t type;
    QAL_DBG(LOG_TAG, "Enter. stream %pK", s);
    ret = s->getStreamType(&type);
    if (0 != ret) {
        QAL_ERR(LOG_TAG, " getStreamType failed with status = %d", ret);
        return ret;
    }
    QAL_DBG(LOG_TAG, "stream type %d", type);
    mutex.lock();
    switch (type) {
        case QAL_STREAM_LOW_LATENCY:
        case QAL_STREAM_VOIP_RX:
        case QAL_STREAM_VOIP_TX:
        {
            StreamPCM* sPCM = dynamic_cast<StreamPCM*>(s);
            ret = deregisterstream(sPCM, active_streams_ll);
            break;
        }
        case QAL_STREAM_DEEP_BUFFER:
        {
            StreamPCM* sDB = dynamic_cast<StreamPCM*>(s);
            ret = deregisterstream(sDB, active_streams_db);
            break;
        }
        case QAL_STREAM_COMPRESSED:
        {
            StreamCompress* sComp = dynamic_cast<StreamCompress*>(s);
            ret = deregisterstream(sComp, active_streams_comp);
            break;
        }
        case QAL_STREAM_GENERIC:
        {
            StreamPCM* sULLA = dynamic_cast<StreamPCM*>(s);
            ret = deregisterstream(sULLA, active_streams_ulla);
            break;
        }
        case QAL_STREAM_VOICE_UI:
        {
            StreamSoundTrigger* sST = dynamic_cast<StreamSoundTrigger*>(s);
            ret = deregisterstream(sST, active_streams_st);
            break;
        }
        default:
            ret = -EINVAL;
            QAL_ERR(LOG_TAG, "Invalid stream type = %d ret %d", type, ret);
            break;
    }
    mutex.unlock();
    QAL_DBG(LOG_TAG, "Exit. ret %d", ret);
    return ret;
}

int ResourceManager::registerDevice(std::shared_ptr<Device> d)
{
    QAL_DBG(LOG_TAG, "Enter.");
    mutex.lock();
    active_devices.push_back(d);
    mutex.unlock();
    QAL_DBG(LOG_TAG, "Exit.");
    return 0;
}

int ResourceManager::deregisterDevice(std::shared_ptr<Device> d)
{
    int ret = 0;
    QAL_DBG(LOG_TAG, "Enter.");
    mutex.lock();
    typename std::vector<std::shared_ptr<Device>>::iterator iter =
        std::find(active_devices.begin(), active_devices.end(), d);
    if (iter != active_devices.end())
        active_devices.erase(iter);
    else {
        ret = -ENOENT;
        QAL_ERR(LOG_TAG, "no device %d found in active device list ret %d",
                d->getDeviceId(), ret);
    }
    mutex.unlock();
    QAL_DBG(LOG_TAG, "Exit. ret %d", ret);
    return ret;
}

int ResourceManager::getActiveDevices(std::vector<std::shared_ptr<Device>> &deviceList)
{
    int ret = 0;
    mutex.lock();
    typename std::vector<std::shared_ptr<Device>>::iterator iter;
    for (iter = active_devices.begin(); iter != active_devices.end(); iter++)
        deviceList.push_back(*iter);
    mutex.unlock();
    return ret;
}

int ResourceManager::getAudioRoute(struct audio_route** ar)
{
    if (!audio_route) {
        QAL_ERR(LOG_TAG, "no audio route found");
        return -ENOENT;
    }
    *ar = audio_route;
    QAL_DBG(LOG_TAG, "ar %pK audio_route %pK", ar, audio_route);
    return 0;
}

int ResourceManager::getAudioMixer(struct audio_mixer * am)
{
    if (!audio_mixer) {
        QAL_ERR(LOG_TAG, "no audio mixer found");
        return -ENOENT;
    }
    am = audio_mixer;
    QAL_DBG(LOG_TAG, "ar %pK audio_mixer %pK", am, audio_mixer);
    return 0;
}

template <class T>
void getActiveStreams(std::shared_ptr<Device> d, std::vector<Stream*> &activestreams,
                      std::vector<T> sourcestreams)
{
    for(typename std::vector<T>::iterator iter = sourcestreams.begin();
                 iter != sourcestreams.end(); iter++) {
        std::vector <std::shared_ptr<Device>> devices;
        (*iter)->getAssociatedDevices(devices);
        typename std::vector<std::shared_ptr<Device>>::iterator result =
                 std::find(devices.begin(), devices.end(), d);
        if (result != devices.end())
            activestreams.push_back(*iter);
    }
}

int ResourceManager::getActiveStream(std::shared_ptr<Device> d,
                                     std::vector<Stream*> &activestreams)
{
    int ret = 0;
    QAL_DBG(LOG_TAG, "Enter.");
    mutex.lock();
    // merge all types of active streams into activestreams
    getActiveStreams(d, activestreams, active_streams_ll);
    getActiveStreams(d, activestreams, active_streams_ulla);
    getActiveStreams(d, activestreams, active_streams_db);
    getActiveStreams(d, activestreams, active_streams_comp);
    getActiveStreams(d, activestreams, active_streams_st);

    if (activestreams.empty()) {
        ret = -ENOENT;
        QAL_ERR(LOG_TAG, "no active streams found for device %d ret %d", d->getDeviceId(), ret);
    }
    mutex.unlock();
    QAL_DBG(LOG_TAG, "Exit. ret %d", ret);
    return ret;
}

/*blsUpdated - to specify if the config is updated by rm*/
int ResourceManager::checkAndGetDeviceConfig(struct qal_device *device, bool* blsUpdated)
{
    int ret = -EINVAL;
    if (!device || !blsUpdated){
        QAL_ERR(LOG_TAG, "Invalid input parameter ret %d", ret);
        return ret;
    }
    //TODO:check if device config is supported
    bool dev_supported = false;
    *blsUpdated = false;
    uint16_t channels = device->config.ch_info->channels;
    uint32_t samplerate = device->config.sample_rate;
    uint32_t bitwidth = device->config.bit_width;

    QAL_DBG(LOG_TAG, "Enter.");
    //TODO: check and rewrite params if needed
    // only compare with default value for now
    // because no config file parsed in init
    if (channels != DEFAULT_CHANNELS) {
        if (bOverwriteFlag) {
            device->config.ch_info->channels = DEFAULT_CHANNELS;
            *blsUpdated = true;
        }
    } else if (samplerate != DEFAULT_SAMPLE_RATE) {
        if (bOverwriteFlag) {
            device->config.sample_rate = DEFAULT_SAMPLE_RATE;
            *blsUpdated = true;
        }
    } else if (bitwidth != DEFAULT_BIT_WIDTH) {
        if (bOverwriteFlag) {
            device->config.bit_width = DEFAULT_BIT_WIDTH;
            *blsUpdated = true;
        }
    } else {
        ret = 0;
        dev_supported = true;
    }
    QAL_DBG(LOG_TAG, "Exit. ret %d", ret);
    return ret;
}

std::shared_ptr<ResourceManager> ResourceManager::getInstance()
{
    QAL_INFO(LOG_TAG, "Enter.");
    if(!rm) {
        std::lock_guard<std::mutex> lock(ResourceManager::mutex);
        if (!rm) {
            std::shared_ptr<ResourceManager> sp(new ResourceManager());
            rm = sp;
        }
    }
    QAL_INFO(LOG_TAG, "Exit.");
    return rm;
}

int ResourceManager::getSndCard()
{
    return snd_card;
}

int ResourceManager::getDeviceName(int deviceId, char *device_name)
{
    if (deviceId >= QAL_DEVICE_OUT_EARPIECE && deviceId <= QAL_DEVICE_IN_PROXY) {
        strlcpy(device_name, sndDeviceNameLUT[deviceId].second.c_str(), DEVICE_NAME_MAX_SIZE);
    } else {
        strlcpy(device_name, "", DEVICE_NAME_MAX_SIZE);
        QAL_ERR(LOG_TAG, "Invalid device id %d", deviceId);
        return -EINVAL;
    }
    return 0;
}

int ResourceManager::getDeviceEpName(int deviceId, std::string &epName)
{
    if (deviceId >= QAL_DEVICE_OUT_EARPIECE && deviceId <= QAL_DEVICE_IN_PROXY) {
        epName.assign(deviceLinkName[deviceId].second);
    } else {
        QAL_ERR(LOG_TAG, "Invalid device id %d", deviceId);
        return -EINVAL;
    }
    return 0;
}
// TODO: Should pcm device be related to usecases used(ll/db/comp/ulla)?
// Use Low Latency as default by now
int ResourceManager::getPcmDeviceId(int deviceId)
{
    int pcm_device_id = -1;
    if (deviceId < QAL_DEVICE_OUT_EARPIECE || deviceId > QAL_DEVICE_IN_PROXY) {
        QAL_ERR(LOG_TAG, " Invalid device id %d", deviceId);
        return -EINVAL;
    }

    pcm_device_id = devicePcmId[deviceId].second;
    return pcm_device_id;
}

void ResourceManager::deinit()
{
    rm = nullptr;
    SessionGsl::deinit();
}

int ResourceManager::getStreamTag(std::vector <int> &tag)
{
    int status = 0;
    for (int i=0; i < streamTag.size(); i++) {
        tag.push_back(streamTag[i]);
    }
    return status;
}

int ResourceManager::getStreamPpTag(std::vector <int> &tag)
{
    int status = 0;
    for (int i=0; i < streamPpTag.size(); i++) {
        tag.push_back(streamPpTag[i]);
    }
    return status;
}

int ResourceManager::getMixerTag(std::vector <int> &tag)
{
    int status = 0;
    for (int i=0; i < mixerTag.size(); i++) {
        tag.push_back(mixerTag[i]);
    }
    return status;
}

int ResourceManager::getDeviceTag(std::vector <int> &tag)
{
    int status = 0;
    for (int i=0; i < deviceTag.size(); i++) {
        tag.push_back(deviceTag[i]);
    }
    return status;
}

int ResourceManager::getDevicePpTag(std::vector <int> &tag)
{
    int status = 0;
    for (int i=0; i < devicePpTag.size(); i++) {
        tag.push_back(devicePpTag[i]);
    }
    return status;
}

void ResourceManager::updatePcmId(int32_t deviceId, int32_t pcmId)
{
    devicePcmId[deviceId].second = pcmId;
}

void ResourceManager::updateLinkName(int32_t deviceId, std::string linkName)
{
    deviceLinkName[deviceId].second = linkName;
}

void ResourceManager::updateSndName(int32_t deviceId, std::string sndName)
{
    sndDeviceNameLUT[deviceId].second = sndName;
}

int convertCharToHex(std::string num)
{
    int32_t hexNum = 0;
    int32_t base = 1;
    const char * charNum = num.c_str();
    int32_t len = strlen(charNum);
    for (int i = len-1; i>=2; i--) {
        if (charNum[i] >= '0' && charNum[i] <= '9') {
            hexNum += (charNum[i] - 48) * base;
            base = base * 16;
        } else if (charNum[i] >= 'A' && charNum[i] <= 'F') {
            hexNum += (charNum[i] - 55) * base;
            base = base * 16;
        } else if (charNum[i] >= 'a' && charNum[i] <= 'f') {
            hexNum += (charNum[i] - 87) * base;
            base = base * 16;
        }
    }
    return hexNum;
}

void ResourceManager::updateStreamTag(int32_t tagId)
{
    streamTag.push_back(tagId);
}

void ResourceManager::updateDeviceTag(int32_t tagId)
{
    deviceTag.push_back(tagId);
}

void ResourceManager::processTagInfo(const XML_Char **attr)
{
    int32_t tagId;
    int32_t found = 0;
    char tagChar[128] = {0};
    if (strcmp(attr[0], "id" ) !=0 ) {
        QAL_ERR(LOG_TAG, " 'id' not found");
        return;
    }
    std::string tagCh(attr[1]);

    tagId = convertCharToHex(tagCh);
    if (strcmp(attr[2], "name") != 0) {
        QAL_ERR(LOG_TAG, " 'name' not found");
        return;
    }

    std::string name(attr[3]);
    std::string String("stream");
    found = name.find(String);
    if (found != std::string::npos){
        updateStreamTag(tagId);
    }
    found = 0;
    found = name.find(std::string("device"));
    if (found != std::string::npos) {
        updateDeviceTag(tagId);
    }

}

void ResourceManager::processDeviceInfo(const XML_Char **attr)
{
    int32_t deviceId;
    int32_t pcmId;
    if(strcmp(attr[0], "name" ) !=0 ) {
        QAL_ERR(LOG_TAG, " 'name' not found");
        return;
    }

    std::string deviceName(attr[1]);
    deviceId = deviceIdLUT.at(deviceName);

    if (strcmp(attr[2],"pcm_id") !=0 ) {
        QAL_ERR(LOG_TAG, " 'pcm_id' not found %s is the tag", attr[2]);
        return;
    }
    pcmId = atoi(attr[3]);
    updatePcmId(deviceId, pcmId);
    if(strcmp(attr[4],"hw_intf") !=0 ) {
        QAL_ERR(LOG_TAG, " 'hw_intf' not found");
        return;
    }
    std::string linkName(attr[5]);
    updateLinkName(deviceId, linkName);

    if(strcmp(attr[6], "snd_device_name") != 0) {
        QAL_ERR(LOG_TAG, " 'snd_device_name' not found");
        return;
    }
    std::string sndName(attr[7]);
    updateSndName(deviceId, sndName);
}

void ResourceManager::startTag(void *userdata __unused, const XML_Char *tag_name,
    const XML_Char **attr)
{
    if (strcmp(tag_name, "device") == 0) {
        processDeviceInfo(attr);
    } else if (strcmp(tag_name, "Tag") == 0) {
        processTagInfo(attr);
    } else if (strcmp(tag_name, "TAG") == 0) {
        processTagInfo(attr);
    }
}

void ResourceManager::endTag(void *userdata __unused, const XML_Char *tag_name)
{
    return;
}


int ResourceManager::XmlParser(std::string xmlFile)
{
    XML_Parser parser;
    FILE *file = NULL;
    int ret = 0;
    int bytes_read;
    void *buf = NULL;
    QAL_DBG(LOG_TAG, "Enter. XML parsing started");
    file = fopen(xmlFile.c_str(), "r");
    if(!file) {
        ret = EINVAL;
        QAL_ERR(LOG_TAG, "Failed to open xml file name %s ret %d", xmlFile.c_str(), ret);
        goto done;
    }

    parser = XML_ParserCreate(NULL);
    if (!parser) {
        ret = -EINVAL;
        QAL_ERR(LOG_TAG, "Failed to create XML ret %d", ret);
        goto closeFile;
    }

    XML_SetElementHandler(parser, startTag, endTag);

    while(1) {
        buf = XML_GetBuffer(parser, 1024);
        if(buf == NULL) {
            ret = -EINVAL;
            QAL_ERR(LOG_TAG, "XML_Getbuffer failed ret %d", ret);
            goto freeParser;
        }

        bytes_read = fread(buf, 1, 1024, file);
        if(bytes_read < 0) {
            ret = -EINVAL;
            QAL_ERR(LOG_TAG, "fread failed ret %d", ret);
            goto freeParser;
        }

        if(XML_ParseBuffer(parser, bytes_read, bytes_read == 0) == XML_STATUS_ERROR) {
            ret = -EINVAL;
            QAL_ERR(LOG_TAG, "XML ParseBuffer failed for %s file ret %d", xmlFile.c_str(), ret);
            goto freeParser;
        }
        if (bytes_read == 0)
            break;
    }
    QAL_DBG(LOG_TAG, "Exit.");

freeParser:
    XML_ParserFree(parser);
closeFile:
    fclose(file);
done:
    return ret;
}