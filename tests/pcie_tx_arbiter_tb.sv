// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
module pcie_tx_arbiter_tb;
  logic clk = 0;
  logic rst_n = 0;
  always #5 clk = ~clk;
  logic completion_valid, completion_ready, completion_sop, completion_eop;
  logic [7:0] completion_keep;
  logic [255:0] completion_data;
  logic dma_valid, dma_ready, dma_sop, dma_eop;
  logic [7:0] dma_keep;
  logic [255:0] dma_data;
  logic tx_wait, tx_sop, tx_eop;
  logic [7:0] tx_valid;
  logic [255:0] tx_data;

  tarugx_pcie_tx_arbiter dut (.*);

  task automatic check(input logic condition, input string message);
    if (!condition) $fatal(1, "%s", message);
  endtask
  task automatic tick; @(posedge clk); #1; endtask

  initial begin
    completion_valid = 0; completion_sop = 1; completion_eop = 1;
    completion_keep = 8'h0f; completion_data = 256'hc0de;
    dma_valid = 0; dma_sop = 1; dma_eop = 1;
    dma_keep = 8'h1f; dma_data = 256'hd00d;
    tx_wait = 0;
    repeat (2) tick(); rst_n = 1; tick();
    #1;
    check(tx_valid == 0 && !completion_ready && !dma_ready, "idle mismatch");

    dma_valid = 1;
    #1;
    check(tx_valid == 8'h1f && tx_data == 256'hd00d && dma_ready,
          "DMA-only routing mismatch");

    tx_wait = 1;
    #1;
    check(tx_valid == 8'h1f && tx_data == 256'hd00d && !dma_ready,
          "DMA packet must remain visible while stalled");
    tick();

    completion_valid = 1;
    #1;
    check(tx_valid == 8'h1f && tx_data == 256'hd00d
          && !completion_ready && !dma_ready,
          "selected DMA packet must remain stable while stalled");

    tx_wait = 0;
    #1;
    check(!completion_ready && dma_ready && tx_data == 256'hd00d,
          "stalled DMA acceptance mismatch");
    tick();

    #1;
    check(completion_ready && !dma_ready && tx_data == 256'hc0de,
          "Completion must get priority after the locked packet");

    completion_valid = 0;
    #1;
    check(dma_ready && tx_data == 256'hd00d,
          "DMA must proceed after Completion");

    dma_valid = 0;
    #1;
    check(tx_valid == 0 && !dma_ready, "return to idle mismatch");
    $display("PCIe TX arbiter: Completion priority and TX stalls PASS");
    $finish;
  end
endmodule
