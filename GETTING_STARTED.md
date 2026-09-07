# 🚀 ISDM 快速上手指南

## 第一次使用？从这里开始！

### 1️⃣ 运行初始化脚本 (5分钟)

```bash
cd /home/wwk/workspace/ISDM
chmod +x setup.sh
./setup.sh
```

这会自动完成：
- ✅ 检查依赖
- ✅ 下载第三方库
- ✅ 创建目录结构
- ✅ 编译项目
- ✅ 运行测试

### 2️⃣ 阅读关键文档 (30分钟)

**必读：**
```bash
# 1. 项目总览
cat PROJECT_SUMMARY.md | less

# 2. 架构设计
cat doc/架构设计与开发计划.md | less

# 3. 开发路线
cat ROADMAP.md | less
```

**选读：**
```bash
# 原始设计文档（160章，很长但值得）
cat doc/高密度工业自描述通信中间件��计文档.md | less
```

### 3️⃣ 开始第一个实现 (今天)

#### 任务：实现 TypeDescriptor

**1. 创建头文件：**
```bash
vim include/isdm/core/type/type_descriptor.h
```

参考结构：
```cpp
class TypeDescriptor {
 public:
  void AddField(const std::string& name, PrimitiveType type);
  const FieldDescriptor* GetField(const std::string& name) const;
  size_t size() const;
  
 private:
  uint32_t type_id_;
  std::string name_;
  std::vector<FieldDescriptor> fields_;
};
```

**2. 实现源文件：**
```bash
vim src/core/type/type_descriptor.cc
```

**3. 编写测试：**
```bash
vim tests/core/type_descriptor_test.cc
```

参考现有的 `tests/core/primitive_type_test.cc`

**4. 编译运行：**
```bash
cd build
make -j8
ctest --output-on-failure
```

---

## 📋 本周任务清单

**Phase 1 - Week 1: Type System**

- [ ] 实现 `TypeDescriptor` 类
  - [ ] 头文件定义
  - [ ] 源文件实现
  - [ ] 单元测试
  - [ ] 测试通过

- [ ] 实现 `FieldDescriptor` 结构
  - [ ] 头文件定义
  - [ ] 测试用例

- [ ] 实现编译期反射宏
  - [ ] `ISDM_STRUCT` 宏
  - [ ] `ISDM_FIELD` 宏
  - [ ] 示例测试

**预计时间：** 3-4天

**验收标准：**
```cpp
TypeDescriptor desc;
desc.AddField("speed", PrimitiveType::kFloat32);
desc.AddField("current", PrimitiveType::kFloat32);

EXPECT_EQ(desc.size(), 8);
EXPECT_EQ(desc.GetField("speed")->offset, 0);
EXPECT_EQ(desc.GetField("current")->offset, 4);
```

---

## 🔍 代码在哪里？

### 关键文件位置

**已实现：**
```
include/isdm/core/type/primitive_type.h  ← 基础类型定义（已完成）
include/isdm/status.h                    ← 错误处理（已完成）
tests/core/primitive_type_test.cc        ← 示例测试（已完成）
```

**待实现（本周）：**
```
include/isdm/core/type/type_descriptor.h      ← 你要实现的
src/core/type/type_descriptor.cc              ← 你要实现的
include/isdm/core/type/field_descriptor.h     ← 你要实现的
tests/core/type_descriptor_test.cc            ← 你要实现的
```

**参考文档：**
```
doc/架构设计与开发计划.md    ← 第2.2.1节有详细接口设计
ROADMAP.md                    ← Phase 1任务清单
PROJECT_SUMMARY.md            ← 总体概览
```

---

## 🛠️ 开发工具

### 推荐IDE: VSCode

```bash
# 安装扩展
code --install-extension llvm-vs-code-extensions.vscode-clangd
code --install-extension ms-vscode.cmake-tools

# 打开项目
code /home/wwk/workspace/ISDM
```

**自动补全：**  
项目已自动生成 `compile_commands.json`，clangd会自动识别。

### 编译和测试命令

```bash
# 进入build目录
cd build

# 清理重新编译
make clean && make -j8

# 运行所有测试
ctest --output-on-failure

# 运行单个测试
./bin/primitive_type_test

# 调试模式编译
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j8

# Release模式编译
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
```

### 调试工具

```bash
# 内存检测
valgrind --leak-check=full ./bin/primitive_type_test

# 地址检测（需要Debug编译）
./bin/primitive_type_test

# 线程检测
clang++ -fsanitize=thread -g test.cc
```

---

## 📞 遇到问题？

### 常见问题

**Q: setup.sh报错找不到依赖？**  
A: 确保安装了 CMake 3.15+ 和 GCC 9+ / Clang 10+

**Q: 编译失败？**  
A: 检查 `build/CMakeFiles/CMakeError.log`

**Q: 测试失败？**  
A: 这是正常的，因为大部分功能还未实现

**Q: 不知道从哪里开始？**  
A: 从实现 TypeDescriptor 开始，参考 `doc/架构设计与开发计划.md` 第2.2.1节

### 获取帮助

1. 查看文档：`doc/架构设计与开发计划.md`
2. 查看示例：`tests/core/primitive_type_test.cc`
3. 查看路线图：`ROADMAP.md`

---

## 🎯 成功的标志

### 今天结束时
- ✅ 项目已初始化
- ✅ 能够成功编译
- ✅ 至少一个测试通过

### 本周结束时
- ✅ TypeDescriptor 实现完成
- ✅ 测试覆盖核心功能
- ✅ 能够创建简单的类型描述

### 下周目标
- ✅ SchemaRegistry 实现
- ✅ 类型可以注册和查询

---

## 💪 你能行！

记住项目的核心原则：

> **不追求一次性完美，而是通过迭代逐步优化。**

现在开始写第一行代码吧！ 🚀

```bash
cd /home/wwk/workspace/ISDM
./setup.sh
vim include/isdm/core/type/type_descriptor.h
```

Good luck! 加油！
