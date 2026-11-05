// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module scanout_async_fifo_tb;
  localparam int COUNT = 2000;
  logic src_clk = 0, dst_clk = 0, src_rst_n = 0, dst_rst_n = 0;
  always #5 src_clk = ~src_clk;   // 100 MHz UI side
  always #3 dst_clk = ~dst_clk;   // Faster-than-148 MHz pixel-side stress

  logic src_valid = 0, src_ready, src_buffered;
  logic [263:0] src_data = 0;
  logic dst_valid, dst_ready = 0;
  logic [263:0] dst_data;
  int sent = 0, received = 0, dst_cycles = 0;

  tarugx_scanout_fifo #(.DEPTH(32), .INDEX_WIDTH(7)) dut (.*);

  always @(negedge src_clk) begin
    if (!src_rst_n) begin
      src_valid <= 0;
      src_data <= 0;
    end else begin
      src_valid <= sent < COUNT;
      src_data <= 264'(sent);
    end
  end

  always @(posedge src_clk) begin
    if (src_rst_n && src_valid && src_ready)
      sent <= sent + 1;
  end

  always @(negedge dst_clk) begin
    if (!dst_rst_n) begin
      dst_ready <= 0;
      dst_cycles <= 0;
    end else begin
      dst_cycles <= dst_cycles + 1;
      // Exercise the elastic output stage without starving it indefinitely.
      dst_ready <= (dst_cycles % 7) != 0 && (dst_cycles % 11) != 0;
    end
  end

  always @(posedge dst_clk) begin
    if (dst_rst_n && dst_valid && dst_ready) begin
      assert(dst_data == 264'(received))
        else $fatal(1, "FIFO order mismatch: expected %0d got %0d", received,
                    dst_data[31:0]);
      received <= received + 1;
      if (received + 1 == COUNT) begin
        $display("Scanout async FIFO: %0d ordered words at 100->166 MHz PASS", COUNT);
        $finish;
      end
    end
  end

  initial begin
    #100;
    src_rst_n = 1;
    dst_rst_n = 1;
  end
  initial begin
    #500000;
    $fatal(1, "timeout: sent=%0d received=%0d", sent, received);
  end
endmodule
