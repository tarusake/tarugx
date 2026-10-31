# Gowin PCIe x1 generation input

`serdes.mod` is a headless Gowin Module Generator input derived from Sipeed's
standard-Dock Gen2 example. It changes the generated SerDes wrapper to Q0 Lane 0
and x1 only. The absolute output path is intentionally under the ignored
`build/` directory.

Generate the structural SerDes wrapper with Gowin EDA V1.9.12.03:

```sh
mkdir -p build/gowin_pcie_x1
"$GOWIN_HOME/IDE/bin/GowinModGen" -do vendor/gowin/pcie_x1/serdes.mod
```

This `.mod` file does not contain PCIe configuration-space parameters. BAR
sizes, IDs, class code, speed, and TLP clock must also be configured in the
PCIe Controller IP and archived as an `.ipc` file. Generated sources are not
treated as verified until their configuration and synthesis result are checked.

`pcie_controller.ipc` contains the current TaruGX proposal: Gen2 x1, 100 MHz
TLP clock, BAR0 64 KiB non-prefetchable, BAR2 4 MiB prefetchable, class
`03/80`, and the Sipeed example IDs `22c2:1100`. The IDs remain provisional.
Generate it headlessly with:

```sh
gw_sh scripts/gowin_generate_pcie.tcl
```
