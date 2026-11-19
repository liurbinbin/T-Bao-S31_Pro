# 中文像素字库来源

本目录对应 Pet Ring 的 10×10 中文位图子集，字库仅收录 `locales/zh_CN.json` 实际使用的非 ASCII 字符。原字体文件不参与固件构建，也不需要联网下载才能使用已经生成的字库。

- 上游：Fusion Pixel Font，https://github.com/TakWolf/fusion-pixel-font
- 上游作者：TakWolf，https://takwolf.com
- 固定版本：2026.11.10
- 字体：`fusion-pixel-10px-monospaced-zh_hans.ttf`
- 发布包：`fusion-pixel-font-10px-monospaced-ttf-v2026.11.10.zip`
- 发布包 SHA256：`b32ec56494a9c9e776d61798e307657e17b250b222830629bb0f43039d715777`
- 主许可证：SIL Open Font License 1.1，见 `OFL.txt`。
- 上游整合字体的授权原文同时保存在本目录，包含 ark-pixel、boutique-bitmap-9x9、galmuri。

生成的字体位图位于 `../LocaleData.inc` 的 `kChineseGlyphs`，继续遵循字体授权，不因项目代码采用其他许可证而改变。这里是本项目的精简位图衍生资源，不应当作上游完整字体发行版或上游对本项目的背书。

## 重新生成

生成工具只需要 Python 和 Pillow，不需要 C++ 编译器。先把发布 ZIP 放进单独的新目录，不要从下载目录运行脚本或执行压缩包中的任何内容。

从仓库根目录执行：

```bash
python -I examples/Lilygo_Pet_Ring/tools/generate_locale.py --font-archive <字体ZIP路径>
```

脚本校验 ZIP 的固定 SHA256，只读取指定字体和授权文本，不解压执行任何脚本。它生成：

- `LocaleData.inc`：中英文词表与字体位图。
- `fonts/subset.json`：条目数、字形数及位图数据大小。
- `fonts/atlas.png`：按实际点阵排列的字形样张。
- 本目录中的授权原文。

新增对白时须先更新 `locales/zh_CN.json`，然后重新生成。仅在 JSON 中加入中文而不生成字库，会显示缺字占位框。
