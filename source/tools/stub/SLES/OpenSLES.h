#pragma once
#include <stdint.h>
typedef uint32_t SLresult; typedef uint32_t SLuint32; typedef int64_t SLAint64; typedef int32_t SLint32; typedef uint32_t SLboolean; typedef int16_t SLmillibel; typedef char SLchar;
struct SLObjectItf_; typedef const struct SLObjectItf_* const* SLObjectItf;
struct SLEngineItf_; typedef const struct SLEngineItf_* const* SLEngineItf;
struct SLPlayItf_; typedef const struct SLPlayItf_* const* SLPlayItf;
struct SLVolumeItf_; typedef const struct SLVolumeItf_* const* SLVolumeItf;
struct SLBufferQueueItf_; typedef const struct SLBufferQueueItf_* const* SLBufferQueueItf;
typedef const void* SLInterfaceID;
struct SLObjectItf_ { SLresult (*Realize)(SLObjectItf,SLboolean); SLresult (*Resume)(SLObjectItf,SLboolean); SLresult (*GetState)(SLObjectItf,SLuint32*); SLresult (*GetInterface)(SLObjectItf,SLInterfaceID,void*); void* a; void* b; void* c; void (*Destroy)(SLObjectItf); };
struct SLEngineItf_ { SLresult (*CreateLEDDevice)(); SLresult (*CreateVibraDevice)(); SLresult (*CreateAudioPlayer)(SLEngineItf, SLObjectItf*, void*, void*, SLuint32, const SLInterfaceID*, const SLboolean*); void* r; void* m; SLresult (*CreateOutputMix)(SLEngineItf, SLObjectItf*, SLuint32, const SLInterfaceID*, const SLboolean*); };
struct SLPlayItf_ { SLresult (*SetPlayState)(SLPlayItf, SLuint32); SLresult (*GetPlayState)(SLPlayItf, SLuint32*); };
struct SLVolumeItf_ { SLresult (*SetVolumeLevel)(SLVolumeItf, SLmillibel); };
struct SLBufferQueueItf_ { SLresult (*Enqueue)(SLBufferQueueItf, const void*, SLuint32); SLresult (*Clear)(SLBufferQueueItf); };
extern SLInterfaceID SL_IID_ENGINE, SL_IID_PLAY, SL_IID_VOLUME, SL_IID_BUFFERQUEUE, SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
#define SL_RESULT_SUCCESS 0
#define SL_BOOLEAN_FALSE 0
#define SL_BOOLEAN_TRUE 1
#define SL_PLAYSTATE_STOPPED 1
#define SL_PLAYSTATE_PLAYING 3
#define SL_DATAFORMAT_MIME 1
#define SL_DATAFORMAT_PCM 2
#define SL_CONTAINERTYPE_UNSPECIFIED 1
#define SL_DATALOCATOR_OUTPUTMIX 4
#define SL_SAMPLINGRATE_44_1 44100000
#define SL_PCMSAMPLEFORMAT_FIXED_16 16
#define SL_SPEAKER_FRONT_CENTER 4
#define SL_SPEAKER_FRONT_LEFT 1
#define SL_SPEAKER_FRONT_RIGHT 2
#define SL_BYTEORDER_LITTLEENDIAN 2
typedef struct { SLuint32 formatType; SLchar* mimeType; SLuint32 containerType; } SLDataFormat_MIME;
typedef struct { SLuint32 formatType, numChannels, samplesPerSec, bitsPerSample, containerSize, channelMask, endianness; } SLDataFormat_PCM;
typedef struct { void* pLocator; void* pFormat; } SLDataSource;
typedef struct { void* pLocator; void* pFormat; } SLDataSink;
typedef struct { SLuint32 locatorType; SLObjectItf outputMix; } SLDataLocator_OutputMix;
SLresult slCreateEngine(SLObjectItf*, SLuint32, const void*, SLuint32, const SLInterfaceID*, const SLboolean*);
