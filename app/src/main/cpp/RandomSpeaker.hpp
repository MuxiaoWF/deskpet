#pragma once

#include <string>
#include <vector>

#include "ModelConfigParser.hpp"

class LAppLive2DManager;

/// Periodically triggers random motions from configured groups when the pet is idle.
class RandomSpeaker {
public:
    void Initialize(const ModelConfig& config);
    void SetEnabled(bool enabled) { _enabled = enabled; }
    void SetInterval(int seconds) { _interval = static_cast<float>(seconds); }
    void Update(float dt);

private:
    bool _enabled = false;
    float _interval = 30.0F;
    float _timer = 0;
    std::vector<std::string> _eligibleGroups;
};
