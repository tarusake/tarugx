# TaruGX/100

Veryl HDLで開発している、PCI Express接続の実験用FPGA GPUです。
Linuxから外部DDR3のFramebufferへ描画し、HDMIへ出力します。

![TaruGX/100](assets/branding/tarugx-logo-concept-v3.png)

## 対象構成

| 項目 | 内容 |
| --- | --- |
| 製品名／型番 | TaruGX/100 / TGX-100 |
| ボード | Sipeed Tang Mega 138K **通常Dock版**（Pro版ではない） |
| FPGA | GW5AST-138BPG484AC2/I1、Rev.B |
| RTL | Veryl、ベンダーIP接続部はVerilog/SystemVerilog |
| FPGAツール | Gowin EDA |
| ホスト | Linux、独自DRM/KMSドライバー `tarugx` |
| 映像 | 640×480、約60Hz、XRGB8888 |
| メモリ | 外部DDR3、BAR2に4MiBのアクセス窓 |
| UEFI | X64 GOP、PCIe Option ROM |

PCIeはGen2 x1を設計目標としています。現在の検証構成にはx4設定のIPを使う
実験版があり、Gen2 x2でのリンクを確認しています。目標構成と実測値は区別しています。

## 実装済みの機能

- BAR0のレジスタアクセス、Byte Enable、Memory ReadへのCompletion応答
- HDMI単色表示と、DDR3からのFramebuffer Scanout
- Linux DRM/KMS、CPU描画のFramebuffer転送、Xfceデスクトップ表示
- ダブルバッファとVBlank同期ページフリップ、MSIによる完了待ち
- FPGA側の矩形fill／VRAM内blit、コマンドキューとfence
- ホストRAMからVRAMへのPCIe Memory Read DMA（診断用の初期実装）
- PCIe Option ROMからのGOP自動読込みとUEFI描画
- 検証したAMIホスト向けのPOST文字表示互換処理
- 起動時のロゴ表示と1秒の保持
- VerylによるUART診断、単体シミュレーションとソフトウェアテスト

実機では4MiBの読戻し、描画負荷試験、ROM全64KiBの読戻し、UEFI表示とLinux復帰を確認しています。
一般的なGPUの代替として完成した製品ではなく、ボード・IP設定・ホストに依存する試作です。

## 構成

```text
Linux / UEFI
    │ PCI Express
Gowin Endpoint IP → TLP Bridge
    ├─ BAR0: GPUレジスタ
    ├─ BAR2: Framebuffer窓 → DDR3 → Scanout → HDMI
    └─ Expansion ROM: X64 GOPドライバー
```

| ディレクトリ | 内容 |
| --- | --- |
| `rtl/` | Veryl RTL |
| `tests/` | RTL・ソフトウェアのテスト |
| `fpga/` | ボード接続、制約、ベンダーIPラッパー |
| `drivers/tarugx/` | Linux DRM/KMSドライバー |
| `uefi/` | GOP、Option ROM、起動ロゴ |
| `host/` | ホスト側の表示設定と接続設定の雛形 |
| `scripts/` / `tools/` | 生成・ビルド・検証ツール |
| `vendor/` | IP設定入力。生成済みの専有IP本体は含めない |

## ビルドとテスト

### Veryl RTL

開発で使用したVerylは0.22.0です。シミュレーションにはVerilatorが必要です。

```sh
veryl fmt --check
veryl check
veryl build
veryl test --sim verilator
```

生成されたSystemVerilogは`build/`へ出力されます。Gowin固有IPを含む実機ビルドと、
モックを使った単体テストは別です。

### Linuxドライバー

実行するLinuxカーネルに対応したヘッダーを用意します。

```sh
make -C drivers/tarugx
# 別のカーネルビルドツリーを使う場合
make -C drivers/tarugx KDIR=/path/to/kernel/build
```

ドライバーのロードやMMIO試験は対象PCIeデバイスを確認して行ってください。
描画中のデバイスへの生MMIO試験やFPGAの書換えは避け、先に描画とドライバーを停止します。
ドライバーはVBlank通知にMSIを1本使用します。INTxフォールバックとMSI-Xには
未対応なので、MSIを割り当てられない環境ではロードに失敗します。ロード後はPCI
デバイスの`page_flips`、`flip_timeouts`、`flip_fallbacks`、`vblank_timeouts`、`bytes_uploaded`、
`msi_interrupts`属性で、VBlank同期した更新の成功回数、timeout回数、VRAM転送量、
MSI受信回数を確認できます。古いFlashイメージでページ切替がtimeoutした場合は、
最初の完成済み非表示ページをCPUで表示中ページへコピーした後、表示中ページへ
変更矩形だけを直接転送する単一バッファ互換モードへ移行します。
`flip_fallbacks`は、この互換更新が行われた回数です。部分更新では、更新前にFPGAのblitで表示中ページを
非表示ページへ同期します。`blit_syncs`と`blit_sync_failures`で成功・失敗回数を
確認でき、blitが失敗した場合だけCPUによるVRAM内コピーへフォールバックします。
DMA対応FPGAでは、既定の全面更新にcoherent staging bufferからPCIe DMAで非表示
ページへ転送します。`dma_upload=0`モジュールパラメーターで64-bit BAR2書込みへ
切り替えられます。DMA非対応の旧FPGAはprobe時に検出し、自動的にBAR2を使用します。
`dma_uploads`と`dma_upload_failures`で成功・失敗回数を確認でき、DMAが失敗した場合は
従来の64-bit BAR2書込みへフォールバックします。部分更新は損傷矩形だけをBAR2へ
転送するため、不要な全画面DMAは行いません。

2D fill/blitは`drivers/tarugx/tarugx_drm.h`のprivate DRM ioctlから利用できます。
ioctlはDRM masterだけに許可し、任意のVRAMアドレスではなく640×480画面内の座標だけを
受け付けます。ドライバーは表示中ページのアドレスへ変換してコマンドキューへ投入し、
CPUをポーリングさせずfence完了のMSIを待ちます。現在は安全性を優先した同期APIで、
各呼出しは完了または100msのtimeoutまで待ちます。成功・失敗数はPCIデバイスの
`render_commands`と`render_failures`属性で確認できます。
複数命令をまとめる`DRM_IOCTL_TARUGX_SUBMIT`は最大4命令を連続投入し、最後のfenceだけを
待ちます。`render_queue_peak`にはドライバーが観測した最大待機命令数を記録します。
`DRM_TARUGX_SUBMIT_ASYNC`を指定するとsubmitは投入直後に戻り、返されたfenceを
`DRM_IOCTL_TARUGX_WAIT`で待てます。未回収の非同期バッチはDRMファイルごとに1件に制限し、
別ファイルからの回収を拒否します。WAITせずファイルを閉じた場合は`postclose`で回収し、
KMS更新と重なった場合もfence完了を待ってから表示用のblitやページ更新を開始します。
2026-11-03の実機回帰では、320×180のfill 2件とblit 2件を1バッチとして120回、
合計480命令を6.666秒（18.00バッチ/秒、プロセスCPU 0.9%）で処理し、
submit自体は0.017msで復帰しました。fence、VRAM読戻し、WAITなしcloseの回収を含めて
`render_failures=0`、最大待機深度3を確認しています。

2D用のVRAMは`DRM_IOCTL_TARUGX_VRAM_CREATE`でGEM handleとして確保できます。
DRM coreのhandle所有権とmmap offset検査を通してBAR2へwrite-combine mappingし、
`drm_mm`が画面用2ページより後ろの約1.66MiBだけを管理します。表示ページや他の
VRAM GEMへはmmapできません。実機では1,228,800 byteの作成、先頭／末尾のmmap読書き、
容量不足時の`ENOSPC`、handle解放後の領域再利用をBAR2読戻しまで確認しています。
この段階ではVRAM GEMを2D命令の対象やscanoutへ指定するAPIはまだ接続していません。

実機カーネル向けのコンパイルだけを行う場合と、テストモジュールを一時ロードして
master制限、境界検査、fill/blit、fence/MSI、VRAM読戻しを検証する場合は次を実行します。
後者は表示サービスを一時停止し、終了時には従来モジュールとサービスを復元します。

```sh
python3 tools/build_remote_driver.py
python3 tools/test_drm_render_ioctl.py
```

BAR2とDMAのDRM全面更新を同じ条件で比較する場合は、次を実行します。リポジトリの
最新KMSテストをホスト上の一時ディレクトリでビルドするため、古いテストバイナリを
誤って使いません。測定前の`dma_upload`値と表示サービスは終了時に復元します。

```sh
python3 tools/benchmark_drm_upload.py
```

モニター上で速度差を見る場合は、次の視覚デモを実行します。赤い`BAR`、青い`DMA`、
緑のFPGA `FILL`、紫の`QUEUE+BLIT`画面で移動バーを120フレームずつ描き、FPS、
フレーム時間、プロセスCPU使用率を表示します。実行中だけ表示サービスを停止し、
`dma_upload`とIRQマスクの元の値、表示を終了時に自動復元します。キュー版は各フレーム
のfill＋blitを順番に投入してfenceを待ち、MSI増加と最終フレームのBAR2読戻しを検査します。

```sh
python3 tools/run_performance_demo.py
```

2要求DMA版の2026-11-03実測では、BAR2が14.99fps・66.70ms/frame・CPU 81.0%、
DMAが19.99fps・50.03ms/frame・CPU 34.5%、FPGA fillが147.56fps・
6.78ms/frame・CPU 14.1%、queue＋blitが759.90fps・1.32ms/frame・CPU 15.5%
でした。fillは全画面消去と80×160矩形描画、queue＋blitは80×160矩形の消去とVRAM内コピー
の2命令を1フレームとして測定しており、処理面積が異なるためFPSの単純比較はできません。

DMA対応FPGAイメージでは、rootでPCIデバイスの`dma_test`属性へ4の倍数のバイト数を
書き込むと、カーネルが割り当てたcoherentバッファをVRAMの画面外領域へ転送し、BAR2
読戻しまで検査します。`1`は従来どおり64バイトを意味し、上限は1フレームです。
この診断転送自体は通常表示から呼ばれません。DMA非対応の旧FPGAイメージでは
ドライバーの全面更新DMAと`dma_test`を使用しないでください。

### UEFI GOPとROM

X64向けのGCC/binutils、GNU-EFIのヘッダーとライブラリ、Python 3が必要です。
GNU-EFIが`/usr`以外にある場合は`EFI_ROOT`で指定します。

```sh
make -C uefi all test
make -C uefi "$PWD/build/uefi/TaruGxSplash.rom"
```

`TaruGxSplash.rom`は文字モード互換処理と1秒の起動ロゴを含む64KiB版です。
`TaruGxEarlyText.rom`はロゴなしの互換版、`TaruGxGop.rom`は通常版です。
画像データの再生成に限りImageMagickが必要です（`make -C uefi logo`）。

### FPGA実機ビルド

正常にライセンス認証できるGowin EDA環境と、通常Dock版の公式サンプル、
対象デバイス向けに生成したPCIe／DDR3／映像関連IPを別途用意してください。
このリポジトリだけで実機用ビットストリームを生成できる構成ではありません。

`GOWIN_ROOT`はEDAのインストール先に合わせてください。
次の環境変数は、使用する生成・ビルドスクリプトに応じて指定が必要です。
サンプル配置場所の既定値は設けていません。

| 環境変数 | 指定する公式サンプルのディレクトリ |
| --- | --- |
| `SIPEED_PCIE_GEN2_DIR` | `examples/pcie_dma_demo/pcie_gen2(5G)` |
| `SIPEED_HDMI_DIR` | `examples/hdmi_colorbar/eda_proj/src` |
| `SIPEED_CAM_DIR` | `examples/cam_dvi/cam2dvi/src` |
| `SIPEED_DDR3_B_DIR` | `examples/cam_dvi/cam2dvi/src/ddr3_memory_interface` |

設定後、
生成IPのポート・品番・リセット・クロック・ピン制約を確認してください。
個別実験の入口は`scripts/gowin_build_framebuffer.tcl`です。SRAM実機回帰と同じ
生成PCIe IP、PHY復旧、Auto ROM、パイプラインDDR構成を再現する場合は、
`GOWIN_SH`を設定して`scripts/build_verified_framebuffer.sh`を実行します。
実験構成の選択には`TARUGX_PCIE_GENERATED_DIR`や`TARUGX_PCIE_*`環境変数を使います。
ロゴ入りROMを選ぶ場合は`TARUGX_PCIE_ROM_FILE`へ生成したROMのパスを指定します。
環境変数を省略した既定構成が最新の実機検証構成と同じとは限りません。

開発中のビットストリームは、外部Flashを書き換えずSRAMへ更新できます。
`build/gowin_framebuffer_pipeline/tarugx/impl/pnr`以外の出力を使う場合は
`TARUGX_SRAM_OUT`で指定します。次のコマンドはタイミングを検査し、ホストの表示と
ドライバーを安全に停止してから更新し、PCIe再列挙と表示復帰まで行います。

```sh
scripts/program_framebuffer_sram.sh
```

接続先と認証情報は`.local/host.env`と`.local/credentials.env`に0600で配置します。
SRAM書込みに失敗した場合は外部Flashの構成へ戻してからホスト復旧を試みます。

## 現在の制約

- DRMの既定経路は、DMA対応FPGAでの全面更新にPCIe DMA、部分更新にCPUからBAR2への
  64-bit書込みを使用します。`dma_upload=0`の場合は全面更新もBAR2を使用します。
  部分更新前のページ同期にはFPGAの2D blitを使用します。fill/blitはDRM master限定の
  同期private ioctlとしてコマンドキュー、fence、MSIまで統合済みですが、通常の
  Xorg描画が自動利用する段階ではありません。VRAM GEMの割当とmmap、非同期fence、
  複数コマンド一括投入までは実装済みですが、GEM handleを2D命令やscanoutの対象に
  する接続と、安全な一般ユーザー向けAPIへの本格統合は未実装です。
- PCIe DMAは最大2要求を異なるtagで並列発行し、Completionの到着順に関係なく
  対応するVRAM位置へ書き込みます。各要求は64-byte RCB内で最大16 DWORDです。
  32-byte境界では8 DWORDをまとめて256-bit DDR書込みし、非整列部と端数は
  64/32-bit書込みへフォールバックします。Completionから生成したDDR書込みは
  8要求までパイプライン化し、PCIe、DDR間のクロック境界も8段FIFOで接続しています。
  2要求版を使った2026-11-03のSRAM実機回帰では、1,228,800 byteのDMA転送5回が13.964–14.126ms
  （約87MB/s）で、直列経路の約53msから約3.8倍に改善しました。
  staging copyとVRAM読戻しを含むKMS試験でも、各120フレームがBAR2の
  8.443/8.438秒に対してDMAは6.443/6.438秒で、DMA全面更新241回の失敗は0でした。
  4要求版はシミュレーションとタイミング解析には合格しましたが、SRAM適用後の
  PCIe再接続でホストが2回連続して応答しなくなったため不採用としました。
  現在の主なレイテンシ要因は転送そのものだけでなく、shadow framebufferからの
  staging copy、VBlank待ち、DRM commitシーケンスです。
- 3Dアクセラレーションは未実装です。
- Legacy VGA I/O、VGA BIOS、他アーキテクチャ向けGOPは対象外です。
- GOPは`PixelBltOnly`で、Framebufferを通常のキャッシュ可能RAMとして公開しません。
- ダブルバッファを使用しますが、長時間安定性・他ホストへの互換性は未検証です。
- 現在実機で動作確認済みの2要求ビルドはSetup +0.026ns、Hold +0.138nsです。
  実機ではdirty更新、ページフリップ各120フレームと部分更新6種を確認しています。
- PCI IDはサンプル由来の暫定値です。製品用の割当IDではありません。
- ベンダーIPや公式サンプルの利用条件は、それぞれの配布元の条件に従います。

## 公開範囲

公開対象は`main`です。開発記録の`docs/`は公開履歴に含めません。
接続先・認証情報は`.local/`、生成物は`build/`で管理し、どちらもGit管理外です。
接続設定の雛形は`host/connection.env.example`を参照してください。

## ライセンス

自作コード・設計の基本ライセンスはSolderpad Hardware License 2.1
（`Apache-2.0 WITH SHL-2.1`）です。LinuxドライバーはGPL-2.0-only、
ロゴは保有する権利の範囲でCC BY 4.0とします。
Gowin IPとその他の第三者由来部分には、それぞれの元の条件が適用されます。
適用範囲とライセンス本文は[LICENSE.md](LICENSE.md)を参照してください。

第三者由来部分の出典・変更内容・外部依存は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)に記載しています。
