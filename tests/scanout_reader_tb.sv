// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module scanout_reader_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic enable=0;logic [31:0] fb_base=0,fb_stride=2560;
 logic config_error,mem_valid,mem_ready=0,mem_rsp_valid=0,mem_rsp_ready;
 logic [21:0] mem_addr;logic [255:0] mem_rsp_data=0;
 logic mem_rsp_error=0,out_valid,out_ready=0,out_error;
 logic [15:0] out_index;logic [255:0] out_data;
 tarugx_scanout_reader dut(.*);
 task tick;@(posedge clk);#1;endtask
 task frame(input int base,input int pitch,input bit modify_config);
  for(int i=0;i<38400;i++) begin
   while(!mem_valid) tick();
   assert(mem_addr==22'(base+(i/80)*pitch+(i%80)*32)) else $fatal(1,"address %0d %h",i,mem_addr);
   if(i%991==0) repeat(3) begin tick();assert(mem_valid && !out_valid) else $fatal;end
   @(negedge clk);mem_ready=1;tick();@(negedge clk);mem_ready=0;
   if(i%997==0) repeat(3) tick();
   mem_rsp_data={8{32'(i)}};mem_rsp_error=(i==123);mem_rsp_valid=1;
   tick();@(negedge clk);mem_rsp_valid=0;
   assert(out_valid && out_index==16'(i) && out_data=={8{32'(i)}} && out_error==(i==123)) else $fatal(1,"output %0d",i);
   if(i%983==0) repeat(3) begin tick();assert(out_valid && !mem_valid && out_index==16'(i)) else $fatal;end
   if(modify_config && i==50) begin fb_base=4096;fb_stride=2592;end
   @(negedge clk);out_ready=1;
   if(!modify_config && i==38399) enable=0;
   tick();@(negedge clk);out_ready=0;
  end
 endtask
 initial begin
  tick();@(negedge clk);rst_n=1;enable=1;fb_base=1;
  repeat(3) tick();assert(config_error && !mem_valid) else $fatal;
  @(negedge clk);fb_base=32'hffffffe0;fb_stride=32'hffffffe0;
  repeat(3) tick();assert(config_error && !mem_valid) else $fatal;
  @(negedge clk);fb_base=0;fb_stride=2560;
  frame(0,2560,1);frame(4096,2592,0);
  @(negedge clk);enable=0;tick();assert(!mem_valid) else $fatal;
  @(negedge clk);enable=1;tick();assert(mem_valid) else $fatal;
  @(negedge clk);rst_n=0;tick();assert(!mem_valid && !out_valid) else $fatal;
  $display("Scanout reader: two full frames, stride, atomic configuration, stalls, error tag, bounds and reset PASS");$finish;
 end
 initial begin #10000000;$fatal(1,"timeout");end
endmodule
