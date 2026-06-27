#include "Cubism2MocLoader.hpp"
#include "Cubism2ParamBridge.hpp"
#include "LAppPal.hpp"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

using namespace Csm;

// Cubism 2 .moc binary format constants
static const Csm::csmInt32 MOC2_HEADER_SIZE = 4 + 4; // magic + version

// --- Cubism2PivotManager ---

Csm::csmInt32 Cubism2PivotManager::GetKeyformCount() const
{
    if (paramPivots.empty()) return 0;
    Csm::csmInt32 count = 1;
    for (const auto& pp : paramPivots) {
        count *= pp.pivotCount;
    }
    return count;
}

void Cubism2PivotManager::FindAxis(Csm::csmFloat32 paramValue, Csm::csmInt32 axis,
                                    Csm::csmInt32& outIndex, Csm::csmFloat32& outWeight) const
{
    if (axis < 0 || axis >= static_cast<Csm::csmInt32>(paramPivots.size())) {
        outIndex = 0;
        outWeight = 0.0F;
        return;
    }
    const auto& pivots = paramPivots[axis].pivotValues;
    if (pivots.empty()) {
        outIndex = 0;
        outWeight = 0.0F;
        return;
    }

    // Clamp to pivot range
    if (paramValue <= pivots.front()) {
        outIndex = 0;
        outWeight = 0.0F;
        return;
    }
    if (paramValue >= pivots.back()) {
        outIndex = static_cast<Csm::csmInt32>(pivots.size()) - 2;
        outWeight = 1.0F;
        return;
    }

    // Find interval
    for (Csm::csmInt32 i = 0; i < static_cast<Csm::csmInt32>(pivots.size()) - 1; i++) {
        if (paramValue >= pivots[i] && paramValue <= pivots[i + 1]) {
            outIndex = i;
            Csm::csmFloat32 range = pivots[i + 1] - pivots[i];
            outWeight = (range > 0.0F) ? (paramValue - pivots[i]) / range : 0.0F;
            return;
        }
    }

    outIndex = 0;
    outWeight = 0.0F;
}

// --- Cubism2MocLoader ---

Cubism2MocLoader::Cubism2MocLoader()
    : _buffersInitialized(false)
    , _canvasWidth(1.0F)
    , _canvasHeight(1.0F)
    , _canvasMinX(0.0F)
    , _canvasMinY(0.0F)
    , _canvasMaxX(0.0F)
    , _canvasMaxY(0.0F)
    , _loaded(false)
    , _shaderProgram(0)
    , _uniformMvp(-1)
    , _uniformTexture(-1)
    , _attribPosition(-1)
    , _attribTexCoord(-1)
    , _uniformOpacity(-1)
{
}

Cubism2MocLoader::~Cubism2MocLoader()
{
    for (auto& buf : _buffers)
    {
        if(buf.vbo[0]) glDeleteBuffers(1, &buf.vbo[0]);
        if(buf.vbo[1]) glDeleteBuffers(1, &buf.vbo[1]);
        if(buf.uvbo) glDeleteBuffers(1, &buf.uvbo);
        if(buf.ibo) glDeleteBuffers(1, &buf.ibo);
    }
    if(_shaderProgram) glDeleteProgram(_shaderProgram);
}

bool Cubism2MocLoader::LoadFromBuffer(const Csm::csmByte* buffer, Csm::csmSizeInt size)
{
    if (buffer == nullptr || size < MOC2_HEADER_SIZE)
    {
        LAppPal::PrintLogLn("[Cubism2] Invalid moc buffer (size=%d)", size);
        return false;
    }

    if (!ParseMocHeader(buffer, size))
    {
        return false;
    }

    if (!ParseMocBody(buffer, size))
    {
        return false;
    }

    // Initialize parameter values to defaults
    for (const auto& p : _data.params)
    {
        _paramValues[p.id] = p.defaultValue;
    }

    _loaded = true;
    LAppPal::PrintLogLn("[Cubism2] Moc loaded: %d params, %d drawables, %d base deformations",
        _data.paramCount, _data.drawableCount, static_cast<int>(_data.baseData.size()));
    return true;
}

bool Cubism2MocLoader::ParseMocHeader(const Csm::csmByte* buffer, Csm::csmSizeInt size)
{
    (void)size;
    // Verify magic bytes
    if (buffer[0] != 'm' || buffer[1] != 'o' || buffer[2] != 'c')
    {
        LAppPal::PrintLogLn("[Cubism2] Invalid moc magic: %c%c%c", buffer[0], buffer[1], buffer[2]);
        return false;
    }

    // Version byte (offset 3): 0 for Cubism 2

    return true;
}

bool Cubism2MocLoader::ParsePartsSection(const Csm::csmByte* buffer, Csm::csmSizeInt size,
                                          Csm::csmInt32 partsOffset, Csm::csmInt32 drawOffset)
{
    // Cubism 2 .moc parts section layout:
    //   [4 bytes] partCount
    //   For each part:
    //     [4 bytes] partID string offset
    //     [4 bytes] baseDataCount
    //     For each baseData:
    //       [4 bytes] baseDataID string offset
    //       [4 bytes] targetBaseDataID string offset (-1 if none)
    //       [4 bytes] deformType (0=affine, 1=grid)
    //       If type==1 (grid):
    //         [4 bytes] col
    //         [4 bytes] row
    //         [4 bytes] paramCount
    //         For each param:
    //           [4 bytes] paramID string offset
    //           [4 bytes] pivotCount
    //           [pivotCount * 4 bytes] pivotValues (floats)
    //         [keyformCount * gridPoints * 8 bytes] deform points (x,y float pairs)
    //         keyformCount = product of all pivotCounts
    //         gridPoints = (col+1) * (row+1)

    auto readI32 = [](const Csm::csmByte* ptr) -> Csm::csmInt32 {
        return (Csm::csmInt32)(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24));
    };
    auto readF = [](const Csm::csmByte* ptr) -> Csm::csmFloat32 {
        Csm::csmFloat32 v; memcpy(&v, ptr, 4); return v;
    };
    auto readStr = [&](Csm::csmInt32 offset) -> std::string {
        if (offset >= 0 && offset < size) {
            return std::string(reinterpret_cast<const char*>(buffer + offset));
        }
        return std::string();
    };

    if (partsOffset <= 0 || partsOffset >= size - 4) {
        LAppPal::PrintLogLn("[Cubism2] Parts section offset invalid: %d", partsOffset);
        return false;
    }

    const Csm::csmByte* ps = buffer + partsOffset;
    Csm::csmInt32 partCount = readI32(ps); ps += 4;
    if (partCount <= 0 || partCount > 1000) {
        LAppPal::PrintLogLn("[Cubism2] Parts count invalid: %d", partCount);
        return false;
    }

    for (Csm::csmInt32 p = 0; p < partCount; p++) {
        if (ps - buffer + 8 > size) break;

        Csm::csmInt32 partIdOffset = readI32(ps); ps += 4;
        Csm::csmInt32 baseDataCount = readI32(ps); ps += 4;
        (void)partIdOffset; // part ID not needed for deformation

        if (baseDataCount < 0 || baseDataCount > 1000) break;

        for (Csm::csmInt32 b = 0; b < baseDataCount; b++) {
            if (ps - buffer + 12 > size) break;

            Csm::csmInt32 bdIdOffset = readI32(ps); ps += 4;
            Csm::csmInt32 targetIdOffset = readI32(ps); ps += 4;
            Csm::csmInt32 deformType = readI32(ps); ps += 4;

            Cubism2BaseData bd;
            bd.id = readStr(bdIdOffset);
            bd.deformType = deformType;
            bd.pivotOpacity = 1.0F;

            if (deformType == 1) {
                // Grid deformation
                if (ps - buffer + 8 > size) break;
                bd.col = readI32(ps); ps += 4;
                bd.row = readI32(ps); ps += 4;

                if (bd.col < 1) bd.col = 1;
                if (bd.row < 1) bd.row = 1;
                if (bd.col > 100 || bd.row > 100) break;

                // Read parameter pivot tables
                if (ps - buffer + 4 > size) break;
                Csm::csmInt32 paramCount = readI32(ps); ps += 4;
                if (paramCount < 0 || paramCount > 10) break;

                bd.pivotManager.paramPivots.resize(paramCount);
                for (Csm::csmInt32 pi = 0; pi < paramCount; pi++) {
                    if (ps - buffer + 8 > size) break;
                    Csm::csmInt32 paramIdOff = readI32(ps); ps += 4;
                    Csm::csmInt32 pivotCount = readI32(ps); ps += 4;

                    bd.pivotManager.paramPivots[pi].paramId = readStr(paramIdOff);
                    bd.pivotManager.paramPivots[pi].pivotCount = pivotCount;

                    if (pivotCount < 0 || pivotCount > 100) break;
                    bd.pivotManager.paramPivots[pi].pivotValues.resize(pivotCount);
                    for (Csm::csmInt32 pv = 0; pv < pivotCount; pv++) {
                        if (ps - buffer + 4 > size) break;
                        bd.pivotManager.paramPivots[pi].pivotValues[pv] = readF(ps); ps += 4;
                    }
                }

                // Read deformation grid points
                Csm::csmInt32 keyformCount = bd.pivotManager.GetKeyformCount();
                Csm::csmInt32 gridPoints = (bd.col + 1) * (bd.row + 1);
                // Use 64-bit to detect overflow before narrowing to csmInt32
                Csm::csmInt64 totalFloats64 = static_cast<Csm::csmInt64>(keyformCount) * gridPoints * 2;

                if (keyformCount > 0 && gridPoints > 0 && totalFloats64 > 0 &&
                    totalFloats64 < 1000000 &&
                    ps - buffer + totalFloats64 * 4 <= size) {
                    Csm::csmInt32 totalFloats = static_cast<Csm::csmInt32>(totalFloats64);
                    bd.deformPoints.resize(totalFloats);
                    for (Csm::csmInt32 f = 0; f < totalFloats; f++) {
                        bd.deformPoints[f] = readF(ps); ps += 4;
                    }
                }
            } else {
                // Affine deformation - skip for now
                if (ps - buffer + 4 > size) break;
                Csm::csmInt32 affineCount = readI32(ps); ps += 4;
                // Skip affine data: 5 floats + 2 bools per affine
                if (affineCount > 0 && affineCount < 1000) {
                    Csm::csmInt32 skipBytes = affineCount * (5 * 4 + 2);
                    if (ps + skipBytes > buffer + size) break;
                    ps += skipBytes;
                }
            }

            _data.baseData.push_back(std::move(bd));
        }
    }

    return true;
}

bool Cubism2MocLoader::ParseMocBody(const Csm::csmByte* buffer, Csm::csmSizeInt size)
{
    if (size < 24)
    {
        LAppPal::PrintLogLn("[Cubism2] Moc too small for body");
        return false;
    }

    const Csm::csmByte* p = buffer + 4; // skip magic+version

    // Read section offsets (little-endian)
    auto readInt32 = [](const Csm::csmByte* ptr) -> Csm::csmInt32 {
        return (Csm::csmInt32)(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24));
    };

    auto readFloat = [](const Csm::csmByte* ptr) -> Csm::csmFloat32 {
        Csm::csmFloat32 val;
        memcpy(&val, ptr, 4);
        return val;
    };

    const Csm::csmInt32 paramSectionOffset = readInt32(p); p += 4;
    const Csm::csmInt32 partsSectionOffset = readInt32(p); p += 4;
    const Csm::csmInt32 drawSectionOffset = readInt32(p); p += 4;

    // Parse parameters section
    if (paramSectionOffset > 0 && paramSectionOffset < size - 8)
    {
        const Csm::csmByte* ps = buffer + paramSectionOffset;
        _data.paramCount = readInt32(ps); ps += 4;

        if (_data.paramCount > 0 && _data.paramCount < 1000)
        {
            const Csm::csmByte* paramBase = ps;
            const Csm::csmInt32 paramIdTableOffset = readInt32(ps); ps += 4;

            _data.params.resize(_data.paramCount);

            for (Csm::csmInt32 i = 0; i < _data.paramCount && i < 500; i++)
            {
                const Csm::csmFloat32 defVal = readFloat(paramBase + 4 + i * 20 + 0);
                const Csm::csmFloat32 minVal = readFloat(paramBase + 4 + i * 20 + 4);
                const Csm::csmFloat32 maxVal = readFloat(paramBase + 4 + i * 20 + 8);

                const Csm::csmInt32 idOffset = readInt32(paramBase + i * 4);
                std::string paramId;
                if (paramIdTableOffset > 0 && paramIdTableOffset + idOffset < size)
                {
                    const char* str = reinterpret_cast<const char*>(buffer + paramSectionOffset + paramIdTableOffset + idOffset);
                    paramId = std::string(str);
                }
                else
                {
                    paramId = "Param" + std::to_string(i);
                }

                _data.params[i].id = paramId;
                _data.params[i].defaultValue = defVal;
                _data.params[i].minValue = minVal;
                _data.params[i].maxValue = maxVal;
            }
        }
    }
    else
    {
        LAppPal::PrintLogLn("[Cubism2] No param section, using defaults");
        _data.paramCount = 0;
    }

    // Parse parts section for deformation data
    if (partsSectionOffset > 0 && partsSectionOffset < drawSectionOffset &&
        partsSectionOffset < size - 4)
    {
        ParsePartsSection(buffer, size, partsSectionOffset, drawSectionOffset);
    }

    // Parse drawables section
    if (drawSectionOffset > 0 && drawSectionOffset < size - 8)
    {
        const Csm::csmByte* ds = buffer + drawSectionOffset;
        _data.drawableCount = readInt32(ds); ds += 4;

        if (_data.drawableCount > 0 && _data.drawableCount < 5000)
        {
            const Csm::csmByte* drawBase = ds;
            Csm::csmInt32 drawOffsetTableSize = _data.drawableCount * 4;
            if (drawSectionOffset + 4 + drawOffsetTableSize > size)
            {
                LAppPal::PrintLogLn("[Cubism2] Drawable offset table overflows buffer");
                _data.drawableCount = 0;
                return true;
            }

            std::vector<Csm::csmInt32> drawOffsets(_data.drawableCount);
            for (Csm::csmInt32 i = 0; i < _data.drawableCount; i++)
            {
                drawOffsets[i] = readInt32(drawBase + i * 4);
            }

            _data.drawables.resize(_data.drawableCount);
            _buffers.resize(_data.drawableCount);

            for (Csm::csmInt32 i = 0; i < _data.drawableCount; i++)
            {
                Csm::csmInt32 off = drawSectionOffset + drawOffsets[i];
                if (off < 0 || off + 36 > size)
                {
                    LAppPal::PrintLogLn("[Cubism2] Drawable %d offset out of bounds", i);
                    continue;
                }

                const Csm::csmByte* dd = buffer + off;

                const Csm::csmInt32 texIndex = readInt32(dd + 4);
                const Csm::csmInt32 drawOrder = readInt32(dd + 8);
                const Csm::csmInt32 vertCount = readInt32(dd + 12);
                const Csm::csmInt32 vertOffset = readInt32(dd + 16);
                const Csm::csmInt32 uvOffset = readInt32(dd + 20);
                const Csm::csmInt32 idxCount = readInt32(dd + 24);
                const Csm::csmInt32 idxOffset = readInt32(dd + 28);
                const Csm::csmFloat32 opacity = readFloat(dd + 32);

                _data.drawables[i].textureIndex = texIndex;
                _data.drawables[i].drawOrder = drawOrder;
                _data.drawables[i].vertexCount = vertCount;
                _data.drawables[i].opacity = opacity;
                _data.drawables[i].id = "Drawable" + std::to_string(i);

                // Read vertex positions (x, y pairs) into dual-buffer
                if (vertOffset > 0 && off + vertOffset + vertCount * 8 <= size)
                {
                    const Csm::csmByte* vp = buffer + off + vertOffset;
                    _buffers[i].positions[0].resize(vertCount * 2);
                    for (Csm::csmInt32 v = 0; v < vertCount; v++)
                    {
                        _buffers[i].positions[0][v * 2 + 0] = readFloat(vp + v * 8 + 0);
                        _buffers[i].positions[0][v * 2 + 1] = readFloat(vp + v * 8 + 4);
                    }
                }

                // Read UV coordinates
                if (uvOffset > 0 && off + uvOffset + vertCount * 8 <= size)
                {
                    const Csm::csmByte* uv = buffer + off + uvOffset;
                    _data.drawables[i].u.resize(vertCount);
                    _data.drawables[i].v.resize(vertCount);
                    _buffers[i].uvs.resize(vertCount * 2);
                    for (Csm::csmInt32 vt = 0; vt < vertCount; vt++)
                    {
                        _data.drawables[i].u[vt] = readFloat(uv + vt * 8 + 0);
                        _data.drawables[i].v[vt] = readFloat(uv + vt * 8 + 4);
                        _buffers[i].uvs[vt * 2 + 0] = _data.drawables[i].u[vt];
                        _buffers[i].uvs[vt * 2 + 1] = _data.drawables[i].v[vt];
                    }
                }

                // Read indices
                if (idxOffset > 0 && off + idxOffset + idxCount * 2 <= size)
                {
                    const Csm::csmByte* ip = buffer + off + idxOffset;
                    _data.drawables[i].indices.resize(idxCount);
                    for (Csm::csmInt32 idx = 0; idx < idxCount; idx++)
                    {
                        _data.drawables[i].indices[idx] = (unsigned short)(ip[idx * 2] | (ip[idx * 2 + 1] << 8));
                    }
                    _buffers[i].indexCount = idxCount;
                }

                // Track bounding box for canvas dimensions
                for (Csm::csmInt32 v = 0; v < vertCount; v++)
                {
                    if (v * 2 + 1 < static_cast<Csm::csmInt32>(_buffers[i].positions[0].size()))
                    {
                        Csm::csmFloat32 px = _buffers[i].positions[0][v * 2];
                        Csm::csmFloat32 py = _buffers[i].positions[0][v * 2 + 1];
                        if(px < _canvasMinX) _canvasMinX = px;
                        if(px > _canvasMaxX) _canvasMaxX = px;
                        if(py < _canvasMinY) _canvasMinY = py;
                        if(py > _canvasMaxY) _canvasMaxY = py;
                    }
                }
            }
        }
    }
    else
    {
        LAppPal::PrintLogLn("[Cubism2] No drawable section");
        _data.drawableCount = 0;
    }

    // Compute canvas dimensions from bounding box
    _canvasWidth = _canvasMaxX - _canvasMinX;
    _canvasHeight = _canvasMaxY - _canvasMinY;
    if(_canvasWidth < 1.0F) _canvasWidth = 1.0F;
    if(_canvasHeight < 1.0F) _canvasHeight = 1.0F;

    return true;
}

void Cubism2MocLoader::InitShader()
{
    const char* vertSrc =
        "attribute vec2 aPosition;\n"
        "attribute vec2 aTexCoord;\n"
        "uniform mat4 uMvp;\n"
        "varying vec2 vTexCoord;\n"
        "void main() {\n"
        "    gl_Position = uMvp * vec4(aPosition, 0.0, 1.0);\n"
        "    vTexCoord = aTexCoord;\n"
        "}\n";

    const char* fragSrc =
        "precision mediump float;\n"
        "varying vec2 vTexCoord;\n"
        "uniform sampler2D uTexture;\n"
        "uniform float uOpacity;\n"
        "uniform vec4 uMultiplyColor;\n"
        "uniform vec4 uScreenColor;\n"
        "void main() {\n"
        "    vec4 color = texture2D(uTexture, vTexCoord);\n"
        "    color.rgb = color.rgb * uMultiplyColor.rgb;\n"
        "    color.rgb = (color.rgb + uScreenColor.rgb * color.a) - (color.rgb * uScreenColor.rgb);\n"
        "    color.a *= uOpacity;\n"
        "    color.rgb *= color.a;\n"
        "    gl_FragColor = color;\n"
        "}\n";

    const GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertSrc, nullptr);
    glCompileShader(vert);
    GLint compiled = 0;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &compiled);
    if(!compiled) LAppPal::PrintLogLn("[Cubism2] Vertex shader compile failed");

    const GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragSrc, nullptr);
    glCompileShader(frag);
    glGetShaderiv(frag, GL_COMPILE_STATUS, &compiled);
    if(!compiled) LAppPal::PrintLogLn("[Cubism2] Fragment shader compile failed");

    _shaderProgram = glCreateProgram();
    glAttachShader(_shaderProgram, vert);
    glAttachShader(_shaderProgram, frag);
    glLinkProgram(_shaderProgram);
    GLint linked = 0;
    glGetProgramiv(_shaderProgram, GL_LINK_STATUS, &linked);
    if(!linked) LAppPal::PrintLogLn("[Cubism2] Shader link failed");

    glDeleteShader(vert);
    glDeleteShader(frag);

    _attribPosition = glGetAttribLocation(_shaderProgram, "aPosition");
    _attribTexCoord = glGetAttribLocation(_shaderProgram, "aTexCoord");
    _uniformMvp = glGetUniformLocation(_shaderProgram, "uMvp");
    _uniformTexture = glGetUniformLocation(_shaderProgram, "uTexture");
    _uniformOpacity = glGetUniformLocation(_shaderProgram, "uOpacity");
    _uniformMultiplyColor = glGetUniformLocation(_shaderProgram, "uMultiplyColor");
    _uniformScreenColor = glGetUniformLocation(_shaderProgram, "uScreenColor");

    // Initialize mask shaders if mask groups are defined
    if (!_maskGroups.empty()) {
        InitMaskShader();
    }
}

void Cubism2MocLoader::SetParameterValue(const char* paramId, Csm::csmFloat32 value)
{
    for (const auto& p : _data.params)
    {
        if (p.id == paramId)
        {
            value = (value < p.minValue) ? p.minValue : (value > p.maxValue) ? p.maxValue : value;
            break;
        }
    }
    _paramValues[paramId] = value;
}

Csm::csmFloat32 Cubism2MocLoader::GetParameterValue(const char* paramId) const
{
    auto it = _paramValues.find(paramId);
    if(it != _paramValues.end()) {
        return it->second;
    }
    return 0.0F;
}

void Cubism2MocLoader::SetTexture(Csm::csmInt32 index, GLuint textureId)
{
    _textures[index] = textureId;
}

// --- Vertex Deformation ---

void Cubism2MocLoader::InterpolateDeformGrid(Csm::csmInt32 baseIdx)
{
    if (baseIdx < 0 || baseIdx >= static_cast<Csm::csmInt32>(_data.baseData.size())) return;
    const auto& bd = _data.baseData[baseIdx];
    if (bd.deformType != 1 || bd.deformPoints.empty()) return;

    const Csm::csmInt32 gridPoints = (bd.col + 1) * (bd.row + 1);
    const Csm::csmInt32 axisCount = static_cast<Csm::csmInt32>(bd.pivotManager.paramPivots.size());
    if (axisCount == 0 || gridPoints <= 0) return;

    // Ensure deformed grid buffer exists
    if (_deformedGridPoints.size() <= static_cast<size_t>(baseIdx)) {
        _deformedGridPoints.resize(baseIdx + 1);
    }
    auto& result = _deformedGridPoints[baseIdx];
    result.resize(gridPoints * 2);

    // Build per-axis indices and weights
    std::vector<Csm::csmInt32> indices(axisCount);
    std::vector<Csm::csmFloat32> weights(axisCount);
    for (Csm::csmInt32 a = 0; a < axisCount; a++) {
        Csm::csmFloat32 pv = GetParameterValue(bd.pivotManager.paramPivots[a].paramId.c_str());
        bd.pivotManager.FindAxis(pv, a, indices[a], weights[a]);
    }

    // Recursive multi-linear interpolation
    const Csm::csmInt32 kfCount = bd.pivotManager.GetKeyformCount();
    std::vector<Csm::csmFloat32> temp(gridPoints * 2);

    std::function<void(Csm::csmInt32, Csm::csmInt32)> interp;
    interp = [&](Csm::csmInt32 depth, Csm::csmInt32 baseKf) {
        if (depth == axisCount) {
            // Base case: copy keyform data
            if (baseKf >= 0 && baseKf < kfCount) {
                const Csm::csmFloat32* src = bd.deformPoints.data() + baseKf * gridPoints * 2;
                for (Csm::csmInt32 i = 0; i < gridPoints * 2; i++) {
                    result[i] = src[i];
                }
            }
            return;
        }

        const Csm::csmInt32 stride = [&]() {
            Csm::csmInt32 s = 1;
            for (Csm::csmInt32 a = depth + 1; a < axisCount; a++) {
                s *= bd.pivotManager.paramPivots[a].pivotCount;
            }
            return s;
        }();

        interp(depth + 1, baseKf);
        std::vector<Csm::csmFloat32> lo = result;

        interp(depth + 1, baseKf + stride);

        Csm::csmFloat32 w = weights[depth];
        for (Csm::csmInt32 i = 0; i < gridPoints * 2; i++) {
            result[i] = lo[i] + (result[i] - lo[i]) * w;
        }
    };

    Csm::csmInt32 startKf = 0;
    Csm::csmInt32 multiplier = 1;
    for (Csm::csmInt32 a = axisCount - 1; a >= 0; a--) {
        startKf += indices[a] * multiplier;
        multiplier *= bd.pivotManager.paramPivots[a].pivotCount;
    }

    interp(0, startKf);
}

void Cubism2MocLoader::ApplyDeformation(Csm::csmInt32 drawableIdx)
{
    if (drawableIdx < 0 || drawableIdx >= static_cast<Csm::csmInt32>(_data.drawables.size())) return;
    if (drawableIdx >= static_cast<Csm::csmInt32>(_buffers.size())) return;

    const auto& drawable = _data.drawables[drawableIdx];
    auto& buf = _buffers[drawableIdx];

    if (buf.positions[0].empty() || drawable.vertexCount <= 0) return;

    // Find matching base data by index (parts section drawables map 1:1 to global drawables)
    Csm::csmInt32 baseIdx = -1;
    for (size_t b = 0; b < _data.baseData.size(); b++) {
        if (_data.baseData[b].deformType == 1 && !_data.baseData[b].deformPoints.empty()) {
            baseIdx = static_cast<Csm::csmInt32>(b);
            break;
        }
    }

    if (baseIdx < 0 || baseIdx >= static_cast<Csm::csmInt32>(_deformedGridPoints.size())) {
        return; // No deformation data
    }

    const auto& bd = _data.baseData[baseIdx];
    const auto& grid = _deformedGridPoints[baseIdx];
    const Csm::csmInt32 gridW = bd.col + 1;
    const Csm::csmInt32 gridH = bd.row + 1;

    if (gridW < 2 || gridH < 2) return;

    const Csm::csmInt32 writeBuf = buf.currentBuffer;
    auto& dst = buf.positions[writeBuf];
    if (dst.size() < static_cast<size_t>(drawable.vertexCount * 2)) return;

    for (Csm::csmInt32 v = 0; v < drawable.vertexCount; v++) {
        Csm::csmFloat32 u = (v < static_cast<Csm::csmInt32>(drawable.u.size())) ? drawable.u[v] : 0.0F;
        Csm::csmFloat32 vv = (v < static_cast<Csm::csmInt32>(drawable.v.size())) ? drawable.v[v] : 0.0F;

        // UV to grid coordinates
        Csm::csmFloat32 gx = u * (gridW - 1);
        Csm::csmFloat32 gy = vv * (gridH - 1);

        // Clamp
        if (gx < 0.0F) gx = 0.0F;
        if (gx > gridW - 1) gx = gridW - 1.0F;
        if (gy < 0.0F) gy = 0.0F;
        if (gy > gridH - 1) gy = gridH - 1.0F;

        Csm::csmInt32 cx = static_cast<Csm::csmInt32>(gx);
        Csm::csmInt32 cy = static_cast<Csm::csmInt32>(gy);
        if (cx >= gridW - 1) cx = gridW - 2;
        if (cy >= gridH - 1) cy = gridH - 2;
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;

        Csm::csmFloat32 fx = gx - cx;
        Csm::csmFloat32 fy = gy - cy;

        // Bilinear interpolation on deformed grid
        Csm::csmInt32 i00 = (cy * gridW + cx) * 2;
        Csm::csmInt32 i10 = (cy * gridW + cx + 1) * 2;
        Csm::csmInt32 i01 = ((cy + 1) * gridW + cx) * 2;
        Csm::csmInt32 i11 = ((cy + 1) * gridW + cx + 1) * 2;

        if (i11 + 1 < static_cast<Csm::csmInt32>(grid.size())) {
            Csm::csmFloat32 x0 = grid[i00] * (1 - fy) + grid[i01] * fy;
            Csm::csmFloat32 x1 = grid[i10] * (1 - fy) + grid[i11] * fy;
            Csm::csmFloat32 y0 = grid[i00 + 1] * (1 - fy) + grid[i01 + 1] * fy;
            Csm::csmFloat32 y1 = grid[i10 + 1] * (1 - fy) + grid[i11 + 1] * fy;

            dst[v * 2 + 0] = x0 * (1 - fx) + x1 * fx;
            dst[v * 2 + 1] = y0 * (1 - fx) + y1 * fx;
        }
    }

    buf.dirty = true;
}

void Cubism2MocLoader::Update()
{
    if(!_loaded) return;

    // Initialize GL buffers on first update
    if (!_buffersInitialized)
    {
        for (size_t i = 0; i < _buffers.size(); i++)
        {
            auto& buf = _buffers[i];
            if(buf.positions[0].empty()) continue;

            buf.positions[1] = buf.positions[0];
            buf.currentBuffer = 0;
            buf.dirty = true;

            glGenBuffers(1, &buf.vbo[0]);
            glBindBuffer(GL_ARRAY_BUFFER, buf.vbo[0]);
            glBufferData(GL_ARRAY_BUFFER,
                buf.positions[0].size() * sizeof(Csm::csmFloat32),
                buf.positions[0].data(), GL_DYNAMIC_DRAW);

            glGenBuffers(1, &buf.vbo[1]);
            glBindBuffer(GL_ARRAY_BUFFER, buf.vbo[1]);
            glBufferData(GL_ARRAY_BUFFER,
                buf.positions[1].size() * sizeof(Csm::csmFloat32),
                buf.positions[1].data(), GL_DYNAMIC_DRAW);

            if (!buf.uvs.empty()) {
                glGenBuffers(1, &buf.uvbo);
                glBindBuffer(GL_ARRAY_BUFFER, buf.uvbo);
                glBufferData(GL_ARRAY_BUFFER,
                    buf.uvs.size() * sizeof(Csm::csmFloat32),
                    buf.uvs.data(), GL_STATIC_DRAW);
            }

            if (!_data.drawables[i].indices.empty()) {
                glGenBuffers(1, &buf.ibo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf.ibo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                    _data.drawables[i].indices.size() * sizeof(unsigned short),
                    _data.drawables[i].indices.data(), GL_STATIC_DRAW);
            }
        }
        _buffersInitialized = true;

        // Cache sorted drawable indices by drawOrder (static after load)
        _sortedDrawableIndices.resize(_data.drawables.size());
        for (size_t i = 0; i < _sortedDrawableIndices.size(); i++) _sortedDrawableIndices[i] = static_cast<Csm::csmInt32>(i);
        std::stable_sort(_sortedDrawableIndices.begin(), _sortedDrawableIndices.end(),
            [this](Csm::csmInt32 a, Csm::csmInt32 b) {
                return _data.drawables[a].drawOrder < _data.drawables[b].drawOrder;
            });
    }

    // Apply deformation to each base data grid
    for (Csm::csmInt32 b = 0; b < static_cast<Csm::csmInt32>(_data.baseData.size()); b++) {
        InterpolateDeformGrid(b);
    }

    // Apply deformation to each drawable
    for (size_t d = 0; d < _data.drawables.size(); d++) {
        ApplyDeformation(static_cast<Csm::csmInt32>(d));
    }

    // Upload dirty VBOs (deformation wrote to positions[currentBuffer],
    // upload to the opposite VBO so GPU reads one while CPU writes the other)
    for (size_t d = 0; d < _data.drawables.size(); d++)
    {
        auto& buf = _buffers[d];
        if(buf.positions[0].empty()) continue;

        if (buf.dirty)
        {
            Csm::csmInt32 nextBuffer = 1 - buf.currentBuffer;
            glBindBuffer(GL_ARRAY_BUFFER, buf.vbo[nextBuffer]);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                buf.positions[buf.currentBuffer].size() * sizeof(Csm::csmFloat32),
                buf.positions[buf.currentBuffer].data());
            buf.currentBuffer = nextBuffer;
            buf.dirty = false;
        }
    }
}

void Cubism2MocLoader::Draw(const Csm::csmFloat32* mvpMatrix)
{
    if(!_loaded || _shaderProgram == 0) return;

    GLint linkStatus = 0;
    glGetProgramiv(_shaderProgram, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == 0) return;

    // Save GL state
    GLint prevProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLboolean prevBlendEnabled = glIsEnabled(GL_BLEND);
    GLint prevBlendSrc = 0, prevBlendDst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);
    GLint prevActiveTex = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);

    // Render masks to FBO if mask groups are defined
    if (!_maskGroups.empty()) {
        EnsureMaskFBO();
        RenderMasks(mvpMatrix);
    }

    glUseProgram(_shaderProgram);
    glUniformMatrix4fv(_uniformMvp, 1, GL_FALSE, mvpMatrix);
    glUniform1i(_uniformTexture, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // Use cached sorted drawable indices (built once after load)
    for (Csm::csmInt32 di : _sortedDrawableIndices)
    {
        auto& drawable = _data.drawables[di];
        auto& buf = _buffers[di];

        if(buf.positions[0].empty() || buf.indexCount == 0) continue;
        if(drawable.opacity <= 0.0F) continue;

        auto texIt = _textures.find(drawable.textureIndex);
        if(texIt == _textures.end()) continue;

        // Check if this drawable is masked
        int maskChannel = -1;
        bool isMasked = _maskTexture != 0 && IsDrawableMasked(di, maskChannel);

        if (isMasked && _maskedShaderProgram != 0) {
            // Use masked shader
            glUseProgram(_maskedShaderProgram);
            glUniformMatrix4fv(_maskedUniformMvp, 1, GL_FALSE, mvpMatrix);
            glUniform1f(_maskedUniformOpacity, drawable.opacity);
            glUniform1i(_maskedUniformTexture, 0);
            glUniform1i(_maskedUniformMaskTexture, 1);

            // Set channel flag for which RGBA channel to sample
            float channelFlag[4] = {0, 0, 0, 0};
            channelFlag[maskChannel] = 1.0F;
            glUniform4fv(_maskedUniformChannelFlag, 1, channelFlag);

            if (_maskedUniformMultiplyColor >= 0) {
                glUniform4f(_maskedUniformMultiplyColor,
                    drawable.multiplyColor[0], drawable.multiplyColor[1],
                    drawable.multiplyColor[2], drawable.multiplyColor[3]);
            }
            if (_maskedUniformScreenColor >= 0) {
                glUniform4f(_maskedUniformScreenColor,
                    drawable.screenColor[0], drawable.screenColor[1],
                    drawable.screenColor[2], drawable.screenColor[3]);
            }

            // Bind mask texture to GL_TEXTURE1
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, _maskTexture);

            // Bind main texture to GL_TEXTURE0
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texIt->second);

            // Set blend mode
            if (drawable.blendMode == Cubism2BlendMode::BlendAdd) {
                glBlendFunc(GL_ONE, GL_ONE);
            } else {
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }

            // Draw with masked shader attributes
            glBindBuffer(GL_ARRAY_BUFFER, buf.vbo[buf.currentBuffer]);
            glVertexAttribPointer(_maskedAttribPosition, 2, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(_maskedAttribPosition);

            if (buf.uvbo) {
                glBindBuffer(GL_ARRAY_BUFFER, buf.uvbo);
                glVertexAttribPointer(_maskedAttribTexCoord, 2, GL_FLOAT, GL_FALSE, 0, 0);
                glEnableVertexAttribArray(_maskedAttribTexCoord);
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf.ibo);
            glDrawElements(GL_TRIANGLES, buf.indexCount, GL_UNSIGNED_SHORT, nullptr);

            glDisableVertexAttribArray(_maskedAttribPosition);
            if (_maskedAttribTexCoord >= 0) glDisableVertexAttribArray(_maskedAttribTexCoord);

            // Restore normal shader
            glUseProgram(_shaderProgram);
            glUniformMatrix4fv(_uniformMvp, 1, GL_FALSE, mvpMatrix);
            glUniform1i(_uniformTexture, 0);
        } else {
            // Use normal shader (unmasked)
            glUniform1f(_uniformOpacity, drawable.opacity);

            if (_uniformMultiplyColor >= 0) {
                glUniform4f(_uniformMultiplyColor,
                    drawable.multiplyColor[0], drawable.multiplyColor[1],
                    drawable.multiplyColor[2], drawable.multiplyColor[3]);
            }
            if (_uniformScreenColor >= 0) {
                glUniform4f(_uniformScreenColor,
                    drawable.screenColor[0], drawable.screenColor[1],
                    drawable.screenColor[2], drawable.screenColor[3]);
            }

            if (drawable.blendMode == Cubism2BlendMode::BlendAdd) {
                glBlendFunc(GL_ONE, GL_ONE);
            } else {
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texIt->second);

            glBindBuffer(GL_ARRAY_BUFFER, buf.vbo[buf.currentBuffer]);
            glVertexAttribPointer(_attribPosition, 2, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(_attribPosition);

            if (buf.uvbo) {
                glBindBuffer(GL_ARRAY_BUFFER, buf.uvbo);
                glVertexAttribPointer(_attribTexCoord, 2, GL_FLOAT, GL_FALSE, 0, 0);
                glEnableVertexAttribArray(_attribTexCoord);
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf.ibo);
            glDrawElements(GL_TRIANGLES, buf.indexCount, GL_UNSIGNED_SHORT, nullptr);
        }
    }

    glDisableVertexAttribArray(_attribPosition);
    if(_attribTexCoord >= 0) glDisableVertexAttribArray(_attribTexCoord);

    // Restore GL state
    if (!prevBlendEnabled) glDisable(GL_BLEND);
    glBlendFunc(prevBlendSrc, prevBlendDst);
    glActiveTexture(prevActiveTex);
    glUseProgram(prevProgram);
}

void Cubism2MocLoader::SetDrawableOpacity(Csm::csmInt32 drawableIndex, Csm::csmFloat32 opacity)
{
    if (drawableIndex >= 0 && drawableIndex < static_cast<Csm::csmInt32>(_data.drawables.size()))
    {
        _data.drawables[drawableIndex].opacity = opacity;
    }
}

Csm::csmInt32 Cubism2MocLoader::FindDrawableIndex(const char* drawableId) const
{
    if (drawableId == nullptr) return -1;
    for (size_t i = 0; i < _data.drawables.size(); i++)
    {
        if (_data.drawables[i].id == drawableId)
            return static_cast<Csm::csmInt32>(i);
    }
    return -1;
}

void Cubism2MocLoader::SetDrawableOpacityById(const char* drawableId, Csm::csmFloat32 opacity)
{
    Csm::csmInt32 idx = FindDrawableIndex(drawableId);
    if (idx >= 0)
    {
        _data.drawables[idx].opacity = opacity;
    }
}

void Cubism2MocLoader::SetDrawableMultiplyColor(int idx, Csm::csmFloat32 r, Csm::csmFloat32 g, Csm::csmFloat32 b, Csm::csmFloat32 a)
{
    if (idx >= 0 && idx < static_cast<int>(_data.drawables.size())) {
        _data.drawables[idx].multiplyColor[0] = r;
        _data.drawables[idx].multiplyColor[1] = g;
        _data.drawables[idx].multiplyColor[2] = b;
        _data.drawables[idx].multiplyColor[3] = a;
    }
}

void Cubism2MocLoader::SetDrawableScreenColor(int idx, Csm::csmFloat32 r, Csm::csmFloat32 g, Csm::csmFloat32 b, Csm::csmFloat32 a)
{
    if (idx >= 0 && idx < static_cast<int>(_data.drawables.size())) {
        _data.drawables[idx].screenColor[0] = r;
        _data.drawables[idx].screenColor[1] = g;
        _data.drawables[idx].screenColor[2] = b;
        _data.drawables[idx].screenColor[3] = a;
    }
}

void Cubism2MocLoader::SetDrawableBlendMode(int idx, Cubism2BlendMode mode)
{
    if (idx >= 0 && idx < static_cast<int>(_data.drawables.size())) {
        _data.drawables[idx].blendMode = mode;
    }
}

void Cubism2MocLoader::SetMaskGroups(const std::vector<Cubism2MaskGroup>& groups)
{
    _maskGroups = groups;
    _maskLookup.clear();
    // Assign channel indices (0=R, 1=G, 2=B, 3=A) for RGBA packing
    for (size_t i = 0; i < _maskGroups.size(); i++) {
        _maskGroups[i].channelIndex = static_cast<int>(i % 4);
        for (Csm::csmInt32 target : _maskGroups[i].targets) {
            _maskLookup[target] = _maskGroups[i].channelIndex;
        }
    }
}

bool Cubism2MocLoader::IsDrawableMasked(Csm::csmInt32 drawableIndex, int& outChannelIndex) const
{
    auto it = _maskLookup.find(drawableIndex);
    if (it != _maskLookup.end()) {
        outChannelIndex = it->second;
        return true;
    }
    return false;
}

void Cubism2MocLoader::InitMaskShader()
{
    // --- Mask generation shader (renders mask shapes into FBO) ---
    const char* maskVertSrc =
        "attribute vec2 aPosition;\n"
        "uniform mat4 uMvp;\n"
        "void main() {\n"
        "    gl_Position = uMvp * vec4(aPosition, 0.0, 1.0);\n"
        "}\n";

    const char* maskFragSrc =
        "precision mediump float;\n"
        "uniform vec4 uChannelFlag;\n"
        "void main() {\n"
        "    gl_FragColor = uChannelFlag;\n"
        "}\n";

    // Mask setup shader: outputs channel color where mask geometry exists
    auto compileShader = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) LAppPal::PrintLogLn("[Cubism2] Mask shader compile failed");
        return s;
    };

    auto linkProgram = [](GLuint vert, GLuint frag) -> GLuint {
        GLuint p = glCreateProgram();
        glAttachShader(p, vert);
        glAttachShader(p, frag);
        glLinkProgram(p);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) LAppPal::PrintLogLn("[Cubism2] Mask shader link failed");
        glDeleteShader(vert);
        glDeleteShader(frag);
        return p;
    };

    {
        GLuint v = compileShader(GL_VERTEX_SHADER, maskVertSrc);
        GLuint f = compileShader(GL_FRAGMENT_SHADER, maskFragSrc);
        _maskShaderProgram = linkProgram(v, f);
        _maskUniformMvp = glGetUniformLocation(_maskShaderProgram, "uMvp");
    }

    // --- Masked drawable shader (samples mask texture during main draw) ---
    const char* maskedVertSrc =
        "attribute vec2 aPosition;\n"
        "attribute vec2 aTexCoord;\n"
        "uniform mat4 uMvp;\n"
        "varying vec2 vTexCoord;\n"
        "varying vec2 vMaskCoord;\n"
        "void main() {\n"
        "    gl_Position = uMvp * vec4(aPosition, 0.0, 1.0);\n"
        "    vTexCoord = aTexCoord;\n"
        "    vMaskCoord = gl_Position.xy * 0.5 + 0.5;\n"
        "}\n";

    const char* maskedFragSrc =
        "precision mediump float;\n"
        "varying vec2 vTexCoord;\n"
        "varying vec2 vMaskCoord;\n"
        "uniform sampler2D uTexture;\n"
        "uniform sampler2D uMaskTexture;\n"
        "uniform float uOpacity;\n"
        "uniform vec4 uMultiplyColor;\n"
        "uniform vec4 uScreenColor;\n"
        "uniform vec4 uChannelFlag;\n"
        "void main() {\n"
        "    vec4 color = texture2D(uTexture, vTexCoord);\n"
        "    vec4 mask = texture2D(uMaskTexture, vMaskCoord);\n"
        "    float maskVal = dot(mask, uChannelFlag);\n"
        "    color.rgb = color.rgb * uMultiplyColor.rgb;\n"
        "    color.rgb = (color.rgb + uScreenColor.rgb * color.a) - (color.rgb * uScreenColor.rgb);\n"
        "    color.a *= uOpacity * maskVal;\n"
        "    color.rgb *= color.a;\n"
        "    gl_FragColor = color;\n"
        "}\n";

    {
        GLuint v = compileShader(GL_VERTEX_SHADER, maskedVertSrc);
        GLuint f = compileShader(GL_FRAGMENT_SHADER, maskedFragSrc);
        _maskedShaderProgram = linkProgram(v, f);
        _maskedAttribPosition = glGetAttribLocation(_maskedShaderProgram, "aPosition");
        _maskedAttribTexCoord = glGetAttribLocation(_maskedShaderProgram, "aTexCoord");
        _maskedUniformMvp = glGetUniformLocation(_maskedShaderProgram, "uMvp");
        _maskedUniformTexture = glGetUniformLocation(_maskedShaderProgram, "uTexture");
        _maskedUniformOpacity = glGetUniformLocation(_maskedShaderProgram, "uOpacity");
        _maskedUniformMultiplyColor = glGetUniformLocation(_maskedShaderProgram, "uMultiplyColor");
        _maskedUniformScreenColor = glGetUniformLocation(_maskedShaderProgram, "uScreenColor");
        _maskedUniformMaskTexture = glGetUniformLocation(_maskedShaderProgram, "uMaskTexture");
        _maskedUniformChannelFlag = glGetUniformLocation(_maskedShaderProgram, "uChannelFlag");
    }
}

void Cubism2MocLoader::EnsureMaskFBO()
{
    if (_maskFBO != 0) return;
    if (_maskGroups.empty()) return;

    glGenFramebuffers(1, &_maskFBO);
    glGenTextures(1, &_maskTexture);

    glBindTexture(GL_TEXTURE_2D, _maskTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _maskWidth, _maskHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, _maskFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _maskTexture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LAppPal::PrintLogLn("[Cubism2] Mask FBO incomplete (status=0x%x), disabling masks", status);
        glDeleteFramebuffers(1, &_maskFBO);
        glDeleteTextures(1, &_maskTexture);
        _maskFBO = 0;
        _maskTexture = 0;
        _maskGroups.clear();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Cubism2MocLoader::RenderMasks(const Csm::csmFloat32* mvpMatrix)
{
    if (_maskGroups.empty() || _maskFBO == 0 || _maskShaderProgram == 0) return;

    // Save GL state
    GLint prevFBO = 0, prevViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    // Bind mask FBO
    glBindFramebuffer(GL_FRAMEBUFFER, _maskFBO);
    glViewport(0, 0, _maskWidth, _maskHeight);
    glClearColor(1.0F, 1.0F, 1.0F, 1.0F);  // White = no mask
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(_maskShaderProgram);
    glUniformMatrix4fv(_maskUniformMvp, 1, GL_FALSE, mvpMatrix);

    glEnable(GL_BLEND);
    // Subtract mask shape from white background: result = white * (1 - mask)
    glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);

    // Render each mask group's drawable into the appropriate channel
    for (const auto& mg : _maskGroups) {
        if (mg.maskDrawableIndex < 0 || mg.maskDrawableIndex >= static_cast<Csm::csmInt32>(_data.drawables.size()))
            continue;

        auto& maskDrawable = _data.drawables[mg.maskDrawableIndex];
        auto& maskBuf = _buffers[mg.maskDrawableIndex];

        if (maskBuf.positions[0].empty() || maskBuf.indexCount == 0) continue;

        // Set channel flag: which RGBA channel this mask writes to
        float channelFlag[4] = {0, 0, 0, 0};
        channelFlag[mg.channelIndex] = 1.0F;
        GLint locCh = glGetUniformLocation(_maskShaderProgram, "uChannelFlag");
        if (locCh >= 0) glUniform4fv(locCh, 1, channelFlag);

        // Draw mask geometry
        glBindBuffer(GL_ARRAY_BUFFER, maskBuf.vbo[maskBuf.currentBuffer]);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, maskBuf.ibo);
        glDrawElements(GL_TRIANGLES, maskBuf.indexCount, GL_UNSIGNED_SHORT, nullptr);
    }

    glDisableVertexAttribArray(0);

    // Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void Cubism2MocLoader::ReloadRenderer()
{
    if (_shaderProgram) {
        glDeleteProgram(_shaderProgram);
        _shaderProgram = 0;
    }
    if (_maskShaderProgram) {
        glDeleteProgram(_maskShaderProgram);
        _maskShaderProgram = 0;
    }
    if (_maskedShaderProgram) {
        glDeleteProgram(_maskedShaderProgram);
        _maskedShaderProgram = 0;
    }
    if (_maskFBO) {
        glDeleteFramebuffers(1, &_maskFBO);
        _maskFBO = 0;
    }
    if (_maskTexture) {
        glDeleteTextures(1, &_maskTexture);
        _maskTexture = 0;
    }
    for (auto& buf : _buffers) {
        if (buf.vbo[0]) { glDeleteBuffers(1, &buf.vbo[0]); buf.vbo[0] = 0; }
        if (buf.vbo[1]) { glDeleteBuffers(1, &buf.vbo[1]); buf.vbo[1] = 0; }
        if (buf.uvbo)   { glDeleteBuffers(1, &buf.uvbo);   buf.uvbo = 0; }
        if (buf.ibo)    { glDeleteBuffers(1, &buf.ibo);    buf.ibo = 0; }
    }
    _buffersInitialized = false;
    InitShader();
    for (auto& buf : _buffers) buf.dirty = true;
}
