# MyPlayer Review

独立 Qt Widgets 审片标注工具：采用 `monitor-live.svg` 的深色青绿色视觉体系，支持本地视频播放、按毫秒记录问题/建议/重点/其他标注、撤销/重做、搜索筛选、崩溃恢复、媒体重新关联、保存 `.review.json` 工程及导出 CSV。设计及验收边界见 [设计文档](../../docs/design/review.md)。工程只记录媒体路径与标注，不复制或修改原视频。

## 构建

在仓库根目录配置现有 MSVC x64、Qt 6.8+、vcpkg 依赖，启用 `MYPLAYER_BUILD_REVIEW`。可沿用已配置的 `build`：

```powershell
cmake -S . -B build -DMYPLAYER_BUILD_REVIEW=ON -DBUILD_TESTING=ON
cmake --build build --config Release --target myplayer_review review_document_tests review_window_tests
ctest --test-dir build -C Release -R "^review_(document|window)_tests$" --output-on-failure
& ./bin/myplayer_review.exe
```

首次配置需按根目录 [README](../../README.md) 设置 Qt 路径和 vcpkg toolchain；不能在现有构建目录切换工具链/架构/CRT。`BUILD_TESTING=OFF` 时没有 `review_document_tests` 目标，应仅构建 `myplayer_review`。也可启动时指定一个本地视频或 `.review.json` 工程路径。Debug 可执行文件名为 `myplayer_review_debug.exe`。

## 使用

- 点击右上角“打开视频”或按 Ctrl+O，通过系统原生文件选择窗口选择本地视频。播放区支持播放/暂停、左右方向键前后定位 5 秒、进度拖动、音量和 0.1×–2.0× 倍速；只有媒体确认含视频轨且可定位时才能添加标注。
- 进度条以类别颜色显示全部标注位置，右侧标题显示当前筛选结果数；标注表中的类别颜色与时间线一致。Ctrl+M 在当前播放时间添加标注；标注时间可按 `小时:分钟:秒.毫秒` 修改。右侧支持文字搜索、类别筛选、上一条/下一条导航；双击标注会暂停并跳至对应时间。
- 标注的添加、编辑、删除及媒体重新关联支持 Ctrl+Z/Ctrl+Y 撤销和重做。媒体缺失时点击“重新关联”选择新视频，已有标注保持不变。
- Ctrl+S 保存工程；另存为会按新工程目录重新计算视频相对路径。未保存内容在修改停止 2 秒后原子写入应用本地数据目录；无命令行文件启动时会提示恢复上次异常退出前的内容。正常保存、明确放弃或正常退出会删除恢复文件。
- 导出 CSV 用于表格软件，不是可重新导入的工程格式；时间精度为毫秒，跳转由解封装定位，不保证逐帧精确。
- 新增审片导航工具栏：Home 转到开头；End 暂停并定位到结尾附近；Ctrl+G 输入 `小时:分钟:秒.毫秒`，暂停并跳转。非法或超出时长的时间不提交给播放器。所有跳转仍为解封装级定位，不保证显示目标帧。
- “上一帧／下一帧”调用 SDK 的实际解码帧步进，快捷键为逗号 `,` / 句号 `.`；播放中使用会暂停，完成后停在目标帧，首尾边界不循环。请求尚未完成时禁用两个步进按钮；步进需要从媒体起点重新解码，长片后段可能有等待。只支持可定位、非直播视频，不将固定时间跳转冒充逐帧。自然播放结束后先重新播放才能继续审阅。
- Shift+J / Shift+L 后退/前进 1 秒；Ctrl+J / Ctrl+L 后退/前进 10 秒；倍速支持 0.1×、0.25×、0.5×、0.75×、1×、1.25×、1.5×、2×。保留左右方向键 5 秒跳转。
- “循环播放”在可定位视频播放结束后重新打开媒体，保留标注及音量/倍速；取消勾选恢复单次播放。它不是无缝循环，不支持区间循环或往返播放，开关只在当前窗口内保留。
- F11 切换全屏、Esc 退出全屏；Ctrl+Shift+H 隐藏/恢复标注面板。“查看 → 媒体信息”显示分辨率、像素宽高比、旋转和音视频/字幕轨道信息。
- Ctrl+Shift+E 导出当前画面为 PNG：在打开保存对话框前截取视口（含缩放、黑边及字幕），原子写入，不覆盖当前媒体或工程。这不是源分辨率、HDR 或色彩管理图像导出；文件名中的毫秒数是播放位置，不是保证精确的画面 PTS。
- 替换视频或工程、退出前，如有未保存修改会询问保存、放弃或取消。

## 边界

新应用只通过 `PlayerSDK::QtWidgets` 消费 SDK。录像、剪辑、转码和第三方媒体云服务不在范围内。构建后会尝试复制可识别的运行时 DLL 并调用 `windeployqt`；整仓现有双应用便携包脚本**不包含**本程序。独立发布还须核对运行时依赖及 GPLv3、Qt、FFmpeg 等许可材料。

## DJV 对齐范围

参考 [DJV](https://github.com/grizzlypeak3d/DJV)、[播放导航](https://grizzlypeak3d.github.io/DJV/playback.html) 和 [视图](https://grizzlypeak3d.github.io/DJV/view.html)。本次是基础审片工作流对齐，不是 DJV 功能等价实现，也未复制其代码或品牌资源。

| 能力 | 当前状态 |
| --- | --- |
| 时间跳转、首尾导航、慢速、单次/循环 | 已实现；毫秒级请求、非帧精确、非无缝循环 |
| 全屏、标注面板切换、媒体信息、PNG 视口导出 | 已实现 |
| 逐帧前后步进 | 已接入 SDK；成功后保持暂停，使用实际帧 PTS，不是平均帧率时间跳转 |
| 帧计数、倒放、I/O 区间、往返循环 | 未实现 |
| EXR/DPX 图像序列、高位深、OCIO/LUT/HDR | 未实现；需要新的媒体与色彩渲染能力 |
| A/B wipe/overlay/difference、OTIO、多轨时间线 | 未实现 |
| 1:1 缩放、平移、通道隔离、镜像 | 未实现；现有 SDK 仅自动适配画面 |

`review_window_tests` 生成短视频并运行 Qt 无界面窗口回归，覆盖空状态禁用、面板切换、慢速预设、EOF 循环重新打开、逐帧前后步进及完成前禁用、时间定位、越界输入拒绝和 PNG 导出；它不代替原生 SDL 窗口、完整格式矩阵或长时间稳定性验收。Windows 测试会部署 offscreen 插件；使用预编译依赖时，必须将匹配的 FFmpeg/libass 及其传递依赖 DLL 目录加入 PATH，避免误加载系统中其他版本。

## 既有验证记录

在 Windows x64/MSVC Release 的独立 `build-review` 目录构建 `myplayer_review` 和 `review_document_tests`；标注文档单项 CTest 通过，覆盖工程/CSV 边界和恢复文件原子往返及无效输入不修改输出。

在 Windows 原生窗口、DPI 感知截图路径中载入含两条标注且媒体缺失的工程，确认深色双栏界面、标注表、搜索/类别控件、重新关联入口、播放控制和缺失媒体提示完整可见。媒体播放格式矩阵、长时间播放和独立便携发行包仍不在本次验收范围内。
