#include "ModelConfigParser.hpp"
#include "third_party/nlohmann/json.hpp"
#include "LAppPal.hpp"
#include <cstring>

using json = nlohmann::json;

// Helper: get string from JSON trying both PascalCase and snake_case
static std::string optStringEither(const json& j, const char* key1, const char* key2, const char* def = "") {
    if (j.contains(key1) && j[key1].is_string()) return j[key1].get<std::string>();
    if (j.contains(key2) && j[key2].is_string()) return j[key2].get<std::string>();
    return def;
}

// Helper: get int from JSON trying both PascalCase and snake_case
static int optIntEither(const json& j, const char* key1, const char* key2, int def = 0) {
    if (j.contains(key1) && j[key1].is_number()) return j[key1].get<int>();
    if (j.contains(key2) && j[key2].is_number()) return j[key2].get<int>();
    return def;
}

// Helper: get float from JSON trying both PascalCase and snake_case
static float optFloatEither(const json& j, const char* key1, const char* key2, float def = 0) {
    if (j.contains(key1) && j[key1].is_number()) return j[key1].get<float>();
    if (j.contains(key2) && j[key2].is_number()) return j[key2].get<float>();
    return def;
}

// Helper: get bool from JSON trying both PascalCase and snake_case
static bool optBoolEither(const json& j, const char* key1, const char* key2, bool def = false) {
    if (j.contains(key1) && j[key1].is_boolean()) return j[key1].get<bool>();
    if (j.contains(key2) && j[key2].is_boolean()) return j[key2].get<bool>();
    return def;
}

static MotionMeta parseMotionMeta(const json& j, const std::string& group, int index) {
    MotionMeta m;
    m.name = optStringEither(j, "Name", "name");
    m.file = optStringEither(j, "File", "file");
    m.sound = optStringEither(j, "Sound", "sound");
    m.text = optStringEither(j, "Text", "text");
    m.expression = optStringEither(j, "Expression", "expression");
    m.command = optStringEither(j, "Command", "command");
    m.post_command = optStringEither(j, "PostCommand", "post_command");
    m.next_mtn = optStringEither(j, "NextMtn", "next_mtn");
    m.pre_mtn = optStringEither(j, "PreMtn", "pre_mtn");
    m.require_costume = optStringEither(j, "RequireCostume", "require_costume");
    m.language = optStringEither(j, "Language", "language");
    m.group = group;
    m.motionIndex = index;

    m.fade_in = optIntEither(j, "FadeIn", "fade_in", 0);
    m.fade_out = optIntEither(j, "FadeOut", "fade_out", 0);
    m.priority = optIntEither(j, "Priority", "priority", 2);
    m.duration = optIntEither(j, "Duration", "duration", 0);
    m.weight = optIntEither(j, "Weight", "weight", 1);
    m.sound_delay = optIntEither(j, "SoundDelay", "sound_delay", 0);
    m.sound_channel = optIntEither(j, "SoundChannel", "sound_channel", 0);
    m.text_delay = optIntEither(j, "TextDelay", "text_delay", 0);
    m.text_duration = optIntEither(j, "TextDuration", "text_duration", 0);
    m.motion_duration = optIntEither(j, "MotionDuration", "motion_duration", 0);
    m.blend_mode = optIntEither(j, "BlendMode", "blend_mode", 0);
    m.wrap_mode = optIntEither(j, "WrapMode", "wrap_mode", 0);

    m.sound_volume = optFloatEither(j, "SoundVolume", "sound_volume", 1.0F);
    m.speed = optFloatEither(j, "Speed", "speed", 1.0F);
    m.blend_weight = optFloatEither(j, "BlendWeight", "blend_weight", 1.0F);

    m.file_loop = optBoolEither(j, "FileLoop", "file_loop", false);
    m.sound_loop = optBoolEither(j, "SoundLoop", "sound_loop", false);
    m.enabled = optBoolEither(j, "Enabled", "enabled", true);
    m.ignorable = optBoolEither(j, "Ignorable", "ignorable", false);
    m.interruptable = optBoolEither(j, "Interruptable", "interruptable", true);
    m.override_face_params = optBoolEither(j, "OverrideFaceParams", "override_face_params", false);

    // Parse choices array
    const json* choicesJson = nullptr;
    if (j.contains("Choices") && j["Choices"].is_array()) {
        choicesJson = &j["Choices"];
    } else if (j.contains("choices") && j["choices"].is_array()) {
        choicesJson = &j["choices"];
    }
    if (choicesJson) {
        for (const auto& c : *choicesJson) {
            if (!c.is_object()) continue;
            ChoiceConfig ch;
            ch.text = c.value("text", "");
            ch.group = c.value("group", "");
            ch.motion = c.value("motion", "");
            ch.next_mtn = c.value("next_mtn", "");
            m.choices.push_back(ch);
        }
    }

    // Parse var_floats array
    const json* vfJson = nullptr;
    if (j.contains("VarFloats") && j["VarFloats"].is_array()) {
        vfJson = &j["VarFloats"];
    } else if (j.contains("var_floats") && j["var_floats"].is_array()) {
        vfJson = &j["var_floats"];
    }
    if (vfJson) {
        for (const auto& v : *vfJson) {
            if (!v.is_object()) continue;
            VarFloatConfig vf;
            // Support both PascalCase (model3.json) and lowercase (config.mlve)
            vf.name = v.value("Name", v.value("name", ""));
            vf.type = v.value("Type", v.value("type", 0));
            vf.code = v.value("Code", v.value("code", ""));
            vf.greater = v.value("Greater", v.value("greater", 0.0F));
            vf.lower = v.value("Lower", v.value("lower", 0.0F));
            vf.equal = v.value("Equal", v.value("equal", 0.0F));
            vf.not_equal = v.value("NotEqual", v.value("not_equal", 0.0F));
            vf.assign = v.value("Assign", v.value("assign", 0.0F));
            vf.add = v.value("Add", v.value("add", 0.0F));
            vf.subtract = v.value("Subtract", v.value("subtract", 0.0F));
            vf.multiply = v.value("Multiply", v.value("multiply", 0.0F));
            vf.divide = v.value("Divide", v.value("divide", 0.0F));

            // Parse Code string into structured fields.
            // Type mapping (aligned with Live2DViewerEX):
            //   Type=0: no VarFloat (unused entry)
            //   Type=1: condition — check if condition is true, then assign
            //   Type=2: unconditional — always assign
            // Code parsing populates condition/assign fields WITHOUT overriding type,
            // except when JSON type is 0 (unset) — then infer from Code content.
            if (!vf.code.empty()) {
                size_t sp = vf.code.find(' ');
                if (sp != std::string::npos) {
                    std::string op = vf.code.substr(0, sp);
                    float val = 0;
                    try { val = std::stof(vf.code.substr(sp + 1)); } catch (...) {}
                    if (op == "equal") {
                        vf.equal = val; vf.has_equal = true;
                        if (vf.type == 0) vf.type = 1;
                    } else if (op == "not_equal") {
                        vf.not_equal = val; vf.has_not_equal = true;
                        if (vf.type == 0) vf.type = 1;
                    } else if (op == "greater") {
                        vf.greater = val; vf.has_greater = true;
                        if (vf.type == 0) vf.type = 1;
                    } else if (op == "greater_equal") {
                        vf.greater = val - 0.001F; vf.has_greater = true;
                        if (vf.type == 0) vf.type = 1;
                    } else if (op == "lower") {
                        vf.lower = val; vf.has_lower = true;
                        if (vf.type == 0) vf.type = 1;
                    } else if (op == "lower_equal") {
                        vf.lower = val + 0.001F; vf.has_lower = true;
                        if (vf.type == 0) vf.type = 1;
                    } else if (op == "assign") {
                        vf.assign = val; vf.has_assign = true;
                        if (vf.type == 0) vf.type = 2;
                    } else if (op == "add") { vf.add = val; vf.has_add = true; }
                    else if (op == "subtract") { vf.subtract = val; vf.has_subtract = true; }
                    else if (op == "multiply") { vf.multiply = val; vf.has_multiply = true; }
                    else if (op == "divide") { vf.divide = val; vf.has_divide = true; }
                }
            }

            // For condition types (type 1), the Assign field in JSON specifies the value
            // to set when the condition is true. The Code field only encodes the condition.
            // Detect Assign from JSON by checking if the key exists explicitly.
            if (vf.type == 1 && !vf.has_assign) {
                bool hasAssignInJson = v.contains("Assign") || v.contains("assign");
                if (hasAssignInJson) {
                    vf.has_assign = true;
                }
            }

            m.var_floats.push_back(vf);
        }
    }

    // Parse time_limit
    const json* tlJson = nullptr;
    if (j.contains("TimeLimit") && j["TimeLimit"].is_object()) {
        tlJson = &j["TimeLimit"];
    } else if (j.contains("time_limit") && j["time_limit"].is_object()) {
        tlJson = &j["time_limit"];
    }
    if (tlJson) {
        m.time_limit.hour = tlJson->value("hour", -1);
        m.time_limit.minute = tlJson->value("minute", -1);
        m.time_limit.month = tlJson->value("month", -1);
        m.time_limit.day = tlJson->value("day", -1);
        m.time_limit.begin = tlJson->value("begin", -1);
        m.time_limit.end = tlJson->value("end", -1);
        m.time_limit.sustain = tlJson->value("sustain", -1);
        m.time_limit.birthday = tlJson->value("birthday", false);
    }

    return m;
}

static GroupConfig parseGroup(const json& j) {
    GroupConfig g;
    g.target = j.value("target", "");
    g.name = j.value("name", "");
    g.text = j.value("text", "");
    g.var_float = optStringEither(j, "VarFloat", "var_float");
    g.hidden = j.value("hidden", false);
    g.value = j.value("value", 0.0F);

    if (j.contains("ids") && j["ids"].is_array()) {
        for (const auto& v : j["ids"]) {
            if (v.is_string()) g.ids.push_back(v.get<std::string>());
        }
    }
    if (j.contains("axes") && j["axes"].is_array()) {
        for (const auto& v : j["axes"]) {
            if (v.is_string()) g.axes.push_back(v.get<std::string>());
        }
    }
    if (j.contains("keys") && j["keys"].is_array()) {
        for (const auto& v : j["keys"]) {
            if (v.is_string()) g.keys.push_back(v.get<std::string>());
        }
    }
    if (j.contains("factors") && j["factors"].is_array()) {
        for (const auto& v : j["factors"]) {
            if (v.is_number()) g.factors.push_back(v.get<float>());
        }
    }
    if (j.contains("values") && j["values"].is_array()) {
        for (const auto& v : j["values"]) {
            if (v.is_number()) g.values.push_back(v.get<float>());
        }
    }
    return g;
}

static HitParamConfig parseHitParam(const json& j) {
    HitParamConfig h;
    h.name = optStringEither(j, "Name", "name");
    h.id = optStringEither(j, "Id", "id");
    h.hit_area = optStringEither(j, "HitArea", "hit_area");
    h.axis = optIntEither(j, "Axis", "axis", 0);
    h.weight = optFloatEither(j, "Weight", "weight", 1.0F);
    h.distance = optIntEither(j, "Distance", "distance", 0);
    h.factor = optFloatEither(j, "Factor", "factor", 1.0F);
    h.release = optIntEither(j, "Release", "release", 0);
    h.release_type = optIntEither(j, "ReleaseType", "release_type", 0);
    h.type = optIntEither(j, "Type", "type", 0);
    h.relative = optBoolEither(j, "Relative", "relative", false);
    h.lock_param = optBoolEither(j, "LockParam", "lock_param", false);
    h.low_priority = optBoolEither(j, "LowPriority", "low_priority", false);
    h.enabled = optBoolEither(j, "Enabled", "enabled", true);
    h.min_group = optStringEither(j, "MinGroup", "min_group");
    h.min_motion = optStringEither(j, "MinMotion", "min_motion");
    h.max_group = optStringEither(j, "MaxGroup", "max_group");
    h.max_motion = optStringEither(j, "MaxMotion", "max_motion");
    h.min_mtn = optStringEither(j, "MinMtn", "min_mtn");
    h.max_mtn = optStringEither(j, "MaxMtn", "max_mtn");
    h.begin_mtn = optStringEither(j, "BeginMtn", "begin_mtn");
    h.end_mtn = optStringEither(j, "EndMtn", "end_mtn");
    return h;
}

static LoopParamConfig parseLoopParam(const json& j) {
    LoopParamConfig l;
    l.name = optStringEither(j, "Name", "name");
    l.id = optStringEither(j, "Id", "id");
    l.type = optIntEither(j, "Type", "type", 0);
    l.duration = optIntEither(j, "Duration", "duration", 0);
    l.blend_mode = optIntEither(j, "BlendMode", "blend_mode", 0);
    l.weight = optFloatEither(j, "Weight", "weight", 1.0F);
    l.time_sync = optBoolEither(j, "TimeSync", "time_sync", false);
    l.min_interval = optIntEither(j, "MinInterval", "min_interval", 0);
    l.max_interval = optIntEither(j, "MaxInterval", "max_interval", 0);
    l.min_value = optFloatEither(j, "MinValue", "min_value", 0.0F);
    l.max_value = optFloatEither(j, "MaxValue", "max_value", 1.0F);
    l.enabled = optBoolEither(j, "Enabled", "enabled", true);
    // Parse ids array (try both cases)
    const json* idsJson = nullptr;
    if (j.contains("Ids") && j["Ids"].is_array()) {
        idsJson = &j["Ids"];
    } else if (j.contains("ids") && j["ids"].is_array()) {
        idsJson = &j["ids"];
    }
    if (idsJson) {
        for (const auto& v : *idsJson) {
            if (v.is_string()) l.ids.push_back(v.get<std::string>());
        }
    }
    return l;
}

static ParamTriggerConfig parseParamTrigger(const json& j) {
    ParamTriggerConfig t;
    t.name = optStringEither(j, "Name", "name");
    t.id = optStringEither(j, "Id", "id");
    const json* itemsJson = nullptr;
    if (j.contains("Items") && j["Items"].is_array()) {
        itemsJson = &j["Items"];
    } else if (j.contains("items") && j["items"].is_array()) {
        itemsJson = &j["items"];
    }
    if (itemsJson) {
        for (const auto& item : *itemsJson) {
            ParamTriggerItemConfig ti;
            ti.value = item.value("value", 0.0F);
            ti.direction = item.value("direction", 0);
            ti.motion = optStringEither(item, "Motion", "motion");
            t.items.push_back(ti);
        }
    }
    return t;
}

static AreaTriggerConfig parseAreaTrigger(const json& j) {
    AreaTriggerConfig a;
    a.name = optStringEither(j, "Name", "name");
    a.target_area = optStringEither(j, "TargetArea", "target_area");
    a.enter_mtn = optStringEither(j, "EnterMtn", "enter_mtn");
    a.exit_mtn = optStringEither(j, "ExitMtn", "exit_mtn");
    const json* taJson = nullptr;
    if (j.contains("TriggerAreas") && j["TriggerAreas"].is_array()) {
        taJson = &j["TriggerAreas"];
    } else if (j.contains("trigger_areas") && j["trigger_areas"].is_array()) {
        taJson = &j["trigger_areas"];
    }
    if (taJson) {
        for (const auto& v : *taJson) {
            if (v.is_string()) a.trigger_areas.push_back(v.get<std::string>());
        }
    }
    return a;
}

static KeyTriggerConfig parseKeyTrigger(const json& j) {
    KeyTriggerConfig k;
    k.input = optIntEither(j, "Input", "input", 0);
    k.down_mtn = optStringEither(j, "DownMtn", "down_mtn");
    k.up_mtn = optStringEither(j, "UpMtn", "up_mtn");
    return k;
}

static HandTriggerConfig parseHandTrigger(const json& j) {
    HandTriggerConfig h;
    h.name = optStringEither(j, "Name", "name");
    h.hand = optIntEither(j, "Hand", "hand", 0);
    h.direction = optIntEither(j, "Direction", "direction", 0);
    h.motion = optStringEither(j, "Motion", "motion");
    const json* fJson = nullptr;
    if (j.contains("Fingers") && j["Fingers"].is_array()) {
        fJson = &j["Fingers"];
    } else if (j.contains("fingers") && j["fingers"].is_array()) {
        fJson = &j["fingers"];
    }
    if (fJson) {
        for (const auto& v : *fJson) {
            if (v.is_number()) h.fingers.push_back(v.get<int>());
        }
    }
    const json* fgJson = nullptr;
    if (j.contains("FingerGaps") && j["FingerGaps"].is_array()) {
        fgJson = &j["FingerGaps"];
    } else if (j.contains("finger_gaps") && j["finger_gaps"].is_array()) {
        fgJson = &j["finger_gaps"];
    }
    if (fgJson) {
        for (const auto& v : *fgJson) {
            if (v.is_number()) h.finger_gaps.push_back(v.get<int>());
        }
    }
    return h;
}

static ParamInfoConfig parseParamInfo(const json& j) {
    ParamInfoConfig p;
    p.id = optStringEither(j, "Id", "id");
    p.min = optFloatEither(j, "Min", "min", 0);
    p.max = optFloatEither(j, "Max", "max", 0);
    p.default_value = optFloatEither(j, "DefaultValue", "default_value", 0);
    p.weight = optFloatEither(j, "Weight", "weight", 1.0F);
    p.inverted = optBoolEither(j, "Inverted", "inverted", false);
    p.blend_mode = optIntEither(j, "BlendMode", "blend_mode", 0);
    p.value = optFloatEither(j, "Value", "value", 0);
    p.axis = optIntEither(j, "Axis", "axis", 0);
    p.order = optIntEither(j, "Order", "order", 0);
    p.factor = optFloatEither(j, "Factor", "factor", 1.0F);
    p.input = optIntEither(j, "Input", "input", 0);
    p.clamp_min_max = optBoolEither(j, "ClampMinMax", "clamp_min_max", false);
    return p;
}

static KeyValueConfig parseKeyValue(const json& j) {
    KeyValueConfig kv;
    kv.key = j.value("key", "");
    kv.value = j.value("value", 0.0F);
    return kv;
}

static ModelValueConfig parseModelValue(const json& j) {
    ModelValueConfig mv;
    mv.name = optStringEither(j, "Name", "name");
    mv.text = optStringEither(j, "Text", "text");
    mv.value = optFloatEither(j, "Value", "value", 0);
    mv.hidden = optBoolEither(j, "Hidden", "hidden", false);
    const json* idsJson = nullptr;
    if(j.contains("Ids") && j["Ids"].is_array()) { idsJson = &j["Ids"]; }
    else if (j.contains("ids") && j["ids"].is_array()) { idsJson = &j["ids"]; }
    if (idsJson) {
        for (const auto& v : *idsJson) {
            if (v.is_string()) { mv.ids.push_back(v.get<std::string>()); }
        }
    }
    const json* kvJson = nullptr;
    if(j.contains("KeyValues") && j["KeyValues"].is_array()) { kvJson = &j["KeyValues"]; }
    else if (j.contains("key_values") && j["key_values"].is_array()) { kvJson = &j["key_values"]; }
    if (kvJson) {
        for (const auto& v : *kvJson) {
            if (v.is_object()) { mv.key_values.push_back(parseKeyValue(v)); }
        }
    }
    return mv;
}

static void parseParamInfoArray(const json& obj, const char* key1, const char* key2, std::vector<ParamInfoConfig>& out) {
    const json* arr = nullptr;
    if(obj.contains(key1) && obj[key1].is_array()) { arr = &obj[key1]; }
    else if(obj.contains(key2) && obj[key2].is_array()) { arr = &obj[key2]; }
    if (arr) {
        for (const auto& v : *arr) {
            if (v.is_object()) { out.push_back(parseParamInfo(v)); }
        }
    }
}

static void parseModelValueArray(const json& obj, const char* key1, const char* key2, std::vector<ModelValueConfig>& out) {
    const json* arr = nullptr;
    if(obj.contains(key1) && obj[key1].is_array()) { arr = &obj[key1]; }
    else if(obj.contains(key2) && obj[key2].is_array()) { arr = &obj[key2]; }
    if (arr) {
        for (const auto& v : *arr) {
            if (v.is_object()) { out.push_back(parseModelValue(v)); }
        }
    }
}

static void parseStringArray(const json& obj, const char* key1, const char* key2, std::vector<std::string>& out) {
    const json* arr = nullptr;
    if(obj.contains(key1) && obj[key1].is_array()) { arr = &obj[key1]; }
    else if(obj.contains(key2) && obj[key2].is_array()) { arr = &obj[key2]; }
    if (arr) {
        for (const auto& v : *arr) {
            if (v.is_string()) { out.push_back(v.get<std::string>()); }
        }
    }
}

static FaceTrackingControllerConfig parseFaceTrackingController(const json& j) {
    FaceTrackingControllerConfig c;
    c.enabled = optBoolEither(j, "Enabled", "enabled", true);
    parseParamInfoArray(j, "AngleX", "angle_x", c.angle_x);
    parseParamInfoArray(j, "AngleY", "angle_y", c.angle_y);
    parseParamInfoArray(j, "AngleZ", "angle_z", c.angle_z);
    parseParamInfoArray(j, "PositionX", "position_x", c.position_x);
    parseParamInfoArray(j, "PositionY", "position_y", c.position_y);
    parseParamInfoArray(j, "PositionZ", "position_z", c.position_z);
    parseParamInfoArray(j, "EyeLOpen", "eye_l_open", c.eye_l_open);
    parseParamInfoArray(j, "EyeROpen", "eye_r_open", c.eye_r_open);
    parseParamInfoArray(j, "EyeWide", "eye_wide", c.eye_wide);
    parseParamInfoArray(j, "EyeSquint", "eye_squint", c.eye_squint);
    parseParamInfoArray(j, "EyeballX", "eyeball_x", c.eyeball_x);
    parseParamInfoArray(j, "EyeballY", "eyeball_y", c.eyeball_y);
    parseParamInfoArray(j, "BrowLY", "brow_l_y", c.brow_l_y);
    parseParamInfoArray(j, "BrowRY", "brow_r_y", c.brow_r_y);
    parseParamInfoArray(j, "MouthOpenY", "mouth_open_y", c.mouth_open_y);
    parseParamInfoArray(j, "MouthForm", "mouth_form", c.mouth_form);
    parseParamInfoArray(j, "MouthPucker", "mouth_pucker", c.mouth_pucker);
    parseParamInfoArray(j, "MouthFunnel", "mouth_funnel", c.mouth_funnel);
    parseParamInfoArray(j, "CheekPuff", "cheek_puff", c.cheek_puff);
    parseParamInfoArray(j, "TongueOut", "tongue_out", c.tongue_out);
    return c;
}

static HandTrackingControllerConfig parseHandTrackingController(const json& j) {
    HandTrackingControllerConfig c;
    c.enabled = optBoolEither(j, "Enabled", "enabled", true);
    parseParamInfoArray(j, "LeftAngleX", "left_angle_x", c.left_angle_x);
    parseParamInfoArray(j, "LeftAngleZ", "left_angle_z", c.left_angle_z);
    parseParamInfoArray(j, "LeftPositionX", "left_position_x", c.left_position_x);
    parseParamInfoArray(j, "LeftPositionY", "left_position_y", c.left_position_y);
    parseParamInfoArray(j, "LeftThumb", "left_thumb", c.left_thumb);
    parseParamInfoArray(j, "LeftIndex", "left_index", c.left_index);
    parseParamInfoArray(j, "LeftMiddle", "left_middle", c.left_middle);
    parseParamInfoArray(j, "LeftRing", "left_ring", c.left_ring);
    parseParamInfoArray(j, "LeftPinky", "left_pinky", c.left_pinky);
    parseParamInfoArray(j, "LeftGapTI", "left_gap_ti", c.left_gap_ti);
    parseParamInfoArray(j, "LeftGapIM", "left_gap_im", c.left_gap_im);
    parseParamInfoArray(j, "LeftGapMR", "left_gap_mr", c.left_gap_mr);
    parseParamInfoArray(j, "LeftGapRP", "left_gap_rp", c.left_gap_rp);
    parseParamInfoArray(j, "RightAngleX", "right_angle_x", c.right_angle_x);
    parseParamInfoArray(j, "RightAngleZ", "right_angle_z", c.right_angle_z);
    parseParamInfoArray(j, "RightPositionX", "right_position_x", c.right_position_x);
    parseParamInfoArray(j, "RightPositionY", "right_position_y", c.right_position_y);
    parseParamInfoArray(j, "RightThumb", "right_thumb", c.right_thumb);
    parseParamInfoArray(j, "RightIndex", "right_index", c.right_index);
    parseParamInfoArray(j, "RightMiddle", "right_middle", c.right_middle);
    parseParamInfoArray(j, "RightRing", "right_ring", c.right_ring);
    parseParamInfoArray(j, "RightPinky", "right_pinky", c.right_pinky);
    parseParamInfoArray(j, "RightGapTI", "right_gap_ti", c.right_gap_ti);
    parseParamInfoArray(j, "RightGapIM", "right_gap_im", c.right_gap_im);
    parseParamInfoArray(j, "RightGapMR", "right_gap_mr", c.right_gap_mr);
    parseParamInfoArray(j, "RightGapRP", "right_gap_rp", c.right_gap_rp);
    return c;
}

static ModelValueControllerConfig parseModelValueController(const json& j) {
    ModelValueControllerConfig c;
    c.enabled = optBoolEither(j, "Enabled", "enabled", true);
    parseModelValueArray(j, "Items", "items", c.items);
    return c;
}

static ArtmeshCullControllerConfig parseArtmeshCullController(const json& j) {
    ArtmeshCullControllerConfig c;
    c.enabled = optBoolEither(j, "Enabled", "enabled", true);
    c.default_mode = optIntEither(j, "DefaultMode", "default_mode", 0);
    parseStringArray(j, "CullFront", "cull_front", c.cull_front);
    parseStringArray(j, "CullBack", "cull_back", c.cull_back);
    parseStringArray(j, "CullNone", "cull_none", c.cull_none);
    return c;
}

static GenericParamControllerConfig parseGenericParamController(const json& j) {
    GenericParamControllerConfig c;
    c.enabled = optBoolEither(j, "Enabled", "enabled", true);
    c.smooth_time = optFloatEither(j, "SmoothTime", "smooth_time", 0);
    parseParamInfoArray(j, "Items", "items", c.items);
    return c;
}

static void parseControllers(const json& j, ControllersConfig& ctrl) {
    // Controller enable flags (handle both cases)
    auto getControllerObj = [&](const char* key1, const char* key2) -> const json* {
        if(j.contains(key1) && j[key1].is_object()) { return &j[key1]; }
        if(j.contains(key2) && j[key2].is_object()) { return &j[key2]; }
        return nullptr;
    };

    auto getItemsArray = [](const json& obj) -> const json* {
        if(obj.contains("Items") && obj["Items"].is_array()) { return &obj["Items"]; }
        if(obj.contains("items") && obj["items"].is_array()) { return &obj["items"]; }
        return nullptr;
    };

    auto getEnabled = [](const json& obj, bool def) -> bool {
        if(obj.contains("Enabled") && obj["Enabled"].is_boolean()) { return obj["Enabled"].get<bool>(); }
        if(obj.contains("enabled") && obj["enabled"].is_boolean()) { return obj["enabled"].get<bool>(); }
        return def;
    };

    // --- Simple flag controllers (backward compat) ---
    if (const auto* eb = getControllerObj("EyeBlink", "eye_blink")) {
        ctrl.eye_blink_enabled = getEnabled(*eb, true);
        ctrl.eye_blink = parseGenericParamController(*eb);
    }
    if (const auto* ab = getControllerObj("AutoBreath", "auto_breath")) {
        ctrl.auto_breath_enabled = getEnabled(*ab, true);
        ctrl.auto_breath = parseGenericParamController(*ab);
    }
    if (const auto* mt = getControllerObj("MouseTracking", "mouse_tracking")) {
        ctrl.mouse_tracking_enabled = getEnabled(*mt, true);
        ctrl.mouse_tracking = parseGenericParamController(*mt);
    }
    if (const auto* ls = getControllerObj("LipSync", "lip_sync")) {
        ctrl.lip_sync_enabled = getEnabled(*ls, false);
        ctrl.lip_sync = parseGenericParamController(*ls);
    }
    if (const auto* em = getControllerObj("ExtraMotion", "extra_motion")) {
        ctrl.extra_motion_enabled = getEnabled(*em, false);
        ctrl.extra_motion = parseGenericParamController(*em);
    }
    if (const auto* ac = getControllerObj("Accelerometer", "accelerometer")) {
        ctrl.accelerometer_enabled = getEnabled(*ac, false);
        ctrl.accelerometer = parseGenericParamController(*ac);
    }
    if (const auto* rs = getControllerObj("RandomSpeak", "random_speak")) {
        ctrl.random_speak_enabled = getEnabled(*rs, false);
        ctrl.random_speak_interval = rs->value("interval", rs->value("Interval", 30));
    }

    // --- Item-based controllers ---
    if (const auto* ph = getControllerObj("ParamHit", "param_hit")) {
        ctrl.smooth_time = ph->value("smooth_time", ph->value("SmoothTime", 0.0F));
        if (const auto* items = getItemsArray(*ph)) {
            for (const auto& item : *items) { ctrl.hit_params.push_back(parseHitParam(item)); }
        }
    }
    if (const auto* pl = getControllerObj("ParamLoop", "param_loop")) {
        if (const auto* items = getItemsArray(*pl)) {
            for (const auto& item : *items) { ctrl.loop_params.push_back(parseLoopParam(item)); }
        }
    }
    if (const auto* pt = getControllerObj("ParamTrigger", "param_trigger")) {
        if (const auto* items = getItemsArray(*pt)) {
            for (const auto& item : *items) { ctrl.param_triggers.push_back(parseParamTrigger(item)); }
        }
    }
    if (const auto* at = getControllerObj("AreaTrigger", "area_trigger")) {
        if (const auto* items = getItemsArray(*at)) {
            for (const auto& item : *items) { ctrl.area_triggers.push_back(parseAreaTrigger(item)); }
        }
    }
    if (const auto* kt = getControllerObj("KeyTrigger", "key_trigger")) {
        if (const auto* items = getItemsArray(*kt)) {
            for (const auto& item : *items) { ctrl.key_triggers.push_back(parseKeyTrigger(item)); }
        }
    }
    if (const auto* ht = getControllerObj("HandTrigger", "hand_trigger")) {
        if (const auto* items = getItemsArray(*ht)) {
            for (const auto& item : *items) { ctrl.hand_triggers.push_back(parseHandTrigger(item)); }
        }
    }

    // --- New controllers from dump.cs alignment ---
    if (const auto* mc = getControllerObj("Microphone", "microphone")) {
        ctrl.microphone = parseGenericParamController(*mc);
    }
    if (const auto* tf = getControllerObj("Transform", "transform")) {
        ctrl.transform = parseGenericParamController(*tf);
    }
    if (const auto* la = getControllerObj("LookAt", "look_at")) {
        ctrl.look_at = parseGenericParamController(*la);
    }
    if (const auto* ft = getControllerObj("FaceTracking", "face_tracking")) {
        ctrl.face_tracking = parseFaceTrackingController(*ft);
    }
    if (const auto* htk = getControllerObj("HandTracking", "hand_tracking")) {
        ctrl.hand_tracking = parseHandTrackingController(*htk);
    }
    if (const auto* pv = getControllerObj("ParamValue", "param_value")) {
        ctrl.param_value = parseModelValueController(*pv);
    }
    if (const auto* po = getControllerObj("PartOpacity", "part_opacity")) {
        ctrl.part_opacity = parseModelValueController(*po);
    }
    if (const auto* ao = getControllerObj("ArtmeshOpacity", "artmesh_opacity")) {
        ctrl.artmesh_opacity = parseModelValueController(*ao);
    }
    if (const auto* ac2 = getControllerObj("ArtmeshColor", "artmesh_color")) {
        ctrl.artmesh_color = parseModelValueController(*ac2);
    }
    if (const auto* ac3 = getControllerObj("ArtmeshCulling", "artmesh_culling")) {
        ctrl.artmesh_culling = parseArtmeshCullController(*ac3);
    }
    if (const auto* so = getControllerObj("SlotOpacity", "slot_opacity")) {
        ctrl.slot_opacity = parseModelValueController(*so);
    }
    if (const auto* sc = getControllerObj("SlotColor", "slot_color")) {
        ctrl.slot_color = parseModelValueController(*sc);
    }
}

ModelConfig ParseModelConfig(const std::string& jsonStr) {
    ModelConfig config;

    if (jsonStr.empty()) return config;

    try {
        json j = json::parse(jsonStr);

        // Parse motions
        // Try multiple locations: "motions", "Motions", "FileReferences.Motions"
        json motionsJson;
        if (j.contains("motions") && j["motions"].is_object()) {
            motionsJson = j["motions"];
        } else if (j.contains("Motions") && j["Motions"].is_object()) {
            motionsJson = j["Motions"];
        } else if (j.contains("FileReferences") && j["FileReferences"].is_object()) {
            const json& fr = j["FileReferences"];
            if (fr.contains("Motions") && fr["Motions"].is_object()) {
                motionsJson = fr["Motions"];
            } else if (fr.contains("motions") && fr["motions"].is_object()) {
                motionsJson = fr["motions"];
            }
        }

        if (!motionsJson.is_null()) {
            for (const auto& item : motionsJson.items()) {
                const std::string& groupName = item.key();
                auto& motionArray = item.value();
                if (!motionArray.is_array()) continue;
                std::vector<MotionMeta> group;
                for (int i = 0; i < (int)motionArray.size(); i++) {
                    auto& motionJson = motionArray[i];
                    if (motionJson.is_object()) {
                        group.push_back(parseMotionMeta(motionJson, groupName, i));
                    }
                }
                config.motions[groupName] = std::move(group);
            }
        }

        // Parse groups
        if (j.contains("groups") && j["groups"].is_array()) {
            for (auto& g : j["groups"]) {
                if (g.is_object()) {
                    config.groups.push_back(parseGroup(g));
                }
            }
        } else if (j.contains("Groups") && j["Groups"].is_array()) {
            for (auto& g : j["Groups"]) {
                if (g.is_object()) {
                    config.groups.push_back(parseGroup(g));
                }
            }
        }

        // Parse top-level hit_params (outside controllers)
        if (j.contains("hit_params") && j["hit_params"].is_array()) {
            for (auto& hp : j["hit_params"]) {
                if (hp.is_object()) {
                    config.controllers.hit_params.push_back(parseHitParam(hp));
                }
            }
        } else if (j.contains("HitParams") && j["HitParams"].is_array()) {
            for (auto& hp : j["HitParams"]) {
                if (hp.is_object()) {
                    config.controllers.hit_params.push_back(parseHitParam(hp));
                }
            }
        }

        // Parse top-level loop_params
        if (j.contains("loop_params") && j["loop_params"].is_array()) {
            for (auto& lp : j["loop_params"]) {
                if (lp.is_object()) {
                    config.controllers.loop_params.push_back(parseLoopParam(lp));
                }
            }
        }

        // Parse controllers
        if (j.contains("controllers") && j["controllers"].is_object()) {
            parseControllers(j["controllers"], config.controllers);
        } else if (j.contains("Controllers") && j["Controllers"].is_object()) {
            parseControllers(j["Controllers"], config.controllers);
        }

        LAppPal::PrintLogLn("[APP]ModelConfig parsed: %d motion groups, %d groups, %d hit_params, %d loop_params, %d param_triggers, %d area_triggers, %d key_triggers, %d hand_triggers, face_tracking=%d, hand_tracking=%d, param_value=%d, part_opacity=%d, artmesh_opacity=%d, artmesh_color=%d, artmesh_culling=%d, vf_overrides=%d",
            (int)config.motions.size(), (int)config.groups.size(),
            (int)config.controllers.hit_params.size(), (int)config.controllers.loop_params.size(),
            (int)config.controllers.param_triggers.size(), (int)config.controllers.area_triggers.size(),
            (int)config.controllers.key_triggers.size(), (int)config.controllers.hand_triggers.size(),
            (int)config.controllers.face_tracking.angle_x.size() + (int)config.controllers.face_tracking.angle_y.size(),
            (int)config.controllers.hand_tracking.left_angle_x.size() + (int)config.controllers.hand_tracking.right_angle_x.size(),
            (int)config.controllers.param_value.items.size(), (int)config.controllers.part_opacity.items.size(),
            (int)config.controllers.artmesh_opacity.items.size(), (int)config.controllers.artmesh_color.items.size(),
            (int)config.controllers.artmesh_culling.cull_front.size() + (int)config.controllers.artmesh_culling.cull_back.size(),
            (int)config.var_float_part_overrides.size());

        // Parse costume_sets
        const json* csJson = nullptr;
        if (j.contains("costume_sets") && j["costume_sets"].is_array()) { csJson = &j["costume_sets"]; }
        else if (j.contains("CostumeSets") && j["CostumeSets"].is_array()) { csJson = &j["CostumeSets"]; }
        if (csJson) {
            for (auto& cs : *csJson) {
                if (!cs.is_object()) continue;
                CostumeSetConfig set;
                set.name = cs.value("name", "");
                set.var_float = cs.value("var_float", "");
                if (cs.contains("groups") && cs["groups"].is_array()) {
                    for (auto& g : cs["groups"]) {
                        if (g.is_string()) set.groups.push_back(g.get<std::string>());
                    }
                }
                if (cs.contains("mutual_exclude") && cs["mutual_exclude"].is_array()) {
                    for (auto& e : cs["mutual_exclude"]) {
                        if (e.is_string()) set.mutual_exclude.push_back(e.get<std::string>());
                    }
                }
                if (!set.name.empty()) {
                    config.costume_sets.push_back(std::move(set));
                }
            }
        }

        // Parse VarFloatPartOverrides: explicit VarFloat→Part ID mappings
        // Supports both PascalCase and snake_case, both top-level and inside "extensions"
        auto parseVfOverrides = [&](const json& obj) {
            const json* vfpoJson = nullptr;
            if (obj.contains("VarFloatPartOverrides") && obj["VarFloatPartOverrides"].is_object()) {
                vfpoJson = &obj["VarFloatPartOverrides"];
            } else if (obj.contains("var_float_part_overrides") && obj["var_float_part_overrides"].is_object()) {
                vfpoJson = &obj["var_float_part_overrides"];
            }
            if (vfpoJson) {
                for (auto& item : vfpoJson->items()) {
                    const std::string& vfName = item.key();
                    const auto& val = item.value();
                    if (val.is_array()) {
                        for (const auto& v : val) {
                            if (v.is_string()) {
                                config.var_float_part_overrides[vfName].push_back(v.get<std::string>());
                            }
                        }
                    } else if (val.is_string()) {
                        config.var_float_part_overrides[vfName].push_back(val.get<std::string>());
                    }
                }
            }
        };
        parseVfOverrides(j);
        if (j.contains("Extensions") && j["Extensions"].is_object()) {
            parseVfOverrides(j["Extensions"]);
        } else if (j.contains("extensions") && j["extensions"].is_object()) {
            parseVfOverrides(j["extensions"]);
        }

    } catch (const std::exception& e) {
        LAppPal::PrintLogLn("[APP]ModelConfig parse error: %s", e.what());
    }

    return config;
}
