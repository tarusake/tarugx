# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
create_clock -name sys_clk -period 20 -waveform {0 10} [get_ports {sys_clk_p}]
# Match the official standard-Dock constraint at the dedicated divider output.
create_clock -name tlp_clk -period 10 -waveform {0 5} [get_pins {u_tlp_div2/CLKOUT}]
# Only the first synchronizer stages are asynchronous. Keep stage 1->2 timed.
set_false_path -to [get_pins {u_display_cdc/ack_sync_0_s0/D}]
set_false_path -to [get_pins {u_display_cdc/request_sync_0_s0/D}]
# Bundled data must settle before the synchronized request can be consumed
# (at least two pixel periods). Bound routing to less than one pixel period.
set_max_delay 20 -from [get_cells {u_display_cdc/held*}] -to [get_cells {u_display_cdc/dst_config*}]
