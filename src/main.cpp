#include <iostream>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

// FFmpeg头文件
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// 前向声明
class Application;

// 全局变量
std::unique_ptr<Application> g_app = nullptr;

// 视频解码器类
class VideoDecoder {
public:
    VideoDecoder() : 
        formatContext(nullptr), 
        codecContext(nullptr), 
        swsContext(nullptr),
        videoStream(nullptr),
        videoStreamIndex(-1),
        frame(nullptr),
        frameRGB(nullptr),
        buffer(nullptr),
        texture(nullptr) {}

    ~VideoDecoder() {
        cleanup();
    }

    bool openFile(const std::string& filename, SDL_Renderer* renderer) {
        // 打开输入文件
        if (avformat_open_input(&formatContext, filename.c_str(), nullptr, nullptr) != 0) {
            std::cerr << "无法打开视频文件: " << filename << std::endl;
            return false;
        }

        // 获取流信息
        if (avformat_find_stream_info(formatContext, nullptr) < 0) {
            std::cerr << "无法获取流信息" << std::endl;
            cleanup();
            return false;
        }

        // 查找视频流
        for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
            if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamIndex = i;
                videoStream = formatContext->streams[i];
                break;
            }
        }

        if (videoStreamIndex == -1) {
            std::cerr << "未找到视频流" << std::endl;
            cleanup();
            return false;
        }

        // 获取解码器
        const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
        if (!codec) {
            std::cerr << "未找到解码器" << std::endl;
            cleanup();
            return false;
        }

        // 分配解码器上下文
        codecContext = avcodec_alloc_context3(codec);
        if (!codecContext) {
            std::cerr << "无法分配解码器上下文" << std::endl;
            cleanup();
            return false;
        }

        // 复制编解码器参数
        if (avcodec_parameters_to_context(codecContext, videoStream->codecpar) < 0) {
            std::cerr << "无法复制编解码器参数" << std::endl;
            cleanup();
            return false;
        }

        // 打开解码器
        if (avcodec_open2(codecContext, codec, nullptr) < 0) {
            std::cerr << "无法打开解码器" << std::endl;
            cleanup();
            return false;
        }

        // 分配帧
        frame = av_frame_alloc();
        frameRGB = av_frame_alloc();
        if (!frame || !frameRGB) {
            std::cerr << "无法分配帧" << std::endl;
            cleanup();
            return false;
        }

        // 分配缓冲区
        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
        buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
        av_image_fill_arrays(frameRGB->data, frameRGB->linesize, buffer, AV_PIX_FMT_RGB24, 
                            codecContext->width, codecContext->height, 1);

        // 创建转换上下文
        swsContext = sws_getContext(
            codecContext->width, codecContext->height, codecContext->pix_fmt,
            codecContext->width, codecContext->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        if (!swsContext) {
            std::cerr << "无法创建转换上下文" << std::endl;
            cleanup();
            return false;
        }

        // 创建SDL纹理
        texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            codecContext->width,
            codecContext->height
        );

        if (!texture) {
            std::cerr << "无法创建SDL纹理: " << SDL_GetError() << std::endl;
            cleanup();
            return false;
        }

        return true;
    }

    bool readFrame() {
        AVPacket packet;
        int ret;

        // 读取下一帧
        if (av_read_frame(formatContext, &packet) < 0) {
            // 文件结束或错误
            return false;
        }

        // 如果是视频帧
        if (packet.stream_index == videoStreamIndex) {
            // 发送数据包到解码器
            ret = avcodec_send_packet(codecContext, &packet);
            if (ret < 0) {
                std::cerr << "发送数据包到解码器失败" << std::endl;
                av_packet_unref(&packet);
                return false;
            }

            // 从解码器接收帧
            ret = avcodec_receive_frame(codecContext, frame);
            if (ret < 0) {
                std::cerr << "从解码器接收帧失败" << std::endl;
                av_packet_unref(&packet);
                return false;
            }

            // 转换帧格式
            sws_scale(
                swsContext,
                (const uint8_t* const*)frame->data, frame->linesize,
                0, codecContext->height,
                frameRGB->data, frameRGB->linesize
            );

            // 更新纹理
            SDL_UpdateTexture(
                texture,
                nullptr,
                frameRGB->data[0],
                frameRGB->linesize[0]
            );
        }

        av_packet_unref(&packet);
        return true;
    }

    SDL_Texture* getTexture() const {
        return texture;
    }

    int getWidth() const {
        return codecContext ? codecContext->width : 0;
    }

    int getHeight() const {
        return codecContext ? codecContext->height : 0;
    }

    // 将这三个方法从private移到public
    double getDuration() const {
        if (formatContext && videoStream) {
            return videoStream->duration * av_q2d(videoStream->time_base);
        }
        return 0.0;
    }

    bool seekToTime(double timeInSeconds) {
        if (!formatContext || videoStreamIndex == -1) {
            return false;
        }

        int64_t targetTs = (int64_t)(timeInSeconds / av_q2d(videoStream->time_base));
        
        if (av_seek_frame(formatContext, videoStreamIndex, targetTs, AVSEEK_FLAG_BACKWARD) < 0) {
            std::cerr << "跳转失败" << std::endl;
            return false;
        }
        
        avcodec_flush_buffers(codecContext);
        
        // 读取并解码一帧以更新当前显示
        AVPacket packet;
        while (av_read_frame(formatContext, &packet) >= 0) {
            if (packet.stream_index == videoStreamIndex) {
                int ret = avcodec_send_packet(codecContext, &packet);
                if (ret < 0) {
                    av_packet_unref(&packet);
                    continue;
                }

                ret = avcodec_receive_frame(codecContext, frame);
                if (ret < 0) {
                    av_packet_unref(&packet);
                    continue;
                }

                // 转换帧格式
                sws_scale(
                    swsContext,
                    (const uint8_t* const*)frame->data, frame->linesize,
                    0, codecContext->height,
                    frameRGB->data, frameRGB->linesize
                );

                // 更新纹理
                SDL_UpdateTexture(
                    texture,
                    nullptr,
                    frameRGB->data[0],
                    frameRGB->linesize[0]
                );
                
                av_packet_unref(&packet);
                break;
            }
            av_packet_unref(&packet);
        }
        
        return true;
    }

    double getCurrentTime() const {
        if (formatContext && videoStream && frame && frame->pts != AV_NOPTS_VALUE) {
            return frame->pts * av_q2d(videoStream->time_base);
        }
        return 0.0;
    }

    void cleanup() {
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }

        if (buffer) {
            av_free(buffer);
            buffer = nullptr;
        }

        if (frameRGB) {
            av_frame_free(&frameRGB);
            frameRGB = nullptr;
        }

        if (frame) {
            av_frame_free(&frame);
            frame = nullptr;
        }

        if (swsContext) {
            sws_freeContext(swsContext);
            swsContext = nullptr;
        }

        if (codecContext) {
            avcodec_free_context(&codecContext);
            codecContext = nullptr;
        }

        if (formatContext) {
            avformat_close_input(&formatContext);
            formatContext = nullptr;
        }

        videoStream = nullptr;
        videoStreamIndex = -1;
    }

private:
    AVFormatContext* formatContext;
    AVCodecContext* codecContext;
    SwsContext* swsContext;
    AVStream* videoStream;
    int videoStreamIndex;
    AVFrame* frame;
    AVFrame* frameRGB;
    uint8_t* buffer;
    SDL_Texture* texture;
    // 删除这里的方法定义，因为已经移到public部分
};

// 应用程序类
class Application {
public:
    Application() : m_running(false), m_window(nullptr), m_renderer(nullptr), 
                   m_videoLoaded(false), m_isPlaying(false), m_frameDelay(33),
                   m_currentTime(0.0), m_timelineDragging(false) {}
    ~Application() {
        cleanup();
    }

    bool initialize() {
        // 初始化SDL
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
            std::cerr << "SDL初始化失败: " << SDL_GetError() << std::endl;
            return false;
        }

        // 创建窗口
        m_window = SDL_CreateWindow(
            "视频编辑器",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            1280, 720,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
        );

        if (!m_window) {
            std::cerr << "窗口创建失败: " << SDL_GetError() << std::endl;
            return false;
        }

        // 创建渲染器
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!m_renderer) {
            std::cerr << "渲染器创建失败: " << SDL_GetError() << std::endl;
            return false;
        }

        m_running = true;
        return true;
    }

    void cleanup() {
        m_videoDecoder.cleanup();

        if (m_renderer) {
            SDL_DestroyRenderer(m_renderer);
            m_renderer = nullptr;
        }

        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }

        SDL_Quit();
    }

    int run() {
        if (!initialize()) {
            return 1;
        }

        // 主循环
        while (m_running) {
            processEvents();
            update();
            render();

            // 控制帧率
            SDL_Delay(m_frameDelay);
        }

        return 0;
    }

// 将drawTimeline方法添加到Application类内部
void drawTimeline(const SDL_Rect& timelineRect) {
    // 绘制时间线背景
    SDL_Rect timelineBarRect = { 
        timelineRect.x + 10, 
        timelineRect.y + 20, 
        timelineRect.w - 20, 
        30 
    };
    SDL_SetRenderDrawColor(m_renderer, 30, 30, 30, 255);
    SDL_RenderFillRect(m_renderer, &timelineBarRect);
    SDL_SetRenderDrawColor(m_renderer, 80, 80, 80, 255);
    SDL_RenderDrawRect(m_renderer, &timelineBarRect);
    
    // 绘制时间刻度
    double duration = m_videoDecoder.getDuration();
    if (duration > 0) {
        // 每10秒绘制一个刻度
        int numTicks = (int)(duration / 10) + 1;
        for (int i = 0; i <= numTicks; i++) {
            double time = i * 10.0;
            if (time > duration) time = duration;
            
            double ratio = time / duration;
            int tickX = timelineBarRect.x + (int)(ratio * timelineBarRect.w);
            
            // 绘制刻度线
            SDL_SetRenderDrawColor(m_renderer, 150, 150, 150, 255);
            SDL_RenderDrawLine(
                m_renderer,
                tickX, timelineBarRect.y,
                tickX, timelineBarRect.y + timelineBarRect.h
            );
            
            // 绘制时间标签（简化为小矩形）
            SDL_Rect tickRect = { tickX - 2, timelineBarRect.y + timelineBarRect.h + 5, 4, 10 };
            SDL_RenderFillRect(m_renderer, &tickRect);
        }
        
        // 绘制当前时间指示器
        double ratio = m_currentTime / duration;
        int currentX = timelineBarRect.x + (int)(ratio * timelineBarRect.w);
        
        // 绘制指示线
        SDL_SetRenderDrawColor(m_renderer, 255, 0, 0, 255);
        SDL_RenderDrawLine(
            m_renderer,
            currentX, timelineBarRect.y - 10,
            currentX, timelineBarRect.y + timelineBarRect.h + 10
        );
        
        // 绘制指示器头部
        SDL_Rect indicatorHead = { currentX - 5, timelineBarRect.y - 15, 10, 10 };
        SDL_RenderFillRect(m_renderer, &indicatorHead);
        
        // 显示当前时间/总时间
        char timeText[50];
        int minutes = (int)m_currentTime / 60;
        int seconds = (int)m_currentTime % 60;
        int totalMinutes = (int)duration / 60;
        int totalSeconds = (int)duration % 60;
        
        // 在时间线下方显示时间信息
        SDL_Rect timeInfoRect = { 
            timelineBarRect.x, 
            timelineBarRect.y + timelineBarRect.h + 20, 
            100, 
            20 
        };
        SDL_SetRenderDrawColor(m_renderer, 60, 60, 60, 255);
        SDL_RenderFillRect(m_renderer, &timeInfoRect);
        
        // 显示进度
        int progressWidth = (int)(ratio * timeInfoRect.w);
        SDL_Rect progressRect = { timeInfoRect.x, timeInfoRect.y, progressWidth, timeInfoRect.h };
        SDL_SetRenderDrawColor(m_renderer, 100, 100, 255, 255);
        SDL_RenderFillRect(m_renderer, &progressRect);
    }
}
    bool loadVideo(const std::string& filename) {
        if (m_videoDecoder.openFile(filename, m_renderer)) {
            m_videoLoaded = true;
            m_isPlaying = true;
            return true;
        }
        return false;
    }

private:
    void processEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                m_running = false;
            } else if (event.type == SDL_KEYDOWN) {
                handleKeyDown(event.key.keysym.sym);
            } else if (event.type == SDL_DROPFILE) {
                // 处理文件拖放
                char* droppedFile = event.drop.file;
                loadVideo(droppedFile);
                SDL_free(droppedFile);
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                handleMouseButtonDown(event);
            } else if (event.type == SDL_MOUSEBUTTONUP) {
                handleMouseButtonUp(event);
            } else if (event.type == SDL_MOUSEMOTION) {
                handleMouseMotion(event);
            }
        }
    }

    // 添加handleKeyDown方法
    void handleKeyDown(SDL_Keycode key) {
        switch (key) {
            case SDLK_ESCAPE:
                m_running = false;
                break;
            case SDLK_SPACE:
                // 播放/暂停
                m_isPlaying = !m_isPlaying;
                break;
            case SDLK_o:
                // 打开文件对话框
                openFileDialog();
                break;
            default:
                break;
        }
    }

    // 添加openFileDialog方法
    void openFileDialog() {
        // 在实际应用中，这里应该打开一个文件对话框
        // 由于SDL没有内置的文件对话框，这里简化为直接加载一个固定的文件
        std::cout << "请输入视频文件路径: ";
        std::string filename;
        std::cin >> filename;
        loadVideo(filename);
    }

    void handleMouseButtonDown(const SDL_Event& event) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            int mouseX = event.button.x;
            int mouseY = event.button.y;
            
            // 检查是否点击在时间线上
            int windowWidth, windowHeight;
            SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);
            
            SDL_Rect timelineRect = { 0, windowHeight / 2, windowWidth * 3 / 4, windowHeight / 2 };
            SDL_Rect timelineBarRect = { 
                timelineRect.x + 10, 
                timelineRect.y + 20, 
                timelineRect.w - 20, 
                30 
            };
            
            if (mouseX >= timelineBarRect.x && mouseX <= timelineBarRect.x + timelineBarRect.w &&
                mouseY >= timelineBarRect.y && mouseY <= timelineBarRect.y + timelineBarRect.h) {
                m_timelineDragging = true;
                updateTimelinePosition(mouseX, timelineBarRect);
            }
        }
    }

    void handleMouseButtonUp(const SDL_Event& event) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            m_timelineDragging = false;
        }
    }

    void handleMouseMotion(const SDL_Event& event) {
        if (m_timelineDragging) {
            int mouseX = event.motion.x;
            
            int windowWidth, windowHeight;
            SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);
            
            SDL_Rect timelineRect = { 0, windowHeight / 2, windowWidth * 3 / 4, windowHeight / 2 };
            SDL_Rect timelineBarRect = { 
                timelineRect.x + 10, 
                timelineRect.y + 20, 
                timelineRect.w - 20, 
                30 
            };
            
            updateTimelinePosition(mouseX, timelineBarRect);
        }
    }

    void updateTimelinePosition(int mouseX, const SDL_Rect& timelineBarRect) {
        if (!m_videoLoaded) return;
        
        // 计算新的时间位置
        double ratio = (double)(mouseX - timelineBarRect.x) / timelineBarRect.w;
        if (ratio < 0.0) ratio = 0.0;
        if (ratio > 1.0) ratio = 1.0;
        
        double duration = m_videoDecoder.getDuration();
        double newTime = ratio * duration;
        
        // 跳转到新时间
        m_videoDecoder.seekToTime(newTime);
        m_currentTime = newTime;
    }

    void update() {
        // 更新应用程序状态
        if (m_videoLoaded && m_isPlaying && !m_timelineDragging) {
            // 读取下一帧
            if (!m_videoDecoder.readFrame()) {
                // 视频结束，重新开始
                m_isPlaying = false;
            } else {
                // 更新当前时间
                m_currentTime = m_videoDecoder.getCurrentTime();
            }
        }
    }

    void render() {
        // 清除屏幕
        SDL_SetRenderDrawColor(m_renderer, 40, 40, 40, 255);
        SDL_RenderClear(m_renderer);

        // 绘制UI布局
        drawUILayout();

        // 更新屏幕
        SDL_RenderPresent(m_renderer);
    }

    // 将drawTimeline方法添加到Application类内部
    void drawUILayout() {
        int windowWidth, windowHeight;
        SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);

        // 绘制预览窗口区域
        SDL_Rect previewRect = { 0, 0, windowWidth * 3 / 4, windowHeight / 2 };
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(m_renderer, &previewRect);
        
        // 如果视频已加载，绘制视频帧
        if (m_videoLoaded && m_videoDecoder.getTexture()) {
            // 计算视频在预览窗口中的位置和大小
            int videoWidth = m_videoDecoder.getWidth();
            int videoHeight = m_videoDecoder.getHeight();
            
            // 保持宽高比
            float videoAspect = (float)videoWidth / videoHeight;
            float previewAspect = (float)previewRect.w / previewRect.h;
            
            SDL_Rect destRect;
            if (videoAspect > previewAspect) {
                // 视频更宽，以宽度为基准
                destRect.w = previewRect.w;
                destRect.h = (int)(previewRect.w / videoAspect);
                destRect.x = previewRect.x;
                destRect.y = previewRect.y + (previewRect.h - destRect.h) / 2;
            } else {
                // 视频更高，以高度为基准
                destRect.h = previewRect.h;
                destRect.w = (int)(previewRect.h * videoAspect);
                destRect.x = previewRect.x + (previewRect.w - destRect.w) / 2;
                destRect.y = previewRect.y;
            }
            
            SDL_RenderCopy(m_renderer, m_videoDecoder.getTexture(), nullptr, &destRect);
        }
        
        SDL_SetRenderDrawColor(m_renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(m_renderer, &previewRect);

        // 绘制时间线区域
        SDL_Rect timelineRect = { 0, windowHeight / 2, windowWidth * 3 / 4, windowHeight / 2 };
        SDL_SetRenderDrawColor(m_renderer, 50, 50, 50, 255);
        SDL_RenderFillRect(m_renderer, &timelineRect);
        
        // 添加时间轴绘制代码
        if (m_videoLoaded) {
            drawTimeline(timelineRect);
        }
        
        SDL_SetRenderDrawColor(m_renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(m_renderer, &timelineRect);

        // 绘制图层面板区域
        SDL_Rect layersRect = { windowWidth * 3 / 4, 0, windowWidth / 4, windowHeight };
        SDL_SetRenderDrawColor(m_renderer, 60, 60, 60, 255);
        SDL_RenderFillRect(m_renderer, &layersRect);
        SDL_SetRenderDrawColor(m_renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(m_renderer, &layersRect);
        
        // 绘制状态信息
        drawStatusInfo(windowWidth, windowHeight);
    }
    
    void drawStatusInfo(int windowWidth, int windowHeight) {
        // 在实际应用中，这里应该绘制状态信息，如播放状态、当前时间等
        // 由于SDL没有内置的文本渲染功能，这里简化为绘制一个状态指示器
        
        // 绘制播放/暂停状态指示器
        SDL_Rect statusRect = { windowWidth - 50, windowHeight - 50, 30, 30 };
        if (m_isPlaying) {
            // 绘制暂停图标
            SDL_SetRenderDrawColor(m_renderer, 0, 255, 0, 255);
            SDL_RenderFillRect(m_renderer, &statusRect);
        } else {
            // 绘制播放图标
            SDL_SetRenderDrawColor(m_renderer, 255, 0, 0, 255);
            SDL_Rect playIcon = { statusRect.x + 5, statusRect.y + 5, 20, 20 };
            SDL_RenderFillRect(m_renderer, &playIcon);
        }
    }

    bool m_running;
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    VideoDecoder m_videoDecoder;
    bool m_videoLoaded;
    bool m_isPlaying;
    int m_frameDelay; // 毫秒
    double m_currentTime; // 当前播放时间（秒）
    bool m_timelineDragging; // 是否正在拖动时间线
};

int main(int argc, char* argv[]) {
    try {
        g_app = std::make_unique<Application>();
        
        // 如果有命令行参数，尝试加载视频文件
        if (argc > 1) {
            g_app->loadVideo(argv[1]);
        }
        
        return g_app->run();
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
}


