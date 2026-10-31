if {![info exists ::env(SIPEED_PCIE_GEN2_DIR)]} {
    error "Set SIPEED_PCIE_GEN2_DIR to the extracted official Gen2 sample"
}

set sample_dir [file normalize $::env(SIPEED_PCIE_GEN2_DIR)]
set project [file join $sample_dir pcie_dma_demo.gprj]
if {![file exists $project]} { error "Official project not found: $project" }

open_project $project
set_csr [file join $sample_dir src serdes serdes.csr]
run all
exit
