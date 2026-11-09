# SDK 第三方授权清单（项目 v2.1.0）

本 SDK 源码包根目录的 `LICENSE`（从本目录访问为 [`../LICENSE`](../LICENSE)）为 GPLv3；`NOTICE` 和各源码版权头一并保留。本清单不提供闭源商业宿主授权结论，也不表示 v2.1.0 附件已发布。

| 内容 | 是否随源码包交付 | 授权来源 / 责任 |
| --- | --- | --- |
| SDK 核心、C ABI 与 Qt 集成 | 是 | SDK 根 LICENSE、NOTICE 及各源码版权头 |
| sigslot 1.2.3 | 是 | [`../third_party/sigslot/LICENSE`](../third_party/sigslot/LICENSE)（保留原文） |
| FFmpeg、SDL2、SoundTouch | 否，由宿主提供 | 随实际依赖发行的 license/copyright 与构建配置 |
| Qt、libass 及字体/文字整形依赖（包括适用时的 Fontconfig） | 否，仅 Qt 组件需要 | 随实际依赖发行的 license/copyright 与构建配置 |

依赖版本及部署说明见 [SDK 接入指南](../README.md)，第三方声明见 [`../THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md)。外部二进制依赖不包含在 SDK 源码归档中，因此本目录不是最终二进制发行的完整许可证包。SoundTouch 的 Windows DLL 包装与非 Windows C++ 链接路径均须按实际发行版本保留许可材料。

Windows x64/MSVC Release 的 C ABI 安装规则会复制 SDK 的 LICENSE、NOTICE 和第三方声明；存在 vcpkg 安装树时，copyright 会位于 `licenses/vcpkg/<port>/copyright`。该安装规则尚未单独复制 sigslot LICENSE，二进制 SDK 发行方须从上述源码路径补齐。应用 portable 包则采用 `licenses/<port>-copyright`、`licenses/sigslot-LICENSE` 及 `licenses/Qt/`，不能混淆两种布局。

最终宿主发行前须核对所用 FFmpeg 功能及 LGPL/GPL 条件、Qt 授权、静态链接义务、对应源码及第三方声明。vcpkg 安装树的 `share/<port>/copyright` 是对应版本的授权原文来源之一，不是全部许可义务的替代品；法律文本本次未修改。
