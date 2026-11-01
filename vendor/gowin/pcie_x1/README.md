# Gowin PCIe x1生成入力

すべてのコマンドはリポジトリのルートで実行してください。
`GOWIN_ROOT`はGowin EDAのインストール先です（`IDE/`を含むディレクトリ）。

`serdes.mod`はSipeed通常Dock版のGen2サンプルを基に、Q0 Lane 0のみの
x1構成に変更したModule Generator入力です。出力先`./build/gowin_pcie_x1/`は
**実行ディレクトリからの相対パス**で、Git管理外です。

Gowin EDA V1.9.12.03で構造ラッパーを生成する例：

```sh
: "${GOWIN_ROOT:?Set GOWIN_ROOT to your Gowin EDA installation}"
mkdir -p build/gowin_pcie_x1
"$GOWIN_ROOT/IDE/bin/GowinModGen" -do vendor/gowin/pcie_x1/serdes.mod
```

`.mod`にはPCIe Configuration Spaceの設定は含まれません。
BAR、ID、Class Code、速度、TLPクロックはPCIe Controller側の設定も必要です。
`pcie_controller.ipc`はGen2 x1、TLPクロック100MHz、BAR0 64KiB
non-prefetchable、BAR2 4MiB prefetchable、Class `03/80`、
暫定ID `22c2:1100`の設定入力です。

次のスクリプトは既定で`serdes.ipc`を読み、`SerDes_Top`を生成します。
`pcie_controller.ipc`を直接生成するコマンドではありません。

```sh
: "${GOWIN_ROOT:?Set GOWIN_ROOT to your Gowin EDA installation}"
"$GOWIN_ROOT/IDE/bin/gw_sh" scripts/gowin_generate_pcie.tcl
```

入力を変更する場合は`TARUGX_PCIE_IPC`を指定します。
生成物は設定と合成結果を確認するまで検証済みとは扱いません。
出典・第三者の条件は[THIRD_PARTY_NOTICES.md](../../../THIRD_PARTY_NOTICES.md)を参照してください。
