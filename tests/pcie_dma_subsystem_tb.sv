// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module pcie_dma_subsystem_tb;
  logic clk = 0, rst_n = 0;
  always #5 clk = ~clk;
  logic [15:0] requester_id = 16'h0100;
  logic cmd_valid = 0, cmd_ready;
  logic [63:0] cmd_host_address = 0;
  logic [31:0] cmd_vram_address = 0, cmd_length_bytes = 0;
  logic busy, done, error;
  logic [3:0] error_code;
  logic [191:0] profile;
  logic rx_sop = 0, rx_eop = 0;
  logic [7:0] rx_valid = 0, rx_error = 0;
  logic [255:0] rx_data = 0;
  logic bar_rx_wait = 0, rx_wait, bar_rx_sop, bar_rx_eop;
  logic [7:0] bar_rx_valid;
  logic completion_valid = 0, completion_ready;
  logic completion_sop = 1, completion_eop = 1;
  logic [7:0] completion_keep = 8'hf0;
  logic [255:0] completion_data = 256'h0;
  logic tx_wait = 1, tx_sop, tx_eop;
  logic [7:0] tx_valid;
  logic [255:0] tx_data;
  logic [7:0] request_tag;
  logic cpu_req_valid = 0, cpu_req_ready, cpu_req_write = 0;
  logic [31:0] cpu_req_addr = 0;
  logic [63:0] cpu_req_wdata = 0;
  logic [7:0] cpu_req_wstrb = 0;
  logic cpu_rsp_valid, cpu_rsp_ready = 0;
  logic [31:0] cpu_rsp_rdata;
  logic cpu_rsp_error;
  logic fb_req_valid, fb_req_ready = 0, fb_req_write;
  logic [31:0] fb_req_addr;
  logic [255:0] fb_req_wdata;
  logic [31:0] fb_req_wstrb;
  logic fb_rsp_valid = 0, fb_rsp_ready;
  logic [31:0] fb_rsp_rdata = 0;
  logic fb_rsp_error = 0;

  tarugx_pcie_dma_subsystem #(.TIMEOUT_CYCLES(100)) dut (.*);

  task automatic tick; @(posedge clk); #1; endtask
  task automatic check(input logic condition, input string message);
    if (!condition) $fatal(1, "%s", message);
  endtask
  function automatic logic [31:0] wire_word(input logic [31:0] value);
    return {value[7:0], value[15:8], value[23:16], value[31:24]};
  endfunction
  task automatic accept_fb(input logic [31:0] address,
                           input logic [63:0] data,
                           input logic response_error);
    while (!fb_req_valid) tick();
    check(fb_req_write && fb_req_addr == address && fb_req_wdata[63:0] == data
          && fb_req_wdata[255:64] == 0 && fb_req_wstrb == 32'hff,
          "DMA framebuffer write mismatch");
    @(negedge clk); fb_req_ready = 1; tick();
    @(negedge clk); fb_req_ready = 0; fb_rsp_error = response_error;
    fb_rsp_valid = 1; tick();
    @(negedge clk); fb_rsp_valid = 0; fb_rsp_error = 0;
  endtask

  initial begin
    repeat (3) tick();
    @(negedge clk); rst_n = 1;

    // Ordinary requests remain available to the BAR path.
    rx_sop = 1; rx_eop = 1; rx_valid = 8'he0;
    rx_data[255:248] = 8'h00; bar_rx_wait = 1; #1;
    check(bar_rx_valid == 8'he0 && rx_wait, "BAR receive pass-through mismatch");
    rx_valid = 0; rx_sop = 0; rx_eop = 0; bar_rx_wait = 0;

    // Launch an eight-byte host-to-VRAM transfer and accept its MRd64.
    cmd_host_address = 64'h12340000_00001020;
    cmd_vram_address = 32'h00002000;
    cmd_length_bytes = 32'd8;
    cmd_valid = 1; tick();
    @(negedge clk); cmd_valid = 0;
    while (tx_valid == 0) tick();
    tick();
    check(profile[31:0] != 0 && profile[63:32] != 0,
          "DMA busy/TX-stall profiling mismatch");
    check(tx_sop && tx_eop && tx_valid == 8'hf0
          && tx_data[255:224] == 32'h20000002,
          "integrated DMA MRd64 mismatch");
    request_tag = tx_data[207:200];
    @(negedge clk); tx_wait = 0; tick();
    @(negedge clk); tx_wait = 1;

    // The matching CplD is consumed by DMA and never exposed to BAR logic.
    rx_data = {{8'h4a, 14'b0, 10'd2}, {16'h0200, 3'b0, 1'b0, 12'd8},
      {requester_id, request_tag, 1'b0, 7'h20},
      wire_word(32'h11223344), wire_word(32'h55667788), 96'b0};
    rx_valid = 8'hf8; rx_sop = 1; rx_eop = 1; #1;
    check(bar_rx_valid == 0 && !rx_wait, "Completion leaked into BAR path");
    tick();
    @(negedge clk); rx_valid = 0; rx_sop = 0; rx_eop = 0;

    accept_fb(32'h00002000, 64'h55667788_11223344, 0);
    while (!done) tick();
    check(!error && !busy, "integrated DMA completion mismatch");
    check(profile[31:0] != 0 && profile[63:32] != 0,
          "DMA profile was not retained after completion");
    check(profile[191:160] != 0,
          "DMA completion-busy profile was not retained after completion");

    // A CPU BAR access still owns its response after sharing the same port.
    cpu_req_write = 0; cpu_req_addr = 32'h00003000;
    cpu_req_wdata = 0; cpu_req_wstrb = 0; cpu_req_valid = 1; #1;
    check(cpu_req_ready, "CPU request not accepted after DMA");
    tick();
    @(negedge clk); cpu_req_valid = 0;
    while (!fb_req_valid) tick();
    check(!fb_req_write && fb_req_addr == 32'h00003000,
          "CPU framebuffer request routing mismatch");
    @(negedge clk); fb_req_ready = 1; tick();
    @(negedge clk); fb_req_ready = 0; fb_rsp_rdata = 32'hcafebabe;
    fb_rsp_valid = 1; tick();
    @(negedge clk); fb_rsp_valid = 0;
    check(cpu_rsp_valid && cpu_rsp_rdata == 32'hcafebabe && !cpu_rsp_error,
          "CPU framebuffer response ownership mismatch");
    cpu_rsp_ready = 1; tick();

    $display("PCIe DMA subsystem: RX/TX/VRAM end-to-end and CPU sharing PASS");
    $finish;
  end
  initial begin #500000; $fatal(1, "timeout"); end
endmodule
