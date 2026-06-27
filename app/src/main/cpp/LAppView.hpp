#pragma once

#include "LAppView_Common.hpp"
#include "TouchManager_Common.hpp"

class LAppModel;

/// Rendering view: dispatches touch events, renders models, and draws debug hit area overlays.
class LAppView : public LAppView_Common
{
public:
    LAppView();
    ~LAppView();

    void Initialize(int width, int height);
    void Render();

    void OnTouchesBegan(float pointX, float pointY) const;
    void OnTouchesMoved(float pointX, float pointY) const;
    void OnTouchesEnded(float pointX, float pointY, bool wasDragging = false);
    void OnTouchesCancelled();

    void PostModelDraw(LAppModel &refModel);

private:
    TouchManager_Common* _touchManager;
};
