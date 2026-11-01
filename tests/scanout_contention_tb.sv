// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module scanout_contention_tb;
 parameter bit PRIORITY=1;
 logic mem_clk=0,pixel_clk=0,rst_n=0;
 always #5 mem_clk=~mem_clk;always #20 pixel_clk=~pixel_clk;
 logic scan_valid,scan_ready,scan_urgent,scan_rsp_valid,scan_rsp_ready,scan_rsp_error;
 logic [21:0] scan_addr;logic [255:0] scan_rsp_data;
 logic cpu_ready,cpu_rsp_valid,cpu_rsp_error;logic [255:0] cpu_rsp_data;
 logic mem_valid,mem_ready,mem_write,mem_rsp_valid=0,mem_rsp_ready;
 logic [21:0] mem_addr;logic [255:0] mem_wdata,mem_rsp_data=0;logic [31:0] mem_wstrb;
 logic config_error,de,hsync,vsync,underflow;logic [23:0] rgb;logic [63:0] fault_snapshot;
 tarugx_scanout_engine scan(
  .mem_clk(mem_clk),.mem_rst_n(rst_n),.enable(1'b1),.fb_base(32'b0),.fb_stride(32'd2560),
  .config_error(config_error),.scan_urgent(scan_urgent),
  .mem_valid(scan_valid),.mem_ready(scan_ready),.mem_addr(scan_addr),
  .mem_rsp_valid(scan_rsp_valid),.mem_rsp_ready(scan_rsp_ready),.mem_rsp_data(scan_rsp_data),.mem_rsp_error(scan_rsp_error),
  .pixel_clk(pixel_clk),.pixel_rst_n(rst_n),.settings(26'h1000000),
  .de(de),.hsync(hsync),.vsync(vsync),.rgb(rgb),.underflow(underflow),.fault_snapshot(fault_snapshot));
 tarugx_memory_arbiter arb(
  .clk(mem_clk),.rst_n(rst_n),.cpu_valid(1'b1),.cpu_ready(cpu_ready),.cpu_write(1'b0),
  .cpu_addr(22'd0),.cpu_wdata(256'b0),.cpu_wstrb(32'b0),.cpu_rsp_valid(cpu_rsp_valid),
  .cpu_rsp_ready(1'b1),.cpu_rsp_data(cpu_rsp_data),.cpu_rsp_error(cpu_rsp_error),
  .scan_valid(scan_valid),.scan_urgent(PRIORITY && scan_urgent),.scan_ready(scan_ready),.scan_addr(scan_addr),
  .scan_rsp_valid(scan_rsp_valid),.scan_rsp_ready(scan_rsp_ready),.scan_rsp_data(scan_rsp_data),.scan_rsp_error(scan_rsp_error),
  .mem_valid(mem_valid),.mem_ready(mem_ready),.mem_write(mem_write),.mem_addr(mem_addr),.mem_wdata(mem_wdata),.mem_wstrb(mem_wstrb),
  .mem_rsp_valid(mem_rsp_valid),.mem_rsp_ready(mem_rsp_ready),.mem_rsp_data(mem_rsp_data),.mem_rsp_error(1'b0));
 int delay_count=0,frame=0,pixels=0,cpu_count=0;logic [21:0] saved_addr;
 assign mem_ready=delay_count==0 && !mem_rsp_valid;
 always @(posedge mem_clk)if(rst_n)begin
  if(cpu_rsp_valid)cpu_count++;
  if(mem_valid && mem_ready)begin saved_addr<=mem_addr;delay_count<=18;end
  if(delay_count!=0)begin
   delay_count<=delay_count-1;
   if(delay_count==1)begin
    mem_rsp_valid<=1;
    for(int i=0;i<8;i++)mem_rsp_data[i*32+:32]<=(32'(saved_addr)/4)+32'(i);
   end
  end
  if(mem_rsp_valid && mem_rsp_ready)mem_rsp_valid<=0;
 end
 always @(posedge pixel_clk)if(rst_n)begin
  if(scan.display.frame_start)frame++;
  if(PRIORITY && frame>=2 && de)begin
   assert(rgb==24'(int'(scan.display.y)*640+int'(scan.display.x)))else $fatal(1,"pixel f%0d %0d,%0d",frame,scan.display.x,scan.display.y);
   pixels++;
  end
 end
 initial begin
  #100;rst_n=1;
  wait(frame==3 && scan.display.y==480);@(negedge pixel_clk);
  assert(cpu_count>1000 && !config_error)else $fatal(1,"CPU progress/config");
  if(PRIORITY)assert(!underflow && pixels==614400)else $fatal(1,"priority scanout failed");
  else assert(underflow)else $fatal(1,"baseline failed to reproduce starvation");
  $display("Scanout contention: priority=%0d pixels=%0d CPU completions=%0d underflow=%0d PASS",PRIORITY,pixels,cpu_count,underflow);$finish;
 end
 initial begin #55000000;$fatal(1,"timeout");end
endmodule
