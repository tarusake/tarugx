// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module framebuffer_display_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic [25:0] settings=26'h1000000;
 logic in_valid,in_ready,de,hsync,vsync,underflow,vblank_toggle,display_active;
 logic [63:0] fault_snapshot;
 logic [272:0] in_data;logic [23:0] rgb;
 int beat=0,frame=0,checked=0;bit inject=0;
 tarugx_framebuffer_display dut(.*);
 always_comb begin
  in_data=0;in_data[271:256]=16'(beat);
  for(int i=0;i<8;i++) in_data[i*32+:32]=32'hff000000 | 32'(beat*8+i);
  in_valid=!(inject && beat==100);
 end
 always @(posedge clk) if(rst_n) begin
  if(dut.frame_start) frame=frame+1;
  if(de) begin
   if(frame==1 || frame==3) begin
    assert(dut.x>0 && rgb==24'(int'(dut.y)*640+int'(dut.x)-1)) else $fatal(1,"pixel f%0d %0d,%0d = %h",frame,dut.x,dut.y,rgb);
    checked++;
   end
   // RGB is registered by one cycle and fault detection is pipelined.  The
   // missing beat itself may expose pixels while the mismatch propagates, but
   // the following beat and the rest of the frame must be black.
   if(frame==2 && (int'(dut.y)*640+int'(dut.x))>=808)
    assert(rgb==0) else $fatal(1,"underflow frame must stay black x=%0d y=%0d rgb=%06x underflow=%0d",dut.x,dut.y,rgb,underflow);
  end else assert(rgb==0) else $fatal(1,"blanking");
  if(in_valid && in_ready) beat <= (beat==38399)?0:beat+1;
 end
 initial begin
  #2;rst_n=0;@(negedge clk);rst_n=1;
  wait(frame==1 && dut.y==480);@(negedge clk);inject=1;
  wait(frame==2 && dut.y==2);@(negedge clk);inject=0;
  wait(frame==3 && dut.y==480);@(negedge clk);
  assert(underflow && checked==614400) else $fatal(1,"count/underflow %0d",checked);
  assert(fault_snapshot[63:54]==160 && fault_snapshot[53:44]==1 &&
         fault_snapshot[43:28]==100 && fault_snapshot[27:12]==100 &&
         fault_snapshot[7:0]==8'hac) else $fatal(1,"first fault snapshot %h",fault_snapshot);
  settings=26'h3123456;
  wait(frame==4);@(negedge clk);assert(rgb==24'h123456) else $fatal(1,"solid mode");
  rst_n=0;#1;assert(!underflow && fault_snapshot==0) else $fatal;
  $display("Framebuffer display: 614400 exact pixels, underflow blacking, frame recovery, solid mode and reset PASS");$finish;
 end
 initial begin #18000000;$fatal(1,"timeout");end
endmodule
