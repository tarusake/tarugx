// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module blit_engine_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cmd_valid=0,cmd_ready;logic [31:0] cmd_src_addr=0,cmd_dst_addr=0,cmd_src_stride=0,cmd_dst_stride=0;
 logic [15:0] cmd_width=0,cmd_height=0;logic busy,done,error;
 logic mem_valid,mem_ready=0,mem_write;logic [23:0] mem_addr;logic [255:0] mem_wdata;logic [31:0] mem_wstrb;
 logic mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;logic [255:0] mem_rsp_data=0;
 byte unsigned memory[0:65535];int reads=0,writes=0,request_number=0;
 tarugx_blit_engine dut(.*);
 task tick;@(posedge clk);#1;endtask
 task command(input int src,input int dst,input int ss,input int ds,input int width,input int height);
  @(negedge clk);assert(cmd_ready)else $fatal(1,"command not ready");
  cmd_src_addr=src;cmd_dst_addr=dst;cmd_src_stride=ss;cmd_dst_stride=ds;
  cmd_width=16'(width);cmd_height=16'(height);cmd_valid=1;tick();@(negedge clk);cmd_valid=0;
 endtask
 task service(input bit inject_error=0);
  while(!done)begin
   if(mem_valid)begin
    logic held_write;logic [23:0] held_addr;logic [255:0] held_data;logic [31:0] held_strobe;
    held_write=mem_write;held_addr=mem_addr;held_data=mem_wdata;held_strobe=mem_wstrb;
    assert(held_addr[4:0]==0)else $fatal(1,"unaligned native request");
    repeat(request_number%3)begin tick();assert(mem_valid&&mem_write==held_write&&mem_addr==held_addr)else $fatal(1,"request changed");end
    if(!held_write)for(int b=0;b<32;b++)mem_rsp_data[b*8+:8]=memory[int'(held_addr)+b];
    @(negedge clk);mem_ready=1;tick();@(negedge clk);mem_ready=0;
    repeat(request_number%2)tick();
    if(held_write&&!inject_error)begin
     assert($countones(held_strobe)==4||$countones(held_strobe)==32)else $fatal(1,"write mask");
     for(int b=0;b<32;b++)if(held_strobe[b])memory[int'(held_addr)+b]=held_data[b*8+:8];
     writes++;
    end else if(!held_write)reads++;
    @(negedge clk);mem_rsp_error=inject_error;mem_rsp_valid=1;tick();
    @(negedge clk);mem_rsp_valid=0;mem_rsp_error=0;request_number++;
    if(inject_error)return;
   end else tick();
  end
 endtask
 task set_pixel(input int address,input int value);
  for(int b=0;b<4;b++)memory[address+b]=8'(value>>(b*8));
 endtask
 function automatic int get_pixel(input int address);
  return {memory[address+3],memory[address+2],memory[address+1],memory[address]};
 endfunction
 int snapshot[0:63];int before_reads,before_writes;
 initial begin
  foreach(memory[i])memory[i]=8'ha5;
  repeat(3)tick();@(negedge clk);rst_n=1;
  // Arbitrary pixel lanes and different source/destination strides.
  for(int y=0;y<3;y++)for(int x=0;x<9;x++)set_pixel(12+y*52+x*4,'h10000000+y*256+x);
  before_reads=reads;before_writes=writes;command(12,1028,52,60,9,3);service();
  $display("non-overlap transfers: reads=%0d writes=%0d error=%0d", reads-before_reads, writes-before_writes, error);
  assert(!error&&reads-before_reads==writes-before_writes&&reads-before_reads<=27)
   else $fatal(1,"non-overlap counts");
  for(int y=0;y<3;y++)for(int x=0;x<9;x++)assert(get_pixel(1028+y*60+x*4)=='h10000000+y*256+x)else $fatal(1,"non-overlap data");
  // Aligned non-overlapping rows use two 256-bit transfers per 16 pixels.
  for(int y=0;y<2;y++)for(int x=0;x<16;x++)set_pixel(2048+y*64+x*4,'h18000000+y*256+x);
  before_reads=reads;before_writes=writes;command(2048,3072,64,64,16,2);service();
  assert(!error&&reads-before_reads==4&&writes-before_writes==4)else $fatal(1,"burst counts");
  for(int y=0;y<2;y++)for(int x=0;x<16;x++)assert(get_pixel(3072+y*64+x*4)=='h18000000+y*256+x)else $fatal(1,"burst data");
  // Horizontal overlap must copy backwards like memmove.
  for(int y=0;y<3;y++)for(int x=0;x<12;x++)begin set_pixel(4096+y*64+x*4,'h20000000+y*256+x);snapshot[y*12+x]=get_pixel(4096+y*64+x*4);end
  command(4096,4100,64,64,12,3);service();assert(!error)else $fatal(1,"horizontal overlap error");
  for(int y=0;y<3;y++)for(int x=0;x<12;x++)assert(get_pixel(4100+y*64+x*4)==snapshot[y*12+x])else $fatal(1,"horizontal overlap");
  // Vertical overlap must process bottom-to-top.
  for(int y=0;y<4;y++)for(int x=0;x<8;x++)begin set_pixel(8192+y*64+x*4,'h30000000+y*256+x);snapshot[y*8+x]=get_pixel(8192+y*64+x*4);end
  command(8192,8256,64,64,8,4);service();assert(!error)else $fatal(1,"vertical overlap error");
  for(int y=0;y<4;y++)for(int x=0;x<8;x++)assert(get_pixel(8256+y*64+x*4)==snapshot[y*8+x])else $fatal(1,"vertical overlap");
  command(2,1024,32,32,4,1);tick();tick();assert(done&&error&&!mem_valid)else $fatal(1,"unaligned source");
  command(0,1024,14,32,4,2);tick();tick();assert(done&&error&&!mem_valid)else $fatal(1,"unaligned stride");
  command('hfffffc,0,8,8,2,1);service();assert(done&&error&&!mem_valid)else $fatal(1,"aperture overflow");
  command(0,16384,32,32,4,1);while(!mem_valid)tick();service(1);tick();assert(done&&error&&!busy)else $fatal(1,"backend error");
  $display("Blit engine: arbitrary lanes, 2D strides, overlap, stalls, bounds and backend errors PASS");$finish;
 end
 initial begin #3000000;$fatal(1,"timeout");end
endmodule
