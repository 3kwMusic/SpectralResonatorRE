# 发布说明

1. 同步版本与 changelog，保留版权、许可证、第三方声明及完整依赖。
2. 完成 Release 构建、真实 processBlock 回归和 pluginval 严格级别 7；核对插件路径、版本和 SHA-256。
3. 检查待上传文件及全部可达 Git 历史；排除凭据、私人联系方式、内部研究、原厂程序和第三方音频素材。
4. 使用 GitHub 隐私邮箱提交源码，为测试对应的提交创建 `v<version>` 标签。
5. 使用 Python 3.9+ 生成发行包：

```powershell
python scripts/package_release.py --juce-dir third_party/JUCE --include-binaries
```

如使用自定义构建目录，传入 `--build-dir <build-directory>`。输出默认位于 `build/releases/<version>/`，含 Windows 包、完整对应源码包和 SHA256SUMS.txt。打包脚本要求已提交且存在版本标签，并核对实际 JUCE 构建目录和 VST3 模块版本。

源码包已含 JUCE 8.0.4，解压后可离线构建（仍需自备 CMake 和 Visual Studio C++ 工具链）：

```powershell
cmake -S . -B build
cmake --build build --config Release --target SpectralResonatorRE_VST3 SpectralResonatorRE_Standalone
```

在对应标签上创建 GitHub Release，同时上传二进制包、完整对应源码包和校验文件。GitHub 自动生成的源码归档不含外部 JUCE，不能单独替代完整对应源码发行包。先核验附件可下载，再更改仓库可见性。

源码与二进制不得混用其他版本的测试结论。标准 AGPL 允许商业使用和收费分发；普通音频作品不因使用本效果器而受到软件许可约束。
