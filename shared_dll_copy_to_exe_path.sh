# 定义可执行文件的路径
exe_path="build/mingw/x86_64/release/VideoEditor.exe"

# 获取可执行文件所在的目录
exe_dir=$(dirname "$exe_path")

# 使用 ldd 命令获取依赖的共享库列表
ldd_output=$(ldd "$exe_path")
dll_list=$(echo "$ldd_output" | grep -o '/[^[:space:]]*\.dll[^[:space:]]*')

# 遍历共享库列表
for dll in $dll_list; do
    # 复制共享库文件到可执行文件所在的目录
    cp "$dll" "$exe_dir"
    echo "Copied $dll to $exe_dir"
done
