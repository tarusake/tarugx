// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module scanout_engine_tb;
 logic mem_clk=0,pixel_clk=0,mem_rst_n=0,pixel_rst_n=0;
 always #5 mem_clk=~mem_clk;always #20 pixel_clk=~pixel_clk;
 logic enable=1;logic [31:0] fb_base=0,fb_stride=2560;
 logic config_error,mem_valid,mem_ready,mem_rsp_valid=0,mem_rsp_ready;
 logic active_buffer;
 logic [31:0] active_fb_base;
 logic [23:0] mem_addr;logic [255:0] mem_rsp_data=0;logic mem_rsp_error=0;
 logic [25:0] settings=26'h1000000;
 logic de,hsync,vsync,underflow,vblank_toggle;logic [23:0] rgb;
 logic [63:0] fault_snapshot;
 int delay_count=0,cycle=0,frame=0,pixels=0;logic [23:0] saved_addr;
 logic scan_urgent;
 tarugx_scanout_engine dut(.*);
 assign mem_ready=delay_count==0 && !mem_rsp_valid && cycle%7!=0;
 always @(posedge mem_clk) if(mem_rst_n) begin
  cycle<=cycle+1;
  if(mem_valid && mem_ready) begin
   assert(mem_addr[4:0]==0 && mem_addr<1228800) else $fatal(1,"memory address");
   saved_addr<=mem_addr;delay_count<=3;
  end
  if(delay_count!=0) begin
   delay_count<=delay_count-1;
   if(delay_count==1) begin
    mem_rsp_valid<=1;
    for(int i=0;i<8;i++) mem_rsp_data[i*32+:32]<=32'hff000000 | (32'(saved_addr)/4+32'(i));
   end
  end
  if(mem_rsp_valid && mem_rsp_ready) mem_rsp_valid<=0;
 end
 always @(posedge pixel_clk) if(pixel_rst_n) begin
  if(dut.display.frame_start) frame++;
  if((frame==2 || frame==3 || frame==4 || frame==5 || frame==8) && de) begin
   assert(rgb==24'(int'(dut.display.y)*640+int'(dut.display.x))) else $fatal(1,"integrated pixel f%0d %0d,%0d %h",frame,dut.display.x,dut.display.y,rgb);
   pixels++;
  end
  if(frame>=6 && frame<=7 && de)
   assert(rgb==0) else $fatal(1,"disabled/prefill frame must be black");
 end
 initial begin
  #100;mem_rst_n=1;pixel_rst_n=1;
  wait(frame==3 && dut.display.y==480);@(negedge pixel_clk);
  assert(pixels==614400 && !config_error) else $fatal(1,"pixel count/config");
  assert(!underflow) else $fatal(1,"startup prefill incorrectly reported as underflow");
  enable=0;
  // Model independent mailbox delivery: UI sees disable before pixel domain.
  wait(frame==5 && dut.display.y==100);@(negedge pixel_clk);settings=0;
  wait(frame==6 && dut.display.y==524 && dut.display.x==799);@(negedge pixel_clk);
  // Enable immediately before a frame boundary: FIFO cannot prefill in time.
  settings=26'h1000000;enable=1;
  wait(frame==8 && dut.display.y==480);@(negedge pixel_clk);
  assert(!underflow && pixels==1536000) else $fatal(1,"restart prefill/count");
  $display("Scanout engine: 1536000 exact pixels, skewed disable, restart prefill and disabled blacking PASS");$finish;
 end
 initial begin #150000000;$fatal(1,"timeout");end
endmodule
