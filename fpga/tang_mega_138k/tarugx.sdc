create_clock -name sys_clk -period 20 -waveform {0 10} [get_ports {sys_clk_p}]
# Match the official standard-Dock constraint at the dedicated divider output.
create_clock -name tlp_clk -period 10 -waveform {0 5} [get_pins {u_tlp_div2/CLKOUT}]
