#include "LAppDelegate.hpp"
#include <iostream>
#include <mutex>
#include <GLES2/gl2.h>
#include "LAppView.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppTextureManager.hpp"
#include "LAppModel.hpp"

#include <Rendering/OpenGL/CubismShader_OpenGLES2.hpp>

using namespace Csm;
using namespace std;
using namespace LAppDefine;

namespace {
    LAppDelegate* s_instance = nullptr;
    bool s_released = false;
    std::mutex s_instanceMutex;
}

LAppDelegate* LAppDelegate::GetInstance()
{
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    if (s_instance == nullptr && !s_released)
    {
        s_instance = new LAppDelegate();
    }
    return s_instance;
}

void LAppDelegate::ReleaseInstance()
{
    if (s_instance != nullptr)
    {
        delete s_instance;
        s_instance = nullptr;
        s_released = true;
    }
}

void LAppDelegate::ResetReleased()
{
    s_released = false;
}

void LAppDelegate::OnStart()
{
    s_released = false;
    _isActive = true;
    _isStopped.store(false, std::memory_order_release);
}

void LAppDelegate::OnStop()
{
    _isStopped.store(true, std::memory_order_release);  // release: Run() acquire will see _view deletion below
    s_released = true;

    if (_view)
    {
        delete _view;
        _view = nullptr;
    }
    if (_textureManager)
    {
        delete _textureManager;
        _textureManager = nullptr;
    }

    LAppLive2DManager::ReleaseInstance();
    CubismFramework::Dispose();
}

void LAppDelegate::OnDestroy()
{
    ReleaseInstance();
}

void LAppDelegate::Run()
{
    if (_isStopped.load(std::memory_order_acquire)) return;  // acquire: ensures _view read below sees OnStop's writes

    LAppPal::UpdateTime();

    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    LAppView* view = _view;  // snapshot after acquire fence
    if (view != nullptr)
    {
        view->Render();
    }

    _randomSpeaker.Update(LAppPal::GetDeltaTime());
}

void LAppDelegate::OnSurfaceCreate()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const bool wasInitialized = CubismFramework::IsInitialized();

    if (_textureManager == nullptr)
    {
        _textureManager = new LAppTextureManager();
    }
    else
    {
        _textureManager->ReleaseInvalidTextures();
    }
    if (_view != nullptr)
    {
        delete _view;
    }
    _view = new LAppView();
    LAppPal::UpdateTime();

    if (!wasInitialized)
    {
        CubismFramework::Initialize();
    }

    // 無効になっているOpenGLリソースを破棄
    Live2D::Cubism::Framework::Rendering::CubismShader_OpenGLES2::GetInstance()->ReleaseInvalidShaderProgram();
    // シェーダコードを再読み込みするためにインスタンスを破棄しておく
    Live2D::Cubism::Framework::Rendering::CubismShader_OpenGLES2::DeleteInstance();

    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    for (Csm::csmUint32 i = 0; i < live2DManager->GetModelNum(); i++)
    {
        LAppModel* m = live2DManager->GetModel(i);
        if (m) m->ReloadRenderer();
    }
    // Reload Cubism 2 model GL resources (shader, VBOs, textures)
    live2DManager->ReloadCubism2Renderers();
}

void LAppDelegate::OnSurfaceChanged(float width, float height)
{
    glViewport(0, 0, width, height);
    _width = width;
    _height = height;

    if (_view != nullptr)
    {
        _view->Initialize(width, height);
    }

    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    if (live2DManager != nullptr)
    {
        live2DManager->SetRenderTargetSize(width, height);
    }
}

void LAppDelegate::LoadModel(const char* modelPath)
{
    if (!CubismFramework::IsInitialized())
    {
        LAppPal::PrintLogLn("[APP]Framework not initialized, cannot load model");
        return;
    }
    LAppLive2DManager::GetInstance()->LoadModelFromPath(modelPath);
}

LAppDelegate::LAppDelegate():
    _cubismOption(),
    _captured(false),
    _mouseX(0.0F),
    _mouseY(0.0F),
    _isActive(true),
    _isStopped(false),
    _textureManager(nullptr),
    _view(nullptr),
    _width(0),
    _height(0)
{
    _cubismOption.LogFunction = LAppPal::PrintMessageLn;
    _cubismOption.LoggingLevel = LAppDefine::CubismLoggingLevel;
    _cubismOption.LoadFileFunction = LAppPal::LoadFileAsBytes;
    _cubismOption.ReleaseBytesFunction = LAppPal::ReleaseBytes;
    CubismFramework::CleanUp();
    CubismFramework::StartUp(&_cubismAllocator, &_cubismOption);
}

LAppDelegate::~LAppDelegate()
{
}

void LAppDelegate::OnTouchBegan(double x, double y)
{
    _mouseX = static_cast<float>(x);
    _mouseY = static_cast<float>(y);

    if (_view != nullptr)
    {
        _captured = true;
        _view->OnTouchesBegan(_mouseX, _mouseY);
    }
}

void LAppDelegate::OnTouchEnded(double x, double y, bool wasDragging)
{
    _mouseX = static_cast<float>(x);
    _mouseY = static_cast<float>(y);

    if (_view != nullptr)
    {
        _captured = false;
        _view->OnTouchesEnded(_mouseX, _mouseY, wasDragging);
    }
}

void LAppDelegate::OnTouchCancelled()
{
    _captured = false;
    if (_view != nullptr)
    {
        _view->OnTouchesCancelled();
    }
}

void LAppDelegate::OnTouchMoved(double x, double y)
{
    _mouseX = static_cast<float>(x);
    _mouseY = static_cast<float>(y);

    if (_captured && _view != nullptr)
    {
        _view->OnTouchesMoved(_mouseX, _mouseY);
    }
}
