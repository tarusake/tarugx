# TaruGX/100

Veryl HDLで開発する、Tang Mega 138K通常Dock版向けの実験用PCIe GPUです。

このコミットは開発途中の機能単位です。最新の機能・実機検証範囲は最新コミットのREADMEを参照してください。

## RTLの確認

VerylとVerilatorを用意します。

```sh
veryl check
veryl build
veryl test --sim verilator
```

Gowin実機ビルドには、ライセンス認証済みのEDAと対象ボード向けのベンダーIPが別途必要です。
生成物や接続情報はGit管理外です。

Linuxドライバー: `make -C drivers/tarugx`（対象カーネルのヘッダーが必要）。
