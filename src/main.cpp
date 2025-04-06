#include <iostream>
#include <SDL2/SDL_main.h>  // Add this line
#include <SDL2/SDL.h>
#include <memory>

// 前向声明
class Application;

// 全局变量
std::unique_ptr<Application> g_app = nullptr;

// 应用程序类
class Application {
public:
    Application() : m_running(false), m_window(nullptr), m_renderer(nullptr) {}
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
        }

        return 0;
    }

private:
    void processEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                m_running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    m_running = false;
                }
            }
        }
    }

    void update() {
        // 更新应用程序状态
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

    void drawUILayout() {
        int windowWidth, windowHeight;
        SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);

        // 绘制预览窗口区域
        SDL_Rect previewRect = { 0, 0, windowWidth * 3 / 4, windowHeight / 2 };
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(m_renderer, &previewRect);
        SDL_SetRenderDrawColor(m_renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(m_renderer, &previewRect);

        // 绘制时间线区域
        SDL_Rect timelineRect = { 0, windowHeight / 2, windowWidth * 3 / 4, windowHeight / 2 };
        SDL_SetRenderDrawColor(m_renderer, 50, 50, 50, 255);
        SDL_RenderFillRect(m_renderer, &timelineRect);
        SDL_SetRenderDrawColor(m_renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(m_renderer, &timelineRect);

        // 绘制图层面板区域
        SDL_Rect layersRect = { windowWidth * 3 / 4, 0, windowWidth / 4, windowHeight };
        SDL_SetRenderDrawColor(m_renderer, 60, 60, 60, 255);
        SDL_RenderFillRect(m_renderer, &layersRect);
        SDL_SetRenderDrawColor(m_renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(m_renderer, &layersRect);
    }

    bool m_running;
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
};

int main(int argc, char* argv[]) {
    try {
        g_app = std::make_unique<Application>();
        return g_app->run();
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
}