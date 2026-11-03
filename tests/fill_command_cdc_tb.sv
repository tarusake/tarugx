// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module fill_command_cdc_tb;
 logic src_clk=0,dst_clk=0,src_rst_n=0,dst_rst_n=0;
 always #5 src_clk=~src_clk;always #7 dst_clk=~dst_clk;
 logic src_valid=0,src_ready,src_busy,src_done,src_error;logic [127:0] src_command=0;
 logic dst_valid,dst_ready=0,dst_done=0,dst_error=0;logic [127:0] dst_command;
 tarugx_fill_command_cdc dut(.*);
 initial begin
  repeat(3)@(negedge src_clk);src_rst_n=1;dst_rst_n=1;
  for(int n=0;n<100;n++)begin
   logic [127:0] expected;
   expected={32'(n^32'hdeadbeef),32'(n*17),32'(~n),32'(n)};
   @(negedge src_clk);assert(src_ready && !src_busy)else $fatal(1,"source ready");
   src_command=expected;src_valid=1;@(negedge src_clk);src_valid=0;
   assert(src_busy && !src_ready)else $fatal(1,"source busy");
   wait(dst_valid);
   repeat(n%4+1)begin @(negedge dst_clk);assert(dst_valid && dst_command==expected)else $fatal(1,"command hold");end
   dst_ready=1;@(negedge dst_clk);dst_ready=0;assert(!dst_valid)else $fatal;
   repeat(n%5+1)@(negedge dst_clk);
   dst_error=(n%11)==0;dst_done=1;@(negedge dst_clk);dst_done=0;
   wait(src_done);assert(src_error==((n%11)==0) && !src_busy)else $fatal(1,"completion");
   @(posedge src_clk);#1;assert(!src_done && src_ready)else $fatal(1,"done pulse");
  end
  @(negedge src_clk);src_valid=1;@(negedge src_clk);src_valid=0;
  wait(dst_valid);dst_ready=1;@(negedge dst_clk);dst_ready=0;
  src_rst_n=0;dst_rst_n=0;repeat(3)@(negedge src_clk);
  assert(!src_busy && !src_done && !dst_valid)else $fatal(1,"reset");
  src_rst_n=1;dst_rst_n=1;repeat(5)@(negedge src_clk);assert(src_ready)else $fatal;
  $display("Fill command CDC: 100 asynchronous commands, stalls, errors, completion pulses and reset PASS");$finish;
 end
 initial begin #1000000;$fatal(1,"timeout");end
endmodule
