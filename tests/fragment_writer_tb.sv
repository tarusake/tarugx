// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module fragment_writer_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic clear=0;logic [31:0] target_base=32'h100,target_stride=64;
 logic [15:0] target_width=16,target_height=8;
 logic fragment_valid=0,fragment_ready;logic [15:0] fragment_x=0,fragment_y=0;
 logic [31:0] fragment_color=0;logic busy,fault;logic [31:0] writes;
 logic mem_valid,mem_ready=0;logic [23:0] mem_addr;logic [255:0] mem_wdata;
 logic [31:0] mem_wstrb;logic mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
 byte unsigned memory[0:4095];int requests=0;
 tarugx_fragment_writer dut(.*);
 task tick;@(posedge clk);#1;endtask
 task automatic fragment(input int x,y,color,input bit inject_error=0);
  logic [23:0] held_addr;logic [255:0] held_data;logic [31:0] held_strobe;
  @(negedge clk);assert(fragment_ready)else $fatal(1,"writer not ready");
  fragment_x=16'(x);fragment_y=16'(y);fragment_color=32'(color);fragment_valid=1;
  tick();@(negedge clk);fragment_valid=0;
  assert(mem_valid&&busy&&!fragment_ready)else $fatal(1,"request not issued");
  held_addr=mem_addr;held_data=mem_wdata;held_strobe=mem_wstrb;
  repeat(requests%3+1)begin tick();assert(mem_valid&&mem_addr==held_addr&&
   mem_wdata==held_data&&mem_wstrb==held_strobe)else $fatal(1,"request changed");end
  @(negedge clk);mem_ready=1;tick();@(negedge clk);mem_ready=0;
  assert(mem_rsp_ready&&!mem_valid)else $fatal(1,"response not requested");
  if(!inject_error)for(int byte_index=0;byte_index<32;byte_index++)
   if(held_strobe[byte_index])memory[int'(held_addr)+byte_index]=held_data[byte_index*8+:8];
  repeat(requests%2+1)tick();
  @(negedge clk);mem_rsp_error=inject_error;mem_rsp_valid=1;tick();
  @(negedge clk);mem_rsp_error=0;mem_rsp_valid=0;requests++;
 endtask
 task clear_fault;
  @(negedge clk);clear=1;tick();@(negedge clk);clear=0;
  assert(fragment_ready&&!fault&&!busy&&writes==0)else $fatal(1,"fault clear failed");
 endtask
 function automatic [15:0] rgb565(input int color);
  rgb565={color[23:19],color[15:10],color[7:3]};
 endfunction
 initial begin
  foreach(memory[i])memory[i]=8'ha5;
  repeat(3)tick();@(negedge clk);rst_n=1;
  fragment(0,0,32'h00ff0000);
  assert(!fault&&writes==1&&{memory[12'h101],memory[12'h100]}==rgb565(32'h00ff0000))
   else $fatal(1,"red pixel mismatch");
  fragment(15,3,32'h0012ab34);
  assert(!fault&&writes==2&&{memory[12'h1df],memory[12'h1de]}==rgb565(32'h0012ab34))
   else $fatal(1,"offset pixel mismatch");
  @(negedge clk);fragment_x=16;fragment_y=0;fragment_valid=1;tick();
  @(negedge clk);fragment_valid=0;assert(fault&&!busy&&!mem_valid)else $fatal(1,"X bounds");
  clear_fault();
  target_stride=31;@(negedge clk);fragment_x=0;fragment_valid=1;tick();
  @(negedge clk);fragment_valid=0;assert(fault&&!mem_valid)else $fatal(1,"short stride");
  clear_fault();target_stride=64;
  target_base=32'h00ffffe0;target_width=16;target_height=2;
  @(negedge clk);fragment_valid=1;tick();@(negedge clk);fragment_valid=0;
  assert(fault&&!mem_valid)else $fatal(1,"aperture overflow");
  clear_fault();target_base=32'h100;target_height=8;
  fragment(1,1,32'h0000ff00,1);assert(fault&&writes==0)else $fatal(1,"backend error");
  clear_fault();
  fragment(2,2,32'h000000ff);assert(writes==1&&!fault)else $fatal(1,"recovery");
  @(negedge clk);fragment_x=3;fragment_y=3;fragment_valid=1;tick();
  while(!mem_valid)tick();@(negedge clk);rst_n=0;tick();
  assert(fragment_ready&&!busy&&!fault&&!mem_valid&&writes==0)else $fatal(1,"reset");
  $display("Fragment writer: RGB565 lanes, addresses, stalls, bounds, errors and recovery PASS");
  $finish;
 end
 initial begin #1000000;$fatal(1,"timeout");end
endmodule
