extends Control

@onready var status_label = $VBoxContainer/StatusLabel
@onready var url_label = $VBoxContainer/URLLabel
@onready var pin_label = $VBoxContainer/PINLabel
@onready var start_btn = $VBoxContainer/HBoxContainer/StartBtn
@onready var stop_btn = $VBoxContainer/HBoxContainer/StopBtn
@onready var log_output = $VBoxContainer/LogOutput

var sdk = null
var video_timer = 0.0

func _ready():
    log_msg("Nearcade Godot Demo v0.2.0")
    log_msg("Initializing SDK...")

    sdk = NearcadeSDK.new()
    var cfg = {
        "port": 3000,
        "screen_width": 1920,
        "screen_height": 1080,
        "max_bitrate": 8000000,
        "fps": 60,
        "enable_audio": 0,
    }

    var result = sdk.init(cfg)
    log_msg("Init result: " + str(result["result"]))
    log_msg("LAN IP: " + result["lan_ip"])
    log_msg("PIN: " + result["pin"])
    log_msg("Signaling URL: " + result["signaling_url"])

    url_label.text = "URL: " + result["signaling_url"]
    pin_label.text = "PIN: " + result["pin"]
    status_label.text = "Ready"

    start_btn.button_down.connect(_on_start_pressed)
    stop_btn.button_down.connect(_on_stop_pressed)

    sdk.viewer_joined.connect(_on_viewer_joined)
    sdk.viewer_left.connect(_on_viewer_left)
    sdk.signaling_message.connect(_on_signaling)
    sdk.error_code.connect(_on_error)
    sdk.streaming_started.connect(_on_streaming_started)


func _process(delta):
    if sdk:
        var ev = sdk.poll_event()
        while not ev.is_empty():
            match ev["type"]:
                0: log_msg("Viewer joined: " + ev.get("viewer_id", "?"))
                1: log_msg("Viewer left: " + ev.get("viewer_id", "?"))
                4: log_msg("Signaling: " + str(ev.get("data", "")).left(80))
                5: log_msg("Error: " + str(ev.get("code", -1)) + " " + ev.get("message", ""))
                6: log_msg("Streaming started: " + str(ev.get("viewer_count", 0)) + " viewers")
            ev = sdk.poll_event()

        video_timer += delta
        if video_timer > 0.5 and start_btn.text == "Streaming":
            var dummy_h264 = PackedByteArray([0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e])
            sdk.send_h264(dummy_h264, Time.get_ticks_usec())
            video_timer = 0.0


func _on_viewer_joined(viewer_id, name):
    log_msg("[signal] Viewer joined: " + viewer_id + " (" + name + ")")


func _on_viewer_left(viewer_id):
    log_msg("[signal] Viewer left: " + viewer_id)


func _on_signaling(viewer_id, data):
    log_msg("[signal] Signaling from " + viewer_id + ": " + data.left(80))


func _on_error(code, message):
    log_msg("[signal] Error " + str(code) + ": " + message)


func _on_streaming_started(viewer_count):
    log_msg("[signal] Streaming active, viewers: " + str(viewer_count))


func _on_start_pressed():
    if sdk:
        var rc = sdk.start_streaming()
        log_msg("start_streaming: " + str(rc))
        if rc == 0:
            status_label.text = "Streaming"
            start_btn.text = "Streaming"
        else:
            status_label.text = "Failed (" + str(rc) + ")"


func _on_stop_pressed():
    if sdk:
        sdk.stop_streaming()
        log_msg("Streaming stopped")
        status_label.text = "Ready"
        start_btn.text = "Start Streaming"


func log_msg(msg):
    print(msg)
    if log_output:
        log_output.text += msg + "\n"
        log_output.scroll_vertical = log_output.get_line_count()
