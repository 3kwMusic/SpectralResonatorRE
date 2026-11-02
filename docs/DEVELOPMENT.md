# 开发说明

## 架构

- `Source/Parameters.*`：28 个参数的 ID、范围、默认值、单位和 APVTS 布局。
- `Source/PluginProcessor.*`：立体声总线、状态保存、采样偏移自动化与 MIDI 调度。
- `Source/SpectralEngine.*`：1024 样本 FFT、谐振包络、频谱合成与声部管理，向宿主报告 1024 样本延迟。
- `Source/PluginEditor.*`：控件、Spectrogram / Spectrum 页签；声谱图采集实际干湿分量，Partials 叠加为参数示意。
- `cmake/SpectralVST3Automation.*`：生成项目内 JUCE VST3 包装器副本，保留参数队列 point/offset；上游 JUCE 文件保持不变。

连续曲线在归一化域插值后映射，离散参数按采样偏移更新。APVTS 原始值、宿主归一化值和显示单位分开；修改时保留插件 ID、参数 ID 及状态兼容性。音频线程不得分配内存。

## 回归

先按 README 构建插件，再执行：

```powershell
cmake -S tests/audio-probe -B build/tests/audio-probe
cmake --build build/tests/audio-probe --config Release
ctest --test-dir build/tests/audio-probe -C Release --output-on-failure
```

自定义插件构建路径时，配置测试传入 `-DSPECTRAL_TEST_VST3=<bundle-path>`。测试调用真实 `processBlock()`，覆盖干湿、动态、谐振、衰减、阻尼、调制、MIDI、复音、采样率、重置、音阶、状态、界面、默认值和实际 VST3 延迟。

```powershell
pluginval --validate "build/SpectralResonatorRE_artefacts/Release/VST3/Spectral Resonator RE.vst3" --strictness-level 7 --timeout-ms 120000 --output-dir build/validation --output-filename pluginval-level7.txt
```

测试前核对宿主实际加载路径与版本；构建产物与已安装插件是两份文件。系统插件目录可能要求管理员权限，安装由用户选择。
