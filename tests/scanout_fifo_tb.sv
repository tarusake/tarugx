// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module scanout_fifo_tb;
 logic src_clk=0,dst_clk=0,src_rst_n=0,dst_rst_n=0;
 always #5 src_clk=~src_clk;always #19 dst_clk=~dst_clk;
 logic src_valid=0,src_ready,dst_valid,dst_ready=0;
 logic [272:0] src_data=0,dst_data;
 int sent=0,received=0;bit saw_full=0;
 logic src_buffered;
 tarugx_scanout_fifo #(.DEPTH(16)) dut(.*);
 function automatic logic [272:0] pattern(input int n);
  return {1'(n%2),16'(n),{8{32'(n^32'ha55a1234)}}};
 endfunction
 always @(posedge src_clk) if(src_rst_n && src_valid) begin
  if(src_ready) sent<=sent+1;else saw_full<=1;
 end
 always @(negedge src_clk) begin
  src_valid=src_rst_n && sent<1000;
  src_data=pattern(sent);
 end
 always @(posedge dst_clk) if(dst_rst_n && dst_valid && dst_ready) begin
  assert(dst_data==pattern(received)) else $fatal(1,"FIFO sequence %0d",received);
  received<=received+1;
 end
 initial begin
  #100;src_rst_n=1;dst_rst_n=1;
  #1500;assert(src_buffered)else $fatal(1,"half-full watermark");dst_ready=1;
  wait(received==1000);#200;
  assert(saw_full && !dst_valid && !src_buffered) else $fatal;
  // Common reset with buffered data must discard all entries.
  @(negedge dst_clk);dst_ready=0;sent=0;
  #500;src_rst_n=0;dst_rst_n=0;
  #100;sent=0;received=0;src_rst_n=1;dst_rst_n=1;
  #1000;dst_ready=1;
  wait(received==1000);#200;
  assert(!dst_valid) else $fatal;
  $display("Scanout FIFO: asynchronous clocks, full/empty, 2000 ordered beats and reset PASS");$finish;
 end
 initial begin #200000;$fatal(1,"timeout");end
endmodule
