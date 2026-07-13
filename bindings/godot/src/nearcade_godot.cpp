#include "nearcade_godot.h"

using namespace godot;

NearcadeSDK *NearcadeSDK::singleton = nullptr;

NearcadeSDK::NearcadeSDK() {
    ERR_FAIL_COND_MSG(singleton != nullptr, "NearcadeSDK singleton already exists.");
    singleton = this;
}

NearcadeSDK::~NearcadeSDK() {
    nearcade_shutdown();
    if (singleton == this) singleton = nullptr;
}

void NearcadeSDK::_event_callback(const nearcade_event *ev, void *userdata) {
    NearcadeSDK *self = static_cast<NearcadeSDK *>(userdata);
    if (!self || !ev) return;
    std::lock_guard<std::mutex> lock(self->queue_mutex);
    if (self->event_queue.size() < 256) {
        self->event_queue.push({*ev});
    }
}

void NearcadeSDK::_bind_methods() {
    ClassDB::bind_method(D_METHOD("init", "config"), &NearcadeSDK::init);
    ClassDB::bind_method(D_METHOD("start_capture"), &NearcadeSDK::start_capture);
    ClassDB::bind_method(D_METHOD("stop_capture"), &NearcadeSDK::stop_capture);
    ClassDB::bind_method(D_METHOD("shutdown"), &NearcadeSDK::shutdown);

    ClassDB::bind_method(D_METHOD("submit_gamepad", "packet"), &NearcadeSDK::submit_gamepad);
    ClassDB::bind_method(D_METHOD("submit_kbm", "viewer_id", "event_type", "key", "dx", "dy"), &NearcadeSDK::submit_kbm);
    ClassDB::bind_method(D_METHOD("flush_slot", "slot"), &NearcadeSDK::flush_slot);
    ClassDB::bind_method(D_METHOD("disconnect_viewer", "viewer_id"), &NearcadeSDK::disconnect_viewer);

    ClassDB::bind_method(D_METHOD("get_signaling_url"), &NearcadeSDK::get_signaling_url);
    ClassDB::bind_method(D_METHOD("get_lan_ip"), &NearcadeSDK::get_lan_ip);
    ClassDB::bind_method(D_METHOD("get_pin"), &NearcadeSDK::get_pin);
    ClassDB::bind_method(D_METHOD("regenerate_pin"), &NearcadeSDK::regenerate_pin);

    ClassDB::bind_method(D_METHOD("poll_event"), &NearcadeSDK::poll_event);

    ADD_SIGNAL(MethodInfo("viewer_joined", PropertyInfo(Variant::STRING, "viewer_id"), PropertyInfo(Variant::STRING, "name")));
    ADD_SIGNAL(MethodInfo("viewer_left", PropertyInfo(Variant::STRING, "viewer_id")));
    ADD_SIGNAL(MethodInfo("input_packet", PropertyInfo(Variant::DICTIONARY, "packet"), PropertyInfo(Variant::STRING, "viewer_id")));
    ADD_SIGNAL(MethodInfo("signaling_message", PropertyInfo(Variant::STRING, "viewer_id"), PropertyInfo(Variant::STRING, "data")));
    ADD_SIGNAL(MethodInfo("error_code", PropertyInfo(Variant::INT, "code"), PropertyInfo(Variant::STRING, "message")));
}

Dictionary NearcadeSDK::init(const Dictionary &config) {
    nearcade_config c = NEARCADE_DEFAULT_CONFIG;

    if (config.has("port"))             c.port = (int)config["port"];
    if (config.has("screen_width"))     c.screen_width = (int)config["screen_width"];
    if (config.has("screen_height"))    c.screen_height = (int)config["screen_height"];
    if (config.has("max_bitrate"))      c.max_bitrate = (int)config["max_bitrate"];
    if (config.has("fps"))             c.fps = (int)config["fps"];
    if (config.has("enable_audio"))     c.enable_audio = (int)config["enable_audio"];

    int ret = nearcade_init(&c);
    if (ret == 0) {
        nearcade_set_event_callback(_event_callback, this);
    }

    Dictionary result;
    result["result"] = ret;
    result["lan_ip"] = get_lan_ip();
    result["pin"] = get_pin();
    result["signaling_url"] = get_signaling_url();
    return result;
}

int NearcadeSDK::start_capture() {
    return nearcade_start_capture();
}

int NearcadeSDK::stop_capture() {
    return nearcade_stop_capture();
}

void NearcadeSDK::shutdown() {
    nearcade_shutdown();
}

int NearcadeSDK::submit_gamepad(const Dictionary &pkt) {
    nearcade_gamepad_packet p;
    memset(&p, 0, sizeof(p));
    p.type    = pkt.has("type")    ? (uint8_t)(int)pkt["type"]    : 0x01;
    p.lx      = pkt.has("lx")      ? (int16_t)(int)pkt["lx"]      : 0;
    p.ly      = pkt.has("ly")      ? (int16_t)(int)pkt["ly"]      : 0;
    p.rx      = pkt.has("rx")      ? (int16_t)(int)pkt["rx"]      : 0;
    p.ry      = pkt.has("ry")      ? (int16_t)(int)pkt["ry"]      : 0;
    p.lt      = pkt.has("lt")      ? (uint8_t)(int)pkt["lt"]      : 0;
    p.rt      = pkt.has("rt")      ? (uint8_t)(int)pkt["rt"]      : 0;
    p.buttons = pkt.has("buttons") ? (uint16_t)(int)pkt["buttons"] : 0;
    p.hx      = pkt.has("hx")      ? (int8_t)(int)pkt["hx"]       : 0;
    p.hy      = pkt.has("hy")      ? (int8_t)(int)pkt["hy"]       : 0;
    p.slot    = pkt.has("slot")    ? (uint8_t)(int)pkt["slot"]    : 0;
    return nearcade_submit_gamepad(&p);
}

int NearcadeSDK::submit_kbm(const String &viewer_id, const String &event_type,
                             const String &key, int dx, int dy) {
    CharString vid = viewer_id.utf8();
    CharString et  = event_type.utf8();
    CharString k   = key.utf8();
    return nearcade_submit_kbm(vid.get_data(), et.get_data(), k.get_data(), dx, dy);
}

int NearcadeSDK::flush_slot(int slot) {
    return nearcade_flush_slot((uint8_t)slot);
}

int NearcadeSDK::disconnect_viewer(const String &viewer_id) {
    CharString vid = viewer_id.utf8();
    return nearcade_disconnect_viewer(vid.get_data());
}

String NearcadeSDK::get_signaling_url() {
    char buf[256] = {0};
    nearcade_get_signaling_url(buf, sizeof(buf));
    return String(buf);
}

String NearcadeSDK::get_lan_ip() {
    char buf[64] = {0};
    nearcade_get_lan_ip(buf, sizeof(buf));
    return String(buf);
}

String NearcadeSDK::get_pin() {
    char buf[8] = {0};
    nearcade_get_pin(buf, sizeof(buf));
    return String(buf);
}

int NearcadeSDK::regenerate_pin() {
    return nearcade_regenerate_pin();
}

Dictionary NearcadeSDK::poll_event() {
    QueuedEvent qe;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (event_queue.empty()) return Dictionary();
        qe = event_queue.front();
        event_queue.pop();
    }

    Dictionary d;
    const nearcade_event &ev = qe.ev;
    d["type"] = (int)ev.type;

    switch (ev.type) {
        case NEARCADE_EVENT_VIEWER_JOINED:
            d["viewer_id"] = String(ev.data.viewer_joined.viewer_id);
            d["name"] = String(ev.data.viewer_joined.name);
            emit_signal("viewer_joined", d["viewer_id"], d["name"]);
            break;
        case NEARCADE_EVENT_VIEWER_LEFT:
            d["viewer_id"] = String(ev.data.viewer_left.viewer_id);
            emit_signal("viewer_left", d["viewer_id"]);
            break;
        case NEARCADE_EVENT_INPUT_PACKET: {
            Dictionary pkt;
            pkt["type"]    = (int)ev.data.input_packet.packet.type;
            pkt["lx"]      = (int)ev.data.input_packet.packet.lx;
            pkt["ly"]      = (int)ev.data.input_packet.packet.ly;
            pkt["rx"]      = (int)ev.data.input_packet.packet.rx;
            pkt["ry"]      = (int)ev.data.input_packet.packet.ry;
            pkt["lt"]      = (int)ev.data.input_packet.packet.lt;
            pkt["rt"]      = (int)ev.data.input_packet.packet.rt;
            pkt["buttons"] = (int)ev.data.input_packet.packet.buttons;
            pkt["hx"]      = (int)ev.data.input_packet.packet.hx;
            pkt["hy"]      = (int)ev.data.input_packet.packet.hy;
            pkt["slot"]    = (int)ev.data.input_packet.packet.slot;
            d["packet"] = pkt;
            d["viewer_id"] = String(ev.data.input_packet.viewer_id);
            emit_signal("input_packet", d["packet"], d["viewer_id"]);
            break;
        }
        case NEARCADE_EVENT_SIGNALING:
            d["viewer_id"] = String(ev.data.signaling.viewer_id);
            d["data"] = String(ev.data.signaling.data);
            emit_signal("signaling_message", d["viewer_id"], d["data"]);
            break;
        case NEARCADE_EVENT_ERROR:
            d["code"] = ev.data.error.code;
            d["message"] = String(ev.data.error.message);
            emit_signal("error_code", d["code"], d["message"]);
            break;
        default:
            break;
    }

    return d;
}
