`timescale 1ns/1ps
module option_rom_completion_stream_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic req_valid=0,req_ready,rom_enabled=1;
 logic [31:0] h0=0,h1=0,h2=0,h3=0;
 logic [15:0] rom_base=16'h8021,completer_id=16'h0100;
 logic tx_valid,tx_ready=0,tx_sop,tx_eop,rom_en,malformed,unsupported;
 logic [31:0] tx_data,rom_data=0;
 logic [13:0] rom_addr;
 logic [31:0] mem[0:16383];
 int reads=0,words=0,packets=0;
 tarugx_option_rom_completion_stream dut(.*);
 always @(posedge clk) if(rom_en) begin rom_data<=mem[rom_addr];reads++;end
 task tick;@(posedge clk);#1;endtask
 task word(input logic[31:0] expected,input bit sop,input bit eop);
  while(!tx_valid) tick();
  assert(tx_data===expected && tx_sop==sop && tx_eop==eop)
   else $fatal(1,"word %0d got %h expected %h SOP/EOP=%b%b",words,tx_data,expected,tx_sop,tx_eop);
  repeat($urandom_range(0,4)) begin
   tick();assert(tx_valid && tx_data===expected && tx_sop==sop && tx_eop==eop) else $fatal(1,"TX stall");
  end
  @(negedge clk);tx_ready=1;tick();@(negedge clk);tx_ready=0;words++;
 endtask
 task start(input int off,input int n,input logic[7:0] be);
  while(!req_ready) tick();
  @(negedge clk);h0=32'(n)&1023;h1={16'h1234,8'ha6,be};h2=32'h80210000+32'(off);
  req_valid=1;tick();@(negedge clk);req_valid=0;
 endtask
 task read_range(input int off,input int n,input int lead,input int tail);
  int pos,left,part,bc,skip;
  start(off,n,{4'(15>>(3-tail)),4'(15<<lead)});
  pos=off;left=n;bc=n*4-lead-(3-tail);skip=lead;
  while(left>0) begin
   part=left<(128-pos%128)/4?left:(128-pos%128)/4;
   word(32'h4a000000 | 32'(part),1,0);
   word(32'h01000000 | 32'(bc&4095),0,0);
   word(32'h1234a600 | 32'((pos+skip)&127),0,0);
   for(int i=0;i<part;i++) word(mem[pos/4+i],0,i==part-1);
   packets++;pos+=part*4;left-=part;bc-=part*4-skip;skip=0;
  end
 endtask
 initial begin
  $readmemh("build/uefi/TaruGxGop.mem",mem);
  tick();@(negedge clk);rst_n=1;
  for(int p=0;p<65536;p+=4096) read_range(p,1024,0,3);
  read_range(124,35,2,1);
  read_range(4,63,3,0);
  start(0,1,8'h00);
  word(32'h4a000001,1,0);word(32'h01000004,0,0);word(32'h1234a600,0,0);word(mem[0],0,1);
  // UR must be header-only and must not touch ROM.
  begin
   int before_reads;
   before_reads=reads;rom_enabled=0;start(0,1,8'h0f);
   word(32'h0a000000,1,0);word(32'h01002000,0,0);word(32'h1234a600,0,1);
   assert(reads==before_reads) else $fatal(1,"UR read ROM");
   rom_enabled=1;
  end
  // Reset during a stalled header cancels both planner and reader.
  start(0,1,8'h0f);while(!tx_valid) tick();
  rst_n=0;tick();assert(!tx_valid && req_ready && !rom_en) else $fatal;
  @(negedge clk);rst_n=1;
  start(0,1,8'h03);
  word(32'h4a000001,1,0);word(32'h01000002,0,0);word(32'h1234a600,0,0);word(mem[0],0,1);
  $display("ROM stream: full 64KiB image, %0d split packets, partial BE, UR, TX stalls/reset PASS",packets);$finish;
 end
 initial begin #20000000;$fatal(1,"timeout");end
endmodule
