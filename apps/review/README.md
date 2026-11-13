# MyPlayer Review

独立 Qt Widgets 审片标注工具：采用 `monitor-live.svg` 的深色青绿色视觉体系，支持本地视频播放、按毫秒记录问题/建议/重点/其他标注、撤销/重做、搜索筛选、崩溃恢复、媒体重新关联、保存 `.review.json` 工程及导出 CSV。设计及验收边界见 [设计文档](../../docs/design/review.md)。工程只记录媒体路径与标注，不复制或修改原视频。

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

- 点击右上角“打开视频”或按 Ctrl+O，通过系统原生文件选择窗口选择本地视频。播放区支持播放/暂停、左右方向键前后定位 5 秒、进度拖动、音量和 0.5×–2.0× 倍速；只有媒体确认含视频轨且可定位时才能添加标注。
- 进度条以类别颜色显示全部标注位置，右侧标题显示当前筛选结果数；标注表中的类别颜色与时间线一致。Ctrl+M 在当前播放时间添加标注；标注时间可按 `小时:分钟:秒.毫秒` 修改。右侧支持文字搜索、类别筛选、上一条/下一条导航；双击标注会暂停并跳至对应时间。
- 标注的添加、编辑、删除及媒体重新关联支持 Ctrl+Z/Ctrl+Y 撤销和重做。媒体缺失时点击“重新关联”选择新视频，已有标注保持不变。
- Ctrl+S 保存工程；另存为会按新工程目录重新计算视频相对路径。未保存内容在修改停止 2 秒后原子写入应用本地数据目录；无命令行文件启动时会提示恢复上次异常退出前的内容。正常保存、明确放弃或正常退出会删除恢复文件。
- 导出 CSV 用于表格软件，不是可重新导入的工程格式；时间精度为毫秒，跳转由解封装定位，不保证逐帧精确。
- 替换视频或工程、退出前，如有未保存修改会询问保存、放弃或取消。

## 边界

新应用只通过 `PlayerSDK::QtWidgets` 消费 SDK。录像、剪辑、转码和第三方媒体云服务不在范围内。构建后会尝试复制可识别的运行时 DLL 并调用 `windeployqt`；整仓现有双应用便携包脚本**不包含**本程序。独立发布还须核对运行时依赖及 GPLv3、Qt、FFmpeg 等许可材料。

## 本次验证

在 Windows x64/MSVC Release 的独立 `build-review` 目录构建 `myplayer_review` 和 `review_document_tests`；标注文档单项 CTest 通过，覆盖工程/CSV 边界和恢复文件原子往返及无效输入不修改输出。

在 Windows 原生窗口、DPI 感知截图路径中载入含两条标注且媒体缺失的工程，确认深色双栏界面、标注表、搜索/类别控件、重新关联入口、播放控制和缺失媒体提示完整可见。媒体播放格式矩阵、长时间播放和独立便携发行包仍不在本次验收范围内。
