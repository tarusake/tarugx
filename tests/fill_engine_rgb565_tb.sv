// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module fill_engine_rgb565_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cmd_valid=0,cmd_ready;logic [31:0] cmd_addr=0,cmd_stride=0,cmd_color=0;
 logic [15:0] cmd_width=0,cmd_height=0;logic busy,done,error;
 logic mem_valid,mem_ready=0;logic [23:0] mem_addr;logic [255:0] mem_wdata;
 logic [31:0] mem_wstrb;logic mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
 int requests=0;logic [23:0] first_addr,last_addr;
 tarugx_fill_engine #(.PIXEL_BYTES(2)) dut(.*);
 task tick;@(posedge clk);#1;endtask
 initial begin
  repeat(3)tick();@(negedge clk);rst_n=1;
  cmd_addr=32'h00bde000;cmd_stride=3840;cmd_width=1920;cmd_height=1080;
  cmd_color=32'h00f8fcf8;cmd_valid=1;tick();@(negedge clk);cmd_valid=0;
  while(!done)begin
   if(mem_valid)begin
    if(requests==0)first_addr=mem_addr;
    last_addr=mem_addr;
    assert(mem_wstrb==32'hffffffff)else $fatal(1,"partial beat %0d",requests);
    assert(mem_wdata=={16{16'hffff}})else $fatal(1,"bad RGB565 data");
    mem_ready=1;tick();@(negedge clk);mem_ready=0;
    assert(mem_rsp_ready)else $fatal(1,"response not ready");
    mem_rsp_valid=1;tick();@(negedge clk);mem_rsp_valid=0;
    requests++;
   end else tick();
  end
  assert(!error && requests==129600)else
   $fatal(1,"1080p fill error=%0d requests=%0d",error,requests);
  assert(first_addr==24'hbde000 && last_addr==24'hfd27e0)else
   $fatal(1,"address range %h..%h",first_addr,last_addr);
  $display("RGB565 fill engine: 1920x1080 high-address clear PASS");
  $finish;
 end
 initial begin #10000000;$fatal(1,"timeout requests=%0d addr=%h",requests,last_addr);end
endmodule
