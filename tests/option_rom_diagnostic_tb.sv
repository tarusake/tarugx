// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module option_rom_diagnostic_tb #(parameter int AUTO_MODE=0);
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic [15:0] completer_id=16'h0100;
 logic rx_sop=1,rx_eop=1;logic [7:0] rx_valid=0,rx_error=0;logic [5:0] rx_bar=0;
 logic [255:0] rx_data=0;
 logic rx_wait,rx_masknp,core_rx_wait,core_rx_masknp,core_tx_wait;
 logic [7:0] core_rx_valid,core_tx_valid,tx_valid;
 logic core_tx_sop,core_tx_eop,tx_sop,tx_eop,tx_wait=1;
 logic [255:0] core_tx_data,tx_data;
 logic rom_en,diagnostic_error;logic [13:0] rom_addr;logic [31:0] rom_data;
 logic [31:0] mem[0:16383],expected[0:34];
 logic framing_error,unsupported,malformed,fb_commit_pulse;
 logic active_buffer=0,vblank_toggle=0;logic [1:0] irq_enable;
 logic fill_busy=0,fill_done=0,fill_error=0,fill_start;logic [127:0] fill_command;
 logic blit_busy=0,blit_done=0,blit_error=0,blit_start;logic [159:0] blit_command;
 logic queue_ready=0,queue_active=0,queue_error=0,queue_submit,queue_clear_error;
 logic [2:0] queue_level=0;logic [31:0] queue_completed=0,queue_error_fence=0;
 logic [193:0] queue_command;
 logic [31:0] display_ctrl,display_color,display_mode,fb_base,fb_stride;
 logic fb_req_valid,fb_req_ready=0,fb_req_write,fb_rsp_valid=0,fb_rsp_ready,fb_rsp_error=0;
 logic [31:0] fb_req_addr,fb_rsp_rdata=0;logic [255:0] fb_req_wdata;logic [31:0] fb_req_wstrb;
 int packets=0,rom_reads=0;
 logic staged_sop,staged_eop,staged_wait,staged_mask,busy;
 logic [19:0] rx_tag=0,staged_tag,force_tag=0;
 logic hw_enabled=0;
 logic [31:0] hw_base=32'h80210000;
 wire rom_hit=staged_tag==20'h00040;
 logic [7:0] staged_valid,staged_error;
 logic [5:0] staged_bar;logic [255:0] staged_data;
 tarugx_tlp_rx_buffer rx_buffer(.clk,.rst_n,.in_sop(rx_sop),.in_eop(rx_eop),.in_valid(rx_valid),
  .in_data(rx_data),.in_bar(rx_bar),.in_error(rx_error),.in_wait(rx_wait),.in_tag(rx_tag),.out_tag(staged_tag),.out_sop(staged_sop),
  .out_eop(staged_eop),.out_valid(staged_valid),.out_data(staged_data),.out_bar(staged_bar),
  .out_error(staged_error),.out_wait(staged_wait),.busy);
 assign rx_masknp=staged_mask|busy;
 tarugx_option_rom_diagnostic #(.AUTO_ROM(32'(AUTO_MODE))) overlay(.rx_sop(staged_sop),.rx_eop(staged_eop),
  .rx_valid(staged_valid),.rx_data(staged_data),.rx_bar(staged_bar),.rx_error(staged_error),
  .rx_wait(staged_wait),.rx_masknp(staged_mask),.*);
 tarugx_pcie_gpu_path core(
  .clk,.rst(rst_n),.completer_id,.rx_sop(staged_sop),.rx_eop(staged_eop),.rx_valid(core_rx_valid),.rx_error(staged_error),
  .rx_bar_decode(staged_bar),.rx_data(staged_data),.rx_wait(core_rx_wait),.rx_masknp(core_rx_masknp),
  .tx_wait(core_tx_wait),.tx_valid(core_tx_valid),.tx_sop(core_tx_sop),.tx_eop(core_tx_eop),.tx_data(core_tx_data),
  .framing_error,.unsupported,.malformed,.fb_commit_pulse,.display_ctrl,.display_color,.display_mode,.fb_base,.fb_stride,
  .active_buffer,.vblank_toggle,.irq_enable,.fill_busy,.fill_done,.fill_error,.fill_command,.fill_start,
  .blit_busy,.blit_done,.blit_error,.blit_command,.blit_start,
  .queue_ready,.queue_level,.queue_active,.queue_completed,.queue_error,.queue_error_fence,
  .queue_command,.queue_submit,.queue_clear_error,
  .fb_req_valid,.fb_req_ready,.fb_req_write,.fb_req_addr,.fb_req_wdata,.fb_req_wstrb,
  .fb_rsp_valid,.fb_rsp_ready,.fb_rsp_error,.fb_rsp_rdata);
 tarugx_option_rom_storage #(.INIT_FILE("build/uefi/TaruGxGop.mem")) storage(.clk,.en(rom_en),.addr(rom_addr),.data(rom_data));
 always @(posedge clk) if(rom_en) rom_reads++;
 task tick;@(posedge clk);#1;endtask
 function automatic logic[31:0] swap(input logic[31:0] a);return {a[7:0],a[15:8],a[23:16],a[31:24]};endfunction
 task offer(input bit wr,input logic[5:0] bar,input int addr,input int n,input logic[7:0] be,input int data=0);
  @(negedge clk);rx_valid=wr?8'hf0:8'he0;rx_bar=bar;
  rx_tag=bar!=0?20'(bar):(force_tag!=0?force_tag:((hw_enabled && (32'(addr)&32'hffff0000)==hw_base)?20'h40:20'h0));
  rx_data={((wr?32'h40000000:0)|(32'(n)&1023)),16'h1234,8'h5a,be,32'(addr),swap(32'(data)),128'b0};
  #1;
 endtask
 task submit(input bit wr,input logic[5:0] bar,input int addr,input int n,input logic[7:0] be,input int data=0);
  offer(wr,bar,addr,n,be,data);while(rx_wait) tick();tick();
  @(negedge clk);rx_valid=0;
 endtask
 task receive(input int count);
  int pos,lanes;
  logic [255:0] held;logic [7:0] mask;
  pos=0;
  while(pos<count) begin
   while(tx_valid==0) tick();
   lanes=count-pos<8?count-pos:8;mask=8'(255<<(8-lanes));
   assert(tx_valid==mask && tx_sop==(pos==0) && tx_eop==(pos+lanes==count)) else $fatal(1,"TX framing");
   for(int i=0;i<lanes;i++) begin
    assert(tx_data[255-i*32 -:32]===expected[pos+i]) else $fatal(1,"packet %0d DW %0d got %h expected %h",packets,pos+i,tx_data[255-i*32 -:32],expected[pos+i]);
   end
   held=tx_data;
   repeat($urandom_range(1,4)) begin tick();assert(tx_valid==mask && tx_data===held && rx_masknp) else $fatal(1,"TX stall changed");end
   @(negedge clk);tx_wait=0;tick();@(negedge clk);tx_wait=1;pos+=lanes;
  end
  packets++;
 endtask
 task reg_read(input int addr,input int data);
  submit(0,1,addr,1,8'h0f);
  expected[0]=32'h4a000001;expected[1]=32'h01000004;expected[2]=32'h12345a00|32'(addr&127);expected[3]=swap(32'(data));
  receive(4);
 endtask
 task unsupported_read(input int addr);
  submit(0,0,addr,1,8'h0f);
  expected[0]=32'h0a000000;expected[1]=32'h01002000;expected[2]=32'h12345a00;receive(3);
 endtask
 task rom_read(input int offset,input int n,input int lead,input int tail);
  int pos,left,part,bc,skip;
  submit(0,0,int'(hw_base)+offset,n,n==1?8'(15<<lead):{4'(15>>(3-tail)),4'(15<<lead)});
  pos=offset;left=n;bc=n*4-lead-(3-tail);skip=lead;
  while(left>0) begin
   part=left<(128-pos%128)/4?left:(128-pos%128)/4;
   expected[0]=32'h4a000000|32'(part);expected[1]=32'h01000000|32'(bc&4095);expected[2]=32'h12345a00|32'((pos+skip)&127);
   for(int i=0;i<part;i++) expected[3+i]=swap(mem[pos/4+i]);
   receive(3+part);left-=part;pos+=part*4;bc-=part*4-skip;skip=0;
  end
 endtask
 initial begin
  $readmemh("build/uefi/TaruGxGop.mem",mem);tick();@(negedge clk);rst_n=1;
  unsupported_read('h80210000);assert(rom_reads==0) else $fatal;
  reg_read('h80200040,0);reg_read('h80200044,0);
  reg_read('h80200048,int'(AUTO_MODE));
  if(AUTO_MODE==0) begin
   submit(1,1,'h80200044,1,8'h0c,'h80210000);reg_read('h80200044,'h80210000);
   submit(1,1,'h80200040,1,8'h01,1);reg_read('h80200040,1);
  end
  hw_enabled=1;
  reg_read('h80200000,'h54475055);
  for(int p=0;p<65536;p+=4096) rom_read(p,1024,0,3);
  // Every payload size and final-beat lane count.
  for(int n=1;n<=32;n++) rom_read(0,n,0,3);
  rom_read(124,35,2,1);
  unsupported_read('h80220000);
  if(AUTO_MODE!=0) begin
   hw_base=32'ha1400000;rom_read(0,32,0,3);hw_base=32'h80210000;
   force_tag=20'h80;unsupported_read('h80210000);force_tag=0;
  end
  // A pending posted DDR write must block a ROM read.
  submit(1,4,'h80400000,1,8'h0f,'h12345678);
  submit(0,0,'h80210000,1,8'h0f);
  repeat(4) begin tick();assert(rx_wait && fb_req_valid && tx_valid==0) else $fatal(1,"posted ordering");end
  @(negedge clk);fb_req_ready=1;tick();@(negedge clk);fb_req_ready=0;
  repeat(4) begin tick();assert(rx_wait && fb_rsp_ready) else $fatal;end
  @(negedge clk);fb_rsp_valid=1;tick();@(negedge clk);fb_rsp_valid=0;
  while(rx_wait) tick();
  expected[0]=32'h4a000001;expected[1]=32'h01000004;expected[2]=32'h12345a00;expected[3]=swap(mem[0]);receive(4);
  // Disable, flush via read, then verify no ROM access.
  if(AUTO_MODE!=0) hw_enabled=0;
  else submit(1,1,'h80200040,1,8'h01,0);
  reg_read('h80200040,0);
  begin int before_reads;before_reads=rom_reads;unsupported_read('h80210000);assert(rom_reads==before_reads) else $fatal;end
  reg_read('h80200000,'h54475055);
  assert(!diagnostic_error && !framing_error) else $fatal;
  $display("ROM diagnostic + real GPU core: AUTO=%0d %0d packets, full 64KiB, all beat sizes, stalls, posted-write fence PASS",AUTO_MODE,packets);$finish;
 end
 initial begin #30000000;$fatal(1,"timeout");end
endmodule
