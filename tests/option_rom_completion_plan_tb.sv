// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module option_rom_completion_plan_tb;
 logic clk=0,rst_n=0; always #5 clk=~clk;
 logic req_valid=0,req_ready,rom_enabled=1,cpl_valid,cpl_ready=0;
 logic [31:0] h0=0,h1=0,h2=0,h3=0;
 logic [15:0] rom_base=16'h8021,cpl_addr,cpl_requester;
 logic [2:0] cpl_status,cpl_tc;
 logic [1:0] cpl_attr;
 logic [5:0] cpl_length;
 logic [11:0] cpl_byte_count;
 logic [6:0] cpl_lower_addr;
 logic [7:0] cpl_tag;
 logic malformed,unsupported;
 tarugx_option_rom_completion_plan dut(.*);
 int cases=0;
 task tick; @(posedge clk); #1; endtask
 task request(input int address,input int n,input int f,input int l,input bit wide=0,input int upper=0,input int flags=0);
  @(negedge clk);
  assert(req_ready) else $fatal(1,"busy at request");
  h0=(wide?32'h20000000:0) | 32'h00502000 | (32'(n)&1023) | 32'(flags);
  h1=32'h1234a600 | (32'(l)<<4) | 32'(f);
  h2=wide?32'(upper):32'(address);h3=wide?32'(address):0;
  req_valid=1;tick();@(negedge clk);req_valid=0;
 endtask
 task good(input int off,input int n,input int f,input int l,input bit wide=0);
  int lo,hi,bytes_left,pos,words_left,part;
  logic [72:0] held;
  lo=0;hi=0;
  for(int i=3;i>=0;i--) if(((f>>i)&1)!=0) lo=i;
  for(int i=0;i<4;i++) if((((n==1?f:l)>>i)&1)!=0) hi=i;
  bytes_left=(f==0 && n==1)?4:4*n-lo-(3-hi);
  pos=off;words_left=n;
  request('h80210000+off,n,f,l,wide);
  // Configuration changes after acceptance must not alter this request.
  rom_base=16'h9876;rom_enabled=0;
  while(words_left>0) begin
   part=words_left < (128-(pos%128))/4 ? words_left : (128-(pos%128))/4;
   assert(cpl_valid && cpl_status==0 && !malformed) else $fatal(1,"missing success");
   assert(int'(cpl_addr)==pos && int'(cpl_length)==part &&
      int'(cpl_byte_count)==(bytes_left&4095) &&
      int'(cpl_lower_addr)==((pos+lo)&127)) else $fatal(1,"descriptor off=%h n=%0d f=%h l=%h pos=%h bytes=%0d got=%0d",off,n,f,l,pos,bytes_left,cpl_byte_count);
   assert(cpl_requester==16'h1234 && cpl_tag==8'ha6 && cpl_tc==5 && cpl_attr==2) else $fatal(1,"request metadata");
   held={cpl_status,cpl_addr,cpl_length,cpl_byte_count,cpl_lower_addr,cpl_requester,cpl_tag,cpl_tc,cpl_attr};
   repeat(3) begin
    tick();assert(cpl_valid && !req_ready && held=={cpl_status,cpl_addr,cpl_length,cpl_byte_count,cpl_lower_addr,cpl_requester,cpl_tag,cpl_tc,cpl_attr}) else $fatal(1,"stall changed descriptor");
   end
   @(negedge clk);cpl_ready=1;tick();@(negedge clk);cpl_ready=0;
   words_left-=part;bytes_left-=4*part-lo;pos+=4*part;lo=0;
  end
  assert(!cpl_valid && req_ready) else $fatal(1,"last descriptor");
  rom_enabled=1;rom_base=16'h8021;cases++;
 endtask
 task ur;
  assert(cpl_valid && cpl_status==1 && cpl_length==0 && cpl_byte_count==0 &&
    cpl_lower_addr==0 && cpl_requester==16'h1234 && cpl_tag==8'ha6) else $fatal(1,"UR");
  repeat(3) begin tick();assert(cpl_valid && cpl_status==1) else $fatal;end
  @(negedge clk);cpl_ready=1;tick();@(negedge clk);cpl_ready=0;
 endtask
 initial begin
  tick();@(negedge clk);rst_n=1;
  for(int f=0;f<16;f++) good(0,1,f,0);
  // Every legal BE pair for aligned 2-DWORD requests, including sparse masks.
  for(int f=1;f<16;f++) for(int l=1;l<16;l++) good(120,2,f,l,1);
  for(int p=0;p<128;p+=4) begin
   good(p,33,14,7);
   good(p,65,8,1,1);
  end
  good(0,1024,15,15);good('hf000,1024,15,15,1);good('hfffc,1,15,0);
  for(int j=0;j<150;j++) begin
   int p,n,f,l;
   p=$urandom_range(0,1023)*4;n=$urandom_range(1,(4096-p)/4);
   f=(15<<$urandom_range(0,3))&15;
   l=n==1?0:(1<<$urandom_range(1,4))-1;
   good(p,n,f,l,1'(j));
  end
  rom_enabled=0;request('h80210000,1,15,0);ur();rom_enabled=1;
  request('h80220000,1,15,0);ur();
  request('h80210000,1,15,0,1,1);ur();
  request('h80210000,1,15,0,0,0,'h4000);ur();
  request('h80210000,1,15,0,0,0,'h8000);ur();
  request('h80210000,1,15,0,0,0,'h40000);ur();
  request('h80210000,1,15,0,0,0,'h400);ur();
  // 4 KiB crossing, nonaligned and malformed byte enables: no Completion.
  request('h80210ffc,2,15,15);assert(malformed && !cpl_valid) else $fatal;
  request('h80210001,1,15,0);assert(malformed && !cpl_valid) else $fatal;
  request('h80210000,3,5,15);assert(malformed && !cpl_valid) else $fatal;
  request('h80210004,2,5,15);assert(malformed && !cpl_valid) else $fatal;
  request('h80210000,1,15,1);assert(malformed && !cpl_valid) else $fatal;
  request('h80210000,2,0,15);assert(malformed && !cpl_valid) else $fatal;
  @(negedge clk);h0=32'h40000001;req_valid=1;tick();
  assert(unsupported && !cpl_valid) else $fatal(1,"posted write");
  @(negedge clk);req_valid=0;
  request('h80210000,64,15,15);rst_n=0;tick();
  assert(!cpl_valid && req_ready) else $fatal(1,"reset");
  @(negedge clk);rst_n=1;good(0,1,15,0);
  $display("ROM completion planner: %0d valid requests, boundaries/BE/stalls/reset/UR PASS",cases);$finish;
 end
 initial begin #10000000;$fatal(1,"timeout");end
endmodule
