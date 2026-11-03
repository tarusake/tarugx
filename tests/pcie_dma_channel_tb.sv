// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module pcie_dma_channel_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic [15:0] requester_id=16'h0100;logic cmd_valid=0,cmd_ready;logic [63:0] cmd_host_address=0;
 logic [31:0] cmd_vram_address=0,cmd_length_bytes=0;logic busy,done,error,tx_ready=0;logic [3:0] error_code;
 logic [7:0] tx_valid;logic tx_sop,tx_eop;logic [255:0] tx_data;
 logic rx_sop=0,rx_eop=0;logic [7:0] rx_valid=0,rx_error=0;logic [255:0] rx_data=0;logic rx_claim,rx_wait;
 logic fb_req_valid,fb_req_ready=0;logic [31:0] fb_req_addr;logic [255:0] fb_req_wdata;logic [31:0] fb_req_wstrb;
 logic fb_rsp_valid=0,fb_rsp_ready,fb_rsp_error=0;
 logic fb_window_full;
 logic [7:0] issued_tag;logic [6:0] issued_lower;
 tarugx_pcie_dma_channel #(.TIMEOUT_CYCLES(100)) dut(.*);
 task tick;@(posedge clk);#1;endtask
 function automatic logic [31:0] wire_word(input logic [31:0] value);
  return {value[7:0],value[15:8],value[23:16],value[31:24]};
 endfunction
 task response(input int count,input int base,input int lower_offset);
  logic [159:0] payload;
  payload=0;for(int i=0;i<count;i++)payload[159-i*32-:32]=wire_word(32'hca000000+base+i);
  @(negedge clk);rx_data={{8'h4a,14'b0,10'(count)},{16'h0200,3'b0,1'b0,12'(count*4)},
   {requester_id,issued_tag,1'b0,issued_lower+7'(lower_offset)},payload};
  rx_valid=8'hff<<(5-count);rx_sop=1;rx_eop=1;#1;assert(!rx_wait)else $fatal(1,"completion ready");
  tick();assert(rx_claim)else $fatal(1,"completion claim");
  @(negedge clk);rx_valid=0;rx_sop=0;rx_eop=0;
 endtask
 task automatic writes(input int count,input int base);
  int i=0;int words;
  while(i<count)begin
   while(!fb_req_valid)tick();words = $countones(fb_req_wstrb)/4;
   if (!(words>=1&&words<=8
          &&fb_req_wstrb==(32'hffff_ffff>>((8-words)*4))
          &&fb_req_wdata[31:0]==32'hca000000+base+i)) begin
    $display("write mismatch i=%0d words=%0d strb=%08x data=%08x expected=%08x addr=%08x",
     i,words,fb_req_wstrb,fb_req_wdata[31:0],32'hca000000+base+i,fb_req_addr);
    $fatal(1,"write data");
   end
   for(int word=1;word<words;word++)
    assert(fb_req_wdata[word*32+:32]==32'hca000000+base+i+word)else $fatal(1,"burst write data");
   @(negedge clk);fb_req_ready=1;tick();@(negedge clk);fb_req_ready=0;fb_rsp_valid=1;tick();@(negedge clk);fb_rsp_valid=0;
   i+=words;
  end
 endtask
 task response_first(input int total,input int base);
  logic [159:0] payload;
  payload=0;for(int i=0;i<5;i++)payload[159-i*32-:32]=wire_word(32'hca000000+base+i);
  @(negedge clk);rx_data={{8'h4a,14'b0,10'(total)},{16'h0200,3'b0,1'b0,12'(total*4)},
   {requester_id,issued_tag,1'b0,issued_lower},payload};
  rx_valid=8'hff;rx_sop=1;rx_eop=0;#1;while(rx_wait)tick();tick();
  @(negedge clk);rx_valid=0;rx_sop=0;
 endtask
 task response_cont(input int count,input int base,input bit last);
  logic [255:0] payload;
  payload=0;for(int i=0;i<count;i++)payload[255-i*32-:32]=wire_word(32'hca000000+base+i);
  @(negedge clk);rx_data=payload;rx_valid=8'hff<<(8-count);rx_sop=0;rx_eop=last;
  #1;while(rx_wait)tick();tick();
  @(negedge clk);rx_valid=0;rx_eop=0;
 endtask
 initial begin
  repeat(3)tick();@(negedge clk);rst_n=1;
  cmd_host_address=64'h12340000_00001020;cmd_vram_address=32'h3f0000;cmd_length_bytes=32'd28;cmd_valid=1;tick();@(negedge clk);cmd_valid=0;
  while(!tx_sop)tick();assert(tx_valid==8'hf0&&tx_data[255:224]==32'h20000007&&tx_data[223:208]==requester_id)else $fatal(1,"MRd64");issued_tag=tx_data[207:200];issued_lower=tx_data[134:128];
  repeat(2)begin tick();assert(tx_valid==8'hf0)else $fatal(1,"TX stall");end
  @(negedge clk);tx_ready=1;tick();@(negedge clk);tx_ready=0;response(5,0,0);writes(5,0);
  response(2,5,20);writes(2,5);
  wait(done);assert(!error&&!busy)else $fatal(1,"DMA channel completion");
  // One aligned request carries a three-beat 64-byte Completion.
  cmd_host_address=64'h12340000_00002000;cmd_vram_address=32'h3e0000;cmd_length_bytes=32'd64;
  cmd_valid=1;tick();@(negedge clk);cmd_valid=0;
  while(!tx_sop)tick();assert(tx_data[255:224]==32'h20000010)else $fatal(1,"16-DWORD MRd64");issued_tag=tx_data[207:200];issued_lower=tx_data[134:128];
  @(negedge clk);tx_ready=1;tick();@(negedge clk);tx_ready=0;
  response_first(16,16);writes(5,16);
  response_cont(8,21,0);writes(8,21);
  response_cont(3,29,1);writes(3,29);
  wait(done);assert(!error&&!busy)else $fatal(1,"multi-beat DMA completion");
  // A Completion for another tag is left for another consumer.
  @(negedge clk);rx_data[255:224]=32'h4a000001;rx_data[191:160]={requester_id,8'hff,8'b0};rx_valid=8'hf0;rx_sop=1;rx_eop=1;#1;
  assert(!rx_claim&&!rx_wait)else $fatal(1,"foreign tag claimed");
  $display("PCIe DMA channel: raw MRd64/CplD round trip, TX/RX stalls and tag isolation PASS");$finish;
 end
 initial begin #500000;$fatal(1,"timeout");end
endmodule
