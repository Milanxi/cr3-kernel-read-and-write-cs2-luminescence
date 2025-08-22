# GlowESP 项目

GlowESP 是一个基于 Windows 的游戏辅助项目，专注于提供 ESP（Extra Sensory Perception，额外感知能力）功能。该项目采用驱动级设计，确保稳定性和性能。

## 主要特性
- 基于内核驱动的内存操作
- 高效的数据处理机制
- 低资源占用
- 稳定可靠的性能表现

## 技术栈
- C++ 应用层开发
- Windows 内核驱动开发
- Windows API 系统调用

## 项目结构

### GlowESP
主要的应用程序组件，包含以下关键文件：
- `source.cpp` - 主要源代码文件
- `arrange.hpp` - 布局相关的头文件
- `memory.hpp` - 内存操作相关的头文件

### SmileDriver
驱动程序组件，包含以下文件：
- `SmileDriver.c` - 驱动程序主要源代码
- `SmileDriver.inf` - 驱动程序信息文件
- `trace.h` - 跟踪功能的头文件

## 开发环境要求
- Visual Studio 2022
- Windows SDK
- Windows Driver Kit (WDK)

## 构建说明
1. 使用 Visual Studio 2022 打开解决方案文件 `GlowESP.sln`
2. 选择适当的构建配置（Debug/Release）
3. 构建解决方案

## 注意事项
- 本项目包含驱动程序组件，需要相应的系统权限才能运行
- 确保已安装所有必要的开发工具和SDK

## 许可证
本项目采用 MIT 许可证。查看 [LICENSE](LICENSE) 文件了解更多详情。

## 贡献
欢迎提交问题和拉取请求。

## 免责声明
请在遵守相关法律法规的前提下使用本项目。
