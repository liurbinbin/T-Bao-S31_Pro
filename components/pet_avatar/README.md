# 芽伴只读头像组件

本组件的 Model/Locale/Renderer 源文件来自 T-QT-C6-main 的 `examples/Lilygo_Pet_Ring`，由该仓库的 `tools/sync_companion.py` 同步。小车通过 `main/pet_companion_avatar.cpp` 调用 `PixelRenderer::renderAvatar()`，只渲染阶段、形态、性格和互动表情，不运行第二份养成、不读写宠物存档、不直接驱动 LCD。

像素缓冲由游戏子界面在 PSRAM 中分配，仅 128×128 RGB565（32768 bytes），经 LVGL image 显示，退出先解除引用和缓存后释放。与 emote、视频、LCD 的所有权交接仍由已有 page manager 管理。

不要单独编辑这些副本。字体位图及授权继续遵循 `fonts/` 中的 SIL OFL 等原文；本目录不改变字体授权。请从指环仓库的工具重新生成字库并同步，组件本身不包含字体生成工具。
