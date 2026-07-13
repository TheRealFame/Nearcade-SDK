#ifndef NEARCADE_GODOT_H
#define NEARCADE_GODOT_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <queue>
#include <mutex>

#include "nearcade.h"

namespace godot {

class NearcadeSDK : public Object {
    GDCLASS(NearcadeSDK, Object)

private:
    struct QueuedEvent {
        nearcade_event ev;
    };
    std::queue<QueuedEvent> event_queue;
    std::mutex queue_mutex;

    static NearcadeSDK *singleton;
    static void _event_callback(const nearcade_event *ev, void *userdata);

protected:
    static void _bind_methods();

public:
    NearcadeSDK();
    ~NearcadeSDK();

    static NearcadeSDK *get_singleton() { return singleton; }

    Dictionary init(const Dictionary &config);
    int start_streaming();
    int stop_streaming();
    int start_capture();
    int stop_capture();
    int send_h264(const PackedByteArray &data, int64_t timestamp_us);
    int send_frame(const PackedByteArray &data, int width, int height, int fmt, int64_t timestamp_us);
    void shutdown();

    int submit_gamepad(const Dictionary &packet);
    int submit_kbm(const String &viewer_id, const String &event_type,
                   const String &key, int dx, int dy);
    int flush_slot(int slot);
    int disconnect_viewer(const String &viewer_id);

    String get_signaling_url();
    String get_lan_ip();
    String get_pin();
    int regenerate_pin();

    Dictionary poll_event();
};

} // namespace godot

#endif // NEARCADE_GODOT_H
