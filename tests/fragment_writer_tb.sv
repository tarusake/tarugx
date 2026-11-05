// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module fragment_writer_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic clear=0;logic [31:0] target_base=32'h100,target_stride=64;
 logic [15:0] target_width=16,target_height=8;
 logic target_validated=1;
 logic depth_enable=0,depth_write_enable=0;logic [31:0] depth_base=32'h800;
 logic [2:0] depth_func=7;
 logic fragment_valid=0,fragment_ready;logic [15:0] fragment_x=0,fragment_y=0;
 logic [31:0] fragment_color=0;logic [15:0] fragment_depth=0;
 logic busy,fault;logic [31:0] writes;
 logic mem_valid,mem_ready=0,mem_write;logic [23:0] mem_addr;logic [255:0] mem_wdata,mem_rsp_data=0;
 logic [31:0] mem_wstrb;logic mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
 byte unsigned memory[0:4095];int requests=0;
 tarugx_fragment_writer dut(.*);
 task tick;@(posedge clk);#1;endtask
 task automatic fragment(input int x,y,color,input bit inject_error=0);
  logic [23:0] held_addr;logic [255:0] held_data;logic [31:0] held_strobe;
  @(negedge clk);assert(fragment_ready)else $fatal(1,"writer not ready");
  fragment_x=16'(x);fragment_y=16'(y);fragment_color=32'(color);fragment_valid=1;
  tick();@(negedge clk);fragment_valid=0;
  while(!mem_valid&&!fault)tick();
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
 task automatic start_fragment(input int x,y,color,depth);
  @(negedge clk);assert(fragment_ready)else $fatal(1,"writer not ready");
  fragment_x=16'(x);fragment_y=16'(y);fragment_color=32'(color);
  fragment_depth=16'(depth);fragment_valid=1;tick();
  @(negedge clk);fragment_valid=0;
 endtask
 task automatic transaction(input logic [255:0] response,input bit inject_error=0);
  logic [23:0] held_addr;logic [255:0] held_data;logic [31:0] held_strobe;
  bit held_write;
  while(!mem_valid&&!fault)tick();
  assert(mem_valid&&!fault)else $fatal(1,"memory transaction missing");
  held_addr=mem_addr;held_data=mem_wdata;held_strobe=mem_wstrb;held_write=mem_write;
  @(negedge clk);mem_ready=1;tick();@(negedge clk);mem_ready=0;
  assert(mem_rsp_ready&&!mem_valid)else $fatal(1,"transaction response not requested");
  if(held_write&&!inject_error)for(int byte_index=0;byte_index<32;byte_index++)
   if(held_strobe[byte_index])memory[int'(held_addr)+byte_index]=held_data[byte_index*8+:8];
  @(negedge clk);mem_rsp_data=response;mem_rsp_error=inject_error;mem_rsp_valid=1;tick();
  @(negedge clk);mem_rsp_valid=0;mem_rsp_error=0;mem_rsp_data=0;requests++;
 endtask
 task automatic depth_compare_case(input logic [2:0] func,input int incoming,stored,
                                   input bit expect_pass);
  logic [255:0] depth_line;
  depth_enable=1;depth_write_enable=0;depth_func=func;clear_fault();
  depth_line='0;depth_line[2*16+:16]=16'(stored);
  start_fragment(2,1,32'h0055aa33,incoming);
  transaction(depth_line);
  if(expect_pass)transaction('0);
  wait(fragment_ready);
  assert(writes==(expect_pass?32'd1:32'd0)&&!fault&&!mem_valid)
   else $fatal(1,"depth compare func=%0d incoming=%x stored=%x",func,incoming,stored);
 endtask
 function automatic [15:0] rgb565(input int color);
  rgb565={color[23:19],color[15:10],color[7:3]};
 endfunction
 initial begin
  foreach(memory[i])memory[i]=8'ha5;
  repeat(3)tick();@(negedge clk);rst_n=1;
  clear_fault();
  fragment(0,0,32'h00ff0000);
  assert(!fault&&writes==1&&{memory[12'h101],memory[12'h100]}==rgb565(32'h00ff0000))
   else $fatal(1,"red pixel mismatch");
  fragment(15,3,32'h0012ab34);
  assert(!fault&&writes==2&&{memory[12'h1df],memory[12'h1de]}==rgb565(32'h0012ab34))
   else $fatal(1,"offset pixel mismatch");
  @(negedge clk);fragment_x=16;fragment_y=0;fragment_valid=1;tick();
  @(negedge clk);fragment_valid=0;while(!fault)tick();
  assert(fault&&!busy&&!mem_valid)else $fatal(1,"X bounds");
  clear_fault();
  target_stride=31;target_validated=0;clear_fault();
  @(negedge clk);fragment_x=0;fragment_valid=1;tick();
  @(negedge clk);fragment_valid=0;while(!fault)tick();
  assert(fault&&!mem_valid)else $fatal(1,"short stride");
  clear_fault();target_stride=64;target_validated=1;
  target_base=32'h00ffffe0;target_width=16;target_height=2;
  target_validated=0;clear_fault();
  @(negedge clk);fragment_valid=1;tick();@(negedge clk);fragment_valid=0;
  while(!fault)tick();
  assert(fault&&!mem_valid)else $fatal(1,"aperture overflow");
  clear_fault();target_base=32'h100;target_height=8;target_validated=1;clear_fault();
  fragment(1,1,32'h0000ff00,1);assert(fault&&writes==0)else $fatal(1,"backend error");
  clear_fault();
  fragment(2,2,32'h000000ff);assert(writes==1&&!fault)else $fatal(1,"recovery");
  depth_enable=1;depth_write_enable=1;depth_func=1;depth_base=32'h800;
  clear_fault();
  begin
   logic [255:0] depth_line;
   depth_line='0;depth_line[2*16+:16]=16'h2000;
   start_fragment(2,1,32'h00abcdef,32'h1000);
   while(!mem_valid)tick();
   assert(!mem_write&&mem_addr==24'h840&&mem_wstrb==0)else $fatal(1,"depth read");
   transaction(depth_line);
   while(!mem_valid)tick();
   assert(mem_write&&mem_addr==24'h140)else $fatal(1,"depth color write");
   transaction('0);
   while(!mem_valid)tick();
   assert(mem_write&&mem_addr==24'h840)else $fatal(1,"depth update write");
   transaction('0);
   wait(fragment_ready);
   assert(writes==1&&{memory[12'h845],memory[12'h844]}==16'h1000)
    else $fatal(1,"passing depth fragment");
  end
  clear_fault();
  begin
   logic [255:0] depth_line;
   depth_line='0;depth_line[2*16+:16]=16'h1000;
   start_fragment(2,1,32'h00ffffff,32'h2000);
   transaction(depth_line);wait(fragment_ready);
   assert(writes==0&&!fault&&!mem_valid)else $fatal(1,"failing depth fragment wrote");
  end
  depth_write_enable=0;depth_func=7;clear_fault();
  start_fragment(2,1,32'h00123456,32'hffff);
  transaction('0);transaction('0);wait(fragment_ready);
  assert(writes==1&&!fault&&!mem_valid)else $fatal(1,"depth always/no-write");
  depth_compare_case(0,32'h1000,32'h2000,0);
  depth_compare_case(1,32'h1000,32'h2000,1);
  depth_compare_case(2,32'h2000,32'h2000,1);
  depth_compare_case(3,32'h2000,32'h2000,1);
  depth_compare_case(4,32'h3000,32'h2000,1);
  depth_compare_case(5,32'h3000,32'h2000,1);
  depth_compare_case(6,32'h2000,32'h2000,1);
  depth_compare_case(7,32'hffff,32'h0000,1);
  depth_enable=0;depth_write_enable=0;clear_fault();
  @(negedge clk);fragment_x=3;fragment_y=3;fragment_valid=1;tick();
  while(!mem_valid)tick();@(negedge clk);rst_n=0;tick();
  assert(fragment_ready&&!busy&&!fault&&!mem_valid&&writes==0)else $fatal(1,"reset");
  $display("Fragment writer: RGB565, Z16 compare/update, stalls, bounds, errors and recovery PASS");
  $finish;
 end
 initial begin #1000000;$fatal(1,"timeout");end
endmodule
