# TaruGX/100 ライセンス

Copyright © 2026 tarusake

本リポジトリでは、以下の適用範囲に従って各ライセンスを適用します。
個別ファイルの第三者の権利表示・ライセンスは保持し、それらを本プロジェクトの
基本ライセンスで置き換えるものではありません。

| 対象 | ライセンス |
| --- | --- |
| 下記の例外を除く自作RTL、ラッパー、UEFIコード、ツール、テスト、設定、文書 | Solderpad Hardware License 2.1 |
| `drivers/tarugx/`の自作Linuxドライバーと関連ビルドファイル | GPL-2.0-only |
| `assets/branding/`内のロゴ・キャラクター画像、およびその変換画像データ | CC BY 4.0（保有する権利の範囲） |
| Gowin提供IP、生成物中のGowin提供部分 | 対応するGowinの利用・配布条件 |
| その他の第三者由来部分 | 各権利者の元のライセンス・利用条件 |

## 自作コード・設計：Solderpad Hardware License 2.1

上記の基本ライセンス対象を、Solderpad Hardware License version 2.1の条件で提供します。
利用者は、同ライセンスに定められているとおり、Apache License version 2.0を
選択して利用することもできます。

```text
SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
```

Solderpad 2.1はApache 2.0に追加・変更を加える形式です。両方の本文を参照してください。

- [Solderpad 2.1本文](LICENSES/SHL-2.1.txt)／[公式サイト](https://solderpad.org/licenses/SHL-2.1/)
- [Apache 2.0本文](LICENSES/Apache-2.0.txt)／[公式サイト](https://www.apache.org/licenses/LICENSE-2.0)

## Linuxドライバー：GPL-2.0-only

`drivers/tarugx/`の自作部分はGNU General Public License version 2のみの条件で提供します。
既存の`SPDX-License-Identifier: GPL-2.0-only`表記を維持します。

- [GPL version 2本文](LICENSES/GPL-2.0-only.txt)

## ロゴ・キャラクター：CC BY 4.0

原画はtarusake本人が制作したオリジナルイラストです。
そのイラストを基に、生成AIを使用してTaruGX/100のロゴを制作しました。
**tarusakeが保有する著作権の範囲で、Creative Commons Attribution 4.0 International
（CC BY 4.0）により提供します。**

対象には`assets/branding/*.png`と、これらから生成した画像データ
（`uefi/boot_logo_data.h`内の画像・パレットを含む）を含めます。
変換・表示プログラム自体は基本ライセンスの対象です。
非公開の原画ファイルを、本リポジトリの公開によって配布するものではありません。

クレジットの例：

> TaruGX/100 logo — tarusake. Based on an original illustration by tarusake,
> created with generative AI. Licensed under CC BY 4.0.
> https://creativecommons.org/licenses/by/4.0/

利用時にはCC BY 4.0に従い、適切なクレジット、ライセンスへのリンク、
変更した場合の変更表示などを行ってください。表示方法は利用媒体に応じた合理的な方法で構いません。
権利が成立しない部分に、この表示によって新たな権利や制限を設けるものではありません。
この許諾は、tarusakeやTaruGXによる公式の承認・推奨を示すものではなく、
商標の使用権を付与するものでもありません。

- [CC BY 4.0本文](LICENSES/CC-BY-4.0.txt)
- [日本語の説明](https://creativecommons.org/licenses/by/4.0/deed.ja)
- [日本語のリーガルコード](https://creativecommons.org/licenses/by/4.0/legalcode.ja)

## Gowin IP・第三者由来部分

GowinのPCIe、DDR3、PLL、映像関連などの提供IPや、生成物に含まれるGowinのコードについて、
本プロジェクトはSolderpadその他のライセンスによる再許諾を行いません。
各バージョン・配布物に適用されるGowinの利用・配布条件に従ってください。
自作ラッパーが公開されていても、接続先IPの再配布を許可するものではありません。

Sipeed公式サンプルなどに由来する部分も、元の権利表示と条件を維持します。
第三者の条件が不明な部分について、本ファイルから追加の利用許諾を推定しないでください。
GNU-EFIなどをリンクした配布物、または第三者IPを含むビットストリームの配布では、
含まれる各部分の条件も確認する必要があります。

## 実験用・無保証

TaruGX/100は実験・研究・学習のための試作プロジェクトです。
動作、性能、安全性、特定用途への適合性を保証しません。
無保証および責任制限の法的な条件は、それぞれのライセンス本文に従います。
「実験用」という説明は、ライセンスが許す商用利用などを追加で制限するものではありません。
