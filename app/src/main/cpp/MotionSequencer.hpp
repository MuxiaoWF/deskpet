#pragma once

#include <string>

#include <CubismFramework.hpp>
#include <Motion/ACubismMotion.hpp>
#include <Type/csmString.hpp>

struct GroupConfig;  // forward declaration from ModelConfigParser.hpp
class LAppLive2DManager;

/// Handles motion chaining (next_mtn / pre_mtn) and VarFloat evaluation.
/// Integrates with both LAppModel and LAppModelCubism2 via callbacks.
namespace MotionSequencer {

    /// Maximum chain depth for next_mtn recursion (prevents infinite loops).
    constexpr int MAX_CHAIN_DEPTH = 10;

    /// Find a group whose id matches a VarFloat name (handles pinyin→PartID mapping).
    /// Returns nullptr if no match found. Pointer is valid as long as model config lives.
    GroupConfig* FindGroupForVarFloat(LAppLive2DManager* mgr, const std::string& vfName);

    /// Call before starting a motion. If pre_mtn is set, plays the predecessor first
    /// and stores the target motion to play after pre_mtn finishes.
    /// Returns true if a pre_motion was started (caller should skip its own start).
    bool HandlePreMotion(const std::string& group, int index, int priority = 3, float fadeIn = -1.0F, float fadeOut = -1.0F);

    /// Set up motion sequencing callbacks on a Cubism3/4/5 motion.
    /// Call this after the motion is created/loaded, before starting it.
    /// The motion pointer must remain valid for the lifetime of the callback.
    /// Optional user callbacks are chained: sequencer runs first, then user callbacks.
    void SetupMotionCallbacks(void* motion, const std::string& group, int index,
                              Csm::ACubismMotion::FinishedMotionCallback userFinished = nullptr,
                              Csm::ACubismMotion::BeganMotionCallback userBegan = nullptr);

    /// Cubism2 motion-finished handler. Call from LAppModelCubism2's NotifyMotionFinished.
    void OnMotionFinishedCubism2(const char* group, int index);

    /// Cubism2 motion-began handler. Call from LAppModelCubism2's NotifyMotionBegan.
    void OnMotionBeganCubism2(const char* group, int index);

    /// Evaluate VarFloat entries for a motion (called on begin and finish).
    void EvaluateVarFloats(const std::string& group, int index);

    /// Parse a motion string "Group:Index" or "Group" and start the motion.
    void StartMotionFromString(const std::string& motionStr);

    /// Select a motion index from a group by evaluating VarFloat conditions.
    /// Returns the index of the first entry whose Type 0 VarFloats are all satisfied,
    /// or -1 if no conditions match (caller should fall back to random).
    int SelectMotionByVarFloats(const std::string& group);

    /// Check if a motion group is a toggle group (has entries with both Type=1 condition
    /// and Type=2 assign VarFloats in the same entry). Detects toggle structure regardless
    /// of group name prefix (replaces hardcoded "开关-" checks).
    bool IsToggleGroup(const std::string& group);

    /// Re-sync GroupConfig.currentIndex from current VarFloat state.
    /// Part opacity is computed per-frame by ApplyVarFloatPartOverrides.
    void SyncVarFloatPartOverrides();

    /// Reset chain depth counter and root. Call when releasing all models
    /// to prevent stale chain state from persisting across model loads.
    void ResetChainState();

    // Gesture types for KeyTrigger
    enum GestureType {
        GestureDoubleTap = 0,
        GestureLongPress = 1,
        GestureSwipeUp = 2,
        GestureSwipeDown = 3,
        GestureSwipeLeft = 4,
        GestureSwipeRight = 5,
        GestureVolumeUp = 6,
        GestureVolumeDown = 7,
        GesturePinch = 8,
        GestureShake = 9,
    };
}
