// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module ddr3_native_adapter_tb;
 logic clk=0,rst_n=0,calibrated=0;
 always #5 clk=~clk;
 logic mem_valid=0,mem_ready,mem_write=0;
 logic [21:0] mem_addr=0;
 logic [255:0] mem_wdata=0,mem_rsp_data;
 logic [31:0] mem_wstrb=0;
 logic mem_rsp_valid,mem_rsp_ready=0,mem_rsp_error;
 logic cmd_ready=0,cmd_en; logic [2:0] cmd; logic [28:0] addr;
 logic wr_data_rdy=0,wr_data_en,wr_data_end; logic [255:0] wr_data;
 logic [31:0] wr_data_mask;
 logic [255:0] rd_data=0; logic rd_data_valid=0,rd_data_end=0;
 tarugx_ddr3_native_adapter dut(.*);
 task tick; @(posedge clk); #1; endtask
 task send(input bit write,input int address);
  @(negedge clk); assert(mem_ready) else $fatal;
  mem_valid=1;mem_write=write;mem_addr=22'(address);mem_wdata=256'h123456789abcdef;mem_wstrb='h0000a55a;
  tick();@(negedge clk);mem_valid=0;
 endtask
 task finish_rsp(input bit error,input logic[255:0] data);
  assert(mem_rsp_valid && mem_rsp_error==error && mem_rsp_data==data) else $fatal;
  repeat(3) begin tick(); assert(mem_rsp_valid && mem_rsp_data==data && !cmd_en && !wr_data_en) else $fatal; end
  @(negedge clk);mem_rsp_ready=1;tick();@(negedge clk);mem_rsp_ready=0;
 endtask
 initial begin
  tick();@(negedge clk);rst_n=1;tick();assert(!mem_ready) else $fatal;
  calibrated=1;
  for(int order=0;order<2;order++) begin
   tick();send(1,'h3fffe0);
   assert(cmd==0 && addr=='hffff8 && wr_data_mask=='hffff5aa5 && wr_data==mem_wdata) else $fatal;
   repeat(3) begin tick();assert(!cmd_en && !wr_data_en && !wr_data_end) else $fatal;end
   @(negedge clk);cmd_ready=(order==0);wr_data_rdy=(order==1);tick();
   @(negedge clk);cmd_ready=0;wr_data_rdy=0;
   repeat(3) begin tick();assert(!cmd_en && !wr_data_en && !mem_rsp_valid) else $fatal;end
   @(negedge clk);cmd_ready=1;wr_data_rdy=1;#1;assert(cmd_en && wr_data_en && wr_data_end) else $fatal;tick();
   @(negedge clk);cmd_ready=0;wr_data_rdy=0;finish_rsp(0,0);
  end
  send(0,32); assert(cmd==1 && addr==8 && !wr_data_en) else $fatal;
  cmd_ready=1;tick();@(negedge clk);cmd_ready=0;
  repeat(4) begin tick();assert(!mem_rsp_valid && !cmd_en && !mem_ready) else $fatal;end
  @(negedge clk);rd_data=256'hcafef00d;rd_data_valid=1;rd_data_end=1;
  tick();@(negedge clk);rd_data_valid=0;finish_rsp(0,256'hcafef00d);
  send(0,64);cmd_ready=1;rd_data_valid=1;rd_data_end=0;
  tick();@(negedge clk);cmd_ready=0;rd_data_valid=0;finish_rsp(1,256'hcafef00d);
  send(1,4);finish_rsp(1,0);
  send(0,0);@(negedge clk);rst_n=0;tick();assert(!cmd_en && !mem_rsp_valid) else $fatal;
  $display("DDR native: split channel stalls, byte mask/address, read capture, errors and reset PASS");$finish;
 end
 initial begin #100000; $fatal(1,"timeout");end
endmodule
