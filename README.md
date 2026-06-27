# DeskPet - Live2D Android 桌面宠物

[中文](README.md) | [English](README.en.md)

Android Live2D 桌面宠物。兼容 Live2DViewerEX 的模型格式（json / LPK / WPK），通过系统悬浮窗常驻桌面，原生 C++ + OpenGL ES 2.0 渲染。

**本应用建议只作为预览使用，部分功能并不完善（例如组件开关、拖拽组件、声音播放等），如要体验模型完整功能建议使用Live2DViewerEX**

**项目可能存在部分展示bug，如要体验模型完整功能建议使用Live2DViewerEX**

**如要体验模型完整功能建议使用Live2DViewerEX**

**项目不得用于非法用途，仅当学习使用**

---

## DeskPet

### 桌面悬浮窗

- **贴边隐藏** — 拖至屏幕边缘自动收起，保留可见比例可调
- **点击穿透** — 模型区域不拦截触摸事件，透明度可调
- **自动跟随** — 模型视线跟随触摸位置

### 其他

- **深色模式** — 顶栏开关
- **多语言** — English / 简体中文 / 繁體中文
- **HitArea 可视化** — 开发者工具，红色线框显示触摸区域
- **拖拽/长按独立开关**
- **wpk/lpk模型导出**

---

## 基础功能

与 Live2DViewerEX 共享的能力，不重复赘述：

- Cubism 2 / 3 / 4 / 5 全版本渲染
- 物理模拟（头发、衣物，权重可调，可能有效）
- 表情切换与混合（Cubism2 支持 Overwrite / Add / Multiply）
- HitArea 触摸（点击区域触发动作或切换组件）
- 组件切换（Groups 配置的服装/配件循环显示）
- 随机语音（空闲时自动播放，文字气泡显示）
- 模型管理（重命名/导出/删除）
- 文件夹导入（SAF）、LPK/WPK 导入、Steam Workshop 支持

---

## 项目结构

```
deskpet/
├── app/src/main/java/com/muxiao/deskpet/
│   ├── MainActivity.java           # 主界面
│   ├── FloatingWindowService.java  # 悬浮窗服务
│   ├── ActionMenuManager.java      # 长按菜单
│   ├── EdgeHideManager.java        # 贴边隐藏
│   ├── TextBubbleOverlay.java      # 文字气泡
│   ├── MotionSoundPlayer.java      # 音效播放
│   ├── ModelImporter.java          # 模型导入（SAF / LPK / WPK）
│   ├── LpkUnpacker.java            # LPK/WPK 解密
│   ├── FileUtils.java              # 文件工具
│   └── live2d/Live2DNativeBridge.java
├── app/src/main/cpp/
│   ├── LAppLive2DManager.cpp       # 模型管理、HitArea、镜像/旋转
│   ├── LAppModel.cpp               # Cubism 3/4/5 模型加载与渲染
│   ├── LAppModelCubism2.cpp        # Cubism 2 模型加载与渲染
│   ├── ControllerEngine.cpp        # 参数控制器引擎
│   ├── ModelConfigParser.cpp       # config.mlve / model3.json 解析
│   ├── MotionSequencer.cpp         # 动作序列、VarFloat、Toggle 组
│   ├── LAppDelegate.cpp            # 应用生命周期、OpenGL 初始化
│   ├── LAppView.cpp                # 触摸事件与渲染视图
│   ├── JniBridgeC.cpp              # Java ↔ C++ JNI 桥接
│   ├── Cubism2MocLoader.cpp        # Cubism 2 moc 文件加载
│   ├── RandomSpeaker.cpp           # 随机语音
│   └── LAppPal.cpp                 # 平台抽象（时间、路径、日志）
├── Core/                           # Live2D Cubism SDK
├── Framework/                      # [submodule] Cubism Native Framework
└── Samples/                        # [submodule] Cubism Native Samples
```

---

## 许可证

本项目仅供学习与研究用途。Live2D Cubism SDK 的使用需遵守 [Live2D 许可协议](https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html)。

GNU GENERAL PUBLIC LICENSE Version 3

---

## 项目预览图：

![header](preview.png)
