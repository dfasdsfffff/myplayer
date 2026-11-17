# MyPlayer Review 审片标注工具设计

## 目标与范围

新增独立 Qt Widgets 应用 `apps/review/`，构建目标 `myplayer_review`，只消费 `PlayerSDK::QtWidgets`。复用播放 SDK，不依赖另两个应用或 SDK 私有实现。

交付：本地视频打开、播放/暂停、前后 5 秒定位、进度定位、音量和倍速；当前播放时间点的文字/类别标注；标注时间编辑、搜索筛选、前后导航、编辑、删除、双击跳转；撤销/重做；媒体重新关联；独立 JSON 工程保存/载入、异常退出恢复及 CSV 导出。不是逐帧精确剪辑器：时间采用 SDK 播放位置的毫秒值，seek 不保证落在精确视频帧。

本轮新增 DJV 风格工作区、可缩放毫秒时间线、会话级入出点/区间播放、SDK 公开视图变换及布局偏好保存；路线见 [DJV 对齐计划](review-djv-roadmap.md)。不包含转码、裁切、云端协作、摄像头、波形或多视频同步。逐帧审阅通过 SDK `stepFrame(-1/+1)` 实现，按实际解码帧前后导航并保持暂停；它不改变普通 `seek()` 的非帧精确语义，也不提供连续倒放。

## 界面与操作

- 原生可调整大小主窗口，配色复用 `monitor-live.svg` 的 `#10171f` 深色背景、`#171f29` 面板、`#56d6a0` 强调色和 `#8395a7` 次级文字。左侧视频、独立时间线和播放控制；右侧可停靠标注面板，包含搜索、类别筛选及标注表。时间线按类别颜色显示全部标注位置，标注表复用相同颜色，标题显示当前筛选结果数。
- 文件菜单：打开视频 Ctrl+O、打开标注工程、重新关联视频、保存 Ctrl+S、另存为、导出 CSV、退出。播放工具提供空格播放/暂停、左右方向键前后单帧（±5 秒保留菜单和按钮）、音量及 0.1×–2.0× 倍速；逗号/句号执行上一帧/下一帧，步进请求完成前禁用重复操作。
- 标注工具：添加（Ctrl+M）、编辑、删除、上一条/下一条；类别为“问题”“建议”“重点”“其他”，文字必填。添加先截取当前时间；对话框允许按 `小时:分钟:秒.毫秒` 修改时间。
- 表格按时间排序，同一毫秒允许多个标注。搜索匹配标注文字，类别筛选与搜索叠加；导航只遍历当前可见结果。双击跳转并暂停；无可定位会话时禁用跳转。
- 添加、编辑、删除和媒体重新关联进入同一 `QUndoStack`，支持 Ctrl+Z/Ctrl+Y。保存点使用 undo clean 状态；新开视频在首次保存前始终视为未保存。
- 更换视频意味着新工程；载入工程替换整个工程。未保存更改在替换或关闭前提供保存/放弃/取消；取消或保存失败保留现状。
- 有播放会话才允许播放；SDK `MediaInfo` 以正宽高报告视频，`tracks` 仅枚举可切换音轨/字幕轨，因此确认正宽高且可定位后才启用标注/进度控制。异步打开中清空旧位置/时长并禁用操作，显示状态或错误。
- 工程载入不要求媒体当时存在；缺失时保留标注、明确提示并禁用播放。可通过“重新关联”选择可读视频，也可继续编辑、保存、导出标注。
- 未保存内容在修改停止 2 秒后写入应用本地数据目录的单一恢复文件。无命令行文件启动时提示恢复或放弃；正常保存、明确放弃和正常退出删除恢复文件。恢复文件使用 `QSaveFile` 原子替换并保存绝对媒体/工程路径，避免因恢复目录改变相对路径语义。

## 架构与接口契约

`review_document.h/.cpp`：纯 Qt Core 值数据和持久化；`ReviewNote { qint64 positionMs; QString category; QString text; }`；`ReviewDocument { QString mediaPath; QList<ReviewNote> notes; }`；`ReviewRecovery { QString projectPath; ReviewDocument document; }`。

命名空间 `review` 提供：

```cpp
QString formatTime(qint64 milliseconds);
bool loadDocument(const QString& path, ReviewDocument& document, QString& error);
bool saveDocument(const QString& path, const ReviewDocument& document, QString& error);
bool exportCsv(const QString& path, const ReviewDocument& document, QString& error);
bool loadRecovery(const QString& path, ReviewRecovery& recovery, QString& error);
bool saveRecovery(const QString& path, const ReviewRecovery& recovery, QString& error);
```
失败返回 false 并提供错误；load 失败不修改传入文档。保存使用 QSaveFile 原子替换。约束：64 MiB 输入、最多 10000 条标注、文字最多 10000 个 UTF-16 单元、类别上述四种、时间为非负且不超过 JSON 精确整数范围；同一时间保持原顺序。mediaPath 在内存中为绝对路径，落盘相对工程目录存储，允许跨目录另存为；读取相对工程文件目录解析。

`review_window.h/.cpp`：`review::ReviewWindow final : public QMainWindow`，提供 `bool openVideo(const QString&)`、`bool openProject(const QString&)` 和 `offerRecovery()`；窗口负责撤销栈、延迟恢复写入、未保存确认、对话框、筛选导航、错误显示和播放状态。QtPlayer / VideoWidget 均由窗口树管理，Qt SDK 调用均在 GUI 线程。命令同步拒绝与异步失败均可见，播放状态取信号而不是按钮猜测。

`main.cpp`：设置独立应用身份，创建窗口，接受最多一个本地视频或 `.review.json` 工程路径，捕获 SDK 构造异常并报告启动失败。不添加假的运行时或演示数据。

## 工程文件与 CSV

JSON 顶层 `format: "myplayer-review"`、`version: 1`、`media`、`notes` 数组。每条 `positionMs`、`category`、`text`。未知格式、版本、非法类型/时间/类别、过大文件拒绝，不部分载入。工程不嵌入视频，不修改原视频。

CSV 为 UTF-8 BOM、CRLF，列为 `media,time_ms,timecode,category,text`。所有文字字段按 CSV 双引号转义（包含换行/逗号/引号），可能以 = + - @ 或制表/回车开头的用户字段加单引号防电子表格公式执行。CSV 是导出结果，不作为可重新载入工程。导出不清除工程未保存标记。禁止保存工程/CSV 覆盖原视频或 CSV 覆盖当前工程。

## 构建与部署

根 CMake 新增默认 OFF 的 `MYPLAYER_BUILD_REVIEW`。启用时引入共享 Qt SDK 并加入 `apps/review`；单独启用此开关也必须能构建，不依赖播放器开关。输出复用 `PLAYERDEMO_OUTPUT_DIR`，Release 为 `myplayer_review.exe`，Debug 为 `myplayer_review_debug.exe`。Windows 构建后复制运行库并调用匹配 Qt 的 windeployqt；沿用项目告警/消毒器规则。现有双应用便携打包脚本维持双应用职责，不声称已经包含新应用。

## 验收

1. 构建 myplayer_review，独立启动真实窗口，打开本地测试视频看到有效画面，暂停/恢复并定位。
2. 在不同时间添加中文/逗号/引号/换行标注，编辑和删除；双击按时间跳转。
3. 保存、载入及跨目录另存为，时间/文字/类别与媒体路径一致；损坏工程不得覆盖当前数据。
4. 导出 CSV 保留毫秒及多行文字，公式文本安全处理；导出失败不丢失标注。
5. 验证撤销/重做、自动恢复的恢复/放弃、媒体重新关联，以及更换媒体和关闭的保存/放弃/取消；缺失媒体工程仍能读取和编辑标注。
6. 对 JSON 验证/原子保存/CSV/恢复文件边界添加确定性的窄回归用例；界面通过实际窗口交互或临时 Qt 驱动验证，不把启动等同于播放成功。

## 授权与发行边界

继承仓库 GPLv3 与第三方授权要求。新应用构建成功不等于独立便携包或全部媒体格式已验收；实际验证结果记录在应用 README。

## DJV 对齐第一阶段架构（2026-11-17）

- `review_timeline.h/.cpp`：独立 QWidget，qint64 毫秒与可见时间窗口；刻度、播放头、范围、标记命中/提示；时间窗口变化不触发 seek。拖动只预览，释放后请求最终位置。标注颜色与表格一致，图例和文字提示不依赖颜色识别。
- `review_playback_controller.h/.cpp`：GUI 线程请求状态机，通过注入的 Pause/Seek/Play 提交函数消费 SDK 公共 Request/OperationResult；只在匹配 session/operation 的成功终态推进，取消/失败不恢复播放；区间边界防重复触发。Window 继续维护播放器 generation 和文档 generation。
- 入出点满足 `0 <= in < out <= duration`，手动浏览可在区间外，播放从区间外回入点；单次到出点暂停，循环回入点。EOF 延迟重新打开并等待 open 成功再定位/播放。毫秒级近似控制，不保证截止帧/音频边界或无缝。区间不写入 JSON v1、undo、恢复或 QSettings，状态栏明确“会话区间”。
- `review_settings.h/.cpp`：独立 QSettings 分组 `reviewWorkspace/v1`；布局、窗口位置、音量、速度、时间线偏好。测试注入临时 INI；异常数值回默认，离屏窗口回可见屏幕。演示/全屏临时状态不持久化。
- SDK `VideoWidget` 新增 fitView/setViewScale/zoomView/panView/setMirrors 及只读状态、viewChanged。缩放单位为 SAR 校正后源像素至 SDL 输出物理像素，平移/锚点使用 Qt 逻辑像素并换算。默认动态 Fit 不变；非有限参数拒绝，倍率 0.01–32。位图字幕沿用视频几何，ASS 字幕沿用视口空间；截图复用实际渲染路径。
- Review 层负责鼠标/焦点策略，SDK 不默认抢占其他应用的交互。单键播放动作限画面/时间线，表格和编辑器优先处理输入。窗口析构前停止恢复定时器并断开子控件到窗口的回调，防止成员已析构后的撤销栈/选择信号重入。
- 工作区：顶部文件/视图 QToolBar、中心视频、底部时间线/控制、右侧 QDockWidget 标注、状态栏。Ctrl+P 演示、Esc 恢复；工作区重置提供无配置依赖的回退。

验证目标新增 `review_workspace_tests`；`review_window_tests` 覆盖真实循环、笔记 CRUD/撤销/保存、配置恢复、缺失媒体/重新关联；SDK Qt 测试扩展像素变换及默认兼容。原生呈现与 offscreen 测试分别记录于应用 README。
