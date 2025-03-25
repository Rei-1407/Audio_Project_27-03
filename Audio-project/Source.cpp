#include <SDL.h>
#include <stdio.h>
#include <fstream>
#include <algorithm> // for std::min, std::max, std::reverse

// Fix for the SDL main issue
#ifdef main
#undef main
#endif

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;

// Dữ liệu âm thanh gốc
static Uint8* wav_data = NULL;
static Uint32 wav_data_len = 0;
static Uint8* audio_pos = NULL;
static Uint32 audio_len = 0;

// Dữ liệu âm thanh đảo ngược
static Uint8* reversed_wav_data = NULL;
static Uint8* reversed_audio_pos = NULL;
static Uint32 reversed_audio_len = 0;

// Thông tin thời gian phát âm thanh
static Uint32 current_play_pos = 0;
const char* filepath = "File_audio.WAV";

// Biến kiểm soát âm lượng
static int volume = SDL_MIX_MAXVOLUME;

// Biến điều khiển phóng to
static float zoom_level = 1.0f; // 1.0 = không phóng to
static float view_position = 0.0f; // 0.0 = bắt đầu, 1.0 = kết thúc
static int mouse_drag_start_x = -1;
static bool is_dragging = false;

// Biến chọn chế độ phát
enum PlayMode {
    PLAY_ORIGINAL,
    PLAY_REVERSED,
    PLAY_BOTH
};
static PlayMode current_play_mode = PLAY_ORIGINAL;

// Cấu trúc để chứa dữ liệu âm thanh cho callback
struct AudioMixData {
    Uint8* original_data;
    Uint32 original_length;
    Uint32 original_position;

    Uint8* reversed_data;
    Uint32 reversed_length;
    Uint32 reversed_position;

    PlayMode mode;
    float mix_ratio; // 0.0 = chỉ gốc, 1.0 = chỉ đảo ngược, 0.5 = cả hai bằng nhau
    int volume;     // Thêm biến điều khiển âm lượng
};

static AudioMixData mix_data;

// Hàm tạo dữ liệu âm thanh đảo ngược
Uint8* createReversedAudio(const Uint8* data, Uint32 length, Uint16 bitsPerSample, Uint16 numChannels) {
    // Cấp phát bộ nhớ cho dữ liệu đảo ngược
    Uint8* reversed = (Uint8*)SDL_malloc(length);
    if (!reversed) return NULL;

    // Sao chép dữ liệu gốc
    SDL_memcpy(reversed, data, length);

    // Tính số mẫu
    int bytesPerSample = bitsPerSample / 8;
    int samplesPerChannel = length / bytesPerSample / numChannels;

    // Xử lý tùy thuộc vào độ sâu bit
    if (bitsPerSample == 16) {
        // Đảo ngược các mẫu 16-bit
        int16_t* samples = reinterpret_cast<int16_t*>(reversed);
        int totalSamples = samplesPerChannel * numChannels;

        // Đảo ngược từng khung (frame) âm thanh, không đảo ngược thứ tự kênh
        for (int i = 0; i < samplesPerChannel / 2; i++) {
            for (int ch = 0; ch < numChannels; ch++) {
                int pos1 = i * numChannels + ch;
                int pos2 = (samplesPerChannel - 1 - i) * numChannels + ch;

                // Hoán đổi mẫu
                int16_t temp = samples[pos1];
                samples[pos1] = samples[pos2];
                samples[pos2] = temp;
            }
        }
    }
    else if (bitsPerSample == 8) {
        // Đảo ngược các mẫu 8-bit
        Uint8* samples = reversed;

        // Đảo ngược từng khung (frame) âm thanh, không đảo ngược thứ tự kênh
        for (int i = 0; i < samplesPerChannel / 2; i++) {
            for (int ch = 0; ch < numChannels; ch++) {
                int pos1 = i * numChannels + ch;
                int pos2 = (samplesPerChannel - 1 - i) * numChannels + ch;

                // Hoán đổi mẫu
                Uint8 temp = samples[pos1];
                samples[pos1] = samples[pos2];
                samples[pos2] = temp;
            }
        }
    }

    printf("Reversed audio data created successfully.\n");
    return reversed;
}

// Hàm xử lý âm thanh SDL
void audioCallback(void* userdata, Uint8* stream, int len) {
    AudioMixData* data = static_cast<AudioMixData*>(userdata);
    if (!data) {
        printf("Error: NULL userdata in callback\n");
        return;
    }

    // Xóa buffer
    SDL_memset(stream, 0, len);

    // Xác định dữ liệu cần phát tùy thuộc vào chế độ
    if (data->mode == PLAY_ORIGINAL || data->mode == PLAY_BOTH) {
        if (data->original_data && data->original_position < data->original_length) {
            // Tính toán số byte cần sao chép
            Uint32 remaining = data->original_length - data->original_position;
            Uint32 bytesToCopy = (remaining < static_cast<Uint32>(len)) ? remaining : static_cast<Uint32>(len);

            // Âm lượng cho âm thanh gốc (1.0 nếu chỉ phát gốc, mix_ratio nếu phát cả hai)
            int volume = (data->mode == PLAY_ORIGINAL) ? data->volume :
                static_cast<int>((1.0f - data->mix_ratio) * data->volume);

            // Sao chép dữ liệu âm thanh gốc
            SDL_MixAudio(stream, data->original_data + data->original_position, bytesToCopy, volume);

            // Cập nhật vị trí phát
            data->original_position += bytesToCopy;

            // Cập nhật vị trí phát hiện tại cho hiển thị
            current_play_pos = data->original_position;

            // Nếu đã phát hết, reset về đầu
            if (data->original_position >= data->original_length) {
                data->original_position = 0;
            }
        }
    }

    if (data->mode == PLAY_REVERSED || data->mode == PLAY_BOTH) {
        if (data->reversed_data && data->reversed_position < data->reversed_length) {
            // Tính toán số byte cần sao chép
            Uint32 remaining = data->reversed_length - data->reversed_position;
            Uint32 bytesToCopy = (remaining < static_cast<Uint32>(len)) ? remaining : static_cast<Uint32>(len);

            // Âm lượng cho âm thanh đảo ngược
            int volume = (data->mode == PLAY_REVERSED) ? data->volume :
                static_cast<int>(data->mix_ratio * data->volume);

            // Sao chép dữ liệu âm thanh đảo ngược
            SDL_MixAudio(stream, data->reversed_data + data->reversed_position, bytesToCopy, volume);

            // Cập nhật vị trí phát
            data->reversed_position += bytesToCopy;

            // Nếu đã phát hết, reset về đầu
            if (data->reversed_position >= data->reversed_length) {
                data->reversed_position = 0;
            }
        }
    }
}

// Hàm vẽ đồ thị dạng sóng
void drawWaveform(SDL_Renderer* renderer, const Uint8* data, Uint32 dataLength,
    Uint16 bitsPerSample, Uint16 numChannels,
    int x, int y, int width, int height,
    float zoom, float position, int channel, SDL_Color color) {

    if (dataLength == 0) return;

    // Tính số byte mỗi mẫu và tổng số mẫu
    int bytesPerSample = bitsPerSample / 8;
    int totalSamples = dataLength / bytesPerSample / numChannels;

    // Tính phạm vi hiển thị dựa trên zoom và position
    int visibleSamples = static_cast<int>(totalSamples / zoom);
    int startSample = static_cast<int>(position * (totalSamples - visibleSamples));

    // Đảm bảo các giá trị hợp lệ
    startSample = std::max(0, startSample);
    visibleSamples = std::min(visibleSamples, totalSamples - startSample);

    // Số mẫu trên mỗi pixel
    float samplesPerPixel = static_cast<float>(visibleSamples) / width;

    // Vẽ trục thời gian (vị trí y trung tâm)
    int centerY = y + height / 2;
    SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
    SDL_RenderDrawLine(renderer, x, centerY, x + width, centerY);

    // Vẽ dạng sóng
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (int i = 0; i < width; i++) {
        // Tính các mẫu cho pixel này
        int pixelStartSample = startSample + static_cast<int>(i * samplesPerPixel);
        int pixelEndSample = startSample + static_cast<int>((i + 1) * samplesPerPixel);

        // Giới hạn trong phạm vi mẫu
        pixelEndSample = std::min(pixelEndSample, totalSamples);

        if (pixelStartSample >= pixelEndSample) continue;

        // Tìm giá trị min và max trong phạm vi mẫu của pixel
        float minValue = 1.0f;
        float maxValue = -1.0f;

        for (int s = pixelStartSample; s < pixelEndSample; s++) {
            float value = 0.0f;

            if (bitsPerSample == 16) {
                const int16_t* samples = reinterpret_cast<const int16_t*>(data);
                value = samples[s * numChannels + channel] / 32768.0f;
            }
            else if (bitsPerSample == 8) {
                value = (data[s * numChannels + channel] - 128) / 128.0f;
            }

            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }

        // Chuyển đổi giá trị sang tọa độ màn hình
        int yMin = centerY - static_cast<int>(minValue * height / 2);
        int yMax = centerY - static_cast<int>(maxValue * height / 2);

        // Đảm bảo vẽ ít nhất một điểm ảnh
        if (yMin == yMax) {
            yMax = yMin - 1;
        }

        // Vẽ đường dọc từ min đến max
        SDL_RenderDrawLine(renderer, x + i, yMin, x + i, yMax);
    }
}

// Vẽ thanh âm lượng
void drawVolumeControl(SDL_Renderer* renderer, int x, int y, int width, int height, int volume) {
    // Vẽ khung
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_Rect frame = { x, y, width, height };
    SDL_RenderDrawRect(renderer, &frame);

    // Vẽ thanh âm lượng
    int volWidth = (volume * width) / SDL_MIX_MAXVOLUME;

    // Chọn màu dựa trên mức âm lượng
    if (volume < SDL_MIX_MAXVOLUME / 3) {
        SDL_SetRenderDrawColor(renderer, 0, 255, 0, 200); // Xanh lá cho âm lượng thấp
    }
    else if (volume < 2 * SDL_MIX_MAXVOLUME / 3) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 200); // Vàng cho âm lượng trung bình
    }
    else {
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 200); // Đỏ cho âm lượng cao
    }

    SDL_Rect volRect = { x, y, volWidth, height };
    SDL_RenderFillRect(renderer, &volRect);
}

// Vẽ thanh phóng to và vị trí
void drawZoomControls(SDL_Renderer* renderer, int x, int y, int width, int height, float zoom, float position) {
    // Vẽ khung
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_Rect frame = { x, y, width, height };
    SDL_RenderDrawRect(renderer, &frame);

    // Vẽ thanh chỉ vị trí và kích thước phần nhìn thấy
    int viewWidth = static_cast<int>(width / zoom);
    int viewX = x + static_cast<int>(position * (width - viewWidth));

    SDL_SetRenderDrawColor(renderer, 100, 200, 255, 200);
    SDL_Rect viewRect = { viewX, y, viewWidth, height };
    SDL_RenderFillRect(renderer, &viewRect);

    // Vẽ viền cho phần nhìn thấy
    SDL_SetRenderDrawColor(renderer, 50, 100, 200, 255);
    SDL_RenderDrawRect(renderer, &viewRect);
}

// Hiển thị thông tin phóng to và chế độ phát
void drawInfo(SDL_Renderer* renderer, int x, int y, float zoom, float position, PlayMode mode, int volume) {
    // Vẽ hình chữ nhật nền
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 200);
    SDL_Rect infoRect = { x, y, 300, 80 };
    SDL_RenderFillRect(renderer, &infoRect);

    // Vẽ viền
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_RenderDrawRect(renderer, &infoRect);

    // In ra thông tin trên console (thay thế bằng text rendering khi có SDL_ttf)
    const char* modeText = "";
    switch (mode) {
    case PLAY_ORIGINAL: modeText = "Original"; break;
    case PLAY_REVERSED: modeText = "Reversed"; break;
    case PLAY_BOTH: modeText = "Both (Mixed)"; break;
    }

    printf("\rZoom: %.1fx | Pos: %.2f | Mode: %s | Volume: %d%%",
        zoom, position, modeText, (volume * 100) / SDL_MIX_MAXVOLUME);
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    printf("Starting audio application...\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        return 1;
    }

    printf("SDL initialized successfully.\n");

    window = SDL_CreateWindow("Audio Waveform Comparison", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1000, 700, 0);
    if (!window) {
        printf("Window could not be created! SDL Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("Renderer could not be created! SDL Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_AudioSpec wav_spec;
    SDL_AudioSpec obtained_spec;

    printf("Attempting to load file: %s\n", filepath);

    if (SDL_LoadWAV(filepath, &wav_spec, &wav_data, &wav_data_len) == NULL) {
        printf("Could not load WAV file: %s\n", SDL_GetError());

        // Try with SDL_GetBasePath as an alternative
        char alt_filepath[512];
        snprintf(alt_filepath, sizeof(alt_filepath), "%sFile_audio.WAV", SDL_GetBasePath());
        printf("Trying alternative path: %s\n", alt_filepath);

        if (SDL_LoadWAV(alt_filepath, &wav_spec, &wav_data, &wav_data_len) == NULL) {
            printf("Could not load WAV file with alternative path: %s\n", SDL_GetError());
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }

    printf("Successfully loaded WAV file.\n");
    printf("Audio specifications:\n");
    printf("  Format: %u\n", wav_spec.format);
    printf("  Channels: %u\n", wav_spec.channels);
    printf("  Frequency: %u Hz\n", wav_spec.freq);
    printf("  Audio data size: %u bytes\n", wav_data_len);

    // Tính toán độ sâu bit từ định dạng
    Uint16 bitsPerSample = 0;
    if (wav_spec.format == AUDIO_U8 || wav_spec.format == AUDIO_S8) {
        bitsPerSample = 8;
    }
    else {
        bitsPerSample = 16; // Giả định là 16-bit cho các định dạng khác
    }
    printf("  Bits per sample: %u\n", bitsPerSample);

    // Tạo dữ liệu âm thanh đảo ngược
    printf("Creating reversed audio data...\n");
    reversed_wav_data = createReversedAudio(wav_data, wav_data_len, bitsPerSample, wav_spec.channels);

    if (!reversed_wav_data) {
        printf("Failed to create reversed audio data\n");
        SDL_FreeWAV(wav_data);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Khởi tạo âm lượng mặc định
    volume = SDL_MIX_MAXVOLUME; // 100%

    // Thiết lập thông tin âm thanh cho callback
    mix_data.original_data = wav_data;
    mix_data.original_length = wav_data_len;
    mix_data.original_position = 0;

    mix_data.reversed_data = reversed_wav_data;
    mix_data.reversed_length = wav_data_len;
    mix_data.reversed_position = 0;

    mix_data.mode = PLAY_ORIGINAL;
    mix_data.mix_ratio = 0.5f; // Trộn 50-50 khi phát cả hai
    mix_data.volume = volume;

    // Thiết lập callback âm thanh
    wav_spec.callback = audioCallback;
    wav_spec.userdata = &mix_data;

    audio_pos = wav_data;
    audio_len = wav_data_len;

    reversed_audio_pos = reversed_wav_data;
    reversed_audio_len = wav_data_len;

    printf("Opening audio device...\n");
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &wav_spec, &obtained_spec,
        SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    if (dev == 0) {
        printf("Failed to open audio: %s\n", SDL_GetError());
        SDL_free(reversed_wav_data);
        SDL_FreeWAV(wav_data);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    printf("Audio device opened with ID: %u\n", dev);
    printf("Obtained audio specs: frequency=%d format=%d channels=%d\n",
        obtained_spec.freq, obtained_spec.format, obtained_spec.channels);

    printf("Starting audio playback. Close window to exit.\n");
    printf("Controls:\n");
    printf("  - Mouse Wheel: Zoom in/out\n");
    printf("  - Mouse Drag: Pan the view\n");
    printf("  - +/- Keys: Zoom in/out\n");
    printf("  - Arrow Keys: Pan left/right\n");
    printf("  - Home: Reset zoom and position\n");
    printf("  - 1: Play original audio\n");
    printf("  - 2: Play reversed audio\n");
    printf("  - 3: Play both (mixed)\n");
    printf("  - Space: Pause/Resume\n");
    printf("  - PgUp/PgDn: Increase/decrease volume\n");

    printf("Starting audio playback...\n");
    SDL_PauseAudioDevice(dev, 0);  // Start playing audio

    SDL_Event event;
    int running = 1;
    bool isPaused = false;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
            else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running = 0;
                    break;
                case SDLK_HOME: // Reset zoom và vị trí
                    zoom_level = 1.0f;
                    view_position = 0.0f;
                    break;
                case SDLK_PLUS:
                case SDLK_KP_PLUS:
                case SDLK_EQUALS: // Phím '=' thường cũng là '+'
                    zoom_level *= 1.2f; // Phóng to thêm 20%
                    zoom_level = std::min(zoom_level, 50.0f); // Giới hạn phóng to tối đa
                    break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS:
                    zoom_level /= 1.2f; // Thu nhỏ 20%
                    zoom_level = std::max(zoom_level, 1.0f); // Không thu nhỏ quá mức ban đầu
                    break;
                case SDLK_LEFT:
                    view_position -= 0.05f / zoom_level; // Di chuyển sang trái
                    view_position = std::max(0.0f, view_position);
                    break;
                case SDLK_RIGHT:
                    view_position += 0.05f / zoom_level; // Di chuyển sang phải
                    view_position = std::min(view_position, 1.0f - (1.0f / zoom_level));
                    break;
                case SDLK_PAGEUP: // Tăng âm lượng
                    volume = std::min(volume + 8, SDL_MIX_MAXVOLUME);
                    mix_data.volume = volume;
                    printf("Volume: %d%%\n", (volume * 100) / SDL_MIX_MAXVOLUME);
                    break;
                case SDLK_PAGEDOWN: // Giảm âm lượng
                    volume = std::max(volume - 8, 0);
                    mix_data.volume = volume;
                    printf("Volume: %d%%\n", (volume * 100) / SDL_MIX_MAXVOLUME);
                    break;
                case SDLK_SPACE: // Tạm dừng/tiếp tục phát
                    isPaused = !isPaused;
                    SDL_PauseAudioDevice(dev, isPaused ? 1 : 0);
                    printf("%s\n", isPaused ? "Paused" : "Playing");
                    break;
                case SDLK_1: // Phát âm thanh gốc
                    mix_data.mode = PLAY_ORIGINAL;
                    printf("Playing original audio\n");
                    break;
                case SDLK_2: // Phát âm thanh đảo ngược
                    mix_data.mode = PLAY_REVERSED;
                    printf("Playing reversed audio\n");
                    break;
                case SDLK_3: // Phát cả hai
                    mix_data.mode = PLAY_BOTH;
                    printf("Playing both (mixed) audio\n");
                    break;
                }
            }
            else if (event.type == SDL_MOUSEWHEEL) {
                float prevZoom = zoom_level;

                // Phóng to/thu nhỏ dựa vào cuộn chuột
                if (event.wheel.y > 0) { // Cuộn lên = phóng to
                    zoom_level *= 1.1f;
                    zoom_level = std::min(zoom_level, 50.0f);
                }
                else if (event.wheel.y < 0) { // Cuộn xuống = thu nhỏ
                    zoom_level /= 1.1f;
                    zoom_level = std::max(zoom_level, 1.0f);
                }

                // Điều chỉnh vị trí để phóng to vào vị trí con trỏ chuột
                if (prevZoom != zoom_level) {
                    int mouseX, mouseY;
                    SDL_GetMouseState(&mouseX, &mouseY);

                    // Áp dụng nếu chuột nằm trên một trong hai đồ thị
                    bool onOriginalWaveform = (mouseY >= 50 && mouseY < 250);
                    bool onReversedWaveform = (mouseY >= 300 && mouseY < 500);

                    if (onOriginalWaveform || onReversedWaveform) {
                        float relativeX = static_cast<float>(mouseX - 50) / 900.0f;
                        float viewCenter = view_position + relativeX / prevZoom;
                        view_position = viewCenter - relativeX / zoom_level;

                        // Đảm bảo vị trí trong phạm vi hợp lệ
                        view_position = std::max(0.0f, std::min(view_position, 1.0f - (1.0f / zoom_level)));
                    }
                }
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    // Bắt đầu kéo
                    mouse_drag_start_x = event.button.x;
                    is_dragging = true;
                }
            }
            else if (event.type == SDL_MOUSEBUTTONUP) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    // Kết thúc kéo
                    is_dragging = false;
                    mouse_drag_start_x = -1;
                }
            }
            else if (event.type == SDL_MOUSEMOTION) {
                if (is_dragging && mouse_drag_start_x != -1) {
                    // Tính toán khoảng cách kéo
                    int dx = mouse_drag_start_x - event.motion.x;
                    float relativeMove = static_cast<float>(dx) / 900.0f;

                    // Di chuyển vị trí xem
                    view_position += relativeMove * (1.0f / zoom_level);
                    view_position = std::max(0.0f, std::min(view_position, 1.0f - (1.0f / zoom_level)));

                    // Cập nhật vị trí bắt đầu kéo
                    mouse_drag_start_x = event.motion.x;
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
        SDL_RenderClear(renderer);

        // Vẽ khu vực cho dạng sóng gốc
        SDL_SetRenderDrawColor(renderer, 40, 40, 60, 255);
        SDL_Rect originalArea = { 50, 50, 900, 200 };
        SDL_RenderFillRect(renderer, &originalArea);

        // Vẽ tên cho dạng sóng gốc
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        // Thay thế bằng text rendering khi có SDL_ttf
        // Hiện tại chỉ vẽ một hình chữ nhật nhỏ ở góc để đại diện cho nhãn
        SDL_Rect originalLabel = { 10, 50, 30, 20 };
        SDL_RenderDrawRect(renderer, &originalLabel);

        // Vẽ dạng sóng gốc
        for (int ch = 0; ch < wav_spec.channels; ch++) {
            SDL_Color waveColor = { 0, 255, 0, 255 }; // Màu xanh lá cho âm thanh gốc
            drawWaveform(renderer, wav_data, wav_data_len, bitsPerSample, wav_spec.channels,
                50, 50, 900, 200, zoom_level, view_position, ch, waveColor);
        }

        // Vẽ vị trí phát hiện tại trên dạng sóng gốc
        if (wav_data_len > 0 && mix_data.mode != PLAY_REVERSED) {
            float playPosition = static_cast<float>(mix_data.original_position) / wav_data_len;

            // Điều chỉnh vị trí phát theo phóng to và vị trí xem
            if (zoom_level > 1.0f) {
                float adjustedPosition = (playPosition - view_position) * zoom_level;
                if (adjustedPosition >= 0.0f && adjustedPosition <= 1.0f) {
                    int screenX = 50 + static_cast<int>(adjustedPosition * 900);
                    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255); // Màu vàng
                    SDL_RenderDrawLine(renderer, screenX, 50, screenX, 250);
                }
            }
            else {
                int screenX = 50 + static_cast<int>(playPosition * 900);
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
                SDL_RenderDrawLine(renderer, screenX, 50, screenX, 250);
            }
        }

        // Vẽ khu vực cho dạng sóng đảo ngược
        SDL_SetRenderDrawColor(renderer, 40, 40, 60, 255);
        SDL_Rect reversedArea = { 50, 300, 900, 200 };
        SDL_RenderFillRect(renderer, &reversedArea);

        // Vẽ tên cho dạng sóng đảo ngược
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_Rect reversedLabel = { 10, 300, 30, 20 };
        SDL_RenderDrawRect(renderer, &reversedLabel);

        // Vẽ dạng sóng đảo ngược
        for (int ch = 0; ch < wav_spec.channels; ch++) {
            SDL_Color waveColor = { 255, 0, 0, 255 }; // Màu đỏ cho âm thanh đảo ngược
            drawWaveform(renderer, reversed_wav_data, wav_data_len, bitsPerSample, wav_spec.channels,
                50, 300, 900, 200, zoom_level, view_position, ch, waveColor);
        }

        // Vẽ vị trí phát hiện tại trên dạng sóng đảo ngược
        if (wav_data_len > 0 && mix_data.mode != PLAY_ORIGINAL) {
            float playPosition = static_cast<float>(mix_data.reversed_position) / wav_data_len;

            // Điều chỉnh vị trí phát theo phóng to và vị trí xem
            if (zoom_level > 1.0f) {
                float adjustedPosition = (playPosition - view_position) * zoom_level;
                if (adjustedPosition >= 0.0f && adjustedPosition <= 1.0f) {
                    int screenX = 50 + static_cast<int>(adjustedPosition * 900);
                    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
                    SDL_RenderDrawLine(renderer, screenX, 300, screenX, 500);
                }
            }
            else {
                int screenX = 50 + static_cast<int>(playPosition * 900);
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
                SDL_RenderDrawLine(renderer, screenX, 300, screenX, 500);
            }
        }

        // Vẽ thanh kiểm soát phóng to
        drawZoomControls(renderer, 50, 550, 900, 20, zoom_level, view_position);

        // Vẽ thanh điều khiển âm lượng
        drawVolumeControl(renderer, 50, 580, 200, 20, volume);

        // Hiển thị thông tin và chế độ phát
        drawInfo(renderer, 50, 610, zoom_level, view_position, mix_data.mode, volume);

        // Hiển thị các điều khiển
        SDL_SetRenderDrawColor(renderer, 40, 40, 50, 255);
        SDL_Rect controlsRect = { 600, 580, 350, 100 };
        SDL_RenderFillRect(renderer, &controlsRect);

        // Vẽ nút điều khiển
        // Thay bằng text rendering khi có SDL_ttf
        // Hiện chỉ vẽ các hình chữ nhật đại diện cho các nút
        int buttonWidth = 100;
        int buttonHeight = 30;
        int buttonY = 600;

        // Nút phát nguyên bản
        SDL_SetRenderDrawColor(renderer, mix_data.mode == PLAY_ORIGINAL ? 0 : 100,
            mix_data.mode == PLAY_ORIGINAL ? 200 : 100,
            0, 255);
        SDL_Rect originalButton = { 620, buttonY, buttonWidth, buttonHeight };
        SDL_RenderFillRect(renderer, &originalButton);
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(renderer, &originalButton);

        // Nút phát đảo ngược
        SDL_SetRenderDrawColor(renderer, 200,
            mix_data.mode == PLAY_REVERSED ? 0 : 100,
            mix_data.mode == PLAY_REVERSED ? 0 : 100,
            255);
        SDL_Rect reversedButton = { 730, buttonY, buttonWidth, buttonHeight };
        SDL_RenderFillRect(renderer, &reversedButton);
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(renderer, &reversedButton);

        // Nút phát cả hai
        SDL_SetRenderDrawColor(renderer, mix_data.mode == PLAY_BOTH ? 200 : 100,
            mix_data.mode == PLAY_BOTH ? 200 : 100,
            mix_data.mode == PLAY_BOTH ? 200 : 100,
            255);
        SDL_Rect bothButton = { 840, buttonY, buttonWidth, buttonHeight };
        SDL_RenderFillRect(renderer, &bothButton);
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(renderer, &bothButton);

        // Nút tạm dừng/tiếp tục
        SDL_SetRenderDrawColor(renderer, isPaused ? 200 : 100,
            isPaused ? 100 : 200,
            100, 255);
        SDL_Rect pauseButton = { 730, buttonY + 40, buttonWidth, buttonHeight };
        SDL_RenderFillRect(renderer, &pauseButton);
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(renderer, &pauseButton);

        SDL_RenderPresent(renderer);
        SDL_Delay(16); // 60 FPS
    }

    // Giải phóng tài nguyên
    printf("Cleaning up resources...\n");
    SDL_PauseAudioDevice(dev, 1); // Dừng phát âm thanh trước khi đóng
    SDL_CloseAudioDevice(dev);
    SDL_free(reversed_wav_data);
    SDL_FreeWAV(wav_data);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("Application closed successfully.\n");

    return 0;
}
