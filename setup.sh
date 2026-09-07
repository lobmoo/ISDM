#!/bin/bash

# ISDM项目快速启动脚本

set -e

echo "======================================"
echo "  ISDM 项目初始化脚本"
echo "======================================"
echo ""

# 检查依赖
echo "[1/7] 检查依赖..."

# 检查CMake
if ! command -v cmake &> /dev/null; then
    echo "❌ CMake未安装，请先安装CMake 3.15+"
    exit 1
fi
echo "✓ CMake已安装: $(cmake --version | head -n1)"

# 检查编译器
if command -v g++ &> /dev/null; then
    echo "✓ GCC已安装: $(g++ --version | head -n1)"
elif command -v clang++ &> /dev/null; then
    echo "✓ Clang已安装: $(clang++ --version | head -n1)"
else
    echo "❌ 未找到C++编译器，请安装GCC 9+或Clang 10+"
    exit 1
fi

# 初始化第三方库
echo ""
echo "[2/7] 初始化第三方库..."

mkdir -p third_party

# Google Test
if [ ! -d "third_party/googletest" ]; then
    echo "  - 克隆googletest..."
    git clone --depth 1 https://github.com/google/googletest.git third_party/googletest
else
    echo "  ✓ googletest已存在"
fi

# Google Benchmark
if [ ! -d "third_party/benchmark" ]; then
    echo "  - 克隆benchmark..."
    git clone --depth 1 https://github.com/google/benchmark.git third_party/benchmark
else
    echo "  ✓ benchmark已存在"
fi

# spdlog
if [ ! -d "third_party/spdlog" ]; then
    echo "  - 克隆spdlog..."
    git clone --depth 1 https://github.com/gabime/spdlog.git third_party/spdlog
else
    echo "  ✓ spdlog已存在"
fi

# 创建目录结构
echo ""
echo "[3/7] 创建目录结构..."

mkdir -p include/isdm/{core/{type,schema,object,api},runtime/{memory,batch,delta},protocol/{control,data,codec},transport/{udp,tcp,shm},util}
mkdir -p src/{core/{type,schema,object,api},runtime/{memory,batch,delta},protocol/{control,data,codec},transport/{udp,tcp,shm},util}
mkdir -p tests/{core,runtime,protocol,transport,integration}
mkdir -p benchmarks/{throughput,latency,scalability}
mkdir -p examples/{simple_pub_sub,batch_demo,shm_demo}
mkdir -p doc
mkdir -p tools/{schema_gen,cli}

echo "  ✓ 目录结构创建完成"

# 创建占位文件
echo ""
echo "[4/7] 创建占位源文件..."

# 创建基础实现文件（如果不存在）
create_placeholder() {
    local file=$1
    if [ ! -f "$file" ]; then
        cat > "$file" << 'EOF'
// Placeholder implementation
// TODO: Implement this module

namespace isdm {

// Implementation will be added here

} // namespace isdm
EOF
        echo "  + 创建 $file"
    fi
}

# Core模块
create_placeholder "src/core/type/primitive_type.cc"
create_placeholder "src/core/type/type_descriptor.cc"
create_placeholder "src/core/schema/schema_registry.cc"
create_placeholder "src/core/object/object_store.cc"
create_placeholder "src/core/api/node.cc"
create_placeholder "src/core/api/publisher.cc"
create_placeholder "src/core/api/subscriber.cc"

# Runtime模块
create_placeholder "src/runtime/memory/memory_pool.cc"
create_placeholder "src/runtime/memory/buffer_pool.cc"
create_placeholder "src/runtime/batch/batch_aggregator.cc"
create_placeholder "src/runtime/delta/delta_engine.cc"

# Protocol模块
create_placeholder "src/protocol/control/discovery.cc"
create_placeholder "src/protocol/data/frame_builder.cc"
create_placeholder "src/protocol/data/frame_parser.cc"
create_placeholder "src/protocol/codec/encoder.cc"
create_placeholder "src/protocol/codec/decoder.cc"

# Transport模块
create_placeholder "src/transport/udp/udp_transport.cc"
create_placeholder "src/transport/shm/shm_transport.cc"

# Util模块
create_placeholder "src/util/logger.cc"

echo "  ✓ 占位文件创建完成"

# 编译项目
echo ""
echo "[5/7] 编译项目 (Debug模式)..."

mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

echo "  ✓ 编译完成"

# 运行测试
echo ""
echo "[6/7] 运行测试..."

if command -v ctest &> /dev/null; then
    ctest --output-on-failure || echo "  ⚠ 部分测试失败（这是正常的，因为还未实现）"
else
    echo "  ⚠ 跳过测试（ctest不可用）"
fi

# 生成编译命令数据库（用于IDE）
echo ""
echo "[7/7] 生成compile_commands.json..."

cd ..
if [ -f "build/compile_commands.json" ]; then
    ln -sf build/compile_commands.json compile_commands.json
    echo "  ✓ compile_commands.json已创建（用于IDE代码补全）"
fi

# 完成
echo ""
echo "======================================"
echo "  ✅ 初始化完成！"
echo "======================================"
echo ""
echo "下一步："
echo "  1. 阅读文档: doc/架构设计与开发计划.md"
echo "  2. 开始开发: 从 include/isdm/core/type/type_descriptor.h 开始"
echo "  3. 编译: cd build && make -j$(nproc)"
echo "  4. 测试: cd build && ctest"
echo ""
echo "推荐IDE: VSCode + clangd"
echo "  - 打开项目根目录"
echo "  - 安装clangd扩展"
echo "  - compile_commands.json已自动配置"
echo ""
