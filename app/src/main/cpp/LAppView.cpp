#include "LAppView.hpp"
#include <cmath>
#include <string>
#include <GLES2/gl2.h>
#include "LAppPal.hpp"
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppTextureManager.hpp"
#include "LAppDefine.hpp"
#include "TouchManager_Common.hpp"
#include "LAppModel.hpp"

using namespace std;
using namespace LAppDefine;

// --- GLES2 debug hit area overlay ---
static GLuint s_debugProgram = 0;
static GLint  s_debugMvpLoc = -1;
static GLint  s_debugPosLoc = -1;
static GLint  s_debugColorLoc = -1;

static const char* s_debugVertSrc =
    "uniform mat4 u_mvp;\n"
    "attribute vec4 a_position;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * a_position;\n"
    "}\n";

static const char* s_debugFragSrc =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "    gl_FragColor = u_color;\n"
    "}\n";

static GLuint compileShader(GLenum type, const char* src) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LAppPal::PrintLogLn("[HITAREA] shader compile FAILED: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void ensureDebugProgram() {
    if(s_debugProgram != 0) {
        return;
    }
    const GLuint vs = compileShader(GL_VERTEX_SHADER, s_debugVertSrc);
    if (!vs) {
        LAppPal::PrintLogLn("[HITAREA] vertex shader compile FAILED");
        return;
    }
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, s_debugFragSrc);
    if (!fs) {
        LAppPal::PrintLogLn("[HITAREA] fragment shader compile FAILED");
        glDeleteShader(vs);
        return;
    }
    s_debugProgram = glCreateProgram();
    glAttachShader(s_debugProgram, vs);
    glAttachShader(s_debugProgram, fs);
    glLinkProgram(s_debugProgram);
    GLint linkOk = 0;
    glGetProgramiv(s_debugProgram, GL_LINK_STATUS, &linkOk);
    if (!linkOk) {
        LAppPal::PrintLogLn("[HITAREA] shader program link FAILED");
        glDeleteProgram(s_debugProgram);
        s_debugProgram = 0;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    s_debugMvpLoc = glGetUniformLocation(s_debugProgram, "u_mvp");
    s_debugPosLoc = glGetAttribLocation(s_debugProgram, "a_position");
    s_debugColorLoc = glGetUniformLocation(s_debugProgram, "u_color");
}

LAppView::LAppView()
    : LAppView_Common()
{
    _touchManager = new TouchManager_Common();

    // Invalidate debug shader on surface recreation.
    // If the GL context survived (surface-only recreation), delete the old program to avoid leak.
    if (s_debugProgram != 0)
    {
        glDeleteProgram(s_debugProgram);
        s_debugProgram = 0;
        s_debugMvpLoc = -1;
        s_debugPosLoc = -1;
        s_debugColorLoc = -1;
    }
}

LAppView::~LAppView()
{
    if (_touchManager)
    {
        delete _touchManager;
    }
}

void LAppView::Initialize(int width, int height)
{
    LAppView_Common::Initialize(width, height);
}

void LAppView::Render()
{
    LAppLive2DManager::GetInstance()->OnUpdate();
}

void LAppView::OnTouchesBegan(float pointX, float pointY) const
{
    _touchManager->TouchesBegan(pointX, pointY);
    float viewX = TransformViewX(pointX);
    float viewY = TransformViewY(pointY);
    LAppLive2DManager::GetInstance()->OnHitAreaBegan(viewX, viewY);
}

void LAppView::OnTouchesMoved(float pointX, float pointY) const
{
    _touchManager->TouchesMoved(pointX, pointY);

    float viewX = this->TransformViewX(_touchManager->GetX());
    float viewY = this->TransformViewY(_touchManager->GetY());

    LAppLive2DManager::GetInstance()->OnDragWithHitArea(viewX, viewY);
}

void LAppView::OnTouchesEnded(float pointX, float pointY, bool wasDragging)
{
    (void)pointX;
    (void)pointY;
    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    live2DManager->OnDrag(0.0F, 0.0F);

    float x = TransformViewX(_touchManager->GetX());
    float y = TransformViewY(_touchManager->GetY());

    if (!wasDragging) {
        // Tap: fire up_mtn then OnTap for hit area dispatch
        live2DManager->OnHitAreaEnded(x, y);
    }
    live2DManager->OnTap(x, y, wasDragging);

    // Reset hit area tracking so next drag starts fresh
    live2DManager->ResetHitAreaTracking();
}

void LAppView::OnTouchesCancelled()
{
    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    live2DManager->OnDrag(0.0F, 0.0F);
    live2DManager->ResetHitAreaTracking();
}

void LAppView::PostModelDraw(LAppModel &refModel)
{
    if(!LAppLive2DManager::GetInstance()->IsDebugHitAreaVisible()) {
        return;
    }

    auto& configs = LAppLive2DManager::GetInstance()->GetHitAreaConfigs();
    if(configs.empty()) {
        return;
    }

    Csm::CubismModelMatrix* modelMatrix = refModel.GetModelMatrix();
    if (modelMatrix == nullptr) {
        LAppPal::PrintLogLn("[HITAREA] modelMatrix is NULL");
        return;
    }

    ensureDebugProgram();
    if(s_debugProgram == 0) {
        return;
    }

    // Build the same MVP as the Cubism renderer: projection * viewMatrix * modelMatrix
    const int width = LAppDelegate::GetInstance()->GetWindowWidth();
    const int height = LAppDelegate::GetInstance()->GetWindowHeight();
    if(width == 0 || height == 0) {
        return;
    }

    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    const float displayRatio = static_cast<float>(height) / static_cast<float>(width);

    Csm::CubismMatrix44 mvp;
    const Csm::CubismModel* cubismModel = refModel.GetModel();
    if (cubismModel != nullptr) {
        float canvasRatio = cubismModel->GetCanvasHeight() / cubismModel->GetCanvasWidth();
        if (canvasRatio < displayRatio) {
            mvp.Scale(1.0F, aspectRatio);
        } else {
            mvp.Scale(1.0F / aspectRatio, 1.0F);
        }
    }

    // Apply mirror/rotation in intermediate space (same as OnUpdate)
    auto* mgr = LAppLive2DManager::GetInstance();
    if (mgr->IsMirrored()) {
        mvp.Scale(-1.0F, 1.0F);
    }
    float rot = mgr->GetRotation();
    if (rot != 0.0F) {
        float rad = rot * 3.14159265f / 180.0f;
        float c = cosf(rad), s = sinf(rad);
        Csm::CubismMatrix44 rotMat;
        float* r = rotMat.GetArray();
        r[0] = c;   r[1] = s;
        r[4] = -s;  r[5] = c;
        mvp.MultiplyByMatrix(&rotMat);
    }

    Csm::CubismMatrix44* viewMatrix = mgr->GetViewMatrix();
    if (viewMatrix != nullptr) {
        mvp.MultiplyByMatrix(viewMatrix);
    }
    mvp.MultiplyByMatrix(modelMatrix);


    // Save and configure GL state for debug overlay
    GLint prevProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(s_debugProgram);
    if(s_debugMvpLoc >= 0) glUniformMatrix4fv(s_debugMvpLoc, 1, GL_FALSE, mvp.GetArray());
    if(s_debugColorLoc >= 0) glUniform4f(s_debugColorLoc, 1.0F, 0.0F, 0.0F, 1.0F);
    glLineWidth(4.0F);

    for (const auto& config : configs) {
        if(config.width <= 0 || config.height <= 0) {
            continue;
        }

        float cx = config.centerX;
        float cy = config.centerY;
        float hw = config.width * 0.5F;
        float hh = config.height * 0.5F;

        // Hit area coords are in model-local space — vertices stay as-is,
        // the MVP matrix handles the full transform to clip space.
        GLfloat vertices[] = {
            cx - hw, cy - hh,
            cx + hw, cy - hh,
            cx + hw, cy - hh,
            cx + hw, cy + hh,
            cx + hw, cy + hh,
            cx - hw, cy + hh,
            cx - hw, cy + hh,
            cx - hw, cy - hh
        };

        glVertexAttribPointer(s_debugPosLoc, 2, GL_FLOAT, GL_FALSE, 0, vertices);
        glEnableVertexAttribArray(s_debugPosLoc);
        glDrawArrays(GL_LINES, 0, 8);
        glDisableVertexAttribArray(s_debugPosLoc);
    }

    // Restore previous GL state
    glUseProgram(prevProgram);
    if(prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if(prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if(prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}
