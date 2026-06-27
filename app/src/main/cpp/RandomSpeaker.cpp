#include "RandomSpeaker.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"
#include "LAppModelCubism2.hpp"
#include "LAppPal.hpp"
#include <cstdlib>

void RandomSpeaker::Initialize(const ModelConfig& config) {
    _eligibleGroups.clear();
    _timer = 0;

    // Collect groups that have motions suitable for random speaking
    for (const auto& entry : config.motions) {
        const std::string& groupName = entry.first;
        const std::vector<MotionMeta>& motions = entry.second;
        if(motions.empty()) {
            continue;
        }

        // Include groups that have at least one enabled motion with text
        bool hasText = false;
        for (const auto& m : motions) {
            if (m.enabled && !m.text.empty()) {
                hasText = true;
                break;
            }
        }
        if (hasText) {
            _eligibleGroups.push_back(groupName);
        }
    }

    // If no text groups, fall back to any enabled motion group
    if (_eligibleGroups.empty()) {
        for (const auto& entry : config.motions) {
            if (!entry.second.empty() && entry.second[0].enabled) {
                _eligibleGroups.push_back(entry.first);
            }
        }
    }

    // Check for random_speak config in controllers
    // (Could be extended to read from ControllersConfig if needed)
}

void RandomSpeaker::Update(float dt) {
    if(!_enabled || _eligibleGroups.empty()) {
        return;
    }

    _timer += dt;
    if (_timer >= _interval) {
        _timer = 0;

        // Pick a random group
        const int idx = rand() % static_cast<int>(_eligibleGroups.size());
        const std::string& group = _eligibleGroups[idx];

        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        if (mgr->IsCubism2Model()) {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if (m) m->StartRandomMotion(group.c_str(), 1);  // PriorityIdle
        } else {
            LAppModel* m = mgr->GetModel(0);
            if (m) m->StartRandomMotion(group.c_str(), 1);  // PriorityIdle
        }
    }
}
