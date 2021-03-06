#include <node.h>
#include <nan.h>
#include <cstdlib>
#include "groove.h"
#include "file.h"
#include "player.h"
#include "playlist.h"
#include "playlist_item.h"
#include "loudness_detector.h"
#include "fingerprinter.h"
#include "waveform_builder.h"
#include "encoder.h"
#include "device.h"

using namespace v8;

static SoundIo *soundio = NULL;
static Groove *groove = NULL;

Groove *get_groove() {
    return groove;
}

NAN_METHOD(SetLogging) {
    Nan::HandleScope scope;

    if (info.Length() < 1 || !info[0]->IsNumber()) {
        Nan::ThrowTypeError("Expected 1 number argument");
        return;
    }
    groove_set_logging(info[0]->NumberValue());
}

NAN_METHOD(ConnectSoundBackend) {
    SoundIoBackend backend = SoundIoBackendNone;
    if (info.Length() == 1) {
        if (!info[0]->IsNumber()) {
            Nan::ThrowTypeError("Expected 0 or 1 args");
            return;
        }
        backend = (SoundIoBackend)(int)info[0]->NumberValue();
    } else if (info.Length() > 1) {
        Nan::ThrowTypeError("Expected 0 or 1 args");
        return;
    }

    if (soundio->current_backend != SoundIoBackendNone)
        soundio_disconnect(soundio);

    int err = (backend == SoundIoBackendNone) ?
        soundio_connect(soundio) : soundio_connect_backend(soundio, backend);

    if (err) {
        Nan::ThrowError(soundio_strerror(err));
        return;
    }
}

NAN_METHOD(DisconnectSoundBackend) {
    if (soundio->current_backend != SoundIoBackendNone)
        soundio_disconnect(soundio);
}

NAN_METHOD(GetDevices) {
    Nan::HandleScope scope;

    if (soundio->current_backend == SoundIoBackendNone) {
        Nan::ThrowError("no backend connected");
        return;
    }

    soundio_flush_events(soundio);

    int output_count = soundio_output_device_count(soundio);
    int default_output = soundio_default_output_device_index(soundio);

    Local<Array> deviceList = Nan::New<Array>();

    for (int i = 0; i < output_count; i += 1) {
        struct SoundIoDevice *device = soundio_get_output_device(soundio, i);
        Local<Value> deviceObject = GNDevice::NewInstance(device);
        Nan::Set(deviceList, Nan::New<Number>(i), deviceObject);
    }

    Local<Object> ret_value = Nan::New<Object>();

    Nan::Set(ret_value, Nan::New<String>("list").ToLocalChecked(), deviceList);
    Nan::Set(ret_value, Nan::New<String>("defaultIndex").ToLocalChecked(), Nan::New<Number>(default_output));

    info.GetReturnValue().Set(ret_value);
}

NAN_METHOD(GetVersion) {
    Nan::HandleScope scope;

    Local<Object> version = Nan::New<Object>();
    Nan::Set(version, Nan::New<String>("major").ToLocalChecked(), Nan::New<Number>(groove_version_major()));
    Nan::Set(version, Nan::New<String>("minor").ToLocalChecked(), Nan::New<Number>(groove_version_minor()));
    Nan::Set(version, Nan::New<String>("patch").ToLocalChecked(), Nan::New<Number>(groove_version_patch()));

    info.GetReturnValue().Set(version);
}

template <typename target_t>
static void SetProperty(target_t obj, const char* name, double n) {
    Nan::Set(obj, Nan::New<String>(name).ToLocalChecked(), Nan::New<Number>(n));
}

template <typename target_t, typename FNPTR>
static void SetMethod(target_t obj, const char* name, FNPTR fn) {
    Nan::Set(obj, Nan::New<String>(name).ToLocalChecked(),
            Nan::GetFunction(Nan::New<FunctionTemplate>(fn)).ToLocalChecked());
}

static void cleanup(void) {
    groove_destroy(groove);
    soundio_destroy(soundio);
}

NAN_MODULE_INIT(Initialize) {
    int err;

    soundio = soundio_create();
    if (!soundio) {
        fprintf(stderr, "unable to initialize libsoundio: out of memory\n");
        abort();
    }

    if ((err = groove_create(&groove))) {
        fprintf(stderr, "unable to initialize libgroove: %s\n", groove_strerror(err));
        abort();
    }
    atexit(cleanup);

    GNFile::Init();
    GNPlayer::Init();
    GNPlaylist::Init();
    GNPlaylistItem::Init();
    GNLoudnessDetector::Init();
    GNEncoder::Init();
    GNFingerprinter::Init();
    GNDevice::Init();
    GNWaveformBuilder::Init();

    SetProperty(target, "LOG_QUIET", GROOVE_LOG_QUIET);
    SetProperty(target, "LOG_ERROR", GROOVE_LOG_ERROR);
    SetProperty(target, "LOG_WARNING", GROOVE_LOG_WARNING);
    SetProperty(target, "LOG_INFO", GROOVE_LOG_INFO);

    SetProperty(target, "TAG_MATCH_CASE", GROOVE_TAG_MATCH_CASE);
    SetProperty(target, "TAG_DONT_OVERWRITE", GROOVE_TAG_DONT_OVERWRITE);
    SetProperty(target, "TAG_APPEND", GROOVE_TAG_APPEND);

    SetProperty(target, "EVERY_SINK_FULL", GrooveFillModeEverySinkFull);
    SetProperty(target, "ANY_SINK_FULL", GrooveFillModeAnySinkFull);

    SetProperty(target, "_EVENT_NOWPLAYING", GROOVE_EVENT_NOWPLAYING);
    SetProperty(target, "_EVENT_BUFFERUNDERRUN", GROOVE_EVENT_BUFFERUNDERRUN);
    SetProperty(target, "_EVENT_DEVICE_CLOSED", GROOVE_EVENT_DEVICE_CLOSED);
    SetProperty(target, "_EVENT_DEVICE_OPENED", GROOVE_EVENT_DEVICE_OPENED);
    SetProperty(target, "_EVENT_DEVICE_OPEN_ERROR", GROOVE_EVENT_DEVICE_OPEN_ERROR);
    SetProperty(target, "_EVENT_END_OF_PLAYLIST", GROOVE_EVENT_END_OF_PLAYLIST);
    SetProperty(target, "_EVENT_WAKEUP", GROOVE_EVENT_WAKEUP);

    SetProperty(target, "BACKEND_JACK", SoundIoBackendJack);
    SetProperty(target, "BACKEND_PULSEAUDIO", SoundIoBackendPulseAudio);
    SetProperty(target, "BACKEND_ALSA", SoundIoBackendAlsa);
    SetProperty(target, "BACKEND_COREAUDIO", SoundIoBackendCoreAudio);
    SetProperty(target, "BACKEND_WASAPI", SoundIoBackendWasapi);
    SetProperty(target, "BACKEND_DUMMY", SoundIoBackendDummy);

    SetMethod(target, "setLogging", SetLogging);
    SetMethod(target, "getDevices", GetDevices);
    SetMethod(target, "connectSoundBackend", ConnectSoundBackend);
    SetMethod(target, "disconnectSoundBackend", DisconnectSoundBackend);
    SetMethod(target, "getVersion", GetVersion);
    SetMethod(target, "open", GNFile::Open);
    SetMethod(target, "createPlayer", GNPlayer::Create);
    SetMethod(target, "createPlaylist", GNPlaylist::Create);
    SetMethod(target, "createLoudnessDetector", GNLoudnessDetector::Create);
    SetMethod(target, "createEncoder", GNEncoder::Create);
    SetMethod(target, "createFingerprinter", GNFingerprinter::Create);
    SetMethod(target, "createWaveformBuilder", GNWaveformBuilder::Create);

    SetMethod(target, "encodeFingerprint", GNFingerprinter::Encode);
    SetMethod(target, "decodeFingerprint", GNFingerprinter::Decode);
}

NODE_MODULE(groove, Initialize)
