#pragma once

#include <string>
#include <vector>
#include <map>

// Forward-declared sub-structures used by MotionMeta

// VarFloat config (mirrors ModelConfigData.VarFloat)
struct VarFloatConfig {
    std::string name;
    int type = 0;
    std::string code;
    float greater = 0;
    float lower = 0;
    float equal = 0;
    float not_equal = 0;
    float assign = 0;
    float add = 0;
    float subtract = 0;
    float multiply = 0;
    float divide = 0;
    // Flags to distinguish "field set to 0" from "field not set" (needed for Code parsing)
    bool has_equal = false;
    bool has_not_equal = false;
    bool has_greater = false;
    bool has_lower = false;
    bool has_assign = false;
    bool has_add = false;
    bool has_subtract = false;
    bool has_multiply = false;
    bool has_divide = false;
};

// Choice config (mirrors ModelConfigData.Choice)
struct ChoiceConfig {
    std::string text;
    std::string group;
    std::string motion;
    std::string next_mtn;
};

// TimeLimit config (mirrors ModelConfigData.TimeLimit)
struct TimeLimitConfig {
    int hour = -1;
    int minute = -1;
    int month = -1;
    int day = -1;
    int begin = -1;
    int end = -1;
    int sustain = -1;
    bool birthday = false;
};

// Motion metadata from config.mlve (mirrors ModelConfigData.Motion from Live2DViewerEX)
struct MotionMeta {
    std::string name;
    std::string file;
    std::string sound;
    std::string text;
    std::string expression;
    std::string command;
    std::string post_command;
    std::string next_mtn;
    std::string pre_mtn;
    std::string language;
    std::string group;
    int fade_in = 0;
    int fade_out = 0;
    int priority = 2;
    int duration = 0;
    int weight = 1;
    int sound_delay = 0;
    int sound_channel = 0;
    int text_delay = 0;
    int text_duration = 0;
    int motion_duration = 0;
    int blend_mode = 0;
    int wrap_mode = 0;  // 0=once, 1=loop, 2=pingpong
    float sound_volume = 1.0F;
    float speed = 1.0F;
    float blend_weight = 1.0F;
    bool file_loop = false;
    bool sound_loop = false;
    bool enabled = true;
    bool ignorable = false;
    bool interruptable = true;
    bool override_face_params = false;
    int motionIndex = 0;  // index within group (set during parsing)

    // Sub-structures (from dump.cs ModelConfigData.Motion)
    std::vector<ChoiceConfig> choices;
    std::vector<VarFloatConfig> var_floats;
    TimeLimitConfig time_limit;
    std::string require_costume;  // costume set name required for this motion to play
};

// Group config from config.mlve (mirrors ModelConfigData.Group)
struct GroupConfig {
    std::string target;
    std::string name;
    std::string text;
    std::string var_float;  // explicit VarFloat name controlling this group (e.g., "1.kuzi")
    std::vector<std::string> ids;
    std::vector<std::string> axes;
    std::vector<std::string> keys;
    std::vector<float> factors;
    std::vector<float> values;
    float value = 0;
    bool hidden = false;
    int currentIndex = -1;  // cycle position for values[], -1 = unset
};

// HitParam config (mirrors ModelConfigData.HitParam)
struct HitParamConfig {
    std::string name;
    std::string id;
    std::string hit_area;
    int axis = 0;
    float weight = 1.0F;
    int distance = 0;
    float factor = 1.0F;
    int release = 0;
    int release_type = 0;
    int type = 0;
    bool relative = false;  // true: use drag displacement from touch start; false: absolute position
    bool lock_param = false;
    bool low_priority = false;
    bool enabled = true;
    std::string min_group;
    std::string min_motion;
    std::string max_group;
    std::string max_motion;
    std::string min_mtn;
    std::string max_mtn;
    std::string begin_mtn;
    std::string end_mtn;
};

// LoopParam config (mirrors ModelConfigData.LoopParam)
struct LoopParamConfig {
    std::string name;
    std::string id;
    std::vector<std::string> ids;
    int type = 0;
    int duration = 0;
    int blend_mode = 0;
    float weight = 1.0F;
    bool time_sync = false;
    int min_interval = 0;
    int max_interval = 0;
    float min_value = 0;
    float max_value = 1.0F;
    bool enabled = true;
};

// ParamTrigger config (mirrors ModelConfigData.ParamTrigger)
struct ParamTriggerItemConfig {
    float value = 0;
    int direction = 0;
    std::string motion;
};

struct ParamTriggerConfig {
    std::string name;
    std::string id;
    std::vector<ParamTriggerItemConfig> items;
};

// AreaTrigger config (mirrors ModelConfigData.AreaTriggerItem)
struct AreaTriggerConfig {
    std::string name;
    std::string target_area;
    std::string enter_mtn;
    std::string exit_mtn;
    std::vector<std::string> trigger_areas;
};

// KeyTrigger config (mirrors ModelConfigData.KeyTrigger)
struct KeyTriggerConfig {
    int input = 0;
    std::string down_mtn;
    std::string up_mtn;
};

// HandTrigger config (mirrors ModelConfigData.HandTriggerItem)
struct HandTriggerConfig {
    std::string name;
    int hand = 0;
    int direction = 0;
    std::string motion;
    std::vector<int> fingers;
    std::vector<int> finger_gaps;
};

// ParamInfo config (mirrors ModelConfigData.ParamInfo)
struct ParamInfoConfig {
    std::string id;
    float min = 0;
    float max = 0;
    float default_value = 0;
    float weight = 1.0F;
    bool inverted = false;
    int blend_mode = 0;
    float value = 0;
    int axis = 0;
    int order = 0;
    float factor = 1.0F;
    int input = 0;
    bool clamp_min_max = false;
};

// KeyValue config (mirrors ModelConfigData.KeyValue)
struct KeyValueConfig {
    std::string key;
    float value = 0;
};

// ModelValue config (mirrors ModelConfigData.ModelValue)
struct ModelValueConfig {
    std::string name;
    std::string text;
    std::vector<std::string> ids;
    float value = 0;
    std::vector<KeyValueConfig> key_values;
    bool hidden = false;
};

// FaceTrackingController config (mirrors ModelConfigData.FaceTrackingController)
struct FaceTrackingControllerConfig {
    bool enabled = true;
    std::vector<ParamInfoConfig> angle_x;
    std::vector<ParamInfoConfig> angle_y;
    std::vector<ParamInfoConfig> angle_z;
    std::vector<ParamInfoConfig> position_x;
    std::vector<ParamInfoConfig> position_y;
    std::vector<ParamInfoConfig> position_z;
    std::vector<ParamInfoConfig> eye_l_open;
    std::vector<ParamInfoConfig> eye_r_open;
    std::vector<ParamInfoConfig> eye_wide;
    std::vector<ParamInfoConfig> eye_squint;
    std::vector<ParamInfoConfig> eyeball_x;
    std::vector<ParamInfoConfig> eyeball_y;
    std::vector<ParamInfoConfig> brow_l_y;
    std::vector<ParamInfoConfig> brow_r_y;
    std::vector<ParamInfoConfig> mouth_open_y;
    std::vector<ParamInfoConfig> mouth_form;
    std::vector<ParamInfoConfig> mouth_pucker;
    std::vector<ParamInfoConfig> mouth_funnel;
    std::vector<ParamInfoConfig> cheek_puff;
    std::vector<ParamInfoConfig> tongue_out;
};

// HandTrackingController config (mirrors ModelConfigData.HandTrackingController)
struct HandTrackingControllerConfig {
    bool enabled = true;
    std::vector<ParamInfoConfig> left_angle_x;
    std::vector<ParamInfoConfig> left_angle_z;
    std::vector<ParamInfoConfig> left_position_x;
    std::vector<ParamInfoConfig> left_position_y;
    std::vector<ParamInfoConfig> left_thumb;
    std::vector<ParamInfoConfig> left_index;
    std::vector<ParamInfoConfig> left_middle;
    std::vector<ParamInfoConfig> left_ring;
    std::vector<ParamInfoConfig> left_pinky;
    std::vector<ParamInfoConfig> left_gap_ti;
    std::vector<ParamInfoConfig> left_gap_im;
    std::vector<ParamInfoConfig> left_gap_mr;
    std::vector<ParamInfoConfig> left_gap_rp;
    std::vector<ParamInfoConfig> right_angle_x;
    std::vector<ParamInfoConfig> right_angle_z;
    std::vector<ParamInfoConfig> right_position_x;
    std::vector<ParamInfoConfig> right_position_y;
    std::vector<ParamInfoConfig> right_thumb;
    std::vector<ParamInfoConfig> right_index;
    std::vector<ParamInfoConfig> right_middle;
    std::vector<ParamInfoConfig> right_ring;
    std::vector<ParamInfoConfig> right_pinky;
    std::vector<ParamInfoConfig> right_gap_ti;
    std::vector<ParamInfoConfig> right_gap_im;
    std::vector<ParamInfoConfig> right_gap_mr;
    std::vector<ParamInfoConfig> right_gap_rp;
};

// ModelValueController config (mirrors ModelConfigData.ModelValueController)
struct ModelValueControllerConfig {
    bool enabled = true;
    std::vector<ModelValueConfig> items;
};

// ArtmeshCullController config (mirrors ModelConfigData.ArtmeshCullController)
struct ArtmeshCullControllerConfig {
    bool enabled = true;
    int default_mode = 0;
    std::vector<std::string> cull_front;
    std::vector<std::string> cull_back;
    std::vector<std::string> cull_none;
};

// GenericParamController config (mirrors ModelConfigData.GenericParamController)
struct GenericParamControllerConfig {
    bool enabled = true;
    float smooth_time = 0;
    std::vector<ParamInfoConfig> items;
};

// Controller configs
struct ControllersConfig {
    // param_hit
    std::vector<HitParamConfig> hit_params;
    // param_loop
    std::vector<LoopParamConfig> loop_params;
    // param_trigger
    std::vector<ParamTriggerConfig> param_triggers;
    // area_trigger
    std::vector<AreaTriggerConfig> area_triggers;
    // key_trigger
    std::vector<KeyTriggerConfig> key_triggers;
    // hand_trigger
    std::vector<HandTriggerConfig> hand_triggers;

    // Controller enable flags (from BaseController.enabled + specific fields)
    bool eye_blink_enabled = true;
    bool auto_breath_enabled = true;
    bool mouse_tracking_enabled = true;
    bool lip_sync_enabled = false;
    bool extra_motion_enabled = false;
    bool accelerometer_enabled = false;
    bool random_speak_enabled = false;
    int random_speak_interval = 30;  // seconds

    // smooth_time for param controllers
    float smooth_time = 0;

    // New controllers from dump.cs alignment
    GenericParamControllerConfig eye_blink;
    GenericParamControllerConfig lip_sync;
    GenericParamControllerConfig mouse_tracking;
    GenericParamControllerConfig auto_breath;
    GenericParamControllerConfig extra_motion;
    GenericParamControllerConfig accelerometer;
    GenericParamControllerConfig microphone;
    GenericParamControllerConfig transform;
    GenericParamControllerConfig look_at;
    FaceTrackingControllerConfig face_tracking;
    HandTrackingControllerConfig hand_tracking;
    ModelValueControllerConfig param_value;
    ModelValueControllerConfig part_opacity;
    ModelValueControllerConfig artmesh_opacity;
    ModelValueControllerConfig artmesh_color;
    ArtmeshCullControllerConfig artmesh_culling;
    ModelValueControllerConfig slot_opacity;
    ModelValueControllerConfig slot_color;
};

// Costume set config (for grouped component switching with mutual exclusion)
struct CostumeSetConfig {
    std::string name;                       // set display name
    std::string var_float;                  // VarFloat variable controlling this set
    std::vector<std::string> groups;        // GroupConfig targets in this set
    std::vector<std::string> mutual_exclude;// other set names to deactivate
};

// Full model config parsed from config.mlve
struct ModelConfig {
    std::map<std::string, std::vector<MotionMeta>> motions;
    std::vector<GroupConfig> groups;
    ControllersConfig controllers;
    std::map<std::string, float> var_floats;  // runtime variable store
    std::vector<CostumeSetConfig> costume_sets;
    std::map<std::string, std::vector<std::string>> var_float_part_overrides;
    // key: var_float name (e.g., "1.pidai"), value: Part ID(s) to control
};

// Parser entry point
ModelConfig ParseModelConfig(const std::string& json);
