# HarmonyOS 4 for AE 0.0.0.3

将 HarmonyOS 4 适配到 AEmulator 0.0.0.3，已完成全新导入验收。仓库包含适配过程中编写的部分源码和构建脚本；ROM 包通过 Release 提供。

[下载包](../../releases/latest) · [修改日志](CHANGELOG.md) · [来源说明](NOTICE.md)

## 下载使用

1. 在 [Release](../../releases/latest) 下载两个分卷、`parts.json` 和 `join_release.py`，放入同一个文件夹。
2. 运行 `python join_release.py`，合并成 `AE-HarmonyOS4.zip`，然后导入 AEmulator。
3. 在 AE 实例设置中调整为 **720×1440 / 320 DPI**。首次启动需等待系统完成初始化。

## 这次改的地方

- 默认中文、中国地区虚拟 SIM，并保留 AE 虚拟来电和短信支持。
- 使用星夜彩绘主题，以及两页桌面和 4×2 时钟布局。
- 预装讯飞输入法和 Via 浏览器，移除原百度输入法。
- 调整联系人、安装器、来电显示、铃声及音频相关兼容逻辑。

讯飞输入法首次使用条款按包内配置自动同意；完整功能可能向讯飞云端传输输入内容。

## 仓库里的文件

- `diagnostics/`：适配过程中编写的初始化、兼容层和虚拟 Radio 桥源码。
- `tools/`：镜像修改和打包相关脚本。

仓库不包含完整 ROM 或第三方应用的源代码，不能仅凭这些文件完整重建发布包。部分源码包含默认关闭的后续实验分支，可能与包内预编译文件不完全一致。

完整包 SHA-256：`f3ab34a5fd28fd1976081c618b7c3ff064ab98366772ccde56b62eddc4d79fa2`
