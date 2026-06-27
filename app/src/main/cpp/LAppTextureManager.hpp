#pragma once

#include <string>
#include <GLES2/gl2.h>
#include "LAppTextureManager_Common.hpp"

/// Loads PNG textures via stb_image and manages GL texture lifecycle.
class LAppTextureManager : public LAppTextureManager_Common
{
public:
    LAppTextureManager();
    ~LAppTextureManager();

    TextureInfo* CreateTextureFromPngFile(std::string fileName);
    void ReleaseTextures();
    void ReleaseInvalidTextures();
};
