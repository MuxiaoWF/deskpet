#pragma once

#include <atomic>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "LAppAllocator_Common.hpp"
#include "RandomSpeaker.hpp"

class LAppView;
class LAppTextureManager;

/// Singleton application delegate: owns GL context lifecycle, rendering loop,
/// touch dispatch, and Cubism framework initialization.
class LAppDelegate
{
public:
    static LAppDelegate* GetInstance();
    static void ReleaseInstance();
    static void ResetReleased();

    void OnStart();
    void OnStop();
    void OnDestroy();

    void OnSurfaceCreate();
    void OnSurfaceChanged(float width, float height);

    void Run();
    bool IsRendering() const { return !_isStopped.load(std::memory_order_acquire); }

    void OnTouchBegan(double x, double y);
    void OnTouchEnded(double x, double y, bool wasDragging = false);
    void OnTouchCancelled();
    void OnTouchMoved(double x, double y);

    void LoadModel(const char* modelPath);

    LAppTextureManager* GetTextureManager() { return _textureManager; }
    int GetWindowWidth() { return _width; }
    int GetWindowHeight() { return _height; }
    LAppView* GetView() { return _view; }
    RandomSpeaker& GetRandomSpeaker() { return _randomSpeaker; }

private:
    LAppDelegate();
    ~LAppDelegate();

    LAppAllocator_Common _cubismAllocator;
    Csm::CubismFramework::Option _cubismOption;
    LAppTextureManager* _textureManager;
    LAppView* _view;
    int _width;
    int _height;
    bool _captured;
    bool _isActive;
    std::atomic<bool> _isStopped;  // true = OnStop/OnDestroy in progress, block Run()
    float _mouseY;
    float _mouseX;
    RandomSpeaker _randomSpeaker;
};
