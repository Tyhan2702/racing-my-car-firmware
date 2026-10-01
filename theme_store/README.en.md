# Theme Store

English | [简体中文](README.md)

**Packed, ready-to-push** runtime-theme binaries plus the metadata an app
picker needs. The full authoring/packing/publishing guide is
**[docs/THEMES.en.md](../docs/THEMES.en.md)** (runtime themes section);
this file is the quick directory reference.

```
theme_store/
├── catalog.json       # generated index (gen_theme_store.py), fetched by the app
└── <theme_id>/
    ├── info.json      # id / title / description / author / version
    ├── preview.png    # 360×360 picker cover (never flashed)
    └── theme.bin      # pack_theme.py output, always 4 MB (0xFF-padded)
```

Publishing in three steps:

```bash
python3 tools/theme_packer/pack_theme.py themes/<id> theme_store/<id>/theme.bin
# write info.json + preview.png
python3 tools/gen_theme_store.py   # regenerate catalog.json (validates and hashes, never repacks)
```

Push `theme.bin` to a device over WiFi OTA (the `/ota/theme` endpoint) or
flash via USB at `0x620000` — see
[docs/APP_PROTOCOL.en.md](../docs/APP_PROTOCOL.en.md) and
[docs/FLASH.en.md](../docs/FLASH.en.md).
