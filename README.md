# WholeDrop · 鲸鱼娘离线文件站

把文件交给鲸鱼娘：连接 ESP32-S3 热点，通过浏览器上传与下载，让手机和电脑在没有互联网的环境中交换文件。

仓库名为 **WholeDrop**；设备热点和现有界面保留 **WhaleDrop / whaledrop** 名称。

![桌面界面](docs/images/desktop.png)

截图使用本地网页与示例文件生成，不包含个人文件或真实容量数据。

## 可以做什么

- **离线文件共享**：网页和角色图片随固件提供，无需账号、App 或外部 CDN。
- **TF 卡存储**：文件保存在 `/share`，支持中文名称、搜索与同名自动编号。
- **可见的传输过程**：上传与浏览器内下载复用进度条，显示百分比、大小、MB/s 和 Mbps，支持取消。
- **鲸鱼娘互动**：8 种互动情绪、每种 3 条回应；支持拖动、位置记忆和悬浮摆动。
- **桌面与手机布局**：空间占用卡片、文件列表，以及浅蓝、青色、淡紫色的静态模糊背景。
- **板载显示**：240×240 屏幕显示设备状态、传输进度、速率和角色状态。

## 硬件

当前验证设备为 **YD ESP32-S3-EYE-MB，8 MB Flash / 8 MB PSRAM**，使用板载 TF 卡槽和 ST7789 屏幕，摄像头不启用。TF 卡使用 FAT32。

| 部件 | GPIO |
| --- | --- |
| TF CLK / CMD / D0 | 39 / 38 / 40 |
| LCD CLK / MOSI | 21 / 47 |
| LCD DC / CS / 背光 | 43 / 44 / 48 |

背光低电平有效。默认使用 **SDMMC 1-bit、20 MHz**。PlatformIO 中其他板型环境属于早期配置，未按当前版本逐一验收；请优先使用 `esp32s3eye_tuned`。

## 快速体验

1. 插入 FAT32 TF 卡，给开发板上电。
2. 手机或电脑连接热点 **`whaledrop`**，默认密码 **`123456789`**。
3. 浏览器访问 **`http://192.168.4.1/`**，等待“TF 卡已就绪”。
4. 选择文件并点击“开始投喂”；另一台连接同一热点的设备刷新列表后即可下载。

热点用于本地文件交换，不提供互联网。请在可信的小范围内使用：当前使用共享热点密码，没有独立用户登录或 HTTPS。需要修改热点名称或密码时，编辑 `firmware/file_share/main.cpp` 中的 `apSsid` / `apPassword` 后重新构建。

## 构建与烧录

以下命令在仓库根目录运行。需要 Python、Git 和可用的 USB 串口驱动；第一次构建会下载 PlatformIO 平台、工具链和依赖库。

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.\.venv\Scripts\python.exe -m platformio run -e esp32s3eye_tuned -j 4
.\.venv\Scripts\python.exe -m platformio run -e esp32s3eye_tuned -t upload --upload-port COM4 -j 4
```

将 `COM4` 替换为实际端口。Linux/macOS 可使用 `.venv/bin/python` 和对应串口路径。默认应用分区为 3 MiB；固件构建所需的生成头文件已提交，普通构建无需安装图像处理或浏览器依赖。

### 修改网页

源文件位于 `web/index.html`、`web/style.css`、`web/app.js`。修改后重新打包，再构建固件：

```powershell
.\.venv\Scripts\python.exe -m pip install -r requirements-ui-test.txt
.\.venv\Scripts\python.exe build_assets.py --web-only
.\.venv\Scripts\python.exe -m platformio run -e esp32s3eye_tuned -j 4
```

完整角色资源重建使用 `pack_reactions.py`，然后运行 `build_assets.py`。重建 LCD 中文标签需要 CJK 字体，Windows 默认使用微软雅黑；其他系统通过 `--font /path/to/cjk-font.ttf` 指定字体。

## 验证与性能

2026-10-28 的真实 Wi-Fi → HTTP → TF 卡测试中，50 MiB 文件上传约 **1.33 MB/s**、下载约 **1.63 MB/s**，SHA-256 一致。条件为近距离、SDMMC 20 MHz、屏幕开启；这是一组历史实测结果，不代表所有网卡、TF 卡和距离下的性能。

最后界面版本已分别在 1280 px 与 390 px 浏览器视口检查；此前也完成了 10 MiB 实机下载进度验证。手机视口测试使用桌面 Edge 模拟尺寸，不等同于所有手机浏览器的实机兼容性测试。验证范围与复现命令见 [验证说明](docs/VALIDATION.md)。

| 限制 | 当前行为 |
| --- | --- |
| 文件大小 | 上传上限 1 GiB；文件名最多 120 个 UTF-8 字节 |
| 浏览器内下载 | 最大 64 MiB，接收后以 Blob 交给浏览器保存 |
| 更大的下载 | 经确认后使用浏览器原生保存，可能被 IDM 等下载器接管；网页无法显示其完整进度 |
| 并发 | 设备一次处理一份文件传输 |
| 距离测速 | 已关闭入口，不作为测距或性能评估工具 |
| 存储恢复 | 后台重试，不自动格式化 TF 卡 |

“下载完成”表示网页完整接收后已请求浏览器保存，不能保证操作系统最终保存成功。容量统计针对整个 TF 卷，文件列表只展示 `/share`。

<details>
<summary>手机页面预览</summary>

<img src="docs/images/mobile.png" alt="手机尺寸页面" width="360">

</details>

## 目录

```text
firmware/file_share/   文件服务、TF 健康检查、传输管线、LCD 与生成资源
web/                  网页源文件、预览页、离线图片及生成记录
docs/                 界面截图、验证说明、架构与资源来源
build_assets.py       网页打包与 LCD 资源生成
pack_reactions.py     已有表情 PNG 的 WebP 打包
platformio.ini        板型、框架与构建环境
sdkconfig.tuned.defaults  网络栈、PSRAM 与文件系统配置
```

虚拟环境、构建缓存、Flash 备份、原始日志和一次性修改脚本不进入仓库。

## 实现与资源

上传通过端口 81 的 ESP-IDF HTTP 服务接收，先写临时文件，再同步、关闭、重命名并检查大小；网页再核对目录。下载使用分块预读。网页与图片嵌入固件，运行时不请求外部图片服务。更多信息见 [架构说明](docs/ARCHITECTURE.md)。

代码沿用仓库的 [MIT License](LICENSE)。鲸鱼娘为社区角色相关创作，图片由 AI 辅助生成；来源和处理方式见 [资源说明](docs/ASSETS.md)，不表示与 DeepSeek 存在官方关联，也不对第三方角色权利作额外授权。
