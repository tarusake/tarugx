// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
module pcie_rx_router_tb;
  logic clk=0,rst_n=0;always #5 clk=~clk;
  logic rx_sop, rx_eop;
  logic [7:0] rx_valid;
  logic [255:0] rx_data;
  logic bar_wait, dma_wait, rx_wait;
  logic bar_rx_sop, bar_rx_eop;
  logic [7:0] bar_rx_valid;
  logic dma_rx_sop, dma_rx_eop;
  logic [7:0] dma_rx_valid;
  logic is_completion;

  tarugx_pcie_rx_router dut (.*);

  task automatic check(input logic condition, input string message);
    if (!condition) $fatal(1, "%s", message);
  endtask
  task automatic settle; #1; endtask
  task automatic tick; @(posedge clk);#1; endtask

  initial begin
    rx_sop = 0; rx_eop = 0; rx_valid = 0; rx_data = 0;
    bar_wait = 0; dma_wait = 0; settle();
    check(!is_completion && !rx_wait && bar_rx_valid == 0 && dma_rx_valid == 0,
          "idle mismatch");
    repeat(2)tick();@(negedge clk);rst_n=1;

    // A normal Memory request is visible only to the BAR path.
    rx_sop = 1; rx_eop = 1; rx_valid = 8'he0;
    rx_data[255:248] = 8'h00; bar_wait = 1; dma_wait = 0; settle();
    check(!is_completion && bar_rx_sop && bar_rx_eop
          && bar_rx_valid == 8'he0 && dma_rx_valid == 0 && rx_wait,
          "BAR request routing mismatch");

    // CplD is isolated from BAR even when DMA does not apply backpressure.
    rx_data[255:248] = 8'h4a; bar_wait = 1; dma_wait = 0; settle();
    check(is_completion && dma_rx_sop && dma_rx_eop
          && dma_rx_valid == 8'he0 && bar_rx_valid == 0 && !rx_wait,
          "CplD routing mismatch");

    dma_wait = 1; settle();
    check(rx_wait && bar_rx_valid == 0, "DMA Completion backpressure mismatch");

    // Cpl without data follows the same route.
    rx_data[255:248] = 8'h0a; rx_valid = 8'he0; dma_wait = 0; settle();
    check(is_completion && dma_rx_valid == 8'he0 && bar_rx_valid == 0,
          "Cpl routing mismatch");

    // Other packet types never leak into the DMA channel.
    rx_data[255:248] = 8'h40; rx_valid = 8'hf0; bar_wait = 0; settle();
    check(!is_completion && bar_rx_valid == 8'hf0 && dma_rx_valid == 0,
          "non-Completion classification mismatch");

    // Every continuation beat follows the route selected by the accepted SOP,
    // regardless of payload bits that resemble another TLP type.
    @(negedge clk);rx_sop=1;rx_eop=0;rx_valid=8'hff;
    rx_data[255:248]=8'h4a;dma_wait=1;settle();
    check(is_completion&&rx_wait,"stalled Completion SOP mismatch");
    tick();check(is_completion,"stalled SOP must not advance route state");
    @(negedge clk);dma_wait=0;tick();
    @(negedge clk);rx_sop=0;rx_data[255:248]=8'h00;settle();
    check(is_completion&&dma_rx_valid==8'hff&&bar_rx_valid==0,
          "Completion continuation route mismatch");
    tick();@(negedge clk);rx_eop=1;rx_valid=8'h0f;settle();
    check(is_completion&&dma_rx_eop,"Completion EOP route mismatch");
    tick();@(negedge clk);rx_sop=1;rx_eop=1;rx_valid=8'hf0;
    rx_data[255:248]=8'h40;settle();
    check(!is_completion&&bar_rx_valid==8'hf0,
          "route must release after Completion EOP");

    rx_valid = 0; bar_wait = 1; dma_wait = 1; settle();
    check(!rx_wait, "empty transfer must not be stalled");
    $display("PCIe RX router: BAR/Completion isolation and backpressure PASS");
    $finish;
  end
endmodule
