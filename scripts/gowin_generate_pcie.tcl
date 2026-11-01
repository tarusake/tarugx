# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
# Generate the TaruGX PCIe controller from its checked-in IPC configuration.
set project_root [file normalize [file join [file dirname [info script]] ..]]
set output_dir [file join $project_root .build gowin_ipgen]
if {[info exists ::env(TARUGX_PCIE_IPC)]} {
    set ipc_file [file normalize $::env(TARUGX_PCIE_IPC)]
} else {
    set ipc_file [file join $project_root vendor gowin pcie_x1 serdes.ipc]
}

create_project \
    -name tarugx_ipgen \
    -dir $output_dir \
    -pn GW5AST-LV138PG484AC2/I1 \
    -device_version B \
    -force

read_ipc $ipc_file
generate_target [get_ips SerDes_Top]
exit
