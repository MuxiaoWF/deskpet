#pragma once

#include <CubismFramework.hpp>
#include <string>

class LAppPal
{
public:
    static Csm::csmByte* LoadFileAsBytes(const std::string filePath, Csm::csmSizeInt* outSize);
    static void ReleaseBytes(Csm::csmByte* byteData);
    static Csm::csmByte* CreateBuffer(const char* path, Csm::csmSizeInt* outSize);
    static void DeleteBuffer(Csm::csmByte* buffer, const char* path);
    static Csm::csmFloat32 GetDeltaTime();
    static void UpdateTime();
    static void PrintLogLn(const Csm::csmChar* format, ...);
    static void PrintMessageLn(const Csm::csmChar* message);

private:
    static double GetSystemTime();

    static double s_currentFrame;
    static double s_lastFrame;
    static double s_deltaTime;
};
