#pragma once

#include <CubismFramework.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Model/CubismModel.hpp>

namespace Live2D { namespace Cubism { namespace Framework {

/// Triangle-level hit testing using barycentric coordinates.
/// Replaces AABB with per-triangle precision for non-rectangular drawables.
///
/// All entry points use the full MVP matrix (view-projection * model) for
/// screen-to-local inverse transform. This correctly handles mirror, rotation,
/// and aspect-ratio scaling that a modelMatrix-only approach would miss.
namespace TriangleHitTest {

/// Set to true to use AABB-only hit testing (for debugging).
/// When false, uses triangle-level precision with AABB early-out.
inline bool& AabbOnlyMode() {
    static bool flag = false;
    return flag;
}

/// Check if a point (px, py) lies inside the triangle defined by (ax,ay), (bx,by), (cx,cy).
/// Uses the cross-product sign method for numerical stability.
inline bool PointInTriangle(float px, float py,
                             float ax, float ay,
                             float bx, float by,
                             float cx, float cy)
{
    const float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
    const float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
    const float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);

    const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(hasNeg && hasPos);
}

/// Core triangle hit test. Takes a pre-computed local-space point and raw drawable data.
/// Shared by all higher-level entry points to avoid code duplication.
inline bool HitTestTriangles(csmFloat32 localX, csmFloat32 localY,
                              const csmFloat32* vertices, csmInt32 vertCount,
                              const csmUint16* indices, csmInt32 indexCount)
{
    // AABB early-out: test bounding box before iterating triangles
    float minX = vertices[0], maxX = vertices[0];
    float minY = vertices[1], maxY = vertices[1];
    for (csmInt32 v = 1; v < vertCount; ++v) {
        const float vx = vertices[v * 2];
        const float vy = vertices[v * 2 + 1];
        if (vx < minX) minX = vx;
        if (vx > maxX) maxX = vx;
        if (vy < minY) minY = vy;
        if (vy > maxY) maxY = vy;
    }
    if (localX < minX || localX > maxX || localY < minY || localY > maxY) {
        return false;
    }

    // AABB-only debug mode: skip triangle tests, treat entire bbox as hit
    if (AabbOnlyMode()) {
        return true;
    }

    // Test each triangle
    for (csmInt32 t = 0; t + 2 < indexCount; t += 3) {
        const csmUint16 i0 = indices[t];
        const csmUint16 i1 = indices[t + 1];
        const csmUint16 i2 = indices[t + 2];

        if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount) continue;

        const float ax = vertices[i0 * 2];
        const float ay = vertices[i0 * 2 + 1];
        const float bx = vertices[i1 * 2];
        const float by = vertices[i1 * 2 + 1];
        const float cx = vertices[i2 * 2];
        const float cy = vertices[i2 * 2 + 1];

        if (PointInTriangle(localX, localY, ax, ay, bx, by, cx, cy)) {
            return true;
        }
    }

    return false;
}

/// Test if a screen-space point hits a drawable using the full MVP matrix for inverse transform.
/// The MVP matrix must be view-projection * model, which correctly accounts for
/// mirror, rotation, and aspect-ratio scaling.
///
/// @param model        The Cubism model (provides vertex/index data)
/// @param drawableIndex Index of the drawable to test
/// @param screenX      Screen-space X coordinate
/// @param screenY      Screen-space Y coordinate
/// @param mvpMatrix    Full MVP matrix (view-projection * model) for inverse transform
/// @return true if the point hits any triangle of the drawable
inline bool HitTestDrawable(CubismModel* model, csmInt32 drawableIndex,
                             csmFloat32 screenX, csmFloat32 screenY,
                             CubismMatrix44* mvpMatrix)
{
    if (!model || drawableIndex < 0 || !mvpMatrix) return false;

    const csmInt32 vertCount = model->GetDrawableVertexCount(drawableIndex);
    const csmInt32 indexCount = model->GetDrawableVertexIndexCount(drawableIndex);
    if (vertCount <= 0 || indexCount <= 0) return false;

    const csmFloat32* vertices = model->GetDrawableVertices(drawableIndex);
    const csmUint16* indices = model->GetDrawableVertexIndices(drawableIndex);
    if (!vertices || !indices) return false;

    // Transform screen point to model-local space via full MVP inverse
    const float localX = mvpMatrix->InvertTransformX(screenX);
    const float localY = mvpMatrix->InvertTransformY(screenY);

    return HitTestTriangles(localX, localY, vertices, vertCount, indices, indexCount);
}

/// High-level hit test: tests a drawable by ID with visibility and opacity checks.
///
/// @param model        The Cubism model
/// @param drawableId   Drawable ID handle
/// @param screenX      Screen-space X coordinate
/// @param screenY      Screen-space Y coordinate
/// @param mvpMatrix    Full MVP matrix (view-projection * model) for inverse transform
/// @param checkVisibility If true, skip invisible/opaque drawables
/// @return true if the point hits the drawable
inline bool IsHit(CubismModel* model, CubismIdHandle drawableId,
                   csmFloat32 screenX, csmFloat32 screenY,
                   CubismMatrix44* mvpMatrix,
                   bool checkVisibility = true)
{
    if (!model || !mvpMatrix) return false;

    const csmInt32 idx = model->GetDrawableIndex(drawableId);
    if (idx < 0) return false;

    if (checkVisibility) {
        if (!model->GetDrawableDynamicFlagIsVisible(idx)) return false;
        if (model->GetDrawableOpacity(idx) <= 0.0F) return false;
    }

    return HitTestDrawable(model, idx, screenX, screenY, mvpMatrix);
}

} // namespace TriangleHitTest
}}} // namespace Live2D::Cubism::Framework
