#include "LAppPal.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cerrno>
#include <sys/stat.h>
#include <sys/mman.h>
#include <ctime>
#include <iostream>
#include <fstream>
#include <malloc.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <Model/CubismMoc.hpp>
#include "LAppDefine.hpp"
#include "JniBridgeC.hpp"

using namespace Csm;
using namespace std;
using namespace LAppDefine;

double LAppPal::s_currentFrame = 0.0;
double LAppPal::s_lastFrame = 0.0;
double LAppPal::s_deltaTime = 0.0;

static const size_t CUBISM_MOC_ALIGNMENT = 64;  // csmAlignofMoc from Live2DCubismCore.h
static const uintptr_t MMAP_MAGIC = 0xDEAD10CC;  // sentinel to detect mmap'd buffers

csmByte* LAppPal::LoadFileAsBytes(const string filePath, csmSizeInt* outSize)
{
    // Absolute paths: read directly via C stdio, skip JNI round-trip.
    // Imported model files always use absolute paths.
    if (!filePath.empty() && filePath[0] == '/')
    {
        FILE* f = fopen(filePath.c_str(), "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            long const size = ftell(f);
            fseek(f, 0, SEEK_SET);
            // Use mmap for file buffers to avoid heap corruption from large memalign().
            // Bionic malloc metadata can be corrupted by memalign() for multi-MB buffers,
            // causing subsequent malloc() to SIGSEGV.
            // Layout (deterministic, no alignment padding needed):
            //   mmap_buf → [alloc_size][magic][buf...]
            // alloc_size is LCM(page_size, alignment), so (alloc_size - header) is
            // alignment-aligned, placing buf exactly at mmap_buf + header.
            const long page_size = sysconf(_SC_PAGESIZE);
            // Header must be alignment-aligned so buf = mmap_buf + header is 64-byte aligned.
            // Stores: [alloc_size (8)] [magic (8)] [padding (48)] = 64 bytes
            const size_t header = CUBISM_MOC_ALIGNMENT;
            const size_t alloc_size = ((header + size + page_size - 1) / page_size) * page_size;
            void* mmap_buf = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mmap_buf == MAP_FAILED)
            {
                PrintLogLn("[IO]ERROR: mmap failed for %s size=%ld errno=%d", filePath.c_str(), size, errno);
                fclose(f);
                *outSize = 0;
                return nullptr;
            }
            *static_cast<size_t*>(mmap_buf) = alloc_size;
            *(reinterpret_cast<size_t*>(mmap_buf) + 1) = MMAP_MAGIC;
            auto* buf = reinterpret_cast<csmByte*>(mmap_buf) + header;

            size_t const read = fread(buf, 1, size, f);
            fclose(f);
            *outSize = static_cast<csmSizeInt>(read);
            if (read != static_cast<size_t>(size))
            {
                PrintLogLn("[IO]WARNING: partial read %s expected=%ld actual=%zu", filePath.c_str(), size, read);
            }
            return buf;
        }
        else
        {
            PrintLogLn("[IO]native read FAIL (fopen returned NULL): %s errno=%d", filePath.c_str(), errno);
        }
    }

    // Relative paths (assets): fall back to JNI (already aligned by JVM)
    const char* path = filePath.c_str();
    char* buf = JniBridgeC::LoadFileAsBytesFromJava(path, outSize);
    return reinterpret_cast<csmByte*>(buf);
}

void LAppPal::ReleaseBytes(csmByte* byteData)
{
    if (!byteData) return;
    // Detect mmap'd buffers via magic sentinel. The mmap base is page-aligned;
    // round down to find it, then verify magic + alloc_size sanity.
    const long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t page_base = reinterpret_cast<uintptr_t>(byteData) & ~(static_cast<uintptr_t>(page_size) - 1);
    size_t magic = *reinterpret_cast<size_t*>(page_base + sizeof(size_t));
    size_t alloc_size = *reinterpret_cast<size_t*>(page_base);
    if (magic == MMAP_MAGIC && alloc_size > 0 &&
        alloc_size < 256 * 1024 * 1024 &&
        alloc_size % page_size == 0)
    {
        munmap(reinterpret_cast<void*>(page_base), alloc_size);
        return;
    }
    free(byteData);  // JNI path: use free()
}

csmByte* LAppPal::CreateBuffer(const char* path, csmSizeInt* outSize)
{
    return CubismFramework::GetLoadFileFunction()(std::string(path), outSize);
}

void LAppPal::DeleteBuffer(csmByte* buffer, const char* path)
{
    (void)path;
    CubismFramework::GetReleaseBytesFunction()(buffer);
}

csmFloat32 LAppPal::GetDeltaTime()
{
    return static_cast<csmFloat32>(s_deltaTime);
}

void LAppPal::UpdateTime()
{
    s_currentFrame = GetSystemTime();
    s_deltaTime = s_currentFrame - s_lastFrame;
    s_lastFrame = s_currentFrame;
}

void LAppPal::PrintLogLn(const csmChar* format, ...)
{
    va_list args;
    va_start(args, format);
    __android_log_vprint(ANDROID_LOG_DEBUG, "DeskPet", format, args);
    va_end(args);
}

void LAppPal::PrintMessageLn(const csmChar* message)
{
    PrintLogLn("%s", message);
}

double LAppPal::GetSystemTime()
{
    struct timespec res;
    clock_gettime(CLOCK_MONOTONIC, &res);
    return (res.tv_sec + res.tv_nsec * 1e-9);
}
