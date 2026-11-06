// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module fill_engine_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cmd_valid=0,cmd_ready;logic [31:0] cmd_addr=0,cmd_stride=0,cmd_color=0;
 logic [15:0] cmd_width=0,cmd_height=0;logic busy,done,error;
 logic mem_valid,mem_ready=0;logic [23:0] mem_addr;logic [255:0] mem_wdata;logic [31:0] mem_wstrb;
 logic mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
 byte unsigned memory[0:65535];int requests=0;
 tarugx_fill_engine dut(.*);
 task tick;@(posedge clk);#1;endtask
 task command(input int addr,input int stride,input int width,input int height,input int color);
  @(negedge clk);assert(cmd_ready)else $fatal(1,"command not ready");
  cmd_addr=addr;cmd_stride=stride;cmd_width=16'(width);cmd_height=16'(height);cmd_color=color;cmd_valid=1;
  tick();@(negedge clk);cmd_valid=0;
 endtask
 task service(input bit inject_error=0);
  while(!done)begin
   if(mem_valid)begin
    logic [255:0] held_data;logic [31:0] held_strobe;logic [23:0] held_addr;
    held_data=mem_wdata;held_strobe=mem_wstrb;held_addr=mem_addr;
    repeat(requests%3)begin tick();assert(mem_valid && mem_addr==held_addr && mem_wdata==held_data && mem_wstrb==held_strobe)else $fatal(1,"request changed");end
    @(negedge clk);mem_ready=1;tick();@(negedge clk);mem_ready=0;
    // The pipelined engine may already present the next request while this
    // response is pending.  Backpressure keeps this legacy test serialized.
    assert(mem_rsp_ready)else $fatal(1,"response wait");
    if(!inject_error)for(int b=0;b<32;b++)if(held_strobe[b])memory[int'(held_addr)+b]=held_data[b*8+:8];
    repeat(requests%4)tick();
    @(negedge clk);mem_rsp_error=inject_error;mem_rsp_valid=1;tick();
    @(negedge clk);mem_rsp_valid=0;mem_rsp_error=0;requests++;
   end else tick();
  end
 endtask
 task verify(input int addr,input int stride,input int width,input int height,input int color);
  for(int y=0;y<height;y++)for(int x=0;x<width*4;x++)
   assert(memory[addr+y*stride+x]==8'(color>>((x%4)*8)))else $fatal(1,"pixel mismatch y=%0d byte=%0d",y,x);
 endtask
 initial begin
  foreach(memory[i])memory[i]=8'ha5;
  repeat(3)tick();@(negedge clk);rst_n=1;
  command(0,256,8,1,32'h11223344);service();assert(!error && requests==1)else $fatal;verify(0,256,8,1,32'h11223344);
  command(12,84,17,5,32'h89abcdef);service();assert(!error && requests==16)else $fatal(1,"request count %0d",requests);verify(12,84,17,5,32'h89abcdef);
  command(4092,128,1,3,32'h55aa00ff);service();assert(!error)else $fatal;verify(4092,128,1,3,32'h55aa00ff);
  command(2,64,4,1,1);tick();assert(done && error && !busy && !mem_valid)else $fatal(1,"unaligned command");
  command(0,12,4,1,1);tick();assert(done && error && !mem_valid)else $fatal(1,"short stride");
  command(0,17,4,2,1);tick();assert(done && error && !mem_valid)else $fatal(1,"unaligned stride");
  command('hfffffc,8,2,1,1);service();assert(done && error && !mem_valid)else $fatal(1,"aperture overflow");
  command(8192,64,8,2,32'hdeadbeef);service(1);assert(done && error && !busy)else $fatal(1,"backend error");
  command(16384,64,8,1,32'h01020304);while(!mem_valid)tick();@(negedge clk);rst_n=0;tick();
  assert(cmd_ready && !busy && !done && !mem_valid)else $fatal(1,"reset");
  $display("Fill engine: aligned/partial multirow writes, stalls, bounds, backend error and reset PASS");$finish;
 end
 initial begin #1000000;$fatal(1,"timeout");end
endmodule
