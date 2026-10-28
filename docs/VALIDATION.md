# 验证范围

## 已有证据

2026-10-28，YD ESP32-S3-EYE-MB，8 MB Flash / 8 MB PSRAM，FAT32 TF 卡，20 MHz SDMMC 1-bit，屏幕开启，近距离 Wi-Fi：

| 检查 | 结果与范围 |
| --- | --- |
| 50 MiB 文件真实无线传输 | 上传 1.328 MB/s、下载 1.635 MB/s，SHA-256 一致；属于此前传输引擎的实测 |
| 八种表情、拖动与位置记忆 | 本地 fixture 和设备提供的网页通过桌面 / 手机尺寸检查 |
| 共用下载进度条 | 两种视口下载 10 MiB，观察到中间进度，完整 Blob 长度正确，最终 100% |
| 静态模糊背景 | 两种视口检查，无横向溢出，背景不拦截点击，实机页面与打包源一致 |
| 距离测速 | 入口关闭；不宣称完成距离测试 |

MB/s 为十进制单位。40 MHz 实验中曾出现读卡中断，发布配置采用 20 MHz。早期四档距离测试未完成，不能视为通过。上述结果不是本次整理仓库时重新执行的完整性能测试。

网页截图来自桌面 Edge 浏览器及其手机尺寸视口。自动化在保存前拦截 Blob，避免唤起 IDM；不覆盖系统保存对话框、真实手机浏览器全量兼容性或物理 LCD 实屏观感。

README 展示图由本地 fixture 提供示例文件和容量数据，主要用于展示布局，不作为上述性能数据的证据。

## 复现

先安装开发和浏览器依赖，确保本机安装 Edge；脚本当前使用 `channel='msedge'`。在仓库根目录执行，结果保存在不入库的 `results/`：

```powershell
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt -r requirements-ui-test.txt
.\.venv\Scripts\python.exe test_pet_ui.py
```

本地 fixture 只验证网页逻辑，不代表真实网络与 TF 卡性能。连接设备热点后：

```powershell
.\.venv\Scripts\python.exe test_pet_ui.py --hardware
.\.venv\Scripts\python.exe verify_download_progress.py
.\.venv\Scripts\python.exe wifi_bench.py --source 192.168.4.2 --sizes 1048576 10485760 52428800 --cleanup
.\.venv\Scripts\python.exe test_transfer_abort.py
```

`verify_download_progress.py` 需要 `/share` 内已有 10 MiB 文件，仅下载，不创建测试文件。`wifi_bench.py` 与取消回归会创建测试文件并执行清理，使用前确认卡片有足够空间；异常退出后可能留下测试文件。`--source` 必须对应本机连接热点的网卡地址，脚本不会切换网卡。界面测试使用默认 `192.168.4.1`。
