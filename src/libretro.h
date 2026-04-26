#pragma once
// libretro.h — FBNeoRageX 에서 사용하는 libretro API 최소 정의
// 원본: https://github.com/libretro/libretro-common/blob/master/include/libretro.h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── API 버전 ─────────────────────────────────────────────
#define RETRO_API_VERSION 1

// ── 픽셀 포맷 ─────────────────────────────────────────────
enum retro_pixel_format {
    RETRO_PIXEL_FORMAT_0RGB1555 = 0,   // 기본값 (레거시)
    RETRO_PIXEL_FORMAT_XRGB8888 = 1,   // 32bit
    RETRO_PIXEL_FORMAT_RGB565   = 2,   // 16bit (NeoGeo 기본)
    RETRO_PIXEL_FORMAT_UNKNOWN  = INT32_MAX
};

// ── 입력 장치 ─────────────────────────────────────────────
#define RETRO_DEVICE_NONE     0
#define RETRO_DEVICE_JOYPAD   1
#define RETRO_DEVICE_MOUSE    2
#define RETRO_DEVICE_KEYBOARD 3

// 조이패드 버튼 ID (retro_input_state_t id 파라미터)
#define RETRO_DEVICE_ID_JOYPAD_B       0
#define RETRO_DEVICE_ID_JOYPAD_Y       1
#define RETRO_DEVICE_ID_JOYPAD_SELECT  2
#define RETRO_DEVICE_ID_JOYPAD_START   3
#define RETRO_DEVICE_ID_JOYPAD_UP      4
#define RETRO_DEVICE_ID_JOYPAD_DOWN    5
#define RETRO_DEVICE_ID_JOYPAD_LEFT    6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT   7
#define RETRO_DEVICE_ID_JOYPAD_A       8
#define RETRO_DEVICE_ID_JOYPAD_X       9
#define RETRO_DEVICE_ID_JOYPAD_L      10
#define RETRO_DEVICE_ID_JOYPAD_R      11
#define RETRO_DEVICE_ID_JOYPAD_L2     12
#define RETRO_DEVICE_ID_JOYPAD_R2     13
#define RETRO_DEVICE_ID_JOYPAD_L3     14
#define RETRO_DEVICE_ID_JOYPAD_R3     15

// ── Environment 명령 ID ────────────────────────────────────
#define RETRO_ENVIRONMENT_SET_ROTATION                1
#define RETRO_ENVIRONMENT_GET_OVERSCAN                2
#define RETRO_ENVIRONMENT_GET_CAN_DUPE                3
#define RETRO_ENVIRONMENT_SET_MESSAGE                 6
#define RETRO_ENVIRONMENT_SHUTDOWN                    7
#define RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL       8
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY        9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT           10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS      11
#define RETRO_ENVIRONMENT_GET_VARIABLE               15  // 표준값
#define RETRO_ENVIRONMENT_SET_VARIABLES              16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE        17  // 표준값
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME        19
#define RETRO_ENVIRONMENT_GET_LIBRETRO_PATH          19
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE          27
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY         31
#define RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO         32
#define RETRO_ENVIRONMENT_SET_CONTROLLER_INFO        35
#define RETRO_ENVIRONMENT_SET_GEOMETRY               37
#define RETRO_ENVIRONMENT_GET_USERNAME               38
#define RETRO_ENVIRONMENT_GET_LANGUAGE               39
#define RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER 40
#define RETRO_ENVIRONMENT_GET_INPUT_BITMASKS         52
#define RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK 62
#define RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY  63

// ── 메모리 타입 (retro_get_memory_data 용) ─────────────────
#define RETRO_MEMORY_MASK        0xff
#define RETRO_MEMORY_SAVE_RAM    0
#define RETRO_MEMORY_RTC         1
#define RETRO_MEMORY_SYSTEM_RAM  2
#define RETRO_MEMORY_VIDEO_RAM   3

// ── 구조체 ────────────────────────────────────────────────
struct retro_game_info {
    const char* path;
    const void* data;
    size_t      size;
    const char* meta;
};

struct retro_game_geometry {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float    aspect_ratio;
};

struct retro_system_timing {
    double fps;
    double sample_rate;
};

struct retro_system_av_info {
    struct retro_game_geometry geometry;
    struct retro_system_timing timing;
};

struct retro_system_info {
    const char* library_name;
    const char* library_version;
    const char* valid_extensions;
    bool        need_fullpath;
    bool        block_extract;
};

struct retro_variable {
    const char* key;
    const char* value;
};

struct retro_message {
    const char* msg;
    unsigned    frames;
};

enum retro_log_level {
    RETRO_LOG_DEBUG = 0,
    RETRO_LOG_INFO,
    RETRO_LOG_WARN,
    RETRO_LOG_ERROR,
    RETRO_LOG_DUMMY = INT32_MAX
};

typedef void (*retro_log_printf_t)(enum retro_log_level level,
                                    const char* fmt, ...);
struct retro_log_callback {
    retro_log_printf_t log;
};

// ── 콜백 타입 ─────────────────────────────────────────────
typedef bool     (*retro_environment_t)(unsigned cmd, void* data);
typedef void     (*retro_video_refresh_t)(const void* data, unsigned width, unsigned height, size_t pitch);
typedef void     (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t   (*retro_audio_sample_batch_t)(const int16_t* data, size_t frames);
typedef void     (*retro_input_poll_t)(void);
typedef int16_t  (*retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);

// ── libretro 함수 시그니처 (동적 로딩용) ──────────────────
typedef void     (*retro_init_t)(void);
typedef void     (*retro_deinit_t)(void);
typedef unsigned (*retro_api_version_t)(void);
typedef void     (*retro_get_system_info_t)(struct retro_system_info* info);
typedef void     (*retro_get_system_av_info_t)(struct retro_system_av_info* info);
typedef bool     (*retro_load_game_t)(const struct retro_game_info* game);
typedef void     (*retro_unload_game_t)(void);
typedef void     (*retro_run_t)(void);
typedef size_t   (*retro_serialize_size_t)(void);
typedef bool     (*retro_serialize_t)(void* data, size_t size);
typedef bool     (*retro_unserialize_t)(const void* data, size_t size);
typedef void     (*retro_reset_t)(void);
typedef void     (*retro_cheat_reset_t)(void);
typedef void     (*retro_cheat_set_t)(unsigned index, bool enabled, const char* code);
typedef void     (*retro_set_environment_t)(retro_environment_t);
typedef void     (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void     (*retro_set_audio_sample_t)(retro_audio_sample_t);
typedef void     (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void     (*retro_set_input_poll_t)(retro_input_poll_t);
typedef void     (*retro_set_input_state_t)(retro_input_state_t);

#ifdef __cplusplus
}
#endif
