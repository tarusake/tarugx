# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
create_clock -name sys_clk -period 20 [get_ports {sys_clk_p}]
# SUG940 4.1.2: derive the OSC clock from its primitive configuration.
# Avoid rounding its period differently from the device timing model.
create_clock -name tlp_clk -period 10 [get_pins {u_tlp_div2/CLKOUT}]
create_clock -name ddr400 -period 2.5 [get_nets {u_ddr/memory_clk}]
create_clock -name ui100 -period 10 [get_pins {u_ddr/u_ddr3/gw3_top/u_ddr_phy_top/fclkdiv/CLKOUT}]
# The official Rev.B cam2dvi TMDS_60HZ.sdc declares its DDR PHY, UI and
# initialization clocks asynchronous. Apply those IP clock-pair exceptions,
# without excluding the CPU/UI/pixel mailbox and FIFO crossings below.
set_false_path -from [get_clocks {ui100}] -to [get_clocks {ddr400}]
set_false_path -from [get_clocks {ddr400}] -to [get_clocks {ui100}]
set_false_path -from [get_clocks {sys_clk}] -to [get_clocks {ui100}]
set_false_path -from [get_clocks {ui100}] -to [get_clocks {sys_clk}]
set_false_path -from [get_clocks {sys_clk}] -to [get_clocks {ddr400}]
set_false_path -from [get_clocks {ddr400}] -to [get_clocks {sys_clk}]
# Mailbox toggle first stages only; subsequent synchronizer stages remain timed.
set_false_path -to [get_pins {u_display_cdc/ack_sync_0_s0/D u_display_cdc/request_sync_0_s0/D}]
set_false_path -to [get_pins {u_memory_config_cdc/ack_sync_0_s0/D u_memory_config_cdc/request_sync_0_s0/D}]
set_false_path -to [get_pins {u_active_buffer_cdc/ack_sync_0_s0/D u_active_buffer_cdc/request_sync_0_s0/D}]
set_false_path -to [get_pins {u_vblank_toggle_cdc/ack_sync_0_s0/D u_vblank_toggle_cdc/request_sync_0_s0/D}]
set_false_path -to [get_pins {u_scanout/display_active_sync_0_s0/D}]
set_max_delay 10 -from [get_pins {u_scanout/display/fault_snapshot*/Q}] -to [get_pins {debug_fault*/D}]
set_false_path -to [get_pins {status_meta*/D}]
set_false_path -to [get_pins {debug_aux_meta*/D sys_toggle_sync_0_s0/D}]
set_false_path -to [get_pins {u_pcie_debug/flags_meta*/D u_pcie_debug/beat_sync_0_s0/D}]
set_false_path -to [get_pins {u_pcie_debug/snapshot_cdc/ack_sync_0_s0/D u_pcie_debug/snapshot_cdc/request_sync_0_s0/D}]
set_max_delay 10 -from [get_cells {u_pcie_debug/snapshot_cdc/held*}] -to [get_cells {u_pcie_debug/snapshot_cdc/dst_config*}]
set_false_path -to [get_pins {pcie_debug_reset*/CLEAR}]
# These registers implement asynchronous assertion / synchronous release.
# The final release register to functional RESET/CLEAR pins remains timed.
set_false_path -to [get_pins {cpu_reset*/CLEAR mem_reset*/CLEAR pixel_reset*/CLEAR}]
# OSER10 RESET follows the vendor reference PHY and is a pixel-clock-registered
# asynchronous primitive control.  Exclude only that control route; PCLK/FCLK
# and all encoded video data paths remain fully timed.
set_false_path -to [get_pins {u_dvi/*dvi_tx_tmds_phy_inst/tmds_serdes_inst0/RESET}]
# Bundled payloads settle before the synchronized toggles can be consumed.
set_max_delay 10 -from [get_cells {u_display_cdc/held*}] -to [get_cells {u_display_cdc/dst_config*}]
set_max_delay 10 -from [get_cells {u_memory_config_cdc/held*}] -to [get_cells {u_memory_config_cdc/dst_config*}]
set_max_delay 10 -from [get_cells {u_active_buffer_cdc/held*}] -to [get_cells {u_active_buffer_cdc/dst_config*}]
set_max_delay 10 -from [get_cells {u_vblank_toggle_cdc/held*}] -to [get_cells {u_vblank_toggle_cdc/dst_config*}]
# Request/response CDC now uses asynchronous FIFOs. Bound both Gray-pointer
# crossings by the faster clock period; FIFO payload storage is dual-clock RAM.
set_max_delay 10 -from [get_cells {u_memory_path/cdc/requests/wp_sclk_gray_ff*}] -to [get_cells {u_memory_path/cdc/requests/u_synchronizer_wp/*rg_0_s0}]
set_max_delay 10 -from [get_cells {u_memory_path/cdc/requests/rp_dclk_gray_ff*}] -to [get_cells {u_memory_path/cdc/requests/u_synchronizer_rp/*rg_0_s0}]
set_max_delay 10 -from [get_cells {u_memory_path/cdc/responses/wp_sclk_gray_ff*}] -to [get_cells {u_memory_path/cdc/responses/u_synchronizer_wp/*rg_0_s0}]
set_max_delay 10 -from [get_cells {u_memory_path/cdc/responses/rp_dclk_gray_ff*}] -to [get_cells {u_memory_path/cdc/responses/u_synchronizer_rp/*rg_0_s0}]
# Bound Gray-pointer crossing delay by the faster clock period, without a
# blanket asynchronous-clock exception that could hide these constraints.
set_max_delay 10 -from [get_cells {u_scanout/fifo/fifo_0/write_gray_?_s0}] -to [get_cells {u_scanout/fifo/fifo_0/write_gray_dst_meta_?_s0}]
set_max_delay 10 -from [get_cells {u_scanout/fifo/fifo_0/read_gray_?_s0}] -to [get_cells {u_scanout/fifo/fifo_0/read_gray_src_meta_?_s0}]
report_timing -setup -max_paths 100 -max_common_paths 1
