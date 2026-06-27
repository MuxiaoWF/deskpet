#pragma once

#include <CubismFramework.hpp>
#include <GLES2/gl2.h>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>

/// Parsed data from a Cubism 2 .moc binary file.
struct Cubism2ParamInfo {
    std::string id;
    Csm::csmFloat32 defaultValue;
    Csm::csmFloat32 minValue;
    Csm::csmFloat32 maxValue;
};

enum class Cubism2BlendMode { BlendNormal = 0, BlendAdd = 1, BlendMultiply = 2, BlendScreen = 3 };

struct Cubism2DrawableInfo {
    std::string id;
    Csm::csmInt32 textureIndex;
    std::vector<Csm::csmFloat32> u;     // UV coords
    std::vector<Csm::csmFloat32> v;
    std::vector<unsigned short> indices;
    Csm::csmInt32 vertexCount;
    Csm::csmFloat32 opacity;
    Csm::csmInt32 drawOrder;
    Cubism2BlendMode blendMode = Cubism2BlendMode::BlendNormal;
    Csm::csmFloat32 multiplyColor[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    Csm::csmFloat32 screenColor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
};

/// Per-parameter pivot table for keyform interpolation.
struct Cubism2ParamPivots {
    std::string paramId;
    Csm::csmInt32 pivotCount;
    std::vector<Csm::csmFloat32> pivotValues;
};

/// Mask group definition for Cubism 2 clipping (external, since .moc has no mask data).
struct Cubism2MaskGroup {
    Csm::csmInt32 maskDrawableIndex;     // the drawable that acts as mask
    std::vector<Csm::csmInt32> targets;  // drawables clipped by this mask
    int channelIndex = 0;                // 0=R, 1=G, 2=B, 3=A
};

/// Manages multi-dimensional pivot tables for deformation interpolation.
struct Cubism2PivotManager {
    std::vector<Cubism2ParamPivots> paramPivots;

    /// Get total number of keyforms (product of all pivot counts).
    Csm::csmInt32 GetKeyformCount() const;

    /// Find the lower pivot index and interpolation weight for a parameter value.
    void FindAxis(Csm::csmFloat32 paramValue, Csm::csmInt32 axis,
                  Csm::csmInt32& outIndex, Csm::csmFloat32& outWeight) const;
};

/// Base deformation data (grid-based deformation from the parts section).
struct Cubism2BaseData {
    std::string id;
    Csm::csmInt32 deformType;      // 0 = affine, 1 = grid
    Csm::csmFloat32 pivotOpacity;

    // Grid deformation (type 1)
    Csm::csmInt32 col;
    Csm::csmInt32 row;
    Cubism2PivotManager pivotManager;
    std::vector<Csm::csmFloat32> deformPoints; // flat: keyformCount * gridPoints * 2
};

struct Cubism2MocData {
    Csm::csmInt32 paramCount;
    std::vector<Cubism2ParamInfo> params;
    Csm::csmInt32 drawableCount;
    std::vector<Cubism2DrawableInfo> drawables;
    std::vector<Cubism2BaseData> baseData;  // deformation data from parts section
};

/// Loads and renders Cubism 2 .moc binary files.
class Cubism2MocLoader {
public:
    Cubism2MocLoader();
    ~Cubism2MocLoader();

    /// Load a .moc binary from buffer. Returns true on success.
    bool LoadFromBuffer(const Csm::csmByte* buffer, Csm::csmSizeInt size);

    /// Set a parameter value by name.
    void SetParameterValue(const char* paramId, Csm::csmFloat32 value);

    /// Get current parameter value.
    Csm::csmFloat32 GetParameterValue(const char* paramId) const;

    /// Get all parameter info.
    const Cubism2MocData& GetMocData() const { return _data; }

    /// Set the texture ID for a given texture index.
    void SetTexture(Csm::csmInt32 index, GLuint textureId);

    /// Set multiply color for a drawable (RGBA, default 1,1,1,1).
    void SetDrawableMultiplyColor(int idx, Csm::csmFloat32 r, Csm::csmFloat32 g, Csm::csmFloat32 b, Csm::csmFloat32 a);

    /// Set screen color for a drawable (RGBA, default 0,0,0,0).
    void SetDrawableScreenColor(int idx, Csm::csmFloat32 r, Csm::csmFloat32 g, Csm::csmFloat32 b, Csm::csmFloat32 a);

    /// Set blend mode for a drawable.
    void SetDrawableBlendMode(int idx, Cubism2BlendMode mode);

    /// Set mask groups for clipping (e.g., mouth mask, eye mask).
    /// Must be called before first Draw(). Mask data is external since .moc has no mask info.
    void SetMaskGroups(const std::vector<Cubism2MaskGroup>& groups);

    /// Check if mask system is active.
    bool HasMasks() const { return !_maskGroups.empty(); }

    /// Update vertex positions based on current parameter values.
    void Update();

    /// Draw all drawables.
    void Draw(const Csm::csmFloat32* mvpMatrix);

    /// Get canvas size.
    Csm::csmFloat32 GetCanvasWidth() const { return _canvasWidth; }
    Csm::csmFloat32 GetCanvasHeight() const { return _canvasHeight; }
    Csm::csmFloat32 GetCanvasMinX() const { return _canvasMinX; }
    Csm::csmFloat32 GetCanvasMaxX() const { return _canvasMaxX; }
    Csm::csmFloat32 GetCanvasMinY() const { return _canvasMinY; }
    Csm::csmFloat32 GetCanvasMaxY() const { return _canvasMaxY; }

    /// Set opacity for a specific drawable (used by motion opacity/part targets).
    void SetDrawableOpacity(Csm::csmInt32 drawableIndex, Csm::csmFloat32 opacity);

    /// Set opacity for a drawable by its string ID (for Group ids part toggle).
    void SetDrawableOpacityById(const char* drawableId, Csm::csmFloat32 opacity);

    /// Get drawable index by ID. Returns -1 if not found.
    Csm::csmInt32 FindDrawableIndex(const char* drawableId) const;

    /// Set global depth offset for render order biasing.
    void SetDepthOffset(Csm::csmFloat32 offset) { _depthOffset = offset; }

    /// Recreate GL resources (shader, VBOs) after GL context loss/recreation.
    void ReloadRenderer();

private:
    bool ParseMocHeader(const Csm::csmByte* buffer, Csm::csmSizeInt size);
    bool ParseMocBody(const Csm::csmByte* buffer, Csm::csmSizeInt size);
    bool ParsePartsSection(const Csm::csmByte* buffer, Csm::csmSizeInt size,
                           Csm::csmInt32 partsOffset, Csm::csmInt32 drawOffset);

    // Vertex deformation
    void InterpolateDeformGrid(Csm::csmInt32 baseIdx);
    void ApplyDeformation(Csm::csmInt32 drawableIdx);

    Cubism2MocData _data;
    std::map<std::string, Csm::csmFloat32> _paramValues{};
    std::map<Csm::csmInt32, GLuint> _textures{};

    // Per-drawable deformed grid points (col+1)*(row+1)*2 floats
    std::vector<std::vector<Csm::csmFloat32>> _deformedGridPoints;

    // Vertex buffers for each drawable (dual-buffered, borrowed from Live2DViewer)
    struct DrawableBuffer {
        GLuint vbo[2]{};      // Double-buffered VBO
        GLuint uvbo;        // UV buffer
        GLuint ibo;
        std::vector<Csm::csmFloat32> positions[2];  // Double-buffered positions
        std::vector<Csm::csmFloat32> uvs;           // UV coordinates (interleaved u,v)
        Csm::csmInt32 indexCount;
        Csm::csmInt32 currentBuffer;  // Current write buffer index (0 or 1)
        bool dirty;              // Dirty flag - only re-upload when changed
        DrawableBuffer() : ibo(0), uvbo(0), indexCount(0), currentBuffer(0), dirty(true) {
            vbo[0] = vbo[1] = 0;
        }
    };
    std::vector<DrawableBuffer> _buffers;
    bool _buffersInitialized;
    std::vector<Csm::csmInt32> _sortedDrawableIndices;  // cached draw order (static after load)

    // Shader program for Cubism 2 rendering
    GLuint _shaderProgram;
    GLint _uniformMvp;
    GLint _uniformTexture;
    GLint _attribPosition;
    GLint _attribTexCoord;
    GLint _uniformOpacity;
    GLint _uniformMultiplyColor = -1;
    GLint _uniformScreenColor = -1;
    void InitShader();

    Csm::csmFloat32 _canvasWidth{};
    Csm::csmFloat32 _canvasHeight{};
    Csm::csmFloat32 _canvasMinX{};
    Csm::csmFloat32 _canvasMinY{};
    Csm::csmFloat32 _canvasMaxX{};
    Csm::csmFloat32 _canvasMaxY{};
    Csm::csmFloat32 _depthOffset = 0.0F;
    bool _loaded;

    // Mask/clipping system (FBO-based, RGBA channel packing)
    std::vector<Cubism2MaskGroup> _maskGroups;
    GLuint _maskFBO = 0;
    GLuint _maskTexture = 0;
    Csm::csmInt32 _maskWidth = 512;
    Csm::csmInt32 _maskHeight = 512;
    GLuint _maskShaderProgram = 0;
    GLint _maskUniformMvp = -1;
    GLuint _maskedShaderProgram = 0;
    GLint _maskedUniformMvp = -1;
    GLint _maskedUniformTexture = -1;
    GLint _maskedUniformOpacity = -1;
    GLint _maskedUniformMultiplyColor = -1;
    GLint _maskedUniformScreenColor = -1;
    GLint _maskedUniformMaskTexture = -1;
    GLint _maskedUniformChannelFlag = -1;
    GLint _maskedAttribPosition = -1;
    GLint _maskedAttribTexCoord = -1;
    void InitMaskShader();
    void EnsureMaskFBO();
    void RenderMasks(const Csm::csmFloat32* mvpMatrix);
    bool IsDrawableMasked(Csm::csmInt32 drawableIndex, int& outChannelIndex) const;
    std::unordered_map<Csm::csmInt32, int> _maskLookup;  // drawable index → channel index
};
