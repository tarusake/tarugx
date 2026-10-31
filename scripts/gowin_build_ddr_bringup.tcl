# Official sample directories must be supplied by the caller.
proc tarugx_required_dir {name} {
    if {![info exists ::env($name)] || $::env($name) eq ""} {
        error "Set $name to the official sample directory"
    }
    if {![file isdirectory $::env($name)]} {
        error "$name must name an existing directory"
    }
    return [file normalize $::env($name)]
}
set repo [file normalize [file join [file dirname [info script]] ..]]
set sample [tarugx_required_dir SIPEED_CAM_DIR]
if {[info exists ::env(SIPEED_CAM_DIR)]} { set sample [tarugx_required_dir SIPEED_CAM_DIR] }
set out [file join $repo build ddr_bringup]
file mkdir $out
# Extract only the official DDR signal constraints; retain our known reset/UART pins.
set f [open [file join $sample 138K_DOCK.cst] r]
set text [read $f]; close $f
set f [open [file join $out ddr.cst] w]
foreach line [split $text "\n"] {
 if {[regexp {^IO_(LOC|PORT)\s+"ddr_} $line]} { puts $f $line }
}
puts $f {IO_LOC "clk" V22;
IO_PORT "clk" IO_TYPE=LVCMOS33 BANK_VCCIO=3.3;
IO_LOC "rst_n" AB13;
IO_PORT "rst_n" PULL_MODE=UP BANK_VCCIO=3.3;
IO_LOC "uart_tx" U15;
IO_PORT "uart_tx" IO_TYPE=LVCMOS33 DRIVE=8 BANK_VCCIO=3.3;}
close $f
set f [open [file join $out ddr.sdc] w]
puts $f {create_clock -name sys50 -period 20 [get_ports {clk}]
create_clock -name ddr400 -period 2.5 [get_nets {memory_clk}]
create_clock -name ui100 -period 10 [get_pins {u_ddr3/gw3_top/u_ddr_phy_top/fclkdiv/CLKOUT}]
set_false_path -to [get_pins {status_meta*/D}]}
close $f
create_project -name ddr_bringup -dir $out -pn GW5AST-LV138PG484AC2/I1 -device_version B -force
foreach source {gowin_pll/gowin_pll_400M.v gowin_pll/gowin_pll_400M_mod.v pll_init.v} {
 add_file [file join $sample $source]
}
add_file [file join $repo build gowin_ddr3_b DDR3MI.vg]
foreach source {memory/memory_self_test.sv memory/ddr3_native_adapter.sv debug/uart_tx.sv debug/ddr_debug_uart.sv} {
 add_file [file join $repo build $source]
}
add_file [file join $repo fpga tang_mega_138k ddr_bringup_top.sv]
add_file [file join $out ddr.cst]
add_file [file join $out ddr.sdc]
set_option -top_module ddr_bringup_top
set_option -verilog_std sysv2017
set_option -use_sspi_as_gpio 1
set_option -use_cpu_as_gpio 1
set_option -bit_compress 1
set_option -loading_rate 105.000
set_option -user_code 54474433
run all
exit
