// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module triangle_engine_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cmd_valid=0,cmd_ready;logic [31:0] cmd_base=32'h100,cmd_stride=16;
 logic [15:0] cmd_width=8,cmd_height=8;
 logic signed [23:0] cmd_x0=0,cmd_y0=0,cmd_x1=0,cmd_y1=0,cmd_x2=0,cmd_y2=0;
 logic [31:0] cmd_color=0;logic busy,done,error;logic [31:0] writes;
 logic mem_valid,mem_ready=0;logic [23:0] mem_addr;logic [255:0] mem_wdata;
 logic [31:0] mem_wstrb;logic mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
 byte unsigned memory[0:4095];int requests=0;
 tarugx_triangle_engine dut(.*);
 task tick;@(posedge clk);#1;endtask
 function automatic [15:0] rgb565(input int color);
  rgb565={color[23:19],color[15:10],color[7:3]};
 endfunction
 task automatic command(
  input integer signed x0,y0,x1,y1,x2,y2,input int color,
  input bit inject_error=0,input bit expect_error=0);
  int local_requests=0,cycles=0;logic [23:0] held_addr;
  logic [255:0] held_data;logic [31:0] held_strobe;
  @(negedge clk);assert(cmd_ready)else $fatal(1,"engine not ready");
  cmd_x0=24'(x0);cmd_y0=24'(y0);cmd_x1=24'(x1);cmd_y1=24'(y1);
  cmd_x2=24'(x2);cmd_y2=24'(y2);cmd_color=32'(color);cmd_valid=1;
  tick();@(negedge clk);cmd_valid=0;
  while(!done)begin
   if(mem_valid)begin
    held_addr=mem_addr;held_data=mem_wdata;held_strobe=mem_wstrb;
    repeat(local_requests%3)begin tick();assert(mem_valid&&mem_addr==held_addr&&
     mem_wdata==held_data&&mem_wstrb==held_strobe)else $fatal(1,"memory request changed");end
    @(negedge clk);mem_ready=1;tick();@(negedge clk);mem_ready=0;
    assert(mem_rsp_ready&&!mem_valid)else $fatal(1,"response not requested");
    if(!(inject_error&&local_requests==0))for(int byte_index=0;byte_index<32;byte_index++)
     if(held_strobe[byte_index])memory[int'(held_addr)+byte_index]=held_data[byte_index*8+:8];
    tick();@(negedge clk);mem_rsp_error=inject_error&&local_requests==0;mem_rsp_valid=1;
    tick();@(negedge clk);mem_rsp_error=0;mem_rsp_valid=0;
    local_requests++;requests++;
   end else tick();
   cycles++;if(cycles>4000)$fatal(1,"triangle engine timeout");
  end
  assert(error==expect_error)else $fatal(1,"completion error mismatch");
  if(!expect_error)assert(writes==local_requests)else $fatal(1,"write counter mismatch");
  tick();assert(cmd_ready&&!busy&&!done)else $fatal(1,"engine did not return idle");
 endtask
 task verify_triangle(input int color);
  for(int y=0;y<8;y++)for(int x=0;x<8;x++)begin
   int address;bit covered;
   address=32'h100+y*16+x*2;covered=(x+y)<3;
   if(covered)begin
    assert({memory[address+1],memory[address]}==rgb565(color))
     else $fatal(1,"pixel mismatch %0d,%0d",x,y);
   end else begin
    assert(memory[address]==8'ha5&&memory[address+1]==8'ha5)
     else $fatal(1,"outside pixel changed %0d,%0d",x,y);
   end
  end
 endtask
 initial begin
  foreach(memory[i])memory[i]=8'ha5;
  repeat(3)tick();@(negedge clk);rst_n=1;
  command(0,0,4*256,0,0,4*256,32'h00f04020);
  assert(writes==6)else $fatal(1,"expected six fragments, got %0d",writes);
  verify_triangle(32'h00f04020);
  foreach(memory[i])memory[i]=8'ha5;
  command(0,0,0,4*256,4*256,0,32'h0010d080);
  assert(writes==6)else $fatal(1,"reverse winding write count");
  verify_triangle(32'h0010d080);
  command(256,256,512,512,768,768,32'h00ffffff);
  assert(writes==0)else $fatal(1,"degenerate triangle wrote pixels");
  cmd_stride=8;
  command(0,0,4*256,0,0,4*256,32'h00ffffff,0,1);
  assert(writes==0)else $fatal(1,"invalid target reached memory");
  cmd_stride=16;
  command(0,0,4*256,0,0,4*256,32'h00ffffff,1,1);
  assert(writes==0)else $fatal(1,"failed write counted as complete");
  command(0,0,4*256,0,0,4*256,32'h000000ff);
  assert(writes==6&&!error)else $fatal(1,"recovery command failed");
  @(negedge clk);cmd_valid=1;tick();@(negedge clk);cmd_valid=0;
  while(!mem_valid)tick();rst_n=0;tick();
  assert(cmd_ready&&!busy&&!done&&!mem_valid)else $fatal(1,"reset failed");
  $display("Triangle engine: raster-to-RGB565 memory, completion, validation, errors and recovery PASS");
  $finish;
 end
 initial begin #2000000;$fatal(1,"timeout");end
endmodule
