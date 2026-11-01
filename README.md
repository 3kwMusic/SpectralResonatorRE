# Spectral Resonator RE

基于 JUCE 8.0.4 / C++17 的立体声频谱谐振效果器，支持 Windows x64、VST3 和 Standalone。当前版本 **1.5.0**。

提供谐波谐振、频率拉伸与偏移、衰减和高低频阻尼、Chorus / Wander / Granular 调制，以及 MIDI 音高、复音和 Unison。参数自动化保留宿主传入的采样偏移与连续曲线，设置随工程保存。

本仓库用于源码管理，当前未发布插件二进制或 GitHub Release。

## 构建

需要 Windows、Visual Studio C++ 工具链、CMake 3.22+ 和 JUCE 8.0.4。

```powershell
git clone --branch 8.0.4 --depth 1 https://github.com/juce-framework/JUCE.git third_party/JUCE
cmake -S . -B build
cmake --build build --config Release --target SpectralResonatorRE_VST3 SpectralResonatorRE_Standalone
```

已有 JUCE 时可设置 `JUCE_DIR` 环境变量或传入 `-DJUCE_DIR=<source-directory>`。JUCE 不随仓库提交。VST3 适配要求所记录版本的上游包装器摘要匹配。

产物位于 `build/SpectralResonatorRE_artefacts/Release/` 的 `VST3/` 与 `Standalone/` 下。构建不会自动安装插件。

## 默认设置与验证

新建空白实例默认 Internal / Mono、110 Hz、256 Harmonics、Decay 222 ms、HF/LF Damp 0%、Mod None、Unison 1、Dry/Wet 100%。Scale/Root 默认 Chromatic/C，Use Scale 为 Off。已有工程或保存状态会覆盖初始值。

开发、回归与兼容性边界见 [开发说明](docs/DEVELOPMENT.md) 和 [验证范围](docs/VALIDATION.md)。高声部、高谐波数、密集自动化叠加多个实例可能超过实时预算，尤其在高采样率或小缓冲下；不作跨机器硬实时保证。不声明与其他产品逐样本或听感完全一致。

## 许可

Copyright (C) 2026 TriM。作者有权授权的项目代码采用 **AGPL-3.0-only**，见 [LICENSE](LICENSE)。第三方组件保留各自版权与许可，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 [licenses/](licenses/)。本软件按许可证规定的“现状”提供，不提供担保。

可以用于个人或商业音乐制作；普通音乐作品不因使用本插件而适用软件许可证。分发软件或修改版本须遵守相应软件许可。未来发布二进制时，应一并提供完整对应源码及依赖，见 [发布说明](docs/RELEASING.md)。本项目不表示与其他厂商有关联或获得其背书。
