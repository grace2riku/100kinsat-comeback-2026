# カメラ実写ダンプ台帳（Issue #52 / camera_bringup.md §4）

実機の `cam dump` で取得した QVGA YUV422(**UYVY**) 生フレーム（153,600 bytes）。
ホストテスト（`test/core/test_cone_detect.cpp` の実写回帰）の入力兼、Phase5（#29）で
DNNRT 検証器導入を判断する場合の学習データ。

- ファイル名規約: `<対象>_<距離>_<光条件>_<連番>.yuv`（例: `cone_3m_front-sunny_001.yuv`）
- PC での閲覧: `ffmpeg -f rawvideo -pix_fmt uyvy422 -video_size 320x240 -i <in>.yuv -update 1 -frames:v 1 <out>.png`
- 追加時は必ず下表に**条件メタデータ**を1行追記する（距離・光条件・時刻・使用閾値・検出結果）

## 台帳

| ファイル | 取得日 | 場所/光条件 | 対象・距離 | WB | 使用閾値 (y0/y1/uMax/vMin) | cam detect 結果 | 備考 |
|---|---|---|---|---|---|---|---|
| `indoor_noCone_room-light_001.yuv` | 2026-07-19 夜 | 室内・電球色照明 | コーン無し | DAYLIGHT 固定 | 既定 32/235/120/160 | **誤検出** detected=1 width=320 conf=0.56-0.76 | WB=DAYLIGHT×電球色で全面がオレンジ転び。実測 V(Cr): p50=162・59.3%が vMin=160 超え・max=188（vMin=190 なら誤検出0）。手順Bの室内予行で取得 |
