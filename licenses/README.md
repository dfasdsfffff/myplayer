# SDK 第三方授权清单

本源码包沿用根目录 `LICENSE`（GPLv3），不提供闭源商业宿主授权结论。

| 内容 | 是否随源码包交付 | 授权来源 / 责任 |
| --- | --- | --- |
| SDK 核心与 Qt 集成 | 是 | 根 LICENSE、NOTICE 及各源码版权头 |
| sigslot 1.2.3 | 是 | `lib/sigslot-1.2.3/LICENSE`（保留原文） |
| FFmpeg、SDL2、SoundTouch | 否，由宿主提供 | 随实际依赖发行的 license/copyright 与构建配置 |
| Qt、libass 及字体/文字整形依赖 | 否，仅 Qt 组件需要 | 随实际依赖发行的 license/copyright 与构建配置 |

依赖版本及部署说明见 `SDK_README.md`。外部二进制依赖不包含在 SDK 源码归档中，因此本目录不是最终二进制发行的完整许可证包。最终宿主发行前须核对所用 FFmpeg 功能及 LGPL/GPL 条件、Qt 授权、静态链接义务、对应源码及第三方声明。vcpkg 安装树的 `share/<port>/copyright` 是对应版本的授权原文来源之一。
