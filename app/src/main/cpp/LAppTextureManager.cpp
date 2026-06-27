#include "LAppTextureManager.hpp"
#include <GLES2/gl2.h>
#include <jni.h>
#include "LAppPal.hpp"
#include "JniBridgeC.hpp"

LAppTextureManager::LAppTextureManager() : LAppTextureManager_Common()
{
}

LAppTextureManager::~LAppTextureManager()
{
    ReleaseTextures();
}

/// Decode image bytes via Android BitmapFactory, returns newly allocated RGBA8888 pixels.
static unsigned char* DecodeBitmap(const unsigned char* data, int dataSize,
                                    int* outWidth, int* outHeight)
{
    JNIEnv* env = nullptr;
    JavaVM* jvm = JniBridgeC::GetJVM();
    if (!jvm || jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return nullptr;
    }

    // BitmapFactory.Options opts = new BitmapFactory.Options();
    // opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
    jclass optCls = env->FindClass("android/graphics/BitmapFactory$Options");
    jmethodID optInit = env->GetMethodID(optCls, "<init>", "()V");
    jobject opts = env->NewObject(optCls, optInit);
    jclass cfgCls = env->FindClass("android/graphics/Bitmap$Config");
    jfieldID argbField = env->GetStaticFieldID(cfgCls, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
    jobject argb = env->GetStaticObjectField(cfgCls, argbField);
    jfieldID cfgField = env->GetFieldID(optCls, "inPreferredConfig", "Landroid/graphics/Bitmap$Config;");
    env->SetObjectField(opts, cfgField, argb);

    // Bitmap bmp = BitmapFactory.decodeByteArray(data, 0, len, opts);
    jclass facCls = env->FindClass("android/graphics/BitmapFactory");
    jmethodID decode = env->GetStaticMethodID(facCls, "decodeByteArray",
        "([BIILandroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;");
    jbyteArray arr = env->NewByteArray(dataSize);
    env->SetByteArrayRegion(arr, 0, dataSize, reinterpret_cast<const jbyte*>(data));
    jobject bmp = env->CallStaticObjectMethod(facCls, decode, arr, 0, dataSize, opts);
    if (!bmp) {
        env->DeleteLocalRef(arr);
        env->DeleteLocalRef(opts);
        return nullptr;
    }

    // int w = bmp.getWidth(); int h = bmp.getHeight();
    jclass bmpCls = env->GetObjectClass(bmp);
    *outWidth  = env->CallIntMethod(bmp, env->GetMethodID(bmpCls, "getWidth",  "()I"));
    *outHeight = env->CallIntMethod(bmp, env->GetMethodID(bmpCls, "getHeight", "()I"));

    // ByteBuffer buf = ByteBuffer.allocate(w * h * 4);
    jclass bbCls = env->FindClass("java/nio/ByteBuffer");
    jobject buf = env->CallStaticObjectMethod(bbCls,
        env->GetStaticMethodID(bbCls, "allocate", "(I)Ljava/nio/ByteBuffer;"),
        (*outWidth) * (*outHeight) * 4);

    // bmp.copyPixelsToBuffer(buf);
    env->CallVoidMethod(bmp,
        env->GetMethodID(bmpCls, "copyPixelsToBuffer", "(Ljava/nio/Buffer;)V"), buf);

    // byte[] px = buf.array();
    jbyteArray px = (jbyteArray)env->CallObjectMethod(buf,
        env->GetMethodID(bbCls, "array", "()[B"));
    jsize len = env->GetArrayLength(px);
    auto* pixels = new unsigned char[len];
    env->GetByteArrayRegion(px, 0, len, reinterpret_cast<jbyte*>(pixels));

    env->DeleteLocalRef(px);
    env->DeleteLocalRef(buf);
    env->DeleteLocalRef(bmp);
    env->DeleteLocalRef(arr);
    env->DeleteLocalRef(opts);
    return pixels;
}

LAppTextureManager::TextureInfo* LAppTextureManager::CreateTextureFromPngFile(std::string fileName)
{
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        if (_texturesInfo[i]->fileName == fileName)
        {
            return _texturesInfo[i];
        }
    }

    Csm::csmSizeInt fileSize = 0;
    auto* address = LAppPal::LoadFileAsBytes(fileName, &fileSize);
    if (address == nullptr || fileSize == 0)
    {
        LAppPal::PrintLogLn("[TEX]ERROR: LoadFileAsBytes returned NULL for: %s", fileName.c_str());
        return nullptr;
    }

    int width = 0, height = 0;
    unsigned char* rgba = DecodeBitmap(address, static_cast<int>(fileSize), &width, &height);
    LAppPal::ReleaseBytes(address);

    if (rgba == nullptr)
    {
        LAppPal::PrintLogLn("[TEX]ERROR: DecodeBitmap failed for: %s", fileName.c_str());
        return nullptr;
    }

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    delete[] rgba;

    auto* textureInfo = new LAppTextureManager::TextureInfo();
    if (textureInfo != nullptr)
    {
        textureInfo->fileName = fileName;
        textureInfo->width = width;
        textureInfo->height = height;
        textureInfo->id = textureId;
        _texturesInfo.PushBack(textureInfo);
    }

    return textureInfo;
}

void LAppTextureManager::ReleaseTextures()
{
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        glDeleteTextures(1, &(_texturesInfo[i]->id));
    }
    ReleaseTexturesInfo();
}

void LAppTextureManager::ReleaseInvalidTextures()
{
    ReleaseTextures();
}
