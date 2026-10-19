#include "nabo_apps.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "application.h"
#include "assets.h"
#include "backlight.h"
#include "board.h"
#include "codecs/es8311_audio_codec.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "mcp_server.h"
#include "nabo_display.h"
#include "nabo_radio_view.h"
#include "nvs.h"
#include "settings.h"
#include "src/draw/lv_image_decoder_private.h"
#include "ssid_manager.h"
#include "wifi_manager.h"
extern "C" {
#include "nabo_radio.h"
#include "nabo_radio_keys.h"
#include "nabo_radio_menu.h"
#include "nabo_voice_commands.h"
LV_FONT_DECLARE(nabo_menu_font_16);
LV_FONT_DECLARE(nabo_mono_font_12);
}
#include "nabo_menu_view.h"

static std::atomic<int> wanted{NABO_HOME}, request_revision{0}, setting_command{-1};
static std::atomic<int> detail_parent{NABO_SETTINGS};
static std::atomic<int64_t> open_after{0}, last_input{0};
static std::atomic<bool> ready{false}, screen_off{false}, speaker{true}, wifi_on{true};
static std::atomic<bool> local_ui_released{true};
static std::atomic<bool> radio_ui_ready{false};
static std::atomic<int> volume{70}, brightness{80}, sleep_seconds{120}, mic_gain{30}, battery{-1};
static lv_obj_t* app_screen;
static NaboApp page = NABO_HOME;
static int selected = 1, settings_selected = 1, edit = -1, shown_revision = -1;
static bool initialized = false, engaged = false, restored = true;
static int64_t last_battery = 0;
static char notice[96] = "";
// The display hook stays: NaboDisplay counts flushes for local programs.
void nabo_apps_record_frame() {}

static const char* tools[] = {"返回 NABO", "网络电台", "更多设置"};
static constexpr int kToolCount = sizeof(tools) / sizeof(*tools);
static const NaboApp tool_apps[] = {NABO_HOME, NABO_RADIO, NABO_SETTINGS};
enum Setting {
    ST_BACK,
    ST_VOLUME,
    ST_SPEAKER,
    ST_BRIGHTNESS,
    ST_SLEEP,
    ST_MIC,
    ST_WIFI_ADD,
    ST_WIFI,
    ST_STATE,
    ST_BATTERY,
    ST_SCREEN,
    ST_POWER,
    ST_COUNT
};
static const char* settings_names[] = {"返回",     "音量",       "扬声器",   "屏幕亮度",
                                       "自动休眠", "麦克风增益", "添加网络", "无线网络",
                                       "设备状态", "电池信息",   "关闭屏幕", "关机"};
extern "C" bool muse_settings_speaker_on(void) { return speaker.load(); }
extern "C" bool muse_wifi_connected(void) {
    return wifi_on.load() && WifiManager::GetInstance().IsConnected();
}
extern "C" bool app_nabo_radio_network_begin(void) {
    return ready.load() && Application::GetInstance().IsLocalAppActive();
}
// XiaoZhi stays paused while its local program is open. The worker resumes it
// only after the decoder has stopped on return to NABO.
extern "C" void app_nabo_radio_network_end(void) {}
extern "C" bool app_nabo_radio_active(void) { return nabo_radio_active(); }
static void ensure_radio_catalog() {
    if (nabo_radio_catalog()->data)
        return;
    void* data = nullptr;
    size_t size = 0;
    if (Assets::GetInstance().GetAssetData("radio_catalog.bin", data, size))
        nabo_radio_bind_catalog(data, size);
}

void nabo_apps_open(NaboApp app, int delay_ms) {
    if (app < NABO_HOME || app > NABO_WIFI)
        return;
    int origin = wanted.load();
    if ((app == NABO_STATUS || app == NABO_BATTERY || app == NABO_WIFI) && origin != app)
        detail_parent = origin == NABO_HOME       ? NABO_HOME
                        : origin == NABO_SETTINGS ? NABO_SETTINGS
                                                  : NABO_TOOLS;
    open_after.store(esp_timer_get_time() + static_cast<int64_t>(delay_ms) * 1000);
    wanted.store(app);
    request_revision.fetch_add(1);
    last_input.store(esp_timer_get_time());
}
bool nabo_apps_active() { return wanted.load() != NABO_HOME || ready.load(); }
bool nabo_apps_is_jump() { return false; }
bool nabo_apps_is_fast() { return false; }

static void save_settings() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("nabo_ui", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, "volume", volume.load());
        if (!err)
            err = nvs_set_i32(h, "brightness", brightness.load());
        if (!err)
            err = nvs_set_i32(h, "sleep", sleep_seconds.load());
        if (!err)
            err = nvs_set_i32(h, "mic", mic_gain.load());
        if (!err)
            err = nvs_set_u8(h, "speaker", speaker.load());
        if (!err)
            err = nvs_set_u8(h, "wifi", wifi_on.load());
        if (!err)
            err = nvs_commit(h);
        nvs_close(h);
    }
    if (err) {
        snprintf(notice, sizeof(notice), "设置未保存，请重试");
        ESP_LOGE("NABO-Apps", "settings save: %d", err);
    }
}
static void load_settings() {
    auto& board = Board::GetInstance();
    volume = board.GetAudioCodec()->output_volume();
    brightness = board.GetBacklight()->brightness();
    nvs_handle_t h;
    if (nvs_open("nabo_ui", NVS_READONLY, &h) == ESP_OK) {
        int32_t v;
        uint8_t flag;
        if (!nvs_get_i32(h, "volume", &v))
            volume = std::clamp<int>(v, 0, 100);
        if (!nvs_get_i32(h, "brightness", &v))
            brightness = std::clamp<int>(v, 10, 100);
        if (!nvs_get_i32(h, "sleep", &v))
            sleep_seconds = std::clamp<int>(v, 0, 600);
        if (!nvs_get_i32(h, "mic", &v))
            mic_gain = std::clamp<int>(v, 0, 36);
        if (!nvs_get_u8(h, "speaker", &flag))
            speaker = flag != 0;
        if (!nvs_get_u8(h, "wifi", &flag))
            wifi_on = flag != 0;
        nvs_close(h);
    }
    board.GetAudioCodec()->SetOutputVolume(speaker ? volume.load() : 0);
    board.GetAudioCodec()->SetInputGain(mic_gain.load());
    board.GetBacklight()->SetBrightness(brightness.load(), false);
}
static void output16(const int16_t* input, size_t count) {
    if (!speaker.load() || !ready.load() || !count)
        return;
    auto codec = Board::GetInstance().GetAudioCodec();
    static std::vector<int16_t> pcm;
    pcm.assign(input, input + count);
    codec->EnableOutput(true);
    codec->OutputData(pcm);
}
static void apply_setting(int id) {
    auto& board = Board::GetInstance();
    switch (id) {
        case ST_VOLUME:
            board.GetAudioCodec()->SetOutputVolume(speaker ? volume.load() : 0);
            break;
        case ST_SPEAKER:
            if (!speaker)
                nabo_radio_stop();
            board.GetAudioCodec()->SetOutputVolume(speaker ? volume.load() : 0);
            break;
        case ST_BRIGHTNESS:
            board.GetBacklight()->SetBrightness(brightness.load(), true);
            break;
        case ST_MIC:
            board.GetAudioCodec()->SetInputGain(mic_gain.load());
            break;
        case ST_WIFI:
            if (wifi_on)
                WifiManager::GetInstance().StartStation();
            else {
                nabo_radio_stop();
                WifiManager::GetInstance().StopStation();
            }
            break;
        case ST_WIFI_ADD:
            WifiManager::GetInstance().StartConfigAp();
            break;
        case ST_SCREEN:
            screen_off = true;
            board.GetBacklight()->SetBrightness(0, false);
            break;
        case ST_POWER:
            save_settings();
            board.GetBacklight()->SetBrightness(0, false);
            board.GetAudioCodec()->EnableInput(false);
            board.GetAudioCodec()->EnableOutput(false);
            esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << 0,
                                                                ESP_GPIO_WAKEUP_GPIO_LOW);
            esp_deep_sleep_start();
            break;
        default:
            break;
    }
    save_settings();
}
static void diagnostic(const char* line) {
    auto& app = Application::GetInstance();
    if (!strcmp(line, ">apps.status")) {
        // USB inspection must never wait for a whole GUI frame on the I2S
        // owner. Take the display snapshot on the main task instead.
        app.Schedule([]() {
        auto& app = Application::GetInstance();
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        printf(
            "\n@apps "
            "{\"wanted\":%d,\"page\":%d,\"ready\":%d,\"local\":%d,\"heap\":%u,\"largest\":%u,"
            "\"radio\":%d,\"radio_state\":\"%s\",\"station\":%d,\"volume\":%d,\"brightness\":%d,"
            "\"sleep\":%d,\"wifi\":%d,\"edit\":%d,\"detail_parent\":%d}\n",
            wanted.load(), (int)page, ready.load(), app.IsLocalAppActive(),
            (unsigned)esp_get_free_heap_size(),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), nabo_radio_active(),
            nabo_radio_state_text(), nabo_radio_current_station(), volume.load(), brightness.load(),
            sleep_seconds.load(), WifiManager::GetInstance().IsConnected(), edit,
            detail_parent.load());
        if (page == NABO_RADIO) {
            char text[640];
            nabo_radio_menu_text(text, sizeof(text));
            printf("@radio-menu %s\n", text);
        }
        });
    } else if (!strncmp(line, ">apps.open=", 11)) {
        const char* name = line + 11;
        struct {
            const char* name;
            NaboApp app;
        } aliases[] = {{"home", NABO_HOME},
                       {"tools", NABO_TOOLS},
                       {"settings", NABO_SETTINGS},
                       {"radio", NABO_RADIO},
                       {"status", NABO_STATUS}};
        bool found = false;
        for (const auto& item : aliases)
            if (!strcmp(name, item.name)) {
                nabo_apps_open(item.app);
                found = true;
                break;
            }
        printf("\n@open %s\n", found ? "queued" : "invalid");
    } else if (!strncmp(line, ">apps.voice=", 12)) {
        const char* name = line + 12;
        if (strlen(name) > 20 || strspn(name, "abcdefghijklmnopqrstuvwxyz") != strlen(name))
            return;
        std::string message =
            "{\"jsonrpc\":\"2.0\",\"id\":919,\"method\":\"tools/"
            "call\",\"params\":{\"name\":\"self.apps.open\",\"arguments\":{\"app\":\"" +
            std::string(name) + "\"}}}";
        app.Schedule([message]() {
            McpServer::GetInstance().ParseMessage(message, [](const std::string& response) {
                printf("\n@mcp %s\n", response.c_str());
            });
        });
    } else if (!strncmp(line, ">apps.key=", 10)) {
        muse_menu_key_t key = MUSE_MENU_SELECT;
        if (!strcmp(line + 10, "up"))
            key = MUSE_MENU_UP;
        else if (!strcmp(line + 10, "down"))
            key = MUSE_MENU_DOWN;
        else if (!strcmp(line + 10, "back"))
            key = MUSE_MENU_BACK;
        else if (!strcmp(line + 10, "parent"))
            key = MUSE_MENU_LEFT;
        else if (strcmp(line + 10, "ok"))
            return;
        app.Schedule([key]() {
            auto display = Board::GetInstance().GetDisplay();
            DisplayLockGuard lock(display);
            if (page == NABO_RADIO) {
                if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN || key == MUSE_MENU_SELECT) {
                    int button = key == MUSE_MENU_UP     ? NRF_UP
                                 : key == MUSE_MENU_DOWN ? NRF_DOWN
                                                         : NRF_OK;
                    uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
                    nabo_radio_view_edge(button, true, now);
                    nabo_radio_view_edge(button, false, now + 1);
                } else
                    nabo_radio_view_back();
            } else
                nabo_apps_key(key);
        });
    } else if (!strcmp(line, ">apps.capture")) {
        app.Schedule(
            []() { static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay())->CaptureLCD(); });
    }
}
static void worker(void*) {
    vTaskDelay(pdMS_TO_TICKS(350));
    load_settings();
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg = {.tx_buffer_size = 2048, .rx_buffer_size = 512};
        usb_serial_jtag_driver_install(&cfg);
    }
    usb_serial_jtag_vfs_use_driver();
    char command_line[96] = {};
    unsigned command_len = 0;
    int16_t samples[480];
    unsigned output_rate = 16000;
    bool usb_attached = false;
    unsigned out_frames = 0, decoded_frames = 0, short_blocks = 0, max_decode_us = 0;
    unsigned max_loop_us = 0, min_buffered = UINT32_MAX;
    int64_t measured_at = esp_timer_get_time();
    nabo_radio_init();
    if (nabo_radio_wanted())
        nabo_apps_open(NABO_RADIO, 1500);
    for (;;) {
        char data[64];
        int received = usb_serial_jtag_read_bytes(data, sizeof(data), 0);
        for (int i = 0; i < received; ++i) {
            char c = data[i];
            if (c == '\n' || c == '\r') {
                command_line[command_len] = 0;
                if (command_len)
                    diagnostic(command_line);
                command_len = 0;
            } else if (command_len < sizeof(command_line) - 1)
                command_line[command_len++] = c;
            else
                command_len = 0;
        }
        auto& app = Application::GetInstance();
        int desired = wanted.load();
        int64_t now = esp_timer_get_time();
        if (desired != NABO_HOME && now >= open_after.load() && !engaged &&
            !app.IsLocalAppActive() && !app.IsProtocolInitializing() &&
            app.GetDeviceState() != kDeviceStateActivating) {
            // Starting (no network yet) is fine: the programs need no network, and XiaoZhi
            // resumes its boot flow when the program closes.
            engaged = true;
            restored = false;
            app.BeginLocalApp();
        }
        if (engaged && !ready.load() && desired != NABO_HOME && app.IsLocalAppActive()) {
            if (app.GetAudioService().WaitStopped(2000)) {
                vTaskDelay(pdMS_TO_TICKS(30));
                auto codec = static_cast<Es8311AudioCodec*>(Board::GetInstance().GetAudioCodec());
                codec->EnableInput(false);
                codec->EnableOutput(false);
                if (!codec->SetDuplexSampleRate(16000)) {
                    nabo_apps_open(NABO_HOME);
                    continue;
                }
                ready = true;
                static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay())->ReleaseGameDrawBuffer();
                output_rate = 16000;
                ESP_LOGI("NABO-Apps", "local audio ownership ready, heap=%u",
                         (unsigned)esp_get_free_heap_size());
            } else {
                ESP_LOGE("NABO-Apps", "audio stop timeout; local playback withheld");
                nabo_apps_open(NABO_HOME);
            }
        }
        bool audio_clocked = false;
        if (ready.load()) {
            memset(samples, 0, sizeof(samples));
            // Settings keep the program running. HOME explicitly gives the audio
            // device and all decoder memory back to XiaoZhi before it resumes.
            if (desired == NABO_HOME && nabo_radio_wanted())
                nabo_radio_stop();
            if (desired == NABO_RADIO)
                ensure_radio_catalog();
            if (desired == NABO_RADIO && radio_ui_ready.load() && nabo_radio_due())
                nabo_radio_begin();
            size_t sample_count = output_rate / 50;
            size_t audio_samples = 0;
            if (nabo_radio_active()) {
                if (nabo_radio_finished())
                    nabo_radio_finish();
                else {
                    int64_t decode_start = esp_timer_get_time();
                    audio_samples = nabo_radio_read(samples, sample_count, 0);
                    max_decode_us = std::max(max_decode_us,
                                            (unsigned)(esp_timer_get_time() - decode_start));
                    decoded_frames += audio_samples;
                    if (nabo_radio_state() == 3 && audio_samples < sample_count)
                        ++short_blocks;
                    min_buffered = std::min(min_buffered, nabo_radio_buffered_bytes());
                    unsigned new_rate = nabo_radio_output_rate();
                    if (new_rate != output_rate) {
                        auto codec =
                            static_cast<Es8311AudioCodec*>(Board::GetInstance().GetAudioCodec());
                        codec->EnableOutput(false);
                        if (!codec->SetDuplexSampleRate(new_rate)) {
                            nabo_radio_stop();
                            audio_samples = 0;
                        } else
                            output_rate = new_rate;
                        // This first block may be shorter; never reinterpret old
                        // samples at a different rate or pad a full 320-sample tail.
                        sample_count = output_rate / 50;
                    }
                }
            }
            if (!audio_samples && desired == NABO_RADIO && nabo_radio_wanted() &&
                nabo_radio_noise() &&
                (nabo_radio_state() == 1 || nabo_radio_state() == 2 || nabo_radio_state() == 5)) {
                static uint32_t noise_rng = 0x6e61626f;
                static int32_t noise_lp = 0;
                for (size_t i = 0; i < sample_count; ++i) {
                    noise_rng ^= noise_rng << 13;
                    noise_rng ^= noise_rng >> 17;
                    noise_rng ^= noise_rng << 5;
                    noise_lp += ((static_cast<int16_t>(noise_rng) / 24) - noise_lp) / 3;
                    samples[i] = static_cast<int16_t>(noise_lp);
                }
            }
            // A continuous 20 ms clock prevents underruns and clears each effect's
            // DMA tail. Do not sleep again after the blocking I2S write.
            if (desired != NABO_HOME && speaker.load()) {
                output16(samples, sample_count);
                out_frames += sample_count;
                audio_clocked = true;
            }
            // XiaoZhi's restore needs the decoder's memory back first.
            if (desired == NABO_HOME && !nabo_radio_active()) {
                Board::GetInstance().GetAudioCodec()->EnableOutput(false);
                ready = false;
            }
        }
        // The GUI releases its 22 KiB sprite and DMA strip on the next tick.
        // Resume TLS/Opus only after both the decoder and UI have returned RAM.
        if (desired == NABO_HOME && !ready.load() && local_ui_released.load() && !restored) {
            restored = true;
            engaged = false;
            app.EndLocalApp();
        }
        int command = setting_command.exchange(-1);
        if (command >= 0)
            apply_setting(command);
        if (now - last_battery > 3000000) {
            int pct;
            bool charging, discharging;
            if (Board::GetInstance().GetBatteryLevel(pct, charging, discharging))
                battery = pct;
            last_battery = now;
        }
        if (now - measured_at >= 5000000) {
            if (desired == NABO_HOME && !app.IsLocalAppActive() && !app.IsProtocolInitializing())
                app.GetAudioService().LogMemoryStats();
            else if (ready.load() && nabo_radio_active()) {
                ESP_LOGI("FM-Audio", "rate=%u out=%u decoded=%u short=%u max_decode_us=%u max_loop_us=%u min_buffered=%u stack=%u",
                         output_rate, out_frames, decoded_frames, short_blocks, max_decode_us,
                         max_loop_us, min_buffered, (unsigned)uxTaskGetStackHighWaterMark(nullptr));
            }
            out_frames = decoded_frames = short_blocks = max_decode_us = max_loop_us = 0;
            min_buffered = UINT32_MAX;
            measured_at = now;
        }
        max_loop_us = std::max(max_loop_us, (unsigned)(esp_timer_get_time() - now));
        bool usb = usb_serial_jtag_is_connected();
        if (usb != usb_attached) {
            usb_attached = usb;
            last_input.store(now);
            if (usb && screen_off.exchange(false))
                Board::GetInstance().GetBacklight()->SetBrightness(brightness.load(), false);
        }
        if (!usb && wanted.load() == NABO_HOME && app.GetDeviceState() == kDeviceStateIdle &&
            !screen_off && sleep_seconds > 0 &&
            now - last_input.load() > sleep_seconds.load() * 1000000LL) {
            screen_off = true;
            Board::GetInstance().GetBacklight()->SetBrightness(0, false);
        }
        nabo_radio_idle_tick();
        if (!audio_clocked)
            vTaskDelay(pdMS_TO_TICKS(20));
        else
            taskYIELD();
    }
}
void nabo_apps_initialize() {
    if (initialized)
        return;
    initialized = true;
    last_input = esp_timer_get_time();
    if (xTaskCreate(worker, "nabo_apps", 6144, nullptr, 5, nullptr) != pdPASS) {
        initialized = false;
        ESP_LOGE("NABO-Apps", "worker allocation failed");
    }
}

void nabo_apps_build(lv_obj_t* screen) {
    if (app_screen)
        return;
    app_screen = screen;
    nabo_apps_initialize();
    nabo_apps_register_tools();
    ESP_LOGI("NABO-Apps", "menus and radio ready");
}
static void change_page(NaboApp next) {
    radio_ui_ready = false;
    if (page == NABO_RADIO) {
        // The original LVGL settings menu needs its own RAM. Release the radio
        // scene before building it; audio keeps its independent pipeline.
        nabo_radio_view_close();
        nabo_radio_menu_close();
    }
    if (next == NABO_HOME)
        nabo_radio_view_close();
    page = next;
    edit = -1;
    selected = next == NABO_TOOLS ? 1 : 0;
    bool menu = next != NABO_HOME && next != NABO_RADIO;
    // The menu panel exists only while a menu page is shown: LVGL allocates
    // from the C3's system heap, which XiaoZhi's TLS and Opus need at home.
    if (menu) {
        menu_view::Build(app_screen);
    } else
        menu_view::Free();
    if (next == NABO_RADIO) {
        nabo_radio_menu_open();
        if (!nabo_radio_view_open()) {
            snprintf(notice, sizeof(notice), "电台资源缺失，请升级应用和资源");
            nabo_apps_open(NABO_TOOLS);
        } else
            radio_ui_ready = true;
    }
    local_ui_released.store(next == NABO_HOME);
    ESP_LOGI("NABO-Apps", "page=%d", static_cast<int>(next));
}
// Renders the radio's "> item" text menus as a now-playing card plus rows.
static void display_text_menu(char* text) {
    if (strncmp(text, "> ", 2) && !strstr(text, "\n> ")) {
        menu_view::Body(text, 96);
        return;
    }
    const char* header[2] = {"", ""};
    const char* items[12];
    int headers = 0, count = 0, cursor = 0;
    for (char* line = text; line;) {
        char* next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        bool item = (line[0] == '>' || line[0] == ' ') && line[1] == ' ';
        if (item && count < 12) {
            if (line[0] == '>')
                cursor = count;
            items[count++] = line + 2;
        } else if (line[0] && !count && headers < 2)
            header[headers++] = line;
        line = next;
    }
    if (header[1][0]) {
        menu_view::Card(header[0], header[1]);
        menu_view::Rows(items, count, cursor, false, 162, 4);
    } else {
        menu_view::Body(header[0], 94);
        menu_view::Rows(items, count, cursor, false, 124, 5);
    }
}
void nabo_apps_tick() {
    if (!app_screen)
        return;
    int revision = request_revision.load();
    if (revision != shown_revision && ((wanted.load() == NABO_HOME && !ready) || ready.load())) {
        shown_revision = revision;
        change_page(static_cast<NaboApp>(wanted.load()));
    }
    if (page == NABO_HOME)
        return;
    if (page == NABO_RADIO) {
        nabo_radio_view_tick(battery.load());
        return;
    }
    if (!menu_view::Built())
        return;
    namespace mv = menu_view;
    char text[640], counter[32];
    const char* hint = "OK 进入 · 双击上 返回";
    mv::Begin();
    if (page == NABO_TOOLS) {
        snprintf(counter, sizeof(counter), "%02d/%02d", selected, kToolCount - 1);
        mv::Header("TOOLS", "NABO 工具箱", mv::kOrange, counter);
        mv::Rows(tools, kToolCount, selected, false);
    } else if (page == NABO_SETTINGS) {
        snprintf(counter, sizeof(counter), "%02d/%02d", settings_selected, ST_COUNT - 1);
        mv::Header(edit >= 0 ? "EDIT" : "SETTINGS",
                   edit >= 0 ? "上/下 调整，OK 保存" : "声音、显示与网络", mv::kBlue, counter);
        mv::Rows(settings_names, ST_COUNT, settings_selected, edit >= 0);
        int first = mv::First(settings_selected, ST_COUNT);
        for (int i = 0; i < mv::kRowsVisible && first + i < ST_COUNT; ++i) {
            int item = first + i;
            text[0] = 0;
            if (item == ST_VOLUME)
                snprintf(text, sizeof(text), "%d%%", volume.load());
            else if (item == ST_BRIGHTNESS)
                snprintf(text, sizeof(text), "%d%%", brightness.load());
            else if (item == ST_MIC)
                snprintf(text, sizeof(text), "%d dB", mic_gain.load());
            else if (item == ST_SLEEP) {
                int seconds = sleep_seconds.load();
                if (!seconds)
                    snprintf(text, sizeof(text), "不休眠");
                else if (seconds % 60)
                    snprintf(text, sizeof(text), "%d 秒", seconds);
                else
                    snprintf(text, sizeof(text), "%d 分钟", seconds / 60);
            } else if (item == ST_SPEAKER)
                snprintf(text, sizeof(text), speaker ? "开" : "关");
            else if (item == ST_WIFI)
                snprintf(text, sizeof(text), wifi_on ? "开" : "关");
            if (item == edit && text[0]) {
                char editing[48];
                snprintf(editing, sizeof(editing), "< %.32s >", text);
                mv::Value(i, editing);
            } else
                mv::Value(i, text);
        }
        if (edit >= 0)
            hint = "上/下 调整 · OK 保存";
    } else if (page == NABO_RADIO) {
        mv::Header("RADIO", "网络电台", mv::kPink, nullptr);
        nabo_radio_menu_text(text, sizeof(text));
        display_text_menu(text);
        hint = "OK 确定 · 双击上 上一级";
    } else if (page == NABO_STATUS) {
        auto& wifi = WifiManager::GetInstance();
        char memory[24], ip[24], ssid[40];
        snprintf(memory, sizeof(memory), "%u KB", (unsigned)(esp_get_free_heap_size() / 1024));
        snprintf(ip, sizeof(ip), "%s", wifi.IsConnected() ? wifi.GetIpAddress().c_str() : "-");
        snprintf(ssid, sizeof(ssid), "%s", wifi.IsConnected() ? wifi.GetSsid().c_str() : "-");
        char power[16] = "-";
        if (battery.load() >= 0)
            snprintf(power, sizeof(power), "%d%%", battery.load());
        const char* names[] = {"Wi-Fi", "网络", "IP", "电量", "可用内存", "固件"};
        const char* texts[] = {
            wifi.IsConnected() ? "已连接" : "未连接", ssid, ip, power, memory, "NABO FM 0.2 dev"};
        mv::Header("STATUS", "设备状态", mv::kInk, nullptr);
        mv::Info(names, texts, 6);
        hint = "OK 返回 · 双击上 上一级";
    } else if (page == NABO_BATTERY) {
        int pct = battery.load();
        mv::Header("BATTERY", "电池信息", mv::kYellow, nullptr);
        if (pct >= 0) {
            snprintf(text, sizeof(text), "%d%%", pct);
            mv::Big(text, 96);
            mv::Meter(pct, 160);
        } else
            mv::Body("暂时无法读取电量", 100);
        const char* names[] = {"电量芯片"};
        const char* texts[] = {"CW2017"};
        mv::Info(names, texts, 1, 204);
        hint = "OK 返回 · 双击上 上一级";
    } else if (page == NABO_WIFI) {
        snprintf(text, sizeof(text),
                 "1  手机连接热点\n    %s\n\n2  浏览器打开\n    %s\n\n3  配网完成后按 OK 返回",
                 WifiManager::GetInstance().GetApSsid().c_str(),
                 WifiManager::GetInstance().GetApWebUrl().c_str());
        mv::Header("WI-FI", "添加网络", mv::kBlue, nullptr);
        mv::Body(text, 96);
        hint = "OK 返回设置";
    }
    mv::Footer(notice[0] ? notice : hint);
    mv::End();
}
static void adjust(int item, int direction) {
    if (item == ST_VOLUME)
        volume = std::clamp(volume.load() + direction * 10, 0, 100);
    else if (item == ST_BRIGHTNESS)
        brightness = std::clamp(brightness.load() + direction * 10, 10, 100);
    else if (item == ST_MIC)
        mic_gain = std::clamp(mic_gain.load() + direction * 6, 0, 36);
    else if (item == ST_SLEEP) {
        static const int times[] = {0, 30, 60, 120, 300, 600};
        int i = 0;
        while (i < 5 && times[i] < sleep_seconds)
            i++;
        i = (i + (direction > 0 ? 1 : 5)) % 6;
        sleep_seconds = times[i];
    }
    setting_command = item;
}
static void return_one_level() {
    NaboApp current = static_cast<NaboApp>(wanted.load());
    if (current == NABO_HOME)
        return;
    bool settled = ready.load() && current == page;
    if (settled && current == NABO_RADIO && nabo_radio_view_back())
        return;
    if (settled && current == NABO_SETTINGS && edit >= 0) {
        edit = -1;
        return;
    }
    if (current == NABO_STATUS || current == NABO_BATTERY || current == NABO_WIFI) {
        if (current == NABO_WIFI)
            WifiManager::GetInstance().StartStation();
        nabo_apps_open(static_cast<NaboApp>(detail_parent.load()));
    } else
        nabo_apps_open(current == NABO_TOOLS ? NABO_HOME : NABO_TOOLS);
}
bool nabo_apps_key(muse_menu_key_t key) {
    last_input = esp_timer_get_time();
    // The FM page owns the raw edges, including double-click and hold timing.
    // Ignore the button library's later click/long callbacks for this page.
    if (wanted.load() == NABO_RADIO && page == NABO_RADIO && ready.load())
        return true;
    if (screen_off.exchange(false)) {
        Board::GetInstance().GetBacklight()->SetBrightness(brightness.load(), false);
        return true;
    }
    // Double UP returns through nested menus. Long UP keeps its direct HOME path.
    if (key == MUSE_MENU_LEFT) {
        return_one_level();
        return true;
    }
    if (!nabo_apps_active()) {
        if (key == MUSE_MENU_DOWN) {
            nabo_apps_open(NABO_TOOLS);
            return true;
        }
        if (key == MUSE_MENU_UP) {
            nabo_apps_open(NABO_STATUS);
            return true;
        }
        return false;
    }
    if (key == MUSE_MENU_BACK) {
        nabo_apps_open(NABO_HOME);
        return true;
    }
    if (!ready)
        return true;
    if (page == NABO_RADIO) {
        if (!nabo_radio_menu_key(key))
            nabo_apps_open(NABO_TOOLS);
        return true;
    }
    if (page == NABO_TOOLS) {
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN)
            selected = (selected + (key == MUSE_MENU_UP ? kToolCount - 1 : 1)) % kToolCount;
        else if (key == MUSE_MENU_SELECT)
            nabo_apps_open(tool_apps[selected]);
    } else if (page == NABO_SETTINGS) {
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN) {
            if (edit >= 0)
                adjust(edit, key == MUSE_MENU_UP ? 1 : -1);
            else
                settings_selected =
                    (settings_selected + (key == MUSE_MENU_UP ? ST_COUNT - 1 : 1)) % ST_COUNT;
        } else if (key == MUSE_MENU_SELECT) {
            if (edit >= 0) {
                edit = -1;
                return true;
            }
            int item = settings_selected;
            if (item == ST_BACK)
                nabo_apps_open(NABO_TOOLS);
            else if (item == ST_VOLUME || item == ST_BRIGHTNESS || item == ST_MIC ||
                     item == ST_SLEEP)
                edit = item;
            else if (item == ST_SPEAKER) {
                speaker = !speaker.load();
                setting_command = item;
            } else if (item == ST_WIFI) {
                wifi_on = !wifi_on.load();
                setting_command = item;
            } else if (item == ST_WIFI_ADD) {
                setting_command = item;
                nabo_apps_open(NABO_WIFI);
            } else if (item == ST_STATE)
                nabo_apps_open(NABO_STATUS);
            else if (item == ST_BATTERY)
                nabo_apps_open(NABO_BATTERY);
            else if (item == ST_SCREEN)
                setting_command = item;
            else if (item == ST_POWER)
                setting_command = item;
        }
    } else if (key == MUSE_MENU_SELECT || key == MUSE_MENU_UP) {
        if (page == NABO_WIFI)
            WifiManager::GetInstance().StartStation();
        nabo_apps_open(NABO_SETTINGS);
    }
    return true;
}
static void radio_edge(int key, bool pressed, uint32_t event_ms) {
    last_input = esp_timer_get_time();
    if (wanted.load() == NABO_RADIO && page == NABO_RADIO && ready.load())
        nabo_radio_view_edge(key, pressed, event_ms);
}
void nabo_apps_down_pressed(uint32_t t) { radio_edge(NRF_DOWN, true, t); }
void nabo_apps_down_released(uint32_t t) { radio_edge(NRF_DOWN, false, t); }
void nabo_apps_up_edge(bool pressed, uint32_t t) { radio_edge(NRF_UP, pressed, t); }
void nabo_apps_charge(bool pressed, uint32_t t) { radio_edge(NRF_OK, pressed, t); }
void nabo_apps_register_tools() {
    auto& server = McpServer::GetInstance();
    server.AddTool(
        "self.apps.open",
        "打开工牌内置程序。app 可用：网络电台(radio)、设置(settings)、工具(tools)。"
        "进入程序会暂停语音对话；双击上键返回上一级，长按上键回到 NABO 恢复小智。",
        PropertyList({Property("app", kPropertyTypeString).SetMaxLength(40)}),
        [](const PropertyList& properties) -> ToolResult {
            const auto& name = properties["app"].value<std::string>();
            NaboApp app = NABO_HOME;
            if (name == "radio" || name == "电台" || name == "网络电台" || name == "收音机")
                app = NABO_RADIO;
            else if (name == "settings" || name == "设置")
                app = NABO_SETTINGS;
            else if (name == "tools" || name == "工具")
                app = NABO_TOOLS;
            else
                return std::unexpected("Unknown app. Use radio, settings or tools.");
            nabo_apps_open(app, 900);
            return std::string("已安排打开 ") + name + "。程序中使用实体按键；长按上键返回小智。";
        });
    server.AddTool(
        "self.radio.search",
        "搜索电台，支持中文台名、地区、类型或拼音首字母；返回最多十个 station 编号。",
        PropertyList({Property("query", kPropertyTypeString).SetMaxLength(63)}),
        [](const PropertyList& p) -> ToolResult {
            ensure_radio_catalog();
            const auto& query = p["query"].value<std::string>();
            cJSON* result = cJSON_CreateArray();
            int found = 0;
            for (int i = 0; i < nabo_radio_station_count() && found < 10; ++i) {
                nrf_station_t s;
                if (!nabo_radio_station_info(i, &s) || !nrf_catalog_match(&s, query.c_str()))
                    continue;
                cJSON* item = cJSON_CreateObject();
                cJSON_AddNumberToObject(item, "station", i);
                cJSON_AddStringToObject(item, "name", s.name);
                cJSON_AddStringToObject(item, "region", s.region);
                cJSON_AddItemToArray(result, item);
                ++found;
            }
            return result;
        });
    server.AddTool("self.radio.play",
                   "播放 search 返回的 station 编号并打开电台。长按上键返回小智。",
                   PropertyList({Property("station", kPropertyTypeInteger, 0, 4095)}),
                   [](const PropertyList& p) -> ToolResult {
                       ensure_radio_catalog();
                       int i = p["station"].value<int>();
                       if (i >= nabo_radio_station_count())
                           return std::unexpected("station 超出电台表范围");
                       nabo_radio_select_station(i);
                       nabo_apps_open(NABO_RADIO, 900);
                       return std::string("即将播放 ") + nabo_radio_station_name(i);
                   });
    server.AddTool("self.radio.next", "播放电台表中的下一台。", PropertyList(),
                   [](const PropertyList&) -> ToolResult {
                       ensure_radio_catalog();
                       int i = (nabo_radio_current_station() + 1) % nabo_radio_station_count();
                       nabo_radio_select_station(i);
                       nabo_apps_open(NABO_RADIO, 900);
                       return std::string(nabo_radio_station_name(i));
                   });
    server.AddTool("self.radio.random", "随机播放一个电台。", PropertyList(),
                   [](const PropertyList&) -> ToolResult {
                       ensure_radio_catalog();
                       int i = static_cast<int>(esp_random() % nabo_radio_station_count());
                       nabo_radio_select_station(i);
                       nabo_apps_open(NABO_RADIO, 900);
                       return std::string(nabo_radio_station_name(i));
                   });
    server.AddTool("self.radio.pause", "暂停电台。", PropertyList(),
                   [](const PropertyList&) -> ToolResult {
                       nabo_radio_stop();
                       return true;
                   });
    server.AddTool("self.radio.resume", "继续上次的电台并打开播放器。", PropertyList(),
                   [](const PropertyList&) -> ToolResult {
                       ensure_radio_catalog();
                       if (!nabo_radio_wanted())
                           nabo_radio_toggle_play();
                       nabo_apps_open(NABO_RADIO, 900);
                       return true;
                   });
    server.AddTool("self.radio.set_volume", "设置工牌扬声器音量，0 到 100。",
                   PropertyList({Property("volume", kPropertyTypeInteger, 0, 100)}),
                   [](const PropertyList& p) -> ToolResult {
                       volume = p["volume"].value<int>();
                       setting_command = ST_VOLUME;
                       return true;
                   });
    server.AddTool("self.radio.now_playing", "查询当前台名、播放状态和定时剩余秒数。",
                   PropertyList(), [](const PropertyList&) -> ToolResult {
                       ensure_radio_catalog();
                       cJSON* result = cJSON_CreateObject();
                       cJSON_AddNumberToObject(result, "station", nabo_radio_current_station());
                       cJSON_AddStringToObject(
                           result, "name", nabo_radio_station_name(nabo_radio_current_station()));
                       cJSON_AddStringToObject(result, "state", nabo_radio_state_text());
                       cJSON_AddNumberToObject(result, "sleep_seconds",
                                               nabo_radio_sleep_remaining());
                       return result;
                   });
    server.AddTool("self.radio.favorite", "收藏或取消收藏当前电台。",
                   PropertyList({Property("favorite", kPropertyTypeBoolean)}),
                   [](const PropertyList& p) -> ToolResult {
                       ensure_radio_catalog();
                       int i = nabo_radio_current_station();
                       bool yes = p["favorite"].value<bool>();
                       if (nabo_radio_favorite(i) != yes)
                           nabo_radio_toggle_favorite(i);
                       return nabo_radio_favorite(i) == yes;
                   });
    server.AddTool("self.radio.sleep_timer", "设置电台定时关闭，0 取消，最多 720 分钟。",
                   PropertyList({Property("minutes", kPropertyTypeInteger, 0, 720)}),
                   [](const PropertyList& p) -> ToolResult {
                       nabo_radio_set_sleep(p["minutes"].value<int>());
                       return true;
                   });
}
bool nabo_apps_voice_command(const char* transcript) {
    if (nabo_apps_active())
        return false;
    const char* name = nabo_voice_launch_app(transcript);
    if (!name)
        return false;
    NaboApp target = !strcmp(name, "radio")      ? NABO_RADIO
                     : !strcmp(name, "settings") ? NABO_SETTINGS
                                                 : NABO_TOOLS;
    nabo_apps_open(target);
    ESP_LOGI("NABO-Voice", "recognized launch -> %s", name);
    return true;
}
