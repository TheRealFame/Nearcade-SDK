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
    log("Nearcade Godot Demo v0.2.0")
    log("Initializing SDK...")

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
    log("Init result: " + str(result["result"]))
    log("LAN IP: " + result["lan_ip"])
    log("PIN: " + result["pin"])
    log("Signaling URL: " + result["signaling_url"])

    url_label.text = "URL: " + result["signaling_url"]
    pin_label.text = "PIN: " + result["pin"]
    status_label.text = "Ready"

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
                0: log("Viewer joined: " + ev.get("viewer_id", "?"))
                1: log("Viewer left: " + ev.get("viewer_id", "?"))
                4: log("Signaling: " + str(ev.get("data", "")).left(80))
                5: log("Error: " + str(ev.get("code", -1)) + " " + ev.get("message", ""))
                6: log("Streaming started: " + str(ev.get("viewer_count", 0)) + " viewers")
            ev = sdk.poll_event()

        # Push a dummy H.264 NAL every 60 frames (simulating engine video)
        video_timer += delta
        if video_timer > 0.5 and start_btn.text == "Streaming":
            # In a real game, you'd encode a real frame here
            var dummy_h264 = PackedByteArray([0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e])
            sdk.send_h264(dummy_h264, Time.get_ticks_usec())
            video_timer = 0.0


func _on_viewer_joined(viewer_id, name):
    log("[signal] Viewer joined: " + viewer_id + " (" + name + ")")


func _on_viewer_left(viewer_id):
    log("[signal] Viewer left: " + viewer_id)


func _on_signaling(viewer_id, data):
    log("[signal] Signaling from " + viewer_id + ": " + data.left(80))


func _on_error(code, message):
    log("[signal] Error " + str(code) + ": " + message)


func _on_streaming_started(viewer_count):
    log("[signal] Streaming active, viewers: " + str(viewer_count))


func _on_start_pressed():
    if sdk:
        var rc = sdk.start_streaming()
        log("start_streaming: " + str(rc))
        if rc == 0:
            status_label.text = "Streaming"
            start_btn.text = "Streaming"
        else:
            status_label.text = "Failed (" + str(rc) + ")"


func _on_stop_pressed():
    if sdk:
        sdk.stop_streaming()
        sdk.stop_capture()
        log("Streaming stopped")
        status_label.text = "Ready"
        start_btn.text = "Start"


func _on_submit_pressed():
    if sdk:
        var packet = {
            "type": 0x01,
            "slot": 0,
            "lx": 0, "ly": -32767,
            "rx": 0, "ry": 0,
            "lt": 0, "rt": 0,
            "buttons": 1,
            "hx": 0, "hy": 0,
        }
        var rc = sdk.submit_gamepad(packet)
        log("submit_gamepad (A+up): " + str(rc))


func _on_disconnect_pressed():
    if sdk:
        sdk.disconnect_viewer("viewer_0")
        log("Disconnected viewer_0")


func log(msg):
    print(msg)
    if log_output:
        log_output.text += msg + "\n"
        log_output.scroll_vertical = log_output.get_line_count()
