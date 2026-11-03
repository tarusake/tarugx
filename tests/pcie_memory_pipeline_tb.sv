// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module pcie_memory_pipeline_tb;
 logic clk=0,rst=0,mem_clk=0,mem_rst_n=0;
 always #5 clk=~clk;always #7 mem_clk=~mem_clk;
 logic [15:0] completer_id=16'h0100;
 logic rx_sop=1,rx_eop=1;logic [7:0] rx_valid=0,rx_error=0;
 logic [255:0] rx_data=0;logic [5:0] rx_bar_decode=0;
 logic rx_wait,rx_masknp,tx_wait=1,tx_sop,tx_eop;logic [7:0] tx_valid;logic [255:0] tx_data;
 logic framing_error,unsupported,malformed,fb_commit_pulse;
 logic active_buffer=0,vblank_toggle=0;logic [31:0] active_fb_base=0;logic [1:0] irq_enable;
 logic fill_busy,fill_done,fill_error,fill_start;logic [127:0] fill_command;
 logic blit_busy,blit_done,blit_error,blit_start;logic [159:0] blit_command;
 logic queue_ready,queue_active,queue_error,queue_submit,queue_clear_error;
 logic [2:0] queue_level;logic [31:0] queue_completed,queue_error_fence;
 logic [193:0] queue_command;
 logic [31:0] display_ctrl,display_color,display_mode,fb_base,fb_stride;
 logic fb_req_valid,fb_req_ready,fb_req_write,fb_rsp_valid,fb_rsp_ready,fb_rsp_error;
 logic [31:0] fb_req_addr,fb_rsp_rdata;logic [255:0] fb_req_wdata;logic [31:0] fb_req_wstrb;
 tarugx_pcie_gpu_path pcie(.*);
 logic queue_dispatch_valid,queue_dispatch_ready,queue_completion_valid,queue_completion_error;
 logic [193:0] queue_dispatch_command;
 logic queued_fill_start,queued_blit_start,fill_cdc_ready,blit_cdc_ready;
 logic [127:0] queued_fill_command;logic [159:0] queued_blit_command;
 tarugx_render_command_queue render_queue(
  .clk(clk),.rst_n(rst),.enqueue_valid(queue_submit),.enqueue_ready(queue_ready),.enqueue_command(queue_command),
  .dispatch_valid(queue_dispatch_valid),.dispatch_ready(queue_dispatch_ready),.dispatch_command(queue_dispatch_command),
  .completion_valid(queue_completion_valid),.completion_error(queue_completion_error),.clear_error(queue_clear_error),
  .level(queue_level),.active(queue_active),.completed_fence(queue_completed),
  .error_sticky(queue_error),.error_fence(queue_error_fence));
 tarugx_render_queue_dispatcher render_dispatcher(
  .clk(clk),.rst_n(rst),.dispatch_valid(queue_dispatch_valid),.dispatch_ready(queue_dispatch_ready),
  .dispatch_command(queue_dispatch_command),.legacy_fill_start(fill_start),.legacy_blit_start(blit_start),
  .fill_ready(fill_cdc_ready),.fill_done(fill_done),.fill_error(fill_error),
  .blit_ready(blit_cdc_ready),.blit_done(blit_done),.blit_error(blit_error),
  .fill_start(queued_fill_start),.fill_command(queued_fill_command),
  .blit_start(queued_blit_start),.blit_command(queued_blit_command),
  .completion_valid(queue_completion_valid),.completion_error(queue_completion_error));
 logic scan_valid=0,scan_ready,scan_rsp_valid,scan_rsp_ready=1,scan_rsp_error;
 logic [23:0] scan_addr=4096;logic [255:0] scan_rsp_data;
 logic fill_mem_valid=0,fill_mem_ready,fill_mem_rsp_valid,fill_mem_rsp_ready=1,fill_mem_rsp_error;
 logic [23:0] fill_mem_addr=0;logic [255:0] fill_mem_wdata=0;logic [31:0] fill_mem_wstrb=0;
 logic blit_mem_valid,blit_mem_ready,blit_mem_write,blit_mem_rsp_valid,blit_mem_rsp_ready,blit_mem_rsp_error;
 logic [23:0] blit_mem_addr;logic [255:0] blit_mem_wdata,blit_mem_rsp_data;logic [31:0] blit_mem_wstrb;
 logic mem_valid,mem_ready,mem_write,mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
 logic [23:0] mem_addr;logic [255:0] mem_wdata,mem_rsp_data=0;logic [31:0] mem_wstrb;
 tarugx_framebuffer_memory_pipeline path(
  .cpu_clk(clk),.cpu_rst_n(rst),.cpu_valid(fb_req_valid),.cpu_ready(fb_req_ready),
  .cpu_request({fb_req_write,fb_req_addr,fb_req_wdata,fb_req_wstrb}),
  .cpu_rsp_valid(fb_rsp_valid),.cpu_rsp_ready(fb_rsp_ready),.cpu_response({fb_rsp_error,fb_rsp_rdata}),
  .mem_clk(mem_clk),.mem_rst_n(mem_rst_n),
  .scan_valid(scan_valid),.scan_urgent(1'b0),.scan_ready(scan_ready),.scan_addr(scan_addr),
  .scan_rsp_valid(scan_rsp_valid),.scan_rsp_ready(scan_rsp_ready),.scan_rsp_data(scan_rsp_data),.scan_rsp_error(scan_rsp_error),
  .fill_valid(fill_mem_valid),.fill_ready(fill_mem_ready),.fill_addr(fill_mem_addr),
  .fill_wdata(fill_mem_wdata),.fill_wstrb(fill_mem_wstrb),.fill_rsp_valid(fill_mem_rsp_valid),
  .fill_rsp_ready(fill_mem_rsp_ready),.fill_rsp_error(fill_mem_rsp_error),
  .blit_valid(blit_mem_valid),.blit_ready(blit_mem_ready),.blit_write(blit_mem_write),.blit_addr(blit_mem_addr),
  .blit_wdata(blit_mem_wdata),.blit_wstrb(blit_mem_wstrb),.blit_rsp_valid(blit_mem_rsp_valid),
  .blit_rsp_ready(blit_mem_rsp_ready),.blit_rsp_data(blit_mem_rsp_data),.blit_rsp_error(blit_mem_rsp_error),
  .mem_valid(mem_valid),.mem_ready(mem_ready),.mem_write(mem_write),.mem_addr(mem_addr),
  .mem_wdata(mem_wdata),.mem_wstrb(mem_wstrb),.mem_rsp_valid(mem_rsp_valid),
  .mem_rsp_ready(mem_rsp_ready),.mem_rsp_data(mem_rsp_data),.mem_rsp_error(mem_rsp_error));
 logic fill_cmd_valid,fill_cmd_ready,fill_engine_done,fill_engine_error;
 logic [127:0] fill_mem_command;
 tarugx_fill_command_cdc fill_cdc(
  .src_clk(clk),.src_rst_n(rst),.src_valid(fill_start||queued_fill_start),.src_ready(fill_cdc_ready),
  .src_command(fill_start?fill_command:queued_fill_command),
  .src_busy(fill_busy),.src_done(fill_done),.src_error(fill_error),
  .dst_clk(mem_clk),.dst_rst_n(mem_rst_n),.dst_valid(fill_cmd_valid),.dst_ready(fill_cmd_ready),
  .dst_command(fill_mem_command),.dst_done(fill_engine_done),.dst_error(fill_engine_error));
 tarugx_fill_engine fill_engine(
  .clk(mem_clk),.rst_n(mem_rst_n),.cmd_valid(fill_cmd_valid),.cmd_ready(fill_cmd_ready),
  .cmd_addr(fill_mem_command[31:0]),.cmd_stride(fill_mem_command[63:32]),
  .cmd_width(fill_mem_command[79:64]),.cmd_height(fill_mem_command[95:80]),
  .cmd_color(fill_mem_command[127:96]),.busy(),.done(fill_engine_done),.error(fill_engine_error),
  .mem_valid(fill_mem_valid),.mem_ready(fill_mem_ready),.mem_addr(fill_mem_addr),
  .mem_wdata(fill_mem_wdata),.mem_wstrb(fill_mem_wstrb),.mem_rsp_valid(fill_mem_rsp_valid),
  .mem_rsp_ready(fill_mem_rsp_ready),.mem_rsp_error(fill_mem_rsp_error));
 logic blit_cmd_valid,blit_cmd_ready,blit_engine_done,blit_engine_error;
 logic [159:0] blit_mem_command;
 tarugx_render_command_cdc #(.WIDTH(160)) blit_cdc(
  .src_clk(clk),.src_rst_n(rst),.src_valid(blit_start||queued_blit_start),.src_ready(blit_cdc_ready),
  .src_command(blit_start?blit_command:queued_blit_command),
  .src_busy(blit_busy),.src_done(blit_done),.src_error(blit_error),
  .dst_clk(mem_clk),.dst_rst_n(mem_rst_n),.dst_valid(blit_cmd_valid),.dst_ready(blit_cmd_ready),
  .dst_command(blit_mem_command),.dst_done(blit_engine_done),.dst_error(blit_engine_error));
 tarugx_blit_engine blit_engine(
  .clk(mem_clk),.rst_n(mem_rst_n),.cmd_valid(blit_cmd_valid),.cmd_ready(blit_cmd_ready),
  .cmd_src_addr(blit_mem_command[31:0]),.cmd_dst_addr(blit_mem_command[63:32]),
  .cmd_src_stride(blit_mem_command[95:64]),.cmd_dst_stride(blit_mem_command[127:96]),
  .cmd_width(blit_mem_command[143:128]),.cmd_height(blit_mem_command[159:144]),
  .busy(),.done(blit_engine_done),.error(blit_engine_error),
  .mem_valid(blit_mem_valid),.mem_ready(blit_mem_ready),.mem_write(blit_mem_write),
  .mem_addr(blit_mem_addr),.mem_wdata(blit_mem_wdata),.mem_wstrb(blit_mem_wstrb),
  .mem_rsp_valid(blit_mem_rsp_valid),.mem_rsp_ready(blit_mem_rsp_ready),
  .mem_rsp_data(blit_mem_rsp_data),.mem_rsp_error(blit_mem_rsp_error));
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
 task send_two(input int address,input int low,input int high);
  @(negedge clk);rx_valid=8'hf0;rx_bar_decode=4;
  rx_data={32'h40000002,{16'h1234,8'h5a,8'hff},address,
   low[7:0],low[15:8],low[23:16],low[31:24],
   high[7:0],high[15:8],high[23:16],high[31:24],96'b0};
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
  for(int i=0;i<32;i++) begin
   int low,high;
   low='h60000000+i;high='h70000000+i;
   expected[i*2]=low;expected[i*2+1]=high;
   send_two('h80400000+i*8,low,high);
   send(0,0,'h80200000,0,15);completion('h54475055);
   assert(writes==129+i) else $fatal(1,"two-DWORD write was not combined");
   send(0,1,'h80400000+i*8,0,15);completion(low);
   send(0,1,'h80400004+i*8,0,15);completion(high);
  end
  // BAR0 fill registers -> command CDC -> fill engine -> shared DDR path.
  scan_valid=0;
  send(1,0,'h80200060,6144,15);
  send(1,0,'h80200064,32,15);
  send(1,0,'h80200068,'h00020004,15);
  send(1,0,'h8020006c,'haabbccdd,15);
  send(1,0,'h80200070,1,15);
  wait(fill_done);tick();
  assert(!fill_error && !fill_busy)else $fatal(1,"fill completion");
  for(int row=0;row<2;row++)for(int pixel=0;pixel<4;pixel++) begin
   int a=6144+row*32+pixel*4;
   assert({memory[a+3],memory[a+2],memory[a+1],memory[a]}==32'haabbccdd)else $fatal(1,"fill data");
  end
  // BAR0 blit registers -> generic command CDC -> blit engine -> shared DDR.
  for(int row=0;row<2;row++)for(int pixel=0;pixel<4;pixel++) begin
   int a=6656+row*32+pixel*4;int value='hbc000000+row*16+pixel;
   for(int b=0;b<4;b++)memory[a+b]=8'(value>>(b*8));
  end
  send(1,0,'h80200080,6656,15);
  send(1,0,'h80200084,7168,15);
  send(1,0,'h80200088,32,15);
  send(1,0,'h8020008c,32,15);
  send(1,0,'h80200090,'h00020004,15);
  send(1,0,'h80200094,1,15);
  wait(blit_done);tick();
  assert(!blit_error&&!blit_busy)else $fatal(1,"blit completion");
  for(int row=0;row<2;row++)for(int pixel=0;pixel<4;pixel++) begin
   int a=7168+row*32+pixel*4;int value='hbc000000+row*16+pixel;
   assert({memory[a+3],memory[a+2],memory[a+1],memory[a]}==value)else $fatal(1,"blit data");
  end
  // Queue a fill followed by a blit.  Fence 101 cannot retire before both
  // commands have crossed CDC and completed against the shared DDR path.
  send(1,0,'h80200060,7424,15);
  send(1,0,'h80200064,32,15);
  send(1,0,'h80200068,'h00020004,15);
  send(1,0,'h8020006c,'h11223344,15);
  send(1,0,'h802000a0,100,15);
  send(1,0,'h802000a4,1,15);
  send(1,0,'h80200080,6656,15);
  send(1,0,'h80200084,7680,15);
  send(1,0,'h80200088,32,15);
  send(1,0,'h8020008c,32,15);
  send(1,0,'h80200090,'h00020004,15);
  send(1,0,'h802000a0,101,15);
  send(1,0,'h802000a4,2,15);
  wait(queue_completed==101);tick();
  assert(!queue_error&&!queue_active&&queue_level==0)else $fatal(1,"queue completion state");
  for(int row=0;row<2;row++)for(int pixel=0;pixel<4;pixel++) begin
   int fill_a=7424+row*32+pixel*4;int blit_a=7680+row*32+pixel*4;
   int value='hbc000000+row*16+pixel;
   assert({memory[fill_a+3],memory[fill_a+2],memory[fill_a+1],memory[fill_a]}==32'h11223344)else $fatal(1,"queued fill data");
   assert({memory[blit_a+3],memory[blit_a+2],memory[blit_a+1],memory[blit_a]}==value)else $fatal(1,"queued blit data");
  end
  send(0,0,'h802000a8,0,15);completion(8);
  send(0,0,'h802000ac,0,15);completion(101);
  assert(scan_reads>128 && !framing_error && !malformed && !unsupported) else $fatal(1,"fairness/error");
  $display("PCIe + CDC + pipelined arbiter: BAR2, direct/queued fill+blit and fences PASS");$finish;
 end
 initial begin #2000000;$fatal(1,"timeout");end
endmodule
