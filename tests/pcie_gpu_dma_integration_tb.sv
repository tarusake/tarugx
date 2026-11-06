// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module pcie_gpu_dma_integration_tb;
  logic clk = 0, rst = 0;
  always #5 clk = ~clk;
  logic [15:0] completer_id = 16'h0100;
  logic rx_sop = 1, rx_eop = 1;
  logic [7:0] rx_valid = 0, rx_error = 0;
  logic [255:0] rx_data = 0;
  logic [5:0] rx_bar_decode = 0;
  logic rx_wait, rx_masknp, tx_wait = 1, tx_sop, tx_eop;
  logic [7:0] tx_valid;
  logic [255:0] tx_data;
  logic framing_error, unsupported, malformed, fb_commit_pulse;
  logic active_buffer = 0, vblank_toggle = 0;
  logic [1:0] irq_enable;
  logic fill_busy = 0, fill_done = 0, fill_error = 0, fill_start;
  logic [127:0] fill_command;
  logic blit_busy = 0, blit_done = 0, blit_error = 0, blit_start;
  logic [159:0] blit_command;
  logic triangle_busy = 0, triangle_done = 0, triangle_error = 0, triangle_start;
  logic [31:0] triangle_writes = 0;
  logic [735:0] triangle_command;
  logic queue_ready = 0, queue_active = 0, queue_error = 0;
  logic queue_submit, queue_clear_error;
  logic [2:0] queue_level = 0;
  logic [31:0] queue_completed = 0, queue_error_fence = 0;
  logic [769:0] queue_command;
  logic [31:0] display_ctrl, display_color, display_mode, fb_base, fb_stride;
  logic [31:0] active_fb_base = 0;
  logic fb_req_valid, fb_req_ready = 0, fb_req_write;
  logic fb_rsp_valid = 0, fb_rsp_ready, fb_rsp_error = 0;
  logic [31:0] fb_req_addr, fb_rsp_rdata = 0;
  logic [255:0] fb_req_wdata;
  logic [31:0] fb_req_wstrb;
  logic [7:0] dma_tag;

  tarugx_pcie_gpu_path dut (.*);

  task automatic tick; @(posedge clk); #1; endtask
  task automatic check(input logic condition, input string message);
    if (!condition) $fatal(1, "%s", message);
  endtask
  task automatic bar0_write(input logic [15:0] address, input logic [31:0] value);
    @(negedge clk);
    rx_valid = 8'hf0; rx_bar_decode = 6'b000001;
    rx_data = {32'h40000001, 32'h12345a0f, {16'b0, address},
      value[7:0], value[15:8], value[23:16], value[31:24], 128'b0};
    tick();
    @(negedge clk); rx_valid = 0;
    tick();
  endtask
  function automatic logic [31:0] wire_word(input logic [31:0] value);
    return {value[7:0], value[15:8], value[23:16], value[31:24]};
  endfunction
  task automatic accept_dma_write(input logic [31:0] address,
                                  input logic [63:0] value);
    while (!fb_req_valid) tick();
    check(fb_req_write && fb_req_addr == address
          && fb_req_wdata[63:0] == value && fb_req_wdata[255:64] == 0
          && fb_req_wstrb == 32'hff,
          "integrated DMA VRAM write mismatch");
    @(negedge clk); fb_req_ready = 1; tick();
    @(negedge clk); fb_req_ready = 0; fb_rsp_valid = 1; tick();
    @(negedge clk); fb_rsp_valid = 0;
  endtask

  initial begin
    tick();
    @(negedge clk); rst = 1;

    bar0_write(16'h00c0, 32'h00001020);
    bar0_write(16'h00c4, 32'h12340000);
    bar0_write(16'h00c8, 32'h00004000);
    bar0_write(16'h00cc, 32'd8);
    bar0_write(16'h00d0, 32'd1);

    while (tx_valid == 0) tick();
    check(tx_valid == 8'hf0 && tx_sop && tx_eop
          && tx_data[255:224] == 32'h20000002,
          "BAR-programmed DMA did not emit MRd64");
    check(tx_data[223:208] == completer_id
          && tx_data[191:160] == 32'h12340000
          && tx_data[159:128] == 32'h00001020,
          "DMA MRd64 address/requester mismatch");
    dma_tag = tx_data[207:200];
    @(negedge clk); tx_wait = 0; tick();
    @(negedge clk); tx_wait = 1;

    rx_data = {{8'h4a, 14'b0, 10'd2}, {16'h0200, 3'b0, 1'b0, 12'd8},
      {completer_id, dma_tag, 1'b0, 7'h20},
      wire_word(32'h11223344), wire_word(32'h55667788), 96'b0};
    rx_valid = 8'hf8; rx_bar_decode = 0; #1;
    check(!rx_wait, "DMA Completion unexpectedly stalled");
    tick();
    @(negedge clk); rx_valid = 0;

    accept_dma_write(32'h00004000, 64'h55667788_11223344);

    // Read DMA status through BAR0. Ready + sticky done must be set.
    wait (dut.dma_done); tick();
    @(negedge clk);
    rx_valid = 8'he0; rx_bar_decode = 6'b000001;
    rx_data = {32'h00000001, 32'h12345b0f, 32'h000000d4, 160'b0};
    tick();
    @(negedge clk); rx_valid = 0;
    while (tx_valid == 0) tick();
    check(tx_valid == 8'hf0 && tx_data[255:224] == 32'h4a000001,
          "DMA status Completion missing");
    check(tx_data[159:128] == wire_word(32'h00000012),
          "DMA status did not report ready + done");
    check(!framing_error && !unsupported && !malformed,
          "integrated DMA raised PCIe decode errors");

    $display("PCIe GPU DMA integration: BAR0 command, MRd64, CplD and VRAM writes PASS");
    $finish;
  end
  initial begin #500000; $fatal(1, "timeout"); end
endmodule
