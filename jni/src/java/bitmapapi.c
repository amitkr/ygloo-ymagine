/**
 * Copyright 2013 Yahoo! Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License. You may
 * obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License. See accompanying LICENSE file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <setjmp.h>

#define LOG_TAG "ymagine::bitmap"

#include "ymagine/ymagine.h"
#include "ymagine/ymagine_ymaginejni.h"

#include "ymagine_config.h"
#include "ymagine_priv.h"

#if HAVE_BITMAPFACTORY
#include <stdio.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

extern PixelShader* getPixelShader(JNIEnv *_env, jobject jshader);

static volatile int gYmagineBitmapFactory_inited = -1;
static pthread_mutex_t gYmagineBitmapFactory_mutex = PTHREAD_MUTEX_INITIALIZER;

static jclass gBitmap_clazz = 0;
static jclass gBitmapConfig_clazz = 0;
static jclass gOptions_clazz = 0;

static jmethodID gBitmap_createBitmapMethodID = 0;
static jfieldID gBitmapConfig_RGBAFieldID = 0;

static jfieldID gOptions_justBounds;
static jfieldID gOptions_sampleSize;
static jfieldID gOptions_config;
static jfieldID gOptions_dither;
static jfieldID gOptions_purgeable;
static jfieldID gOptions_shareable;
static jfieldID gOptions_scaled;
static jfieldID gOptions_density;
static jfieldID gOptions_screenDensity;
static jfieldID gOptions_targetDensity;
static jfieldID gOptions_width;
static jfieldID gOptions_height;
static jfieldID gOptions_mime;
static jfieldID gOptions_mCancel;

#if 0
/* this code block is commented out because the variables from
 BitmapFactory.Options required higher API level, with current code
 app will crash if compiled targeting API lower than their API level.
 Commented out for now as a temporary fix.
 TODO: fix this in a better way. */
static jfieldID gOptions_premultiplied;
static jfieldID gOptions_mutable;
static jfieldID gOptions_preferQualityOverSpeed;
#endif

static jfieldID gOptions_inMaxWidth;
static jfieldID gOptions_inMaxHeight;
static jfieldID gOptions_inKeepRatio;
static jfieldID gOptions_inCrop;
static jfieldID gOptions_inFit;
static jfieldID gOptions_inNative;
static jfieldID gOptions_inStream;
static jfieldID gOptions_inBitmap;
static jfieldID gOptions_inBlur;
static jfieldID gOptions_inShader;
static jfieldID gOptions_inQuality;
static jfieldID gOptions_outPanoMode;
static jfieldID gOptions_outPanoCroppedWidth;
static jfieldID gOptions_outPanoCroppedHeight;
static jfieldID gOptions_outPanoFullWidth;
static jfieldID gOptions_outPanoFullHeight;
static jfieldID gOptions_outPanoX;
static jfieldID gOptions_outPanoY;
static jfieldID gOptions_outOrientation;

static int
bitmapfactory_init(JNIEnv *_env)
{
  if (gYmagineBitmapFactory_inited < 0) {
    pthread_mutex_lock(&gYmagineBitmapFactory_mutex);
    if (gYmagineBitmapFactory_inited < 0) {
      jclass clazz;

      /* one time initializations, such as lookup tables */
      ycolor_yuv_prepare();

      /* Resolve classes */
      clazz = (*_env)->FindClass(_env, "android/graphics/Bitmap");
      if (clazz != 0) {
        gBitmap_clazz = (*_env)->NewGlobalRef(_env, clazz);
        gBitmap_createBitmapMethodID = (*_env)->GetStaticMethodID(_env,
                                                                  gBitmap_clazz,
                                                                  "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
      }

      clazz = (*_env)->FindClass(_env, "android/graphics/Bitmap$Config");
      if (clazz != 0) {
        gBitmapConfig_clazz = (*_env)->NewGlobalRef(_env, clazz);
        gBitmapConfig_RGBAFieldID = (*_env)->GetStaticFieldID(_env,
                                                              gBitmapConfig_clazz, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
      }


      if ( (gBitmap_clazz == 0) || (gBitmapConfig_clazz == 0) ||
          (gBitmap_createBitmapMethodID == 0) || (gBitmapConfig_RGBAFieldID == 0) ) {
        gYmagineBitmapFactory_inited = 0;
      } else {
        gYmagineBitmapFactory_inited = 1;
      }
    }
    pthread_mutex_unlock(&gYmagineBitmapFactory_mutex);
  }

  return (gYmagineBitmapFactory_inited > 0);
}

jobject
createAndroidBitmap(JNIEnv* _env, int width, int height)
{
  jobject bitmap = 0;

  if (width <= 0 || width >= 1<<16 || height <= 0 || height >= 1<<16) {
    return bitmap;
  }

  if (bitmapfactory_init(_env) <= 0) {
    return bitmap;
  }

  jobject jbitmapconfig = (jobject) (*_env)->GetStaticObjectField(_env, gBitmapConfig_clazz, gBitmapConfig_RGBAFieldID);
  if (jbitmapconfig != 0) {
    bitmap = (*_env)->CallStaticObjectMethod(_env, gBitmap_clazz, gBitmap_createBitmapMethodID, width, height, jbitmapconfig);
    if ((*_env)->ExceptionCheck(_env)) {
      (*_env)->ExceptionClear(_env);
      bitmap = 0;
    }
  }

  /* Return a mutable bitmap in ARGB format */
  return bitmap;
}

static jobject
decodeChannel(JNIEnv* _env, jobject object,
              Ychannel *channel, jobject jbitmap,
              jint maxWidth, jint maxHeight, jint scaleMode)
{
  jobject outbitmap = NULL;
  Vbitmap *vbitmap;
  int rc = YMAGINE_ERROR;

  if (!YchannelReadable(channel)) {
    return NULL;
  }

  vbitmap = VbitmapInitAndroid(_env, jbitmap);
  rc = YmagineDecodeResize(vbitmap, channel, maxWidth, maxHeight, scaleMode);

  if (rc == YMAGINE_OK) {
    outbitmap = VbitmapGetAndroid(vbitmap);
  }

  VbitmapRelease(vbitmap);

  return outbitmap;
}

static jobject
bitmap_jni_createOrb(JNIEnv* _env, jobject object,
                     jobject jbitmap, jint reqSize)
{
  jobject outbitmap = NULL;
  Vbitmap *vbitmap;
  int rc = YMAGINE_ERROR;

  if (jbitmap == NULL) {
    if (reqSize <= 0) {
      return NULL;
    }

    jbitmap = createAndroidBitmap(_env, reqSize, reqSize);
  }

  vbitmap = VbitmapInitAndroid(_env, jbitmap);
  rc = VbitmapOrbLoad(vbitmap, reqSize);
  if (rc == YMAGINE_OK) {
    outbitmap = VbitmapGetAndroid(vbitmap);
  }
  VbitmapRelease(vbitmap);

  return outbitmap;
}

static jint
bitmap_jni_renderOrbTile(JNIEnv* _env, jobject object,
                         jobject jbitmap, jint ntiles, jint tileid, jobject stream)
{
  Ychannel *channel;
  int rc = -1;

  if (stream == NULL) {
    return rc;
  }

  channel = YchannelInitJavaInputStream(_env, stream);
  if (channel == NULL) {
    ALOGD("failed to create Ychannel for input stream");
  } else {
    Vbitmap *vbitmap = VbitmapInitAndroid(_env, jbitmap);
    if (vbitmap != NULL) {
      if (VbitmapOrbRenderTile(vbitmap, ntiles, tileid, channel) == YMAGINE_OK) {
        rc = tileid;
      }
      VbitmapRelease(vbitmap);
    }
    YchannelRelease(channel);
  }

  return rc;
#if 0
  int ninfiles = 0;
  int rc = 0;
  int i;
  const char** infiles;
  Vbitmap *vbitmap;

  if (jinfiles == NULL) {
    return 0;
  }
  ninfiles = (*_env)->GetArrayLength(_env, jinfiles);
  if (ninfiles <= 0) {
    return 0;
  }

  infiles = Ymem_malloc(ninfiles * sizeof(char*));
  if (infiles != NULL) {
    for (i = 0; i < ninfiles; i++) {
      jstring jstr = (jstring) (*_env)->GetObjectArrayElement(_env, jinfiles, i);
      if (jstr == NULL) {
        infiles[i] = NULL;
      } else {
        infiles[i] = (*_env)->GetStringUTFChars(_env, jstr, NULL);
      }
    }

    vbitmap = VbitmapInitAndroid(_env, jbitmap);
    if (VbitmapOrbRenderTile(vbitmap, ntiles, tileid, infiles) != YMAGINE_OK) {
      rc = 0;
    } else {
      rc = ninfiles;
    }
    VbitmapRelease(vbitmap);

    for (i = 0; i < ninfiles; i++) {
      if (infiles[i] != NULL) {
        jstring jstr = (jstring) (*_env)->GetObjectArrayElement(_env, jinfiles, i);
        if (jstr != NULL) {
          (*_env)->ReleaseStringUTFChars(_env, jstr, infiles[i]);
        }
      }
    }
  }
#endif
}

JNIEXPORT jobject JNICALL
bitmap_jni_decodeFile(JNIEnv* _env, jobject object,
                      jstring filename, jobject bitmap,
                      jint maxWidth, jint maxHeight, jint scaleMode)
{
  jobject outbitmap = NULL;
  Ychannel *channel;
  FILE *file = NULL;

  if (filename != NULL) {
    char const * filenameStr = (*_env)->GetStringUTFChars(_env, filename, NULL);
    if (filenameStr != NULL) {
      file = fopen(filenameStr, "rb");
      if (file == NULL) {
        ALOGE("failed to open image file \"%s\"", filenameStr);
      }
      (*_env)->ReleaseStringUTFChars(_env, filename, filenameStr);
    }
  }
  if (file == NULL) {
    return NULL;
  }

  channel = YchannelInitFile(file, 0);
  if (channel != NULL) {
    outbitmap = decodeChannel(_env, object, channel, bitmap, maxWidth, maxHeight, scaleMode);
    YchannelRelease(channel);
  }

  fclose(file);

  return outbitmap;
}

JNIEXPORT jobject JNICALL
bitmap_jni_decodeNV21ByteArray(JNIEnv* _env, jobject object, jobject bitmap,
                               jbyteArray dataObject, jint width, jint height)
{
  jbyte *data;
  int datalen;
  jobject outbitmap = NULL;
  Vbitmap *vbitmap;

  if (dataObject == NULL) {
    return NULL;
  }

  datalen = (*_env)->GetArrayLength(_env, dataObject);
  if (datalen <= 0) {
    return NULL;
  }

  data = (jbyte*) (*_env)->GetByteArrayElements(_env, dataObject, NULL);
  if (data == NULL) {
    return NULL;
  }

  ALOGD("JNI decode NV21 to RGB of size %d, width: %d, height: %d", datalen, width, height);

  vbitmap = VbitmapInitAndroid(_env, bitmap);
  VbitmapLock(vbitmap);
  VbitmapWriteNV21Buffer(vbitmap, (const unsigned char*)data, width, height, YMAGINE_SCALE_HALF_QUICK);
  VbitmapUnlock(vbitmap);

  outbitmap = VbitmapGetAndroid(vbitmap);

  return outbitmap;
}

/* not in use right now */
#if 0
JNIEXPORT jobject JNICALL
bitmap_jni_decodeByteArray(JNIEnv* _env, jobject object,
                           jbyteArray dataObject, jobject bitmap,
                           jint maxWidth, jint maxHeight, jint scaleMode)
{
  jobject outbitmap = NULL;
  Ychannel *channel;
  jbyte *data;
  jsize length;

  if (dataObject == NULL) {
    return NULL;
  }
  length = (*_env)->GetArrayLength(_env, dataObject);
  if (length <= 0) {
    return NULL;
  }

  data = (jbyte*) (*_env)->GetByteArrayElements(_env, dataObject, NULL);
  if (data != NULL) {
    channel = YchannelInitByteArray((const char*) data, length);
    if (channel != NULL) {
      outbitmap = decodeChannel(_env, object, channel, bitmap, maxWidth, maxHeight, scaleMode);
      YchannelRelease(channel);
    }
    (*_env)->ReleaseByteArrayElements(_env, dataObject, data, 0 );
  }

  return outbitmap;
}
#endif

JNIEXPORT jobject JNICALL
bitmap_jni_decodeStream(JNIEnv* _env, jobject object,
                        jobject stream, jobject bitmap,
                        jint maxWidth, jint maxHeight, jint scaleMode)
{
  jobject outbitmap = NULL;
  Ychannel *channel;

  if (stream == NULL) {
    return NULL;
  }

  channel = YchannelInitJavaInputStream(_env, stream);
  if (channel == NULL) {
    ALOGD("failed to create Ychannel for input stream");
  } else {
    outbitmap = decodeChannel(_env, object, channel, bitmap,
                              maxWidth, maxHeight, scaleMode);
    YchannelRelease(channel);
  }

  return outbitmap;
}

static jobject JNICALL
bitmap_jni_decodeOptions(JNIEnv* _env, jobject object,
                         jobject stream, jobject jsrcvbitmap, jobject joptions)
{
  Vbitmap *vbitmap = NULL;
  int rc = YMAGINE_ERROR;
  Ychannel *channel = NULL;
  Vbitmap *srcvbitmap = NULL;
  jint maxWidth = -1;
  jint maxHeight = -1;
  jboolean scaleCrop = 0;
  jboolean scaleFit = 0;
  jboolean justBounds = 0;
  jobject jbitmap = NULL;
  jobject jshader = NULL;
  PixelShader* shader = NULL;
  jobject outbitmap = NULL;
  //jobject outstream = NULL;
  int scaleMode = YMAGINE_SCALE_LETTERBOX;
  float blur = 0.0f;
  YmagineFormatOptions *options;

  if (stream != NULL) {
    channel = YchannelInitJavaInputStream(_env, stream);
    if (channel == NULL) {
      ALOGD("failed to create Ychannel for input stream");
      return NULL;
    }
  } else if (jsrcvbitmap != NULL) {
    srcvbitmap = YmagineJNI_VbitmapRetain(_env, jsrcvbitmap);
    if (srcvbitmap == NULL) {
      return NULL;
    }
  } else {
    return NULL;
  }

  if (joptions != NULL) {
    maxWidth = (*_env)->GetIntField(_env, joptions, gOptions_inMaxWidth);
    maxHeight = (*_env)->GetIntField(_env, joptions, gOptions_inMaxHeight);
    scaleCrop = (*_env)->GetBooleanField(_env, joptions, gOptions_inCrop);
    scaleFit = (*_env)->GetBooleanField(_env, joptions, gOptions_inFit);
    justBounds = (*_env)->GetBooleanField(_env, joptions, gOptions_justBounds);
    jbitmap = (*_env)->GetObjectField(_env, joptions, gOptions_inBitmap);
    blur = (float) (*_env)->GetFloatField(_env, joptions, gOptions_inBlur);

    jshader = (*_env)->GetObjectField(_env, joptions, gOptions_inShader);
    shader = getPixelShader(_env, jshader);

    //outstream = (*_env)->GetObjectField(_env, joptions, gOptions_inStream);

    if (scaleFit) {
      scaleMode = YMAGINE_SCALE_FIT;
    } else if (scaleCrop) {
      scaleMode = YMAGINE_SCALE_CROP;
    } else {
      scaleMode = YMAGINE_SCALE_LETTERBOX;
    }
  }

  options = YmagineFormatOptions_Create();
  if (options != NULL) {
    YmagineFormatOptions_setResize(options, maxWidth, maxHeight, scaleMode);
    YmagineFormatOptions_setBlur(options, blur);
    YmagineFormatOptions_setShader(options, shader);

    if (justBounds) {
      vbitmap = VbitmapInitNone();
      YmagineFormatOptions_setResizable(options, 1);
    } else {
      vbitmap = VbitmapInitAndroid(_env, jbitmap);
      if (jbitmap != NULL) {
        YmagineFormatOptions_setResizable(options, 0);
      } else {
        YmagineFormatOptions_setResizable(options, 1);
      }
    }
    if (channel != NULL) {
      rc = YmagineDecode(vbitmap, channel, options);
    } else if (srcvbitmap != NULL) {
      rc = YmagineDecodeCopy(vbitmap, srcvbitmap, options);
    } else {
      rc = YMAGINE_ERROR;
    }
    YmagineFormatOptions_Release(options);

    if (rc == YMAGINE_OK && VbitmapType(vbitmap) == VBITMAP_ANDROID) {
      outbitmap = VbitmapGetAndroid(vbitmap);
    }
  }

  if (joptions != NULL && rc == YMAGINE_OK && vbitmap != NULL) {
    int orientation;
    /* Set output options */
    (*_env)->SetIntField(_env, joptions, gOptions_width, VbitmapWidth(vbitmap));
    (*_env)->SetIntField(_env, joptions, gOptions_height,  VbitmapHeight(vbitmap));
    (*_env)->SetObjectField(_env, joptions, gOptions_mime,  NULL);
    orientation = VbitmapGetOrientation(vbitmap);
    switch (orientation) {
      case VBITMAP_ORIENTATION_ROTATE_90:
      (*_env)->SetIntField(_env, joptions, gOptions_outOrientation, 90);
      break;

      case VBITMAP_ORIENTATION_ROTATE_180:
      (*_env)->SetIntField(_env, joptions, gOptions_outOrientation, 180);
      break;

      case VBITMAP_ORIENTATION_ROTATE_270:
      (*_env)->SetIntField(_env, joptions, gOptions_outOrientation, 270);
      break;

      default:
      (*_env)->SetIntField(_env, joptions, gOptions_outOrientation, 0);
      break;
    }

    VbitmapXmp *xmp = VbitmapGetXMP(vbitmap);
    if (xmp != NULL) {
      (*_env)->SetIntField(_env, joptions, gOptions_outPanoMode, xmp->UsePano);
      (*_env)->SetIntField(_env, joptions, gOptions_outPanoCroppedWidth, xmp->CroppedWidth);
      (*_env)->SetIntField(_env, joptions, gOptions_outPanoCroppedHeight, xmp->CroppedHeight);
      (*_env)->SetIntField(_env, joptions, gOptions_outPanoFullWidth, xmp->FullWidth);
      (*_env)->SetIntField(_env, joptions, gOptions_outPanoFullHeight, xmp->FullHeight);
      (*_env)->SetIntField(_env, joptions, gOptions_outPanoX, xmp->Left);
      (*_env)->SetIntField(_env, joptions, gOptions_outPanoY, xmp->Top);
    } else {
      (*_env)->SetIntField(_env, joptions, gOptions_outPanoMode, 0);
    }
  }

  if (jsrcvbitmap != NULL) {
    YmagineJNI_VbitmapRelease(_env, jsrcvbitmap);
  }
  if (vbitmap != NULL) {
    VbitmapRelease(vbitmap);
  }

  return outbitmap;
}

JNIEXPORT jobject JNICALL
bitmap_jni_decodeStreamOptions(JNIEnv* _env, jobject object,
                               jobject stream, jobject joptions)
{
  return bitmap_jni_decodeOptions(_env, object, stream, NULL, joptions);
}

JNIEXPORT jobject JNICALL
bitmap_jni_decodeVbitmapOptions(JNIEnv* _env, jobject object,
                                jobject jvbitmap, jobject joptions)
{
  return bitmap_jni_decodeOptions(_env, object, NULL, jvbitmap, joptions);
}

JNIEXPORT jint JNICALL
bitmap_jni_transcodeStream(JNIEnv* _env, jobject object,
                           jobject streamin, jobject streamout,
                           jint maxWidth, jint maxHeight,
                           jint scaleMode, jint quality)
{
  Ychannel *channelin;
  Ychannel *channelout;
  jint rc = -1;
  YmagineFormatOptions *options;

  if (streamin == NULL || streamout == NULL) {
    return rc;
  }

  channelin = YchannelInitJavaInputStream(_env, streamin);
  if (channelin != NULL) {
    channelout = YchannelInitJavaOutputStream(_env, streamout);
    if (channelout != NULL) {
      options = YmagineFormatOptions_Create();
      if (YmagineTranscode(channelin, channelout, options) == YMAGINE_OK) {
        rc = 0;
      }
      YmagineFormatOptions_Release(options);
      YchannelRelease(channelout);
    }
    YchannelRelease(channelin);
  }

  return rc;
}

JNIEXPORT jobject JNICALL
bitmap_jni_copyBitmap(JNIEnv* _env, jobject object,
                      jobject refbitmap, jobject bitmap,
                      jint maxWidth, jint maxHeight,
                      jint scaleMode)
{
  AndroidBitmapInfo  bitmapinfo;
  AndroidBitmapInfo  refbitmapinfo;
  unsigned char *pixels;
  unsigned char *refpixels;
  jobject outbitmap = NULL;
  int ret;

  if (refbitmap == NULL) {
    return NULL;
  }

  ret = AndroidBitmap_getInfo(_env, refbitmap, &refbitmapinfo);
  if (ret < 0) {
    ALOGE("AndroidBitmap_getInfo() failed (code %d)", ret);
    return NULL;
  }

  if (refbitmapinfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
    ALOGE("Bitmap format is not RGBA_8888");
    return NULL;
  }

  if (bitmap == NULL) {
    int reqwidth, reqheight;

    computeBounds(refbitmapinfo.width, refbitmapinfo.height,
                  maxWidth, maxHeight, scaleMode,
                  &reqwidth, &reqheight);
    if (reqwidth == refbitmapinfo.width && reqheight == refbitmapinfo.height) {
      return refbitmap;
    }

    bitmap = createAndroidBitmap(_env, reqwidth, reqheight);
    if (bitmap == NULL) {
      return NULL;
    }
  }

  ret = AndroidBitmap_getInfo(_env, bitmap, &bitmapinfo);
  if (ret < 0) {
    ALOGE("AndroidBitmap_getInfo() failed (code %d)", ret);
    return NULL;
  }

  if (bitmapinfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
    ALOGE("Bitmap format is not RGBA_8888");
    return NULL;
  }

  ret = AndroidBitmap_lockPixels(_env, refbitmap, (void**) &refpixels);
  if (ret < 0) {
    ALOGE("AndroidBitmap_lockPixels() failed (code %d)", ret);
  } else {
    ret = AndroidBitmap_lockPixels(_env, bitmap, (void**) &pixels);
    if (ret < 0) {
      ALOGE("AndroidBitmap_lockPixels() failed (code %d)", ret);
    } else {
      ret = copyBitmap(refpixels, refbitmapinfo.width,
                       refbitmapinfo.height, refbitmapinfo.stride, pixels,
                       bitmapinfo.width, bitmapinfo.height, bitmapinfo.stride,
                       scaleMode);
      AndroidBitmap_unlockPixels(_env, bitmap);
      if (ret > 0) {
        outbitmap = bitmap;
      }
    }
    AndroidBitmap_unlockPixels(_env, refbitmap);
  }

  return outbitmap;
}

/* Shift for little-endian architecture (ARGB) */
#define ASHIFT 24
#define RSHIFT 16
#define GSHIFT 8
#define BSHIFT 0

#define RGBA(r,g,b,a) \
( (((uint32_t) (r)) << RSHIFT) | \
(((uint32_t) (g)) << GSHIFT) | \
(((uint32_t) (b)) << BSHIFT) | \
(((uint32_t) (a)) << ASHIFT) )

JNIEXPORT jintArray JNICALL
bitmap_jni_quantize(JNIEnv * _env, jobject  obj,
                    jobject jbitmap, jint reqColors)
{
  Vbitmap *vbitmap;
  Vcolor colors[16];
  int scores[16];
  jint jcolors[16];
  int ncolors = 0;
  jintArray result = NULL;
  int i;

  if (reqColors > 16) {
    reqColors = 16;
  }

  vbitmap = VbitmapInitAndroid(_env, jbitmap);
  if (vbitmap != NULL) {
    ncolors = quantize(vbitmap, reqColors, colors, scores);
    VbitmapRelease(vbitmap);
  }

  if (ncolors > 0) {
    result = (*_env)->NewIntArray(_env, ncolors);
    if (result != NULL) {
      for (i = 0; i < ncolors; i++) {
	      jcolors[i] = RGBA(colors[i].red,
                          colors[i].green,
                          colors[i].blue,
                          colors[i].alpha);
      }
      (*_env)->SetIntArrayRegion(_env, result, 0, ncolors, jcolors);
    }
  }

  return result;
}

JNIEXPORT jint JNICALL
bitmap_jni_getThemeColor(JNIEnv * _env, jobject  obj,
                         jobject jbitmap)
{
  Vbitmap *vbitmap;
  int color = RGBA(0,0,0,0);

  if (jbitmap != NULL) {
    vbitmap = VbitmapInitAndroid(_env, jbitmap);
    if (vbitmap != NULL) {
      color = getThemeColor(vbitmap);
      VbitmapRelease(vbitmap);
    }
  }

  return (jint) color;
}

JNIEXPORT jint JNICALL
bitmap_jni_blur(JNIEnv * _env, jobject obj,
                jobject jbitmap, jint radius)
{
  Vbitmap *vbitmap;
  jint rc = YMAGINE_ERROR;

  if (jbitmap == NULL) {
    return YMAGINE_OK;
  }
  vbitmap = VbitmapInitAndroid(_env, jbitmap);
  if (vbitmap != NULL) {
    if (Ymagine_blur(vbitmap, radius) == YMAGINE_OK) {
      rc = YMAGINE_OK;
    }
    VbitmapRelease(vbitmap);
  }

  return rc;
}

JNIEXPORT jint JNICALL
bitmap_jni_colorize(JNIEnv * _env, jobject obj,
                    jobject jbitmap, jint color)
{
  Vbitmap *vbitmap;
  jint rc = YMAGINE_ERROR;

  if (jbitmap == NULL) {
    return YMAGINE_OK;
  }
  vbitmap = VbitmapInitAndroid(_env, jbitmap);
  if (vbitmap != NULL) {
    if (Ymagine_colorize(vbitmap, color) == YMAGINE_OK) {
      rc = YMAGINE_OK;
    }
    VbitmapRelease(vbitmap);
  }

  return rc;
}

JNIEXPORT jint JNICALL
bitmap_jni_composeColor(JNIEnv * _env, jobject obj,
                        jobject jbitmap, jint color,
                        jint composeMode)
{
  Vbitmap *vbitmap;
  jint rc = YMAGINE_ERROR;

  if (jbitmap == NULL) {
    return YMAGINE_OK;
  }

  vbitmap = VbitmapInitAndroid(_env, jbitmap);
  if (vbitmap != NULL) {
    if (Ymagine_composeColor(vbitmap, color,
                             (int) composeMode) == YMAGINE_OK) {
      rc = YMAGINE_OK;
    }
    VbitmapRelease(vbitmap);
  }

  return rc;
}

JNIEXPORT jint JNICALL
bitmap_jni_applyShader(JNIEnv * _env, jobject obj,
                       jobject jbitmap, jobject jshader)
{
  Vbitmap *vbitmap;
  jint rc = YMAGINE_ERROR;
  PixelShader *shader = NULL;

  if (jbitmap == NULL || jshader == NULL) {
    return YMAGINE_OK;
  }

  if (jshader != NULL) {
    shader = getPixelShader(_env, jshader);

    if (shader == NULL) {
      return YMAGINE_ERROR;
    }
  }

  vbitmap = VbitmapInitAndroid(_env, jbitmap);
  if (vbitmap != NULL) {
    if (Ymagine_PixelShader_applyOnBitmap(vbitmap, shader) == YMAGINE_OK) {
      rc = YMAGINE_OK;
    }
    VbitmapRelease(vbitmap);
  }

  return rc;
}

/* Dalvik VM type signatures */
static JNINativeMethod bitmap_methods[] = {
  {   "native_decodeFile",
    "(Ljava/lang/String;Landroid/graphics/Bitmap;III)Landroid/graphics/Bitmap;",
    (void*) bitmap_jni_decodeFile
  },
  {   "native_decodeNV21ByteArray",
    "(Landroid/graphics/Bitmap;[BII)Landroid/graphics/Bitmap;",
    (void*) bitmap_jni_decodeNV21ByteArray
  },
  {   "native_decodeStream",
    "(Ljava/io/InputStream;Landroid/graphics/Bitmap;III)Landroid/graphics/Bitmap;",
    (void*) bitmap_jni_decodeStream
  },
  {   "native_copyBitmap",
    "(Landroid/graphics/Bitmap;Landroid/graphics/Bitmap;III)Landroid/graphics/Bitmap;",
    (void*) bitmap_jni_copyBitmap
  },
  {   "native_quantize",
    "(Landroid/graphics/Bitmap;I)[I",
    (void*) bitmap_jni_quantize
  },
  {   "native_getThemeColor",
    "(Landroid/graphics/Bitmap;)I",
    (void*) bitmap_jni_getThemeColor
  },
  {   "native_blur",
    "(Landroid/graphics/Bitmap;I)I",
    (void*) bitmap_jni_blur
  },
  {   "native_colorize",
    "(Landroid/graphics/Bitmap;I)I",
    (void*) bitmap_jni_colorize
  },
  {    "native_compose",
    "(Landroid/graphics/Bitmap;II)I",
    (void*) bitmap_jni_composeColor
  },
  {   "native_transcodeStream",
    "(Ljava/io/InputStream;Ljava/io/OutputStream;IIII)I",
    (void*) bitmap_jni_transcodeStream
  },
  {   "native_applyShader",
    "(Landroid/graphics/Bitmap;Lcom/yahoo/ymagine/Shader;)I",
    (void*) bitmap_jni_applyShader
  },
  {   "native_createOrb",
    "(Landroid/graphics/Bitmap;I)Landroid/graphics/Bitmap;",
    (void*) bitmap_jni_createOrb
  },
  {   "native_renderOrbTile",
    "(Landroid/graphics/Bitmap;IILjava/io/InputStream;)I",
    (void*) bitmap_jni_renderOrbTile
  }
};

int register_BitmapFactory(JNIEnv *_env, const char *classPathName)
{
  int rc;
  char buf[256];
  JNINativeMethod options_methods[1];
  jclass clazz;
  int l;

  if (classPathName == NULL) {
    return JNI_FALSE;
  }

  l = strlen(classPathName);
  if (l > 128) {
    return JNI_FALSE;
  }

  rc = bitmapfactory_init(_env);
  if (rc <= 0) {
    return JNI_FALSE;
  }

  rc = jniutils_registerNativeMethods(_env, classPathName,
                                      bitmap_methods, NELEM(bitmap_methods));
  if (rc != JNI_TRUE) {
    return JNI_FALSE;
  }

  snprintf(buf, sizeof(buf),
           "(Ljava/io/InputStream;L%s$Options;)Landroid/graphics/Bitmap;", classPathName);

  options_methods[0].name = "native_decodeStreamOptions";
  options_methods[0].signature = buf;
  options_methods[0].fnPtr = (void*) bitmap_jni_decodeStreamOptions;

  rc = jniutils_registerNativeMethods(_env, classPathName,
                                      options_methods, 1);
  if (rc != JNI_TRUE) {
    return JNI_FALSE;
  }

  snprintf(buf, sizeof(buf),
           "(Lcom/yahoo/ymagine/Vbitmap;L%s$Options;)Landroid/graphics/Bitmap;", classPathName);

  options_methods[0].name = "native_decodeVbitmapOptions";
  options_methods[0].signature = buf;
  options_methods[0].fnPtr = (void*) bitmap_jni_decodeVbitmapOptions;

  rc = jniutils_registerNativeMethods(_env, classPathName,
                                      options_methods, 1);
  if (rc != JNI_TRUE) {
    return JNI_FALSE;
  }

  snprintf(buf, sizeof(buf), "%s$Options", classPathName);
  clazz = (*_env)->FindClass(_env, buf);
  if (clazz == NULL) {
    ALOGE("Can't find %s", buf);
    return JNI_FALSE;
  }

  /* keep a global reference to Option clazz so that fielId is alreadys valid */
  gOptions_clazz = (*_env)->NewGlobalRef(_env, clazz);

  /* Standard BitmapFactory.Options fields */
  gOptions_justBounds = (*_env)->GetFieldID(_env, clazz, "inJustDecodeBounds", "Z");
  gOptions_sampleSize = (*_env)->GetFieldID(_env, clazz, "inSampleSize", "I");
  gOptions_config = (*_env)->GetFieldID(_env, clazz, "inPreferredConfig", "Landroid/graphics/Bitmap$Config;");
  gOptions_dither = (*_env)->GetFieldID(_env, clazz, "inDither", "Z");
  gOptions_purgeable = (*_env)->GetFieldID(_env, clazz, "inPurgeable", "Z");
  gOptions_shareable = (*_env)->GetFieldID(_env, clazz, "inInputShareable", "Z");
  gOptions_scaled = (*_env)->GetFieldID(_env, clazz, "inScaled", "Z");
  gOptions_density = (*_env)->GetFieldID(_env, clazz, "inDensity", "I");
  gOptions_screenDensity = (*_env)->GetFieldID(_env, clazz, "inScreenDensity", "I");
  gOptions_targetDensity = (*_env)->GetFieldID(_env, clazz, "inTargetDensity", "I");
  gOptions_width = (*_env)->GetFieldID(_env, clazz, "outWidth", "I");
  gOptions_height = (*_env)->GetFieldID(_env, clazz, "outHeight", "I");
  gOptions_mime = (*_env)->GetFieldID(_env, clazz, "outMimeType", "Ljava/lang/String;");
  gOptions_mCancel = (*_env)->GetFieldID(_env, clazz, "mCancel", "Z");

#if 0
  /* this code block is commented out because the variables from
     BitmapFactory.Options required higher API level, with current code
     app will crash if compiled targeting API lower than their API level.
     Commented out for now as a temporary fix.
     TODO: fix this in a better way. */
  gOptions_premultiplied = (*_env)->GetFieldID(_env, clazz, "inPremultiplied", "Z");  // API 19
  gOptions_mutable = (*_env)->GetFieldID(_env, clazz, "inMutable", "Z");  // API 11
  gOptions_preferQualityOverSpeed = (*_env)->GetFieldID(_env, clazz, "inPreferQualityOverSpeed", "Z");  // API 10
#endif

  gOptions_inMaxWidth = (*_env)->GetFieldID(_env, clazz, "inMaxWidth", "I");
  gOptions_inMaxHeight = (*_env)->GetFieldID(_env, clazz, "inMaxHeight", "I");
  gOptions_inKeepRatio = (*_env)->GetFieldID(_env, clazz, "inKeepRatio", "Z");
  gOptions_inCrop = (*_env)->GetFieldID(_env, clazz, "inCrop", "Z");
  gOptions_inFit = (*_env)->GetFieldID(_env, clazz, "inFit", "Z");
  gOptions_inNative = (*_env)->GetFieldID(_env, clazz, "inNative", "Z");
  gOptions_inStream = (*_env)->GetFieldID(_env, clazz, "inStream", "Ljava/io/OutputStream;");
  gOptions_inBitmap = (*_env)->GetFieldID(_env, clazz, "inBitmap", "Landroid/graphics/Bitmap;");
  gOptions_inBlur = (*_env)->GetFieldID(_env, clazz, "inBlur", "F");
  gOptions_inShader = (*_env)->GetFieldID(_env, clazz, "inShader", "Lcom/yahoo/ymagine/Shader;");
  gOptions_inQuality = (*_env)->GetFieldID(_env, clazz, "inQuality", "I");

  gOptions_outPanoMode = (*_env)->GetFieldID(_env, clazz, "outPanoMode", "I");
  gOptions_outPanoCroppedWidth = (*_env)->GetFieldID(_env, clazz, "outPanoCroppedWidth", "I");
  gOptions_outPanoCroppedHeight = (*_env)->GetFieldID(_env, clazz, "outPanoCroppedHeight", "I");
  gOptions_outPanoFullWidth = (*_env)->GetFieldID(_env, clazz, "outPanoFullWidth", "I");
  gOptions_outPanoFullHeight = (*_env)->GetFieldID(_env, clazz, "outPanoFullHeight", "I");
  gOptions_outPanoX = (*_env)->GetFieldID(_env, clazz, "outPanoX", "I");
  gOptions_outPanoY = (*_env)->GetFieldID(_env, clazz, "outPanoY", "I");
  gOptions_outOrientation = (*_env)->GetFieldID(_env, clazz, "outOrientation", "I");

  return JNI_TRUE;
}
#endif /* HAVE_BITMAPFACTORY */
