#ifdef _WIN32
#include "nearcade_internal.h"
#include <stdio.h>

int capture_init(const nearcade_config *config) { (void)config; return NEARCADE_OK; }
int capture_start(void) { LOG_ERROR("capture_start: not implemented on Windows"); return NEARCADE_ERR_CAPTURE; }
int capture_stop(void) { return NEARCADE_OK; }
void capture_shutdown(void) {}
#else
#include "nearcade_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

typedef struct {
    pid_t pid;
    int   active;
    char  encoder[32];
} ffmpeg_capture;

static ffmpeg_capture g_ff = {0};

static int detect_vaapi(char *device, size_t len)
{
    LOG_TRACE("detect_vaapi: probing for h264_vaapi...");
    FILE *fp = popen("ffmpeg -hide_banner -encoders 2>/dev/null | grep -q h264_vaapi && echo yes || echo no", "r");
    if (!fp) {
        LOG_WARN("detect_vaapi: popen failed");
        return 0;
    }
    char buf[8] = {0};
    if (fgets(buf, sizeof(buf), fp)) {}
    int status = pclose(fp);
    LOG_TRACE("detect_vaapi: popen exited status=%d result='%s'", status, buf);
    if (strncmp(buf, "yes", 3) == 0) {
        snprintf(device, len, "/dev/dri/renderD128");
        LOG_INFO("detect_vaapi: VAAPI hardware encoding available via %s", device);
        return 1;
    }
    LOG_INFO("detect_vaapi: VAAPI not available, will use software encoding");
    return 0;
}

int capture_init(const nearcade_config *config)
{
    LOG_TRACE("capture_init: entering (config=%p)", (void*)config);
    (void)config;
    memset(&g_ff, 0, sizeof(g_ff));
    LOG_DEBUG("capture_init: OK");
    return NEARCADE_OK;
}

int capture_start(void)
{
    LOG_TRACE("capture_start: entering");

#ifdef __linux__
    const nearcade_config *cfg = &g_state.config;

    if (g_ff.active) {
        LOG_DEBUG("capture_start: already active (pid=%d)", g_ff.pid);
        return NEARCADE_OK;
    }

    LOG_DEBUG("capture_start: forking FFmpeg process...");
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("capture_start: fork failed: %s", strerror(errno));
        return NEARCADE_ERR_CAPTURE;
    }

    if (pid == 0) {
        char res[32];
        snprintf(res, sizeof(res), "%dx%d", cfg->screen_width, cfg->screen_height);
        char br[32];
        snprintf(br, sizeof(br), "%dk", cfg->max_bitrate / 1000);
        char fps_str[8];
        snprintf(fps_str, sizeof(fps_str), "%d", cfg->fps);
        char gop_str[8];
        snprintf(gop_str, sizeof(gop_str), "%d", cfg->fps * 2);

        char vaapi_dev[64] = {0};
        int use_vaapi = detect_vaapi(vaapi_dev, sizeof(vaapi_dev));

        LOG_DEBUG("capture_start: child configuring FFmpeg capture: res=%s fps=%s bitrate=%s gop=%s vaapi=%d",
                  res, fps_str, br, gop_str, use_vaapi);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        if (use_vaapi) {
            LOG_DEBUG("capture_start: exec'ing ffmpeg with h264_vaapi on %s", vaapi_dev);
            execlp("ffmpeg", "ffmpeg",
                   "-hide_banner", "-loglevel", "error",
                   "-f", "x11grab",
                   "-framerate", fps_str, "-video_size", res,
                   "-i", ":0",
                   "-vf", "format=nv12,hwupload",
                   "-vaapi_device", vaapi_dev,
                   "-c:v", "h264_vaapi",
                   "-profile:v", "high",
                   "-b:v", br,
                   "-bf", "0",
                   "-g", gop_str,
                   "-tune", "zerolatency",
                   "-f", "mp4",
                   "-movflags", "empty_moov+default_base_moof+frag_keyframe",
                   "pipe:1",
                   NULL);
        } else {
            LOG_DEBUG("capture_start: exec'ing ffmpeg with libx264");
            execlp("ffmpeg", "ffmpeg",
                   "-hide_banner", "-loglevel", "error",
                   "-f", "x11grab",
                   "-framerate", fps_str, "-video_size", res,
                   "-i", ":0",
                   "-c:v", "libx264",
                   "-preset", "ultrafast",
                   "-tune", "zerolatency",
                   "-b:v", br,
                   "-bf", "0",
                   "-g", gop_str,
                   "-f", "mp4",
                   "-movflags", "empty_moov+default_base_moof+frag_keyframe",
                   "pipe:1",
                   NULL);
        }

        LOG_ERROR("capture_start: execlp(ffmpeg) failed: %s", strerror(errno));
        _exit(1);
    }

    g_ff.pid = pid;
    g_ff.active = 1;
    LOG_INFO("capture_start: FFmpeg started (pid=%d)", pid);
    return NEARCADE_OK;

#elif __APPLE__
    LOG_ERROR("capture_start: not implemented on macOS");
    return NEARCADE_ERR_CAPTURE;
#endif
}

int capture_stop(void)
{
    LOG_TRACE("capture_stop: entering (active=%d pid=%d)", g_ff.active, g_ff.pid);
    if (g_ff.active && g_ff.pid > 0) {
        LOG_DEBUG("capture_stop: sending SIGTERM to pid=%d", g_ff.pid);
        kill(g_ff.pid, SIGTERM);

        int status = 0;
        pid_t ret = waitpid(g_ff.pid, &status, WNOHANG);
        LOG_DEBUG("capture_stop: waitpid returned %d (status=%d)", (int)ret, status);
        if (ret == 0) {
            usleep(100000);
            ret = waitpid(g_ff.pid, &status, WNOHANG);
            if (ret == 0) {
                LOG_WARN("capture_stop: FFmpeg didn't exit after SIGTERM, sending SIGKILL");
                kill(g_ff.pid, SIGKILL);
                waitpid(g_ff.pid, &status, 0);
            }
        }

        g_ff.active = 0;
        g_ff.pid = 0;
        LOG_INFO("capture_stop: FFmpeg stopped");
    } else {
        LOG_DEBUG("capture_stop: nothing to stop");
    }
    return NEARCADE_OK;
}

void capture_shutdown(void)
{
    LOG_TRACE("capture_shutdown: entering");
    capture_stop();
}
#endif
