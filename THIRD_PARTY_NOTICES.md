# 第三者由来部分と外部依存

自作部分のライセンスは[LICENSE.md](LICENSE.md)を参照してください。
以下は公開ソースに含む部分と、ビルド時に別途必要なものを区別した記録です。

## Sipeed公式サンプル

出典は通常Dock版の[TangMega-138K-example](https://github.com/sipeed/TangMega-138K-example)です。
照合に使用した版は`06e7d8b118d345915ab6f257b7c22226f81575cd`です。
その版の[LICENSE](https://github.com/sipeed/TangMega-138K-example/blob/06e7d8b118d345915ab6f257b7c22226f81575cd/LICENSE)
はApache-2.0で、末尾の権利表示を含めて[原文を保存](LICENSES/Sipeed-Apache-2.0.txt)しています。

> Copyright [2024] [Shenzhen Sipeed Technology Co,Ltd.]

| 本リポジトリの部分 | 出典（上記リポジトリ内） | TaruGXでの変更・扱い |
| --- | --- | --- |
| `fpga/tang_mega_138k/dvi_tx_tmds_phy.sv` | `hdmi_colorbar/eda_proj/src/dvi-tx/dvi_tx_tmds_phy.v` | OSER10のリセット解除を高速クロックで同期化。Apache-2.0を維持 |
| `fpga/tang_mega_138k/dvi_tx_tmds_enc.sv` | `hdmi_colorbar/eda_proj/src/dvi-tx/dvi_tx_tmds_enc.v` | 148MHz向けにq_m生成とディスパリティ計算をパイプライン化。Apache-2.0を維持 |
| `fpga/tang_mega_138k/Pins.cst` | `pcie_dma_demo/pcie_gen2(5G)/src/Pins.cst`および`hdmi_colorbar`のHDMI0配線 | 必要な信号に絞り、UART・HDMIを統合。Apache-2.0を維持 |
| `vendor/gowin/pcie_x1/serdes.mod` | `pcie_dma_demo/pcie_gen2(5G)/src/serdes/serdes.mod` | x1化、Rev.B用デバイス設定、相対出力パスへ変更した生成設定 |

サンプル内のGowin提供IPまで一律にApache-2.0へ再許諾するものではありません。
PCIe、DDR3、PLL、DVIエンコーダーなど、スクリプトが外部サンプルから参照する
ソースは本リポジトリに同梱していません。各ファイルの権利表示・提供条件も確認してください。

## Gowin EDA・IP

[公式PCIe IP情報](https://www.gowinsemi.com/en/support/ip_detail/128/)を参照してください。
`vendor/gowin/`の`.ipc`、`.mod`、`define.vh`はIP生成・構成用の入力です。
これらを置くことは、Gowinの実装ソースや暗号化IPの再配布許諾を意味しません。
自作RTLとIP接続ラッパーを除くGowin提供部分は、使用するEDA・IPに付属する条件が適用されます。
バージョンをまたいだ配布許諾は未確認です。IPを含むビットストリームを配布する場合も、
使用した配布物の条件を確認してください。ライセンスファイルや生成されたIP本体は同梱していません。

## GNU-EFI

UEFIコードは[GNU-EFI](https://sourceforge.net/projects/gnu-efi/)のヘッダーを使用し、
`crt0-efi-x86_64.o`、`libefi`、`libgnuefi`をリンクします。
確認に使用したパッケージは`gnu-efi 3.0.15-1build1`（amd64）です。
パッケージの[著作権・ライセンス一覧](LICENSES/GNU-EFI-copyright.txt)を原文のまま保存しています。
BSD系など複数の条件がファイル単位で適用されるため、GNU-EFI全体を単一のライセンスとは扱いません。
一覧には本プロジェクトで使用しないアーキテクチャ向けファイルの条件も含まれます。

GNU-EFI本体とリンク済みEFI/ROMは公開ソースに同梱していません。
リンク済みバイナリを配布する場合には、実際に組み込んだ版の各権利表示・条件を添付してください。
自作GOPのSolderpad表記はGNU-EFI部分の条件を置き換えません。

## 実行時の外部ライブラリ

- `tools/capture_uart_d2xx.py`は[FTDI D2XX](https://ftdichip.com/drivers/d2xx-drivers/)を動的に読み込みます。ライブラリは別途取得し、その配布物の条件に従ってください。
- `tools/ftdi-driver.py`は[libusb](https://libusb.info/)を動的に読み込みます。libusb本体は同梱していません。

ビルドツールや外部ライブラリのライセンスを、自作の呼出しコードに付けたSPDX表記で変更するものではありません。
