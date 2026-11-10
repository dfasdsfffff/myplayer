# MyPlayer Review

独立 Qt Widgets 审片标注工具：本地视频播放、按毫秒记录问题/建议/重点/其他标注，保存为 `.review.json` 工程，导出 CSV。设计及验收边界见 [设计文档](../../docs/design/review.md)。工程只记录媒体路径与标注，不复制或修改原视频。

## 构建

在仓库根目录配置现有 MSVC x64、Qt 6.8+、vcpkg 依赖，启用 `MYPLAYER_BUILD_REVIEW`。可沿用已配置的 `build`：

```powershell
cmake -S . -B build -DMYPLAYER_BUILD_REVIEW=ON -DBUILD_TESTING=ON
cmake --build build --config Release --target myplayer_review review_document_tests
ctest --test-dir build -C Release -R "^review_document_tests$" --output-on-failure
& ./bin/myplayer_review.exe
```

首次配置需按根目录 [README](../../README.md) 设置 Qt 路径和 vcpkg toolchain；不能在现有构建目录切换工具链/架构/CRT。`BUILD_TESTING=OFF` 时没有 `review_document_tests` 目标，应仅构建 `myplayer_review`。也可启动时指定一个本地视频或 `.review.json` 工程路径。Debug 可执行文件名为 `myplayer_review_debug.exe`。

## 使用

- 点击右上角“打开视频…”或按 Ctrl+O 选择本地视频。窗口初始尺寸按可用屏幕空间调整；播放/暂停、拖动进度定位；只有媒体确认含视频轨且可定位时才能添加标注。上方显示文件名，悬停可查看完整路径。
- Ctrl+M 在当前播放时间添加标注；表格双击标注会暂停并跳至对应时间；可以编辑或删除。
- Ctrl+S 保存工程；另存为会按新工程目录重新计算视频相对路径。打开工程时若原视频不在，标注仍可查看和保存，播放不可用。
- 导出 CSV 用于表格软件，不是可重新导入的工程格式；时间精度为毫秒，跳转由解封装定位，不保证逐帧精确。
- 替换视频或工程、退出前，如有未保存修改会询问保存、放弃或取消。

## 边界

新应用只通过 `PlayerSDK::QtWidgets` 消费 SDK。录像、剪辑、转码和第三方媒体云服务不在范围内。构建后会尝试复制可识别的运行时 DLL 并调用 `windeployqt`；整仓现有双应用便携包脚本**不包含**本程序。独立发布还须核对运行时依赖及 GPLv3、Qt、FFmpeg 等许可材料。

## 本次验证

在 Windows x64/MSVC Release 构建 `myplayer_review` 和 `review_document_tests`；标注文档单项 CTest 通过。使用 FFmpeg 生成 4 秒本地视频，临时 Qt 驱动在 `offscreen` 和 Windows 原生窗口分别验证真实帧呈现、暂停和定位，并完成标注添加/编辑/删除、工程保存/重载、CSV 导出、缺失媒体载入、损坏工程拒绝、未保存操作的取消及保存。`offscreen` 截图显示了画面与界面；Windows 原生路径的 QWidget 截图不包含 SDL 原生子窗口画面，帧另由 `VideoWidget::grabVideoFrame()` 检查。尚未对完整媒体格式矩阵、长时间播放或独立便携发行包验收。

后续在 Windows 桌面 200% 缩放下启动实际程序并截图核对空窗口与 12 秒本地视频的画面、标注区、进度和控制条；点击“打开视频…”验证文件选择器出现。最初使用未启用 DPI 感知的截图脚本导致截取不完整，并非软件窗口裁切；改为 DPI 感知截图后确认布局正常。现在空视频区域展示操作提示、无标注时显示说明，已保留真实播放画面可见。该桌面检查不等于覆盖其他缩放或多显示器组合。
