# AE-HarmonyOS4

AEmulator 0.0.0.3 的 HarmonyOS 4 ROM。此包未完成完整的全新导入验收：虚拟信号格仍不可用，短信铃声首次初始化可能失败，内核版本页面可能显示不可用。AE 实例分辨率需手动设置为 720×1440、320 DPI。

预装讯飞输入法采用自动同意首次使用条款的配置，完整功能可能向讯飞云端传输输入内容；Via 浏览器也已预装。

## 下载和合并

下载两个 `AE-HarmonyOS4.zip.part` 分卷、`parts.json` 和 `join_release.py` 到同一目录，运行 `python join_release.py`。脚本会检查分卷和完整包的 SHA-256，然后生成 `AE-HarmonyOS4.zip`，供 AE 导入。
