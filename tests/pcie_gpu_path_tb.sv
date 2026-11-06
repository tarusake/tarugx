// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module pcie_gpu_path_tb;
 logic clk=0,rst=0; always #5 clk=~clk;
 logic [15:0] completer_id=16'h0100;
 logic rx_sop=1,rx_eop=1; logic [7:0] rx_valid=0,rx_error=0;
 logic [255:0] rx_data=0; logic [5:0] rx_bar_decode=0;
 logic rx_wait,rx_masknp,tx_wait=1,tx_sop,tx_eop; logic [7:0] tx_valid;
 logic [255:0] tx_data;
 logic framing_error,unsupported,malformed,fb_commit_pulse;
 logic active_buffer=0,vblank_toggle=0; logic [31:0] active_fb_base=0;
 logic [1:0] irq_enable;
 logic fill_busy=0,fill_done=0,fill_error=0,fill_start;logic [127:0] fill_command;
 logic blit_busy=0,blit_done=0,blit_error=0,blit_start;logic [159:0] blit_command;
 logic triangle_busy=0,triangle_done=0,triangle_error=0,triangle_start;
 logic [31:0] triangle_writes=0;logic [735:0] triangle_command;
 logic queue_ready=0,queue_active=0,queue_error=0,queue_submit,queue_clear_error;
 logic [2:0] queue_level=0;logic [31:0] queue_completed=0,queue_error_fence=0;
 logic [769:0] queue_command;
 logic [31:0] display_ctrl,display_color,display_mode,fb_base,fb_stride;
 logic fb_req_valid,fb_req_ready=0,fb_req_write,fb_rsp_valid=0,fb_rsp_ready,fb_rsp_error=0;
 logic [31:0] fb_req_addr,fb_rsp_rdata=0; logic [255:0] fb_req_wdata; logic [31:0] fb_req_wstrb;
 tarugx_pcie_gpu_path dut(.*);
 task tick; @(posedge clk); #1; endtask
 task packet(input bit wr,input bit bar2,input int address,input int data);
  @(negedge clk);rx_valid=wr?8'hf0:8'he0;rx_bar_decode=bar2?6'b000100:6'b000001;
  rx_data={wr?32'h40000001:32'h00000001,32'h12345a0f,address,
   data[7:0],data[15:8],data[23:16],data[31:24],128'b0};
 endtask
 task clear_rx; @(negedge clk);rx_valid=0; endtask
 task completion(input int expected);
  while (tx_valid == 0) tick();
  repeat(4) begin
   assert(tx_valid==8'hf0 && tx_sop && tx_eop && rx_masknp) else $fatal(1,"completion handshake");
   assert(tx_data[255:224]==32'h4a000001 && tx_data[223:192]==32'h01000004 && tx_data[191:168]==24'h12345a) else $fatal(1,"completion headers");
   assert(tx_data[159:128]=={expected[7:0],expected[15:8],expected[23:16],expected[31:24]}) else $fatal(1,"completion data");tick();
  end
  @(negedge clk);tx_wait=0;tick();@(negedge clk);tx_wait=1;
 endtask
 initial begin
  tick();@(negedge clk);rst=1;
  packet(1,1,'h80401234,'h12345678);tick();clear_rx();
  while (!fb_req_valid) tick();
  assert(fb_req_valid && fb_req_write && fb_req_addr=='h1234
         && fb_req_wdata==(256'('h12345678)<<160) && fb_req_wstrb==(32'hf<<20)) else $fatal;
  // A BAR0 read must not pass a pending posted BAR2 write.
  packet(0,0,'h80200000,0);
  repeat(4) begin tick();assert(rx_wait && tx_valid==0 && fb_req_valid) else $fatal;end
  @(negedge clk);fb_req_ready=1;tick();@(negedge clk);fb_req_ready=0;
  repeat(4) begin tick();assert(rx_wait && tx_valid==0 && fb_rsp_ready) else $fatal;end
  @(negedge clk);fb_rsp_valid=1;tick();@(negedge clk);fb_rsp_valid=0;
  while (rx_wait) tick();
  tick();clear_rx();completion('h54475055);
  packet(0,1,'h80401234,0);tick();clear_rx();
  while (!fb_req_valid) tick();
  assert(fb_req_valid && !fb_req_write) else $fatal;
  @(negedge clk);fb_req_ready=1;tick();@(negedge clk);fb_req_ready=0;
  fb_rsp_rdata='hdeadbeef;fb_rsp_valid=1;tick();@(negedge clk);fb_rsp_valid=0;
  completion('hdeadbeef);
  assert(!framing_error && !unsupported && !malformed) else $fatal;
  // Unmatched requests must neither hang reads nor alias register writes.
  for (int wide=0; wide<2; wide++) begin
   packet(0,0,'h90000000,0);rx_bar_decode=0;
   if (wide != 0) begin rx_data[255:224]=32'h20000001;rx_valid=8'hf0;end
   tick();clear_rx();
   while (tx_valid == 0) tick();
   repeat(4) begin
    assert(tx_valid==8'he0 && tx_sop && tx_eop && rx_masknp) else $fatal(1,"UR handshake");
    assert(tx_data[255:224]==32'h0a000000 && tx_data[223:192]==32'h01002000
      && tx_data[191:168]==24'h12345a) else $fatal(1,"UR status/requester/tag");
    assert(!fb_req_valid) else $fatal(1,"unmatched read reached DDR");tick();
   end
   @(negedge clk);tx_wait=0;tick();@(negedge clk);tx_wait=1;
  end
  packet(1,0,'h90000014,'h00abcdef);rx_bar_decode=6'b000010;
  tick();clear_rx();repeat(4) begin
   tick();assert(tx_valid==0 && !fb_req_valid && display_color==0) else $fatal(1,"unmatched posted write side effect");
  end
  packet(0,0,'h80200000,0);tick();clear_rx();completion('h54475055);
  $display("PCIe GPU path: unmatched MRd32/64 UR and posted-write isolation PASS");
  $display("PCIe GPU path: BAR2 byte order, BAR0 posted-write fence, completion stalls/tags PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
