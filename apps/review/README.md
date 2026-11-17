# MyPlayer Review

独立 Qt Widgets 视频审片工具。保留本地播放、真实单帧步进、文字标注、撤销/重做、搜索筛选、媒体重新关联、JSON 工程、CSV 和异常恢复，在此基础上分阶段向 DJV 对齐。

参考用户提供的 DJV 源码提交 `bbd2ff33075c5f76947516702a806c74572e31c7`。本轮实现专业审片基础工作区，不是 DJV 功能等价实现；没有引入 DJV/tlRender/feather-tk，也没有复制其品牌或源码。详见 [设计](../../docs/design/review.md) 和 [对齐路线](../../docs/design/review-djv-roadmap.md)。

## 构建

要求 Qt 6.8+、匹配的 C++20 编译器及仓库 vcpkg 依赖。推荐独立构建目录，不覆盖 Qt Creator 的 Debug 缓存或混用 MSVC/MinGW 的 DLL。本机已验证的配置：

```powershell
cmake -S . -B build-review-msvc -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=D:/ProgramData/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DPLAYERDEMO_QT_ROOT=D:/Qt/Qt6/6.9.3/msvc2022_64 `
  -DPLAYERDEMO_OUTPUT_DIR="$PWD/build-review-msvc/bin" `
  -DMYPLAYER_BUILD_QT_APP=OFF -DMYPLAYER_BUILD_MONITOR=OFF `
  -DMYPLAYER_BUILD_REVIEW=ON -DBUILD_TESTING=ON -DPLAYER_SDK_BUILD_TESTS=ON
cmake --build build-review-msvc --config Release --target myplayer_review review_document_tests review_workspace_tests review_window_tests player_sdk_tests player_sdk_qt_tests
$env:PATH = "D:/Qt/Qt6/6.9.3/msvc2022_64/bin;$PWD/build-review-msvc/vcpkg_installed/x64-windows/bin;$env:PATH"
ctest --test-dir build-review-msvc -C Release -R "^(review_(document|window|workspace)_tests|player_sdk(_qt)?_tests)$" --output-on-failure
& ./build-review-msvc/bin/myplayer_review.exe
```

其他机器按实际路径、已安装生成器调整。首次配置参照根 [README](../../README.md)。应用也接受一个本地视频或 `.review.json` 路径参数。Debug 产物名为 `myplayer_review_debug.exe`。`BUILD_TESTING=OFF` 不生成 Review 测试目标。

## 工作区和画面

- 顶部统一文件/视图工具栏，中央视频，底部时间线和播放工具，右侧可停靠、浮动和隐藏的标注面板。工具栏空间不足时通过溢出菜单访问剩余动作。
- **画面**菜单：适应窗口、1:1、放大/缩小、水平/垂直镜像。鼠标在画面上滚轮缩放，以鼠标位置为锚点；中键拖动平移；双击适应窗口。
- 画面获得焦点后：`0` 为 1:1，`-` / `=` 缩放，Backspace 适应窗口。1:1 是按 SAR 校正后的源像素与渲染输出物理像素的比例，不是 Qt 逻辑像素；支持媒体旋转。倍率范围为 1%–3200%，平移限制在图像边界内。
- F11 全屏，Esc 退出；Ctrl+P 演示模式隐藏菜单、工具栏、标注和状态区，Esc 恢复原工作区。Ctrl+Shift+H 显隐标注面板；**工作区 → 重置工作区**恢复默认布局和画面。
- 关闭时保存窗口布局、音量、速度、时间线跟随和拖动偏好。新媒体默认 Fit；平移、播放位置、镜像和入出点不会作为全局设置延续到另一媒体。

## 时间线和播放

- 自适应毫秒刻度、播放头、I/O 范围与全部标注标记。滚轮锚点缩放、中键平移；时间线获得焦点时 `-` / `=` 缩放、Backspace 显示全片。可关闭**跟随播放头**自由浏览。
- 左键点击或拖动定位。拖动期间暂停、只预览时间，释放时提交最终定位；默认成功后恢复拖动前的播放意图，可启用**拖动后保持暂停**。取消拖动不自动恢复。
- 标注标记悬停显示类别、时间、文字，点击选择并暂停定位。时间线始终显示全部标注；如果标注被右侧筛选隐藏，保留筛选并提示，不误选其他标注。
- 空格播放/暂停；左右方向键以及 `,` / `.` 执行 SDK 的真实上一帧/下一帧，完成后暂停。**左右键不再是 ±5 秒**，±5 秒仍有按钮和菜单；Shift+J/L 为 ±1 秒、Ctrl+J/L 为 ±10 秒。
- Home 开头，End 暂停并转到结尾附近；Ctrl+G 输入 `小时:分钟:秒.毫秒`。音量及 0.1×–2.0×速度。
- **I / O** 将当前位置设为入/出点，Shift+I/O 重置对应端点；合法范围为 `0 ≤ in < out ≤ duration`。默认全片，状态栏明确显示“会话区间”。
- 播放从区间外开始时先回入点。单次播放到出点暂停；勾选**循环播放**则回入点继续。手动定位和标注导航可查看区间外内容；拖动或跳转到区间外时保持暂停，下次播放回入点。同媒体 EOF 重开保留区间，新开媒体/工程或重新关联清空。
- **区间仅本次媒体会话有效，不写入 v1 工程、恢复文件或全局偏好。** 这是应用层毫秒区间控制，普通 seek 仍是解封装级定位，可能因关键帧、通知延迟或音频缓冲短暂越界；不是帧精确裁切，也不是无缝循环。
- 单键媒体快捷键只在画面或时间线获得焦点时生效，不吞掉搜索框、表格及对话框输入。逐帧动作不接受自动重复；请求尚未完成时禁用。步进会从媒体起点重新解码，长片后段可能等待；不支持直播或连续倒放。EOF 后先重新播放。

## 标注、工程与导出

- Ctrl+M 添加当前毫秒位置的标注，可编辑时间、类别（问题/建议/重点/其他）和文字；搜索与类别筛选叠加，双击暂停定位。
- 添加、编辑、删除、重新关联支持 Ctrl+Z/Ctrl+Y。缺失媒体工程仍保留可编辑的标注；重新关联可恢复播放。
- Ctrl+S 保存 `.review.json`，跨目录另存为重新计算媒体相对路径。工程不嵌入、不修改原视频。未保存修改在停止编辑 2 秒后原子写入恢复文件；下次无参数启动可恢复。更换/退出提供保存、放弃、取消。
- CSV 为 UTF-8 BOM，包含毫秒、类别与文字，处理引号/换行及公式注入；不能作为工程重新载入。
- Ctrl+Shift+E 导出当前视口 PNG（含画面变换、黑边、字幕），保存对话框前捕获，原子写入，禁止覆盖原媒体/工程。位图字幕跟随视频几何，ASS/外部文字字幕维持视口排版。这不是源分辨率、高位深或色彩管理导出。

## 验证记录与边界

2026-11-17：独立 MSVC Release / Qt 6.9.3 构建成功，五项 CTest 各连续运行三次通过：文档、时间线/控制器/设置、窗口集成、Core SDK、Qt SDK。覆盖区间请求串联与取消、实际区间循环、标注增改删和撤销、保存、配置恢复、缺失媒体重新关联及撤销，以及缩放/平移/镜像和 SAR/旋转的像素回归。测试也回归了窗口析构时撤销栈/子控件回调的生命周期问题。另以 Windows 原生 SDL 渲染路径运行窗口集成测试及 Qt SDK 像素测试通过；修复了原生驱动在 YUV 范围/矩阵变化时复用旧着色器状态的问题，测试采样适配高 DPI 输出。

已启动实际 Windows 原生应用，载入 12 秒 MPEG-4/PCM 测试视频与标注工程，通过 UI Automation 调用 1:1/适应窗口，并检查仅该窗口的截图：视频画面、时间线、标注正常呈现。未据此宣称主观音质、长片性能、完整格式矩阵或长时间稳定性通过。

后续能力：A/B 对比、多素材同步、图像序列、帧编号、高位深、OCIO、OTIO、波形/缩略图、倒放和往返播放。当前没有这些功能的假按钮。现有双应用便携打包脚本不包含 Review；独立发行仍须验证依赖及 GPLv3、Qt、FFmpeg 等许可材料。
