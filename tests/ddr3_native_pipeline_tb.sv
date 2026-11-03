// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module ddr3_native_pipeline_tb;
 logic clk=0,rst_n=0,calibrated=0;always #5 clk=~clk;
 logic mem_valid=0,mem_ready,mem_write=0,mem_rsp_valid,mem_rsp_ready=0,mem_rsp_error;
 logic [23:0] mem_addr=0;logic [255:0] mem_wdata=256'h1234,mem_rsp_data;
 logic [31:0] mem_wstrb=32'h0000a55a;
 logic cmd_ready=0,cmd_en;logic [2:0] cmd;logic [28:0] addr;
 logic wr_data_rdy=0,wr_data_en,wr_data_end;logic [255:0] wr_data;
 logic [31:0] wr_data_mask;logic [255:0] rd_data=0;logic rd_data_valid=0,rd_data_end=1;
 tarugx_ddr3_native_pipeline dut(.*);
 task tick;@(posedge clk);#1;endtask
 task request(input bit w,input int a);
  @(negedge clk);assert(mem_ready)else $fatal(1,"ready");mem_valid=1;mem_write=w;mem_addr=24'(a);
  tick();@(negedge clk);mem_valid=0;
 endtask
 initial begin
  tick();@(negedge clk);rst_n=1;tick();assert(!mem_ready)else $fatal;
  calibrated=1;tick();
  for(int batch=0;batch<4;batch++)begin
   int base=batch==3?'hfff000:0;
   for(int n=0;n<8;n++)begin
    request(1'(n),base+32*(batch*8+n));
    assert(addr==29'((base+32*(batch*8+n))/4) && cmd==3'((n%2!=0)?0:1))else $fatal(1,"address/cmd");
    if(n%2!=0)begin
     cmd_ready=1;wr_data_rdy=0;tick();assert(!cmd_en)else $fatal(1,"split ready");
     @(negedge clk);cmd_ready=0;wr_data_rdy=1;tick();assert(!cmd_en)else $fatal(1,"split ready");
    end
    @(negedge clk);cmd_ready=1;wr_data_rdy=1;#1;
    assert(cmd_en && wr_data_en==1'(n) && wr_data_mask==32'hffff5aa5)else $fatal(1,"issue");
    tick();@(negedge clk);cmd_ready=0;wr_data_rdy=0;
   end
   assert(!mem_ready && !mem_rsp_valid)else $fatal(1,"credit/order barrier");
   // No ready on DDR returns: all four must fit while upstream is blocked.
   for(int r=0;r<4;r++)begin
    rd_data=256'(batch*8+r*2+100);rd_data_valid=1;rd_data_end=(r!=2);tick();@(negedge clk);
   end
   rd_data_valid=0;rd_data_end=1;
   for(int n=0;n<8;n++)begin
    assert(mem_rsp_valid && mem_rsp_error==(n==4) && mem_rsp_data==((n%2!=0)?256'b0:256'(32'(batch*8+n+100))))else $fatal(1,"ordered response %0d %0d",batch,n);
    repeat(3)begin tick();assert(mem_rsp_valid)else $fatal(1,"held response");end
    @(negedge clk);mem_rsp_ready=1;tick();@(negedge clk);mem_rsp_ready=0;
   end
  end
  request(0,64);cmd_ready=1;tick();
  @(negedge clk);cmd_ready=0;rd_data_valid=1;rd_data=256'hbeef;tick();
  @(negedge clk);rd_data_valid=0;
  assert(mem_rsp_valid && mem_rsp_data==256'hbeef && !mem_rsp_error)else $fatal(1,"registered read return");
  mem_rsp_ready=1;tick();@(negedge clk);mem_rsp_ready=0;
  request(1,4);assert(mem_rsp_valid && mem_rsp_error && !cmd_en)else $fatal(1,"alignment");
  mem_rsp_ready=1;tick();@(negedge clk);mem_rsp_ready=0;
  request(0,0);cmd_ready=1;tick();@(negedge clk);cmd_ready=0;rst_n=0;tick();
  assert(!cmd_en && !mem_rsp_valid)else $fatal(1,"reset pending");
  $display("Native pipeline: 8 reserved slots, mixed ordered responses, return bursts, backpressure, wrap, errors, reset PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
