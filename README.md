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
| 映像 | Flash版640×480、SRAM実験版1920×1080・約60Hz・RGB565 |
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
- ホストRAMからVRAMへのPCIe Memory Read DMA
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
`update_profile`は直近の更新について、全体、XRGB8888→RGB565変換、PCIe DMA、
VBlank待ちの時間をそれぞれnsで表示します。
`conversion_profile`はXRGB8888変換を通常のhost GEMと外部PRIME GEMに分け、
それぞれの累積回数、累積時間、平均時間をnsで表示します。2026-11-04の1080p実機では
host平均9.83ms、PRIME平均10.23msでほぼ同等だったため、外部PRIMEの読出しではなく
CPU色変換自体がボトルネックだと確認できました。8画素単位への手動展開と
write-combine staging bufferはいずれも遅化したため不採用としています。
ドライバーremove／shutdown開始時はrender fence待ちとVBlank待ちの両方へ終了状態を
公開して即時に起床させます。IRQ停止後に100ms timeoutまで古いBARを参照し続けないため、
SRAM更新やPCIe再列挙時のteardownを短く安全にします。2026-11-04に更新版自身の
remove→再probeを2回連続で通し、各回のDMA 6回、page flip 4回、全timeout 0を確認しました。
DMA対応FPGAでは、既定の全面更新にcoherent staging bufferからPCIe DMAで非表示
ページへ転送します。`dma_upload=0`モジュールパラメーターで64-bit BAR2書込みへ
切り替えられます。DMA非対応の旧FPGAはprobe時に検出し、自動的にBAR2を使用します。
`dma_uploads`と`dma_upload_failures`で成功・失敗回数を確認でき、DMAが失敗した場合は
従来の64-bit BAR2書込みへフォールバックします。部分更新は損傷矩形だけをBAR2へ
転送するため、不要な全画面DMAは行いません。

2D fill/blitは`drivers/tarugx/tarugx_drm.h`のprivate DRM ioctlから利用できます。
画面座標を対象にするfill/blit/submitはprimary nodeのDRM masterだけに許可します。
ドライバーは表示中ページのアドレスへ変換してコマンドキューへ投入し、
CPUをポーリングさせずfence完了のMSIを待ちます。現在は安全性を優先した同期APIで、
各呼出しは完了または100msのtimeoutまで待ちます。成功・失敗数はPCIデバイスの
`render_commands`と`render_failures`属性で確認できます。
複数命令をまとめる`DRM_IOCTL_TARUGX_SUBMIT`は最大4命令を連続投入し、最後のfenceだけを
待ちます。`render_queue_peak`にはドライバーが観測した最大待機命令数を記録します。
`DRM_TARUGX_SUBMIT_ASYNC`を指定するとsubmitは投入直後に戻り、返されたfenceを
`DRM_IOCTL_TARUGX_WAIT`で待てます。未回収の非同期バッチはDRMファイルごとに1件に制限し、
別ファイルからの回収を拒否します。標準syncobjで完了を待った同じfdから次の非同期
batchを投入する場合は、完了済みprivate記録を自動回収するためprivate WAITは不要です。
WAITせずファイルを閉じた場合は`postclose`で回収し、
KMS更新と重なった場合もfence完了を待ってから表示用のblitやページ更新を開始します。
2026-11-03の実機回帰では、320×180のfill 2件とblit 2件を1バッチとして120回、
合計480命令を6.666秒（18.00バッチ/秒、プロセスCPU 0.9%）で処理し、
submit自体は0.017msで復帰しました。fence、VRAM読戻し、WAITなしcloseの回収を含めて
`render_failures=0`、最大待機深度3を確認しています。
ドライバー0.14以降は、1080p表示に使うDMA-coherent dumb bufferを標準PRIME
dma-buf FDへexportできます。同じTaruGXの別DRM fileへself-importしてGEM handleを
共有でき、XRGB8888とRGB565の両方で表示回帰を実機確認しました。同時にschedulerのrunqueue数を
標準priority数に合わせ、probe時のpriority範囲外警告を解消しました。
ドライバー0.16以降は外部dma-buf専用GEM objectでscatter/gather tableとvmapを保持し、
非連続な各DMA segmentをVRAM上の連続した位置へ順番に転送します。Linux DMA GEM helperの
連続IOVA制限に依存しないため、断片化したPRIME framebufferも受け付けます。
`prime_profile`属性はimport回数、直近と最大のsegment数を表示します。2つのmemfdを束ねた
udmabufによる実機試験ではRGB565が64 segment、XRGB8888が129 segmentとなり、両方とも
59.85fps、各DMA 127回、page flip 125回、全failure／timeout 0で1080p表示できました。
さらに表示中の断片化PRIMEへ2回の部分dirtyを行い、非0のSG offsetから非表示ページへ
転送して旧表示ページへmirrorする経路を検証しています。RGB565とXRGB8888の両方で
DMA 131回、page flip 127回、全failure／timeout 0、59.85fps以上を確認しました。
さらに未完了の全面blit fenceを`DMA_BUF_IOCTL_IMPORT_SYNC_FILE`で外部framebufferの
WRITE fenceとして追加し、modeset直前は未完了、modeset復帰後は完了となることを
確認しました。KMSがPRIME reservation fenceを待ってからPCIe DMAを始めるため、
外部producerが書込み中の画像を途中で表示しません。
同じ未完了fenceをplaneの標準`IN_FENCE_FD`に設定し、CRTCの`OUT_FENCE_PTR`から
完了sync_fileを取得するatomic modesetも実機で通しました。atomic commit後に入力fenceと
出力fenceの両方がsignalされ、XRGB8888外部PRIME framebufferのDMA転送と表示まで
完了します。これによりimplicit reservation fenceとexplicit atomic fenceの両経路を回帰で覆います。
`DRM_MODE_ATOMIC_NONBLOCK`で通常のDMA-backed framebufferへ切り替える回帰も追加し、
XRGB8888実機でatomic ioctlは0.399msで復帰、約30.000ms後にout fenceがsignalされました。
これにより、ユーザー空間は全面変換／PCIe DMA／VBlankをioctl内で待たず、完了を
標準sync_fileで受け取れます。
外部PRIME framebufferと通常dumb bufferを交互に120回nonblocking atomic flipする
実機ストレスでは、RGB565が59.85fps、最大ioctl 0.235ms、プロセスCPU 1.3%で
1080p60を実現しました。XRGB8888も120行単位でCPU色変換とPCIe DMAを重ねることで、
従来の30.43fpsから59.84～59.85fpsへ改善しました。変換約9.7～10.8msとDMA約11.2msを
直列に足さず、更新全体を約16.36～16.39msへ収めています。XRGB8888の2回連続試験と
RGB565試験、合計DMA 381回、page flip 375回で全failure／timeout 0を確認しました。
回帰試験は両形式とも50fps未満を失敗として、このパイプライン性能を保護します。

2D用のVRAMは`DRM_IOCTL_TARUGX_VRAM_CREATE`でGEM handleとして確保できます。
DRM coreのhandle所有権とmmap offset検査を通してBAR2へwrite-combine mappingし、
`drm_mm`が画面用2ページより後ろの約1.66MiBだけを管理します。表示ページや他の
VRAM GEMへはmmapできません。実機では1,228,800 byteの作成、先頭／末尾のmmap読書き、
容量不足時の`ENOSPC`、handle解放後の領域再利用をBAR2読戻しまで確認しています。
`DRM_IOCTL_TARUGX_SUBMIT_GEM`では、このhandleを非同期fill/blitの対象にできます。
surface作成時に幅、高さ、pitchを固定し、全矩形をその範囲内に検査します。非同期命令中は
ドライバーがGEM参照を保持するため、アプリが先にhandleやmmapを閉じてもfence完了までは
領域を再割当しません。実機でGEMへのfill→blit、早期close、MSI fence、BAR2読戻し、
完了後の再利用まで確認しています。ドライバー0.6以降はrender nodeを公開し、非master
アプリも自分が所有するVRAM GEMだけを作成してfill/blitできます。画面座標ioctlはrender
nodeから明示的に拒否し、別fdのGEM handleによるsubmitとfence回収もそれぞれ`ENOENT`、
`EPERM`で拒否します。2026-11-04の実機回帰ではrender nodeを含む498命令、120バッチを
render failure 0、プロセスCPU 0.6%で処理し、RGB565 GEMの直接scanoutにも復帰しました。
ドライバー0.7以降は非同期`DRM_IOCTL_TARUGX_SUBMIT_GEM`の`out_syncobj`へ任意のbinary
syncobj handleを指定できます。ドライバーは標準`dma_fence`を設定し、最終コマンドの
完了MSIでsignalします。queue errorやdevice removalではerror付きでsignalします。
`out_syncobj=0`の既存アプリとprivate fence／WAIT ABIもそのまま利用できます。実機では
最初のバッチを`drmSyncobjWait()`だけで回収して次のバッチを投入し、そのprivate WAIT
まで含む498命令をfailure 0で完了しました。syncobjからexportした`sync_file`も
`poll()`で完了を受信できます。
存在しないsyncobj、同期submitとの組合せ、別fdのhandleも拒否します。
ドライバー0.8以降はすべての非同期GEM submitの`dma_fence`を対象GEMのreservationへ
WRITE fenceとして登録します。同じGEMをDRM framebufferへ設定すると`prepare_fb`が
描画完了を自動的に待つため、アプリによる明示WAITなしで安全に直接scanoutできます。
実機では4本のfill直後にmodesetし、implicit fence完了後の`direct_scanouts=1`、
render／DMA failure 0を確認しました。
ドライバー0.9以降は`out_point`へ0以外の値を指定すると、`out_syncobj`へ標準timeline
syncobj pointを追加します。従来の予約領域を再利用してioctl構造体サイズを維持し、
`out_point=0`のbinary syncobj ABIにも互換です。同じsyncobjへpoint 7、11を順に登録した
実機回帰では、両方の`drmSyncobjTimelineWait()`とquery、binary syncobj、sync_file、
private fence、VRAM GEM直接scanoutを併用し、502 commandをfailure 0で完了しました。
ドライバー0.10以降は`DRM_IOCTL_TARUGX_SUBMIT_GEM2`でbinary／timeline syncobjを入力依存に
指定できます。未完了fenceはrender lockを保持せず割り込み可能なsleepで待ち、完了後だけ
FPGAキューへ投入します。2026-11-04の実機では640×480全面fill 4件のpoint 20を入力にした
別batchが5.509ms待ってから実行され、出力point 21をsignalしました。存在しない入力syncobjと
syncobjなしの非0 pointも拒否し、全508 command、render failure 0でした。
ドライバー0.11ではこの待ちを標準DRM schedulerへ移し、ioctlを待たせず依存解決とFPGA投入を
専用workerで行います。schedulerのfinished fenceをGEM reservationと出力syncobjへ公開し、
4 entityでCPUからの投入を受けつつ、hardware credit 1でFPGAアクセスを安全に直列実行します。
1920×1080全面blitのproducer 63本と、最後のpoint 82へ依存するbatchを同時に
queueした実機回帰では、64本目のioctlが0.008～0.009msで復帰し、
software queue peak 64、point 83まで順序どおり完了しました。各2回とも全567 command、
render failure 0です。4 entityの各queueに複数jobを積めるため、64 jobまでの
即時投入を実機確認済みです。
FPGA側の実行は引き続き1 batchずつです。
ドライバー0.12ではscheduler backendがFPGAコマンド投入後に専用の親fenceを返し、
render MSIでこれをsignalする構成にしました。これによりworkerは全面blitの完了まで
占有されません。4 batch依存テスト2回とremove→reprobe、通常表示回帰を実機で通し、
render failure 0、DMA 12回、page flip 8回、全timeout 0を確認しました。
ドライバー0.13では未完了software jobを64本へ上限化し、無制限なメモリ消費を防ぎます。
実機で64本を受付けた直後の65本目が`EBUSY`で拒否され、完了後の通常表示も
DMA 6回、page flip 4回、全timeout 0で通ることを確認しました。

GPU version 1.1以降では、モードと同じ寸法・pixel format・pitchのVRAM GEMを標準DRM
framebufferとして登録し、CPU shadow framebufferへのコピーなしで直接scanoutできます。
atomic checkはVRAM GEMの実寸、pitch、offsetとFPGA機能版を検査し、旧FPGAでは
`EOPNOTSUPP`として従来のDMA/BAR転送だけを使用します。成功回数はPCIデバイスの
`direct_scanouts`属性で確認でき、直接表示では`bytes_uploaded`は増えません。

GPU version 1.1以降は、BAR0の`ACTIVE_FB_BASE`（offset `0x34`）から、scanoutが
フレーム境界で採用した32-bit VRAM先頭アドレスを読み出せます。従来の
`ACTIVE_BUFFER`（offset `0x28`）は固定2ページとの互換用に残します。Linuxドライバーは
version 1.1以降では完全なアドレスでページフリップ完了を確認し、version 1.0では従来の
0/1ページ判定へ自動的に戻ります。これは任意のVRAM GEMを直接scanoutするための
レジスタ境界です。RTLシミュレーションと実機SRAM表示を確認済みです。

実機カーネル向けのコンパイルだけを行う場合と、テストモジュールを一時ロードして
master制限、境界検査、fill/blit、fence/MSI、VRAM読戻しを検証する場合は次を実行します。
後者は表示サービスを一時停止し、終了時には従来モジュールとサービスを復元します。
1080p SRAM実験後も作業版ドライバーとメイン表示を維持する場合は
`--keep-driver`を付けます。

```sh
python3 tools/build_remote_driver.py
python3 tools/test_drm_render_ioctl.py
python3 tools/test_drm_render_ioctl.py --keep-driver
```

Flash版と組み合わせて起動時から現行ドライバーを使う場合は、ホストの実行中カーネル向けに
ビルドして永続インストールします。既存モジュールは初回だけ`tarugx.ko.previous`へ保存され、
配置後に`depmod`とSHA-256照合を行います。

```sh
python3 tools/install_remote_driver.py
```

モジュールビルド、XRGB8888／RGB565のKMS・断片化PRIME・fence・ページフリップ、
2D scheduler・直接scanoutを一括検証する場合は次を実行します。途中の各試験は作業版
ドライバーを維持し、最後に従来ドライバーへ復元します。作業版をそのまま使う場合は
`--keep-driver`を付けます。

```sh
python3 tools/test_drm_integration.py
python3 tools/test_drm_integration.py --keep-driver
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

8要求DMA・DDR書込み8要求パイプライン版の2026-11-03実測では、BAR2が
14.99fps・66.70ms/frame・CPU 77.1%、DMAが59.97fps・16.68ms/frame・
CPU 29.7%、FPGA fillが147.16fps・6.80ms/frame・CPU 14.0%、
queue＋blitが766.50fps・1.30ms/frame・CPU 15.0%
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
検証ビルドは合成成功時にVeryl、生成SystemVerilog、FPGA wrapper、制約、主要生成IP、
bitstream、タイミングレポートのSHA-256を`tarugx_build_manifest.json`へ保存します。
SRAM／Flash書込みスクリプトはこの台帳との完全一致を必須とし、RTLの編集、入力追加、
生成IPやbitstreamの差替え、台帳のない旧出力を検出した場合は書込み前に停止します。
したがってソース変更後は、書込み前に検証ビルドを再実行してください。

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
  部分更新前のページ同期にはFPGAの2D blitを使用します。画面fill/blitはDRM master限定、
  GEM対象fill/blitはrender node対応のprivate ioctlとしてコマンドキュー、fence、MSIまで
  統合済みですが、通常のXorg描画が自動利用する段階ではありません。VRAM GEMの割当とmmap、非同期fence、
  複数コマンド一括投入、GEM handleを対象にしたfill/blit、対応FPGAでのVRAM GEM
  直接scanout、binary／timeline syncobj入出力、標準DRM schedulerによる非同期入力fence待ち、
  64 jobの同時software queueを実機確認済みで、schedulerは4 entity／hardware credit 1です。
  未完了jobの上限も64とし、満杯時は`EBUSY`を返します。
  backendはFPGA完了のMSI fenceを返すため、workerを実行完了まで占有しません。
- Flashリリースと配布済みLinux DRM/KMSは640×480固定です。一方、実験用
  `TARUGX_VIDEO_MODE=1080p60_scanout`構成では、148.4375MHz画素クロックで
  1920×1080・約60HzのDDR scanoutを2026-11-04にSRAM実機確認しました。
  現在のGPU version 1.3はDDR scanoutをRGB565（pitch 3840、4,147,200 byte）にし、
  32bit版に対して表示読出し帯域を半減しています。現在の3:1 urgent調停版の
  合成結果はSetup +0.186ns、Hold +0.133ns、Recovery +0.670ns、Removal
  +0.264nsでした。DRMはRGB565を直接
  DMAできるほか、標準Xorgが作るXRGB8888 coherent GEMをCPUでRGB565 stagingへ
  変換してからDMAします。これによりXfceを1920×1080で表示でき、RGB565直接更新は
  `active_full=46.743ms`、XRGB8888変換経路は`active_full=47.616ms`でした。
  いずれもDMA、flip、VBlank timeoutは0です。
  `tools/test_drm_mode.py --bpp 16`でRGB565を、`--bpp 32`でXRGB8888変換経路を
  強制できます。`--iterations 5 --hold 2`のように指定するとドライバーを再ロードせず
  mode set、DMA更新、ページフリップを連続実行し、累積カウンターからDMA、flip、
  VBlank timeoutを検査できます。2026-11-04の同条件比較ではXRGB8888が変換6.531ms、DMA10.751ms、
  VBlank待ち30.217ms、全体47.510ms、RGB565が変換0ms、DMA17.215ms、
  VBlank待ち29.647ms、全体46.871msでした。MSI周期は実測59.93Hzで、約47msの
  主因はCPU色変換ではなくDDR競合中のページ切替タイミングです。
  framebuffer応答ready経路から不要なDMA state decodeを除去した実機確認では、
  XRGB8888全面更新が`active_full=30.846ms`、DMA失敗、flip timeout、VBlank
  timeoutはいずれも0でした。同じSRAM版の5回連続回帰ではDMA 30回、ページフリップ
  20回を実行し、各失敗・timeoutは0、2回目以降の全面更新は28.210–28.530msでした。
  ホスト側のXRGB8888→RGB565変換は4画素を2組の64-bit並列演算で処理し、64-bitで
  まとめて格納します。同じ実機でウォームアップ後の変換時間を単画素版の約11.2msから
  約7.03msへ約37%短縮しました。2画素／32-bit格納版の約7.5msとの比較でも約6%短縮です。
  VBlank境界に同期するため全面更新時間は約28msのままですが、CPUが変換に拘束される
  時間を約4.2ms削減しています。XRGB8888 7回とRGB565 3回の連続回帰では、DMA失敗、
  flip timeout、VBlank timeoutはいずれも0でした。
  DDR調停器には、下流`ready`を上流へ組合せで戻さない2段コマンドキューを追加しました。
  3:1 scanout優先を維持しながら、待ちのない区間では8要求を8クロック連続発行できます。
  2026-11-04のSRAM実機では、XRGB8888全面更新のDMAが従来の約11.05–11.17msから
  10.44–10.48msへ約6%短縮し、DDR待ちは約66.7万cycleから約57.7万cycleへ減少しました。
  XRGB8888 5回とRGB565 3回の連続回帰はDMA 48回、page flip 32回で失敗・timeout 0、
  さらに2D 498 command、120非同期batch、VRAM GEM直接scanoutも失敗0でした。
  4MiB BAR2の範囲外へCPUフォールバックしないよう、1080pではDMAを必須とします。
  配布済み0.1.0ドライバーへ戻すとGPU 1.2の1080p SRAM版を扱えず黒画面になるため、
  1080p実験中は作業ツリーの0.8.0ドライバーを使用してください。
  全面更新後は2物理ページを未同期として扱い、最初のdamageで非表示ページを全面
  再構築してから変更行を旧表示ページへ反映します。以後は変更された縦範囲だけを
  非表示ページへDMA、VBlank flip、旧表示ページへmirror DMAするため、両ページの
  同一性を維持できます。実機では同期確立に90.456msかかる一方、その後の1行更新は
  10.093msで、同じ試験の全面更新23.226msから約57%短縮しました。80行の初回更新は
  90.492ms、同期後の60行更新も31.733msで全面更新より遅いため、小さいdamage向けの
  最適化です。ただしscanout動作中に60行damageを全面DMAへ昇格する比較では
  83.395ms（DMA busy 6,499,636 cycle）まで悪化しました。DDR競合下では同期済み
  60行部分DMAの31.733msの方が速いため、単純な行数閾値による全面DMA昇格は
  採用していません。さらにscanout FIFOが半分未満の間、scanout読出しを8要求ずつ
  優先するwatermark burst調停も試しましたが、表示中の全面DMAが164.205ms
  （DMA busy 14,663,957 cycle）へ悪化しました。従来round-robinへ戻すと
  64.314–95.812ms（DMA busy 4,320,363–7,471,855 cycle）へ回復し、失敗・flip/VBlank
  timeoutはいずれも0でした。この方式も不採用です。DDR調停器自身を毎クロック
  連続発行できるようにする方式も試しましたが、表示中の全面DMAは
  63.934–95.838ms（DMA busy 4,178,204–7,464,452 cycle）で改善せず、100MHzの
  setup余裕が+0.068nsから+0.001nsへ減少したため不採用としました。
  `tools/test_drm_mode.py`は最後に
  表示中の全面dirtyを行い、`active_full`として継続測定します。
  1080p SRAM実験後も作業ツリードライバーで表示を続ける場合は
  `python3 tools/test_drm_mode.py --keep-driver`を使います。既定では従来どおり試験前の
  モジュールへ戻します。
  2ページの後ろに残るDDR領域は`drm_mm`でVRAM GEMとして管理します。1080pの
  VRAM GEMは4MiB BAR外なのでCPU mmapを`EOPNOTSUPP`にし、FPGAのfill/blitだけで
  内容を生成します。2026-11-04の統合試験では、1920×1080・RGB565 GEMを4本の
  fill commandで生成し、fence/MSI完了後に標準DRM framebufferとしてコピーなしで
  直接scanoutしました。2Dコマンド494件、render failure 0、DMA failure 0、
  flip timeout 0、`direct_scanouts=1`を確認し、120 batchのCPU使用率は0.7%でした。
- PCIe DMAは最大8要求を異なるtagで並列発行し、Completionの到着順に関係なく
  対応するVRAM位置へ書き込みます。各要求は4KiB境界を越えない最大64 DWORDです。
  Completion TLPの先頭5 DWORDと後続8 DWORDチャンクは、EOPまで512-bitバッファで
  32-byte境界へ再パックします。典型的な64 DWORD TLPのDDR書込みは16回から8回へ
  半減します。2026-11-04の1,228,800-byte実機測定では、scanout停止時のDMA busyが
  173,192–175,995 cycle（1.73–1.76ms、約700MB/s）、scanout中は
  676,848–679,282 cycle（6.77–6.79ms）でした。1080p表示中の全面更新では
  DMA busyが従来約4.29M cycleから3.94–4.01M cycleへ約7–8%減り、失敗と
  flip/VBlank timeoutはいずれも0でした。診断DMAの固定VRAM位置は640x480の2ページ
  直後にあるため、1080pではscanoutと重なります。ドライバーは1080pで`dma_test`を
  `EOPNOTSUPP`として拒否し、`tools/benchmark_dma_test.py`も書込み前に停止します。
  640x480版では同ツールで表示ON/OFFを同条件測定でき、表示制御は例外時も元へ戻します。
  32-byte境界では8 DWORDをまとめて256-bit DDR書込みし、非整列部と端数は
  64/32-bit書込みへフォールバックします。Completionから生成したDDR書込みは
  8要求までパイプライン化し、PCIe、DDR間のクロック境界も8段FIFOで接続しています。
  scanout FIFOが半分以上埋まっている間だけ非表示側DDR要求を最大4回まとめる
  FIFO-aware調停も追加しました。不足時は従来のround-robinへ即座に戻ります。
  2026-11-04の1080p SRAM実機測定では、安定位相が64.15–64.35ms、DMA busy
  3.94–3.95M cycleで従来値と同等でした。速度向上は確認できませんでしたが、
  2D fill/blit、4-command batch、fence/MSI、120 batch（480 command）は失敗0、
  scanout underflowとDMA/flip/VBlank timeoutも0でした。100MHzの最小setup余裕は
  +0.104nsです。
  その後FR17 UART診断で、round-robin版はFIFO最小水位1 beat、最大scanout待ち
  72 cycleで実際にunderflowしていたことを確認しました。FIFOが半分未満の間だけ
  scanoutをCPU/DMAに対して3:1で発行する版では、起動後の最小水位249 beatを維持し、
  最大待ち81 cycleでもunderflow 0、fault 0でした。XRGB8888のDRM全面更新でも
  DMA 6回、page flip 4回、各timeout 0のまま`active_full=30.889ms`となり、従来の
  47.510msから約35%短縮しました。8要求連続優先版と異なり、3要求ごとにCPU/DMAへ
  1要求を返すため、表示保護と転送進行を両立します。
  旧2要求版を使った2026-11-03のSRAM実機回帰では、1,228,800 byteのDMA転送5回が13.964–14.126ms
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
