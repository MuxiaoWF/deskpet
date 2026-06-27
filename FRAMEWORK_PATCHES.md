# Framework SDK Patches

`Framework/` 是 Live2D Cubism 官方 SDK ([Live2D/CubismNativeFramework](https://github.com/Live2D/CubismNativeFramework))，当前 commit `c5cc765` (Cubism 5 SDK R5)。

以下为本项目额外修改，共 5 处，涉及 4 个文件。

**上游更新 Framework 时必须重新应用这些修改。**

---

## 1. PartOpacity 曲线求值 Bug 修复

**文件:** `Framework/src/Motion/CubismMotion.cpp` — `DoUpdateParameters()`

**问题:** PartOpacity 曲线存储的是 Part ID（如 `"Part6"`），但原始代码用 `model->GetParameterIndex()` 查找。Cubism SDK 中 Part 和 Parameter 是两个独立数组，`GetParameterIndex` 对 Part ID 返回 -1，导致 PartOpacity 曲线永远不生效。

**修复:** 改用 `model->GetPartIndex()` + `model->SetPartOpacity()`。

**严重性:** 关键 — 不修复则 PartOpacity 驱动的可见性切换（服装/组件开关）完全无效。

```diff
 for (; c < _motionData->CurveCount && curves[c].Type == CubismMotionCurveTarget_PartOpacity; ++c)
 {
-    // Find parameter index.
-    parameterIndex = model->GetParameterIndex(curves[c].Id);
+    // PartOpacity curves use Part IDs (e.g. "Part6"), not Parameter IDs.
+    // Parts and Parameters are separate arrays in the Cubism SDK.
+    const csmInt32 partIndex = model->GetPartIndex(curves[c].Id);

-    // Skip curve evaluation if no value in sink.
-    if (parameterIndex == -1)
+    if (partIndex < 0)
     {
         continue;
     }

-    // Evaluate curve and apply value.
     value = EvaluateCurve(_motionData, c, time, isCorrection, duration);

-    model->SetParameterValue(parameterIndex, value);
+    model->SetPartOpacity(partIndex, value);
 }
```

---

## 2. 选择性循环播放

**文件:** `Framework/src/Motion/CubismMotion.cpp` — `Create()`

**问题:** 原始代码 `ret->_loop = (ret->_motionData->Loop > 0)` 被注释掉，所有动作循环均被禁用。开关动作（仅含 Parameter 曲线，如眼神状态）需要循环才能保持参数值；菜单/过渡动作（含 PartOpacity 曲线）不能循环，否则 Part 可见性会在 0/1 之间振荡导致闪烁。

**修复:** 重新启用循环，但排除含 PartOpacity 曲线的动作。

**严重性:** 高 — 不修复则开关参数会回弹到默认值；菜单动作循环会导致闪烁。

```diff
     // NOTE: Editorではループありのモーション書き出しは非対応
-    // ret->_loop = (ret->_motionData->Loop > 0);
+    // Only enable looping for motions without PartOpacity curves.
+    // Toggle motions only have Parameter curves — need looping to maintain state.
+    // Menu/transition motions have PartOpacity curves — looping causes flickering.
+    if (ret && ret->_motionData->Loop > 0) {
+        ret->SetLoop(true);
+        for (csmInt32 ci = 0; ci < ret->_motionData->CurveCount; ci++) {
+            if (ret->_motionData->Curves[ci].Type == CubismMotionCurveTarget_PartOpacity) {
+                ret->SetLoop(false);
+                break;
+            }
+        }
+    }
```

**PartOpacity 持久化:** 菜单动作不循环，播放一次后结束。`LAppModel::Update` 在动作结束的瞬间捕获 PartOpacity 值存入 `_partOverrides`，确保 Part 可见性在动作停止后仍然保持。

---

## 3. 循环动作队列泄漏修复

**文件:** `Framework/src/Motion/CubismMotionQueueManager.cpp` — `DoUpdateMotion()`

**问题:** 循环动作永远不会自然结束（`_isLoop` 阻止 finish 路径）。被新动作替换后收到 fadeout 信号，但 fadeout 完成后仍留在队列中，导致队列无限增长。

**修复:** fadeout 完成后检查是否为循环动作且已过结束时间，强制 finish 以清理队列。

**严重性:** 中 — 逐渐导致内存泄漏和队列条目堆积。

```diff
             if (motionQueueEntry->IsTriggeredFadeOut())
             {
                 motionQueueEntry->StartFadeout(motionQueueEntry->GetFadeOutSeconds(), userTimeSeconds);
             }

+            // Looping motions never finish naturally. After fadeout completes,
+            // force-finish them so they get cleaned up from the queue.
+            if (motion->GetLoop() && motionQueueEntry->GetEndTime() > 0.0f
+                && motionQueueEntry->GetEndTime() < userTimeSeconds)
+            {
+                motionQueueEntry->IsFinished(true);
+            }
+
             ++ite;
```

---

## 4. 模型纹理环绕模式修复

**文件:** `Framework/src/Rendering/OpenGL/CubismShader_OpenGLES2.cpp` — `SetupTexture()`

**问题:** 模型纹理使用 `GL_REPEAT` 环绕模式。当 UV 坐标位于边界（0.0 或 1.0）时，线性过滤会从对边采样，导致 PNG 切分的模型组件边缘出现缝隙/色差。

**修复:** 改为 `GL_CLAMP_TO_EDGE`，避免边界采样穿越。

**严重性:** 中 — 影响所有使用紧密 UV 排布的模型的边缘渲染。

```diff
     glActiveTexture(GL_TEXTURE0);
     glBindTexture(GL_TEXTURE_2D, textureId);
-    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
-    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
+    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
+    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
```

---

## 5. CubismMotion 只读查询 API

**文件:** `Framework/src/Motion/CubismMotion.cpp` + `Framework/src/Motion/CubismMotion.hpp`

**问题:** 应用层需要查询运动曲线的目标 Part/Parameter 及最终帧值，用于切换持久化（`_paramOverrides`）和 Part 透明度保护（`_motionControlledParts`）。`_motionData` 是私有成员，无法从外部访问。

**修复:** 在 `CubismMotion` 中添加 4 个只读查询方法，不改变任何现有行为。

**严重性:** 关键 — 不添加则切换动画的参数持久化和 Part 透明度平滑过渡无法实现。

### 新增方法

| 方法 | 返回值 | 用途 |
|------|--------|------|
| `GetPartOpacityTargets()` | `vector<CubismIdHandle>` | 运动中 PartOpacity 曲线的 Part ID 列表 |
| `GetParameterTargets()` | `vector<CubismIdHandle>` | 运动中 Parameter 曲线的参数 ID 列表 |
| `GetParameterFinalValues()` | `vector<pair<Id, float>>` | 所有参数曲线的最终帧值 |
| `GetParameterChangedValues()` | `vector<pair<Id, float>>` | 仅返回首尾值不同的参数曲线 |

### CubismMotion.hpp

```diff
 #include "Type/CubismBasicType.hpp"
 #include "Type/csmVector.hpp"
 #include "Id/CubismId.hpp"
+#include <vector>
+#include <utility>

 // ... 在 public 区域添加：

+    std::vector<CubismIdHandle> GetPartOpacityTargets() const;
+    std::vector<CubismIdHandle> GetParameterTargets() const;
+    std::vector<std::pair<CubismIdHandle, float>> GetParameterFinalValues() const;
+    std::vector<std::pair<CubismIdHandle, float>> GetParameterChangedValues() const;
```

### CubismMotion.cpp

```diff
+std::vector<CubismIdHandle> CubismMotion::GetPartOpacityTargets() const
+{
+    std::vector<CubismIdHandle> result;
+    if (!_motionData) return result;
+    for (csmInt32 i = 0; i < _motionData->CurveCount; i++)
+    {
+        if (_motionData->Curves[i].Type == CubismMotionCurveTarget_PartOpacity)
+            result.push_back(_motionData->Curves[i].Id);
+    }
+    return result;
+}
+
+std::vector<CubismIdHandle> CubismMotion::GetParameterTargets() const
+{
+    std::vector<CubismIdHandle> result;
+    if (!_motionData) return result;
+    for (csmInt32 i = 0; i < _motionData->CurveCount; i++)
+    {
+        if (_motionData->Curves[i].Type == CubismMotionCurveTarget_Parameter)
+            result.push_back(_motionData->Curves[i].Id);
+    }
+    return result;
+}
+
+std::vector<std::pair<CubismIdHandle, float>> CubismMotion::GetParameterFinalValues() const
+{
+    std::vector<std::pair<CubismIdHandle, float>> result;
+    if (!_motionData) return result;
+    for (csmInt32 i = 0; i < _motionData->CurveCount; i++)
+    {
+        const auto& curve = _motionData->Curves[i];
+        if (curve.Type != CubismMotionCurveTarget_Parameter || curve.SegmentCount <= 0) continue;
+        const auto& lastSeg = _motionData->Segments[curve.BaseSegmentIndex + curve.SegmentCount - 1];
+        csmInt32 lastPointIndex;
+        switch (lastSeg.SegmentType) {
+            case CubismMotionSegmentType_Bezier:
+                lastPointIndex = lastSeg.BasePointIndex + 3; break;
+            default:
+                lastPointIndex = lastSeg.BasePointIndex + 1; break;
+        }
+        result.push_back({curve.Id, _motionData->Points[lastPointIndex].Value});
+    }
+    return result;
+}
+
+std::vector<std::pair<CubismIdHandle, float>> CubismMotion::GetParameterChangedValues() const
+{
+    std::vector<std::pair<CubismIdHandle, float>> result;
+    if (!_motionData) return result;
+    for (csmInt32 i = 0; i < _motionData->CurveCount; i++)
+    {
+        const auto& curve = _motionData->Curves[i];
+        if (curve.Type != CubismMotionCurveTarget_Parameter || curve.SegmentCount <= 0) continue;
+        float startValue = _motionData->Points[_motionData->Segments[curve.BaseSegmentIndex].BasePointIndex].Value;
+        const auto& lastSeg = _motionData->Segments[curve.BaseSegmentIndex + curve.SegmentCount - 1];
+        csmInt32 lastPointIndex;
+        switch (lastSeg.SegmentType) {
+            case CubismMotionSegmentType_Bezier:
+                lastPointIndex = lastSeg.BasePointIndex + 3; break;
+            default:
+                lastPointIndex = lastSeg.BasePointIndex + 1; break;
+        }
+        float finalValue = _motionData->Points[lastPointIndex].Value;
+        if (std::abs(finalValue - startValue) > 0.001F)
+            result.push_back({curve.Id, finalValue});
+    }
+    return result;
+}
```

---

## 上游更新流程

1. 替换 `Framework/` 文件夹为新版 SDK
2. 在项目根目录执行：
   ```bash
   cd Framework && git diff HEAD > ../framework-patches.patch && cd ..
   # 或者如果补丁文件已存在：
   git apply framework-patches.patch
   ```
3. 若补丁因行号偏移失败，按上方 diff 手动修改 5 处
4. 编译并测试：
   - 开关动作是否正常切换（循环 + PartOpacity 不闪烁）
   - 服装/组件切换是否正常（PartOpacity 生效）
   - 动作队列是否正常清理（无泄漏）
