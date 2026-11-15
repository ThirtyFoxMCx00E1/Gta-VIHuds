#pragma once
#define SL_DATALOCATOR_ANDROIDFD 0x800007BC
#define SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE 0x800007BD
typedef struct { SLuint32 locatorType; SLint32 fd; SLAint64 offset; SLAint64 length; } SLDataLocator_AndroidFD;
typedef struct { SLuint32 locatorType; SLuint32 numBuffers; } SLDataLocator_AndroidSimpleBufferQueue;
struct SLAndroidSimpleBufferQueueItf_; typedef const struct SLAndroidSimpleBufferQueueItf_* const* SLAndroidSimpleBufferQueueItf;
struct SLAndroidSimpleBufferQueueItf_ { SLresult (*Enqueue)(SLAndroidSimpleBufferQueueItf, const void*, SLuint32); SLresult (*Clear)(SLAndroidSimpleBufferQueueItf); void* gs; SLresult (*RegisterCallback)(SLAndroidSimpleBufferQueueItf, void(*)(SLAndroidSimpleBufferQueueItf, void*), void*); };
