#pragma once
#include <stdint.h>
typedef void* jobject; typedef jobject jstring; typedef jobject jclass; typedef jobject jobjectArray; typedef void* jmethodID; typedef void* jfieldID;
typedef int32_t jint; typedef int64_t jlong; typedef uint8_t jboolean; typedef int8_t jbyte; typedef uint16_t jchar; typedef int16_t jshort; typedef float jfloat; typedef double jdouble; typedef jint jsize;
struct _JNIEnv {
  jclass GetObjectClass(jobject) { return 0; } jmethodID GetMethodID(jclass, const char*, const char*) { return 0; } jmethodID GetStaticMethodID(jclass, const char*, const char*) { return 0; } void DeleteLocalRef(jobject) {} jboolean ExceptionCheck() { return 0; } void ExceptionClear() {} jclass FindClass(const char*) { return 0; }
  template<class... A> void CallVoidMethod(A...) {}
  template<class... A> jobject CallObjectMethod(A...) { return 0; }
  template<class... A> jboolean CallBooleanMethod(A...) { return 0; }
  template<class... A> jbyte CallByteMethod(A...) { return 0; }
  template<class... A> jchar CallCharMethod(A...) { return 0; }
  template<class... A> jshort CallShortMethod(A...) { return 0; }
  template<class... A> jint CallIntMethod(A...) { return 0; }
  template<class... A> jlong CallLongMethod(A...) { return 0; }
  template<class... A> jfloat CallFloatMethod(A...) { return 0; }
  template<class... A> jdouble CallDoubleMethod(A...) { return 0; }
  template<class... A> void CallStaticVoidMethod(A...) {}
  template<class... A> jobject CallStaticObjectMethod(A...) { return 0; }
  template<class... A> jboolean CallStaticBooleanMethod(A...) { return 0; }
  template<class... A> jint CallStaticIntMethod(A...) { return 0; }
  template<class... A> jlong CallStaticLongMethod(A...) { return 0; }
  template<class... A> jfloat CallStaticFloatMethod(A...) { return 0; }
  template<class... A> jdouble CallStaticDoubleMethod(A...) { return 0; }
  template<class... A> jbyte CallStaticByteMethod(A...) { return 0; }
  template<class... A> jchar CallStaticCharMethod(A...) { return 0; }
  template<class... A> jshort CallStaticShortMethod(A...) { return 0; }
};
typedef _JNIEnv JNIEnv; struct _JavaVM {}; typedef _JavaVM JavaVM;
#define JNIEXPORT
#define JNICALL
