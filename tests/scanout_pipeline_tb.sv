// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module scanout_pipeline_tb;
 logic mem_clk=0,pixel_clk=0,rst_n=0;
 always #5 mem_clk=~mem_clk;always #20 pixel_clk=~pixel_clk;
 logic scan_valid,scan_ready,scan_urgent,scan_rsp_valid,scan_rsp_ready,scan_rsp_error;
 logic [23:0] scan_addr;logic [255:0] scan_rsp_data;
 logic cpu_ready,cpu_rsp_valid,cpu_rsp_error;logic [255:0] cpu_rsp_data;
 logic mem_valid,mem_ready,mem_write,mem_rsp_valid,mem_rsp_ready;
 logic [23:0] mem_addr;logic [255:0] mem_wdata,mem_rsp_data;logic [31:0] mem_wstrb;
 logic config_error,de,hsync,vsync,underflow,vblank_toggle;logic [23:0] rgb;logic [63:0] fault_snapshot;
 logic active_buffer;
 logic [31:0] active_fb_base;
 logic debug_push_valid,debug_push_ready,debug_pop_valid,debug_pop_ready;
 tarugx_scanout_pipeline_engine scan(
  .mem_clk(mem_clk),.mem_rst_n(rst_n),.enable(1'b1),.fb_base(32'b0),.fb_stride(32'd2560),
  .config_error(config_error),.active_buffer(active_buffer),.active_fb_base(active_fb_base),
  .scan_urgent(scan_urgent),
  .mem_valid(scan_valid),.mem_ready(scan_ready),.mem_addr(scan_addr),
  .mem_rsp_valid(scan_rsp_valid),.mem_rsp_ready(scan_rsp_ready),.mem_rsp_data(scan_rsp_data),.mem_rsp_error(scan_rsp_error),
  .pixel_clk(pixel_clk),.pixel_rst_n(rst_n),.settings(26'h1000000),
  .de(de),.hsync(hsync),.vsync(vsync),.rgb(rgb),.underflow(underflow),.vblank_toggle(vblank_toggle),.fault_snapshot(fault_snapshot),
  .debug_push_valid(debug_push_valid),.debug_push_ready(debug_push_ready),
  .debug_pop_valid(debug_pop_valid),.debug_pop_ready(debug_pop_ready));
 tarugx_memory_arbiter_pipelined arb(
  .clk(mem_clk),.rst_n(rst_n),.cpu_valid(1'b1),.cpu_ready(cpu_ready),.cpu_write(1'b0),
  .cpu_addr(24'd0),.cpu_wdata(256'b0),.cpu_wstrb(32'b0),.cpu_rsp_valid(cpu_rsp_valid),
  .cpu_rsp_ready(1'b1),.cpu_rsp_data(cpu_rsp_data),.cpu_rsp_error(cpu_rsp_error),
  .scan_valid(scan_valid),.scan_urgent(scan_urgent),.scan_ready(scan_ready),.scan_addr(scan_addr),
  .scan_rsp_valid(scan_rsp_valid),.scan_rsp_ready(scan_rsp_ready),.scan_rsp_data(scan_rsp_data),.scan_rsp_error(scan_rsp_error),
  .mem_valid(mem_valid),.mem_ready(mem_ready),.mem_write(mem_write),.mem_addr(mem_addr),.mem_wdata(mem_wdata),.mem_wstrb(mem_wstrb),
  .mem_rsp_valid(mem_rsp_valid),.mem_rsp_ready(mem_rsp_ready),.mem_rsp_data(mem_rsp_data),.mem_rsp_error(1'b0));
 logic cmd_ready,cmd_en;logic [2:0] cmd;logic [28:0] addr;
 logic wr_data_rdy=1,wr_data_en,wr_data_end;logic [255:0] wr_data;
 logic [31:0] wr_data_mask;logic [255:0] rd_data;logic rd_data_valid,rd_data_end=1;
 tarugx_ddr3_native_pipeline native(
  .clk(mem_clk),.rst_n(rst_n),.calibrated(1'b1),
  .mem_valid(mem_valid),.mem_ready(mem_ready),.mem_write(mem_write),.mem_addr(mem_addr),.mem_wdata(mem_wdata),.mem_wstrb(mem_wstrb),
  .mem_rsp_valid(mem_rsp_valid),.mem_rsp_ready(mem_rsp_ready),.mem_rsp_data(mem_rsp_data),.mem_rsp_error(),
  .cmd_ready(cmd_ready),.cmd(cmd),.cmd_en(cmd_en),.addr(addr),.wr_data_rdy(wr_data_rdy),
  .wr_data(wr_data),.wr_data_en(wr_data_en),.wr_data_end(wr_data_end),.wr_data_mask(wr_data_mask),
  .rd_data(rd_data),.rd_data_valid(rd_data_valid),.rd_data_end(rd_data_end));
 int frame=0,pixels=0,cpu_count=0,cycle=0,qhead=0,qtail=0,qcount=0,max_outstanding=0;
 int due[16];logic [28:0] addresses[16];
 assign cmd_ready=cycle%101<85;
 assign rd_data_valid=qcount!=0 && cycle>=due[qhead] && cycle%503>=80;
 always_comb for(int i=0;i<8;i++)rd_data[i*32+:32]=32'(addresses[qhead])+32'(i);
 always @(posedge mem_clk)if(rst_n)begin
  cycle<=cycle+1;
  if(cpu_rsp_valid)begin
   cpu_count++;
   for(int i=0;i<8;i++)assert(cpu_rsp_data[i*32+:32]==32'(i))else $fatal(1,"CPU response owner/order");
  end
  if(cmd_en)begin
   assert(cmd==1 && !wr_data_en && qcount<16)else $fatal(1,"native request");
   addresses[qtail]<=addr;due[qtail]<=cycle+64;qtail<=(qtail+1)%16;
  end
  if(rd_data_valid)qhead<=(qhead+1)%16;
  if(cmd_en && !rd_data_valid)qcount<=qcount+1;
  else if(rd_data_valid && !cmd_en)qcount<=qcount-1;
  if(qcount>max_outstanding)max_outstanding=qcount;
 end
 always @(posedge pixel_clk)if(rst_n)begin
  if(scan.display.frame_start)frame++;
  if(frame>=2 && de)begin
   assert(scan.display.x>0 && rgb==24'(int'(scan.display.y)*640+int'(scan.display.x)-1))else
    $fatal(1,"pixel f%0d %0d,%0d expected=%06x actual=%06x underflow=%0d qcount=%0d outstanding=%0d",
     frame,scan.display.x,scan.display.y,24'(int'(scan.display.y)*640+int'(scan.display.x)-1),rgb,underflow,qcount,max_outstanding);
   pixels++;
  end
 end
 initial begin
  #100;rst_n=1;
  wait(frame==3 && scan.display.y==480);@(negedge pixel_clk);
  assert(cpu_count>1000 && !config_error)else $fatal(1,"CPU progress/config");
  assert(!underflow && pixels==614400 && max_outstanding>=4)else $fatal(1,"pipeline supply");
  $display("Pipelined DDR contention: 614400 exact pixels, CPU completions=%0d, outstanding=%0d, latency64+refresh stalls, underflow=0 PASS",cpu_count,max_outstanding);$finish;
 end
 initial begin #55000000;$fatal(1,"timeout");end
endmodule
