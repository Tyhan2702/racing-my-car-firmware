# 主题商店

存放**打包好、可直接推送**的运行时主题二进制与 App 选择器所需的元信息。
主题制作、打包与上架的完整文档见 **[docs/THEMES.md](../docs/THEMES.md)**
（运行时主题一节）；这里是目录职责速览。

```
theme_store/
├── catalog.json       # 自动生成的索引（gen_theme_store.py），App 拉这份
└── <theme_id>/
    ├── info.json      # id / title / description / author / version
    ├── preview.png    # 360×360 选择器封面（不刷进设备）
    └── theme.bin      # pack_theme.py 产出，恒为 4MB（0xFF 填充）
```

上架三步：

```bash
python3 tools/theme_packer/pack_theme.py themes/<id> theme_store/<id>/theme.bin
# 写 info.json + preview.png
python3 tools/gen_theme_store.py   # 重新生成 catalog.json（只校验与算哈希，不重打包）
```

`theme.bin` 推送到设备走 WiFi OTA（`/ota/theme` 端点）或 USB 烧到 `0x620000`，
见 [docs/OTA_APP.md](../docs/OTA_APP.md)。
