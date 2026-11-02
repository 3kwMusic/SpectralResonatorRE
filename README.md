# Spectral Resonator RE

基于 JUCE 8.0.4 / C++17 的立体声频谱谐振效果器，提供 Windows x64 VST3 和 Standalone。当前版本 **1.5.4**。

支持谐波谐振、频率拉伸与偏移、衰减和高低频阻尼、Chorus / Wander / Granular 调制，以及 MIDI 音高、复音和 Unison。包含采样偏移参数自动化、工程状态保存和黄干/蓝湿声谱图；界面尺寸为 880 × 500。

[下载 Windows 插件及完整对应源码](https://github.com/3kwMusic/SpectralResonatorRE/releases/latest)

## 安装

1. 下载 Release 中的 `SpectralResonatorRE-1.5.4-windows.zip` 并解压。
2. 将整个 `Spectral Resonator RE.vst3` 文件夹复制到 `C:\Program Files\Common Files\VST3`，按 Windows 提示授予管理员权限。
3. 在宿主中重新扫描 VST3，重新加载插件。Standalone 可直接运行 `Spectral Resonator RE.exe`。

请保留 bundle 的内部目录结构。更新时先关闭使用该插件的实例并备份旧版本。此版本面向 Windows x64，未提供 macOS 或 Linux 二进制。

## 构建

需要 Windows、Visual Studio C++ 工具链、CMake 3.22+ 和 JUCE 8.0.4。

```powershell
git clone --branch 8.0.4 --depth 1 https://github.com/juce-framework/JUCE.git third_party/JUCE
cmake -S . -B build
cmake --build build --config Release --target SpectralResonatorRE_VST3 SpectralResonatorRE_Standalone
```

完整对应源码发行包包含 `third_party/JUCE`，可跳过下载。已有 JUCE 时可设置 `JUCE_DIR` 环境变量或传入 `-DJUCE_DIR=<source-directory>`。VST3 包装器适配有上游 SHA-256 校验。产物位于 `build/SpectralResonatorRE_artefacts/Release/`，构建不会自动安装插件。

## 默认设置与验证

默认 Internal / Mono、110 Hz、256 Harmonics、Decay 222 ms、HF/LF Damp 0%、Mod None、Unison 1、Dry/Wet 100%。Scale/Root 默认 Chromatic/C，Use Scale 为 Off。已有工程或保存状态可覆盖初始值。

1.5.4 修复静默后重新播放的额外相位和立体声宽度变化，并保护短击和 Granular 延后尾音。已完成 Release 构建、40 个回归入口、真实 `processBlock()` 冲激/行为验证及 pluginval 严格级别 7。用户已完成本轮手动核对；没有原版同输入自动音频 A/B，不声明逐样本或听感完全一致。

高声部、高谐波数与密集自动化叠加多个实例可能超过实时预算，尤其在高采样率或小缓冲下。架构、测试命令与覆盖范围见 [开发说明](docs/DEVELOPMENT.md) 和 [验证范围](docs/VALIDATION.md)。

## 许可与发布

Copyright (C) 2026 TriM。作者有权授权的项目代码采用 **AGPL-3.0-only**，见 [LICENSE](LICENSE)。第三方组件保留各自版权与许可，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 [licenses/](licenses/)。

可以用于个人或商业音乐制作；普通音乐作品不因使用本插件而适用软件许可证。分发软件或修改版须遵守相应软件许可。本软件按许可证规定的“现状”提供，不提供担保。本项目与 Ableton 无关联或背书；相关产品名称和商标归其权利人所有。

变更见 [CHANGELOG.md](CHANGELOG.md)。二进制与含完整 JUCE 依赖的对应源码包同时提供，GitHub 自动生成的源码包不含 JUCE。发布步骤见 [发布说明](docs/RELEASING.md)。
