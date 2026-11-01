`timescale 1ns/1ps
module pcie_memory_integration_tb;
 logic clk=0,rst=0,mem_clk=0,mem_rst_n=0;
 always #5 clk=~clk;always #7 mem_clk=~mem_clk;
 logic [15:0] completer_id=16'h0100;
 logic rx_sop=1,rx_eop=1;logic [7:0] rx_valid=0,rx_error=0;
 logic [255:0] rx_data=0;logic [5:0] rx_bar_decode=0;
 logic rx_wait,rx_masknp,tx_wait=1,tx_sop,tx_eop;logic [7:0] tx_valid;logic [255:0] tx_data;
 logic framing_error,unsupported,malformed,fb_commit_pulse;
 logic [31:0] display_ctrl,display_color,display_mode,fb_base,fb_stride;
 logic fb_req_valid,fb_req_ready,fb_req_write,fb_rsp_valid,fb_rsp_ready,fb_rsp_error;
 logic [31:0] fb_req_addr,fb_req_wdata,fb_rsp_rdata;logic [3:0] fb_req_wstrb;
 tarugx_pcie_gpu_path pcie(.*);
 logic scan_valid=0,scan_ready,scan_rsp_valid,scan_rsp_ready=1,scan_rsp_error;
 logic [21:0] scan_addr=4096;logic [255:0] scan_rsp_data;
 logic mem_valid,mem_ready,mem_write,mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
 logic [21:0] mem_addr;logic [255:0] mem_wdata,mem_rsp_data=0;logic [31:0] mem_wstrb;
 logic scan_urgent=0;
 tarugx_framebuffer_memory_path path(
  .cpu_clk(clk),.cpu_rst_n(rst),.cpu_valid(fb_req_valid),.cpu_ready(fb_req_ready),
  .cpu_request({fb_req_write,fb_req_addr,fb_req_wdata,fb_req_wstrb}),
  .cpu_rsp_valid(fb_rsp_valid),.cpu_rsp_ready(fb_rsp_ready),.cpu_response({fb_rsp_error,fb_rsp_rdata}),
  .mem_clk(mem_clk),.mem_rst_n(mem_rst_n),
  .scan_valid(scan_valid),.scan_urgent(scan_urgent),.scan_ready(scan_ready),.scan_addr(scan_addr),
  .scan_rsp_valid(scan_rsp_valid),.scan_rsp_ready(scan_rsp_ready),.scan_rsp_data(scan_rsp_data),.scan_rsp_error(scan_rsp_error),
  .mem_valid(mem_valid),.mem_ready(mem_ready),.mem_write(mem_write),.mem_addr(mem_addr),
  .mem_wdata(mem_wdata),.mem_wstrb(mem_wstrb),.mem_rsp_valid(mem_rsp_valid),
  .mem_rsp_ready(mem_rsp_ready),.mem_rsp_data(mem_rsp_data),.mem_rsp_error(mem_rsp_error));
 byte unsigned memory[0:8191];int cycle=0,delay_count=0,scan_reads=0,writes=0;
 assign mem_ready=delay_count==0 && !mem_rsp_valid && cycle%5!=0;
 always @(posedge mem_clk) if(mem_rst_n) begin
  cycle<=cycle+1;
  if(mem_valid && mem_ready) begin
   assert(mem_addr[4:0]==0 && mem_addr<8192) else $fatal(1,"backend address");
   if(mem_write) begin
    for(int b=0;b<32;b++) if(mem_wstrb[b]) memory[int'(mem_addr)+b]=mem_wdata[b*8+:8];
    writes++;
   end
   for(int b=0;b<32;b++) mem_rsp_data[b*8+:8]<=memory[int'(mem_addr)+b];
   delay_count<=4;
  end
  if(delay_count!=0) begin
   delay_count<=delay_count-1;
   if(delay_count==1) mem_rsp_valid<=1;
  end
  if(mem_rsp_valid && mem_rsp_ready) mem_rsp_valid<=0;
  if(scan_rsp_valid && scan_rsp_ready) begin
   assert(!scan_rsp_error && scan_rsp_data=={32{8'h5a}}) else $fatal(1,"scan response ownership");
   scan_reads++;
  end
 end
 task tick;@(posedge clk);#1;endtask
 task send(input bit wr,input bit bar2,input int address,input int data,input bit[3:0] be);
  @(negedge clk);rx_valid=wr?8'hf0:8'he0;rx_bar_decode=bar2?4:1;
  rx_data={wr?32'h40000001:32'h00000001,{16'h1234,8'h5a,4'b0,be},address,
   data[7:0],data[15:8],data[23:16],data[31:24],128'b0};
  #1;while(rx_wait) begin tick();@(negedge clk);#1;end
  tick();@(negedge clk);rx_valid=0;
 endtask
 task completion(input int expected);
  while(tx_valid==0) tick();
  repeat(3) begin
   assert(tx_valid==8'hf0 && tx_data[255:224]==32'h4a000001 &&
    tx_data[223:192]==32'h01000004 && tx_data[191:168]==24'h12345a) else $fatal(1,"completion");
   assert(tx_data[159:128]=={expected[7:0],expected[15:8],expected[23:16],expected[31:24]}) else $fatal(1,"readback expected %h got %h",expected,tx_data[159:128]);
   tick();
  end
  @(negedge clk);tx_wait=0;tick();@(negedge clk);tx_wait=1;
 endtask
 int expected[64];int data_value;bit[3:0] mask;
 initial begin
  foreach(memory[i]) memory[i]=8'h5a;
  foreach(expected[i]) expected[i]='h5a5a5a5a;
  #30;@(negedge clk);rst=1;mem_rst_n=1;scan_valid=1;
  for(int i=0;i<128;i++) begin
   data_value='h12340000+i;mask=4'((i%15)+1);
   for(int b=0;b<4;b++) if(mask[b]) expected[i%64][b*8+:8]=data_value[b*8+:8];
   send(1,1,'h80400000+(i%64)*4,data_value,mask);
   // BAR0 must wait for the posted write to reach the modeled memory.
   send(0,0,'h80200000,0,15);completion('h54475055);
   assert(writes==i+1) else $fatal(1,"posted write fence");
   send(0,1,'h80400000+(i%64)*4,0,15);completion(expected[i%64]);
  end
  assert(scan_reads>128 && !framing_error && !malformed && !unsupported) else $fatal(1,"fairness/error");
  $display("PCIe + CDC + lane bridge + arbiter: 128 masked writes/readbacks, BAR0 fences, concurrent scanout PASS");$finish;
 end
 initial begin #2000000;$fatal(1,"timeout");end
endmodule
