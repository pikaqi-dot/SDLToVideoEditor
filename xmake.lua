add_rules("mode.debug", "mode.release")

-- 设置C++标准
set_languages("c++17")

-- 添加SDL2和FFmpeg依赖
add_requires("libsdl2", "ffmpeg")

-- 定义目标
target("VideoEditor")
    set_kind("binary")
    
    -- 添加源文件
    add_files("src/*.cpp")

    -- 添加包依赖
    add_packages("libsdl2", "ffmpeg")
    
    -- 添加包含目录
    add_includedirs("src")
    
    -- 添加定义，解决SDL main问题
    add_defines("SDL_MAIN_HANDLED")
