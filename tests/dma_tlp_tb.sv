// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module dma_tlp_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic request_valid=0;logic [15:0] requester_id=16'h0100;logic [7:0] request_tag=8'h5a;
 logic [63:0] host_address=64'h12345678_9abcdef0;logic [6:0] length_dw=1;logic tx_wait=0;
 logic request_ready,request_error,tx_sop,tx_eop;logic [7:0] tx_valid;logic [255:0] tx_data;
 tarugx_dma_read_request_tx tx(.*);
 logic rx_sop=0,rx_eop=0;logic [7:0] rx_valid=0,rx_error=0;logic [255:0] rx_data=0;
 logic clear=0;
 logic [15:0] expected_requester=16'h0100;logic [7:0] expected_tag=8'h5a;
 logic [7:0] active_valid=8'b00000001;logic [63:0] active_tags=64'h000000000000005a;logic completion_ready=0;
 logic rx_wait,completion_valid,completion_has_data,completion_error;logic [2:0] completion_status;
 logic [3:0] completion_length_dw;logic [11:0] completion_byte_count;logic [6:0] completion_lower_address;logic [7:0] completion_tag;
 logic [255:0] completion_data;
 tarugx_dma_completion_rx rx(.*);
 task tick;@(posedge clk);#1;endtask
 function automatic logic [31:0] wire_word(input logic [31:0] value);
  return {value[7:0],value[15:8],value[23:16],value[31:24]};
 endfunction
 task consume(input int count,input int base,input int lower);
  wait(completion_valid);#1;
  assert(!completion_error&&completion_has_data&&completion_length_dw==4'(count)
         &&completion_lower_address==7'(lower)&&completion_tag==expected_tag)else $fatal(1,"chunk metadata");
  for(int i=0;i<count;i++)assert(completion_data[i*32+:32]==32'h11000000+base+i)
   else $fatal(1,"chunk data %0d",base+i);
  completion_ready=1;tick();@(negedge clk);completion_ready=0;
 endtask
 task first_beat(input int total,input int base,input int lower,input bit eop);
  logic [159:0] payload;
  payload=0;for(int i=0;i<5;i++)payload[159-i*32-:32]=wire_word(32'h11000000+base+i);
  @(negedge clk);rx_data={{8'h4a,14'b0,10'(total)},{16'h0200,3'b0,1'b0,12'(total*4)},
    {expected_requester,expected_tag,1'b0,7'(lower)},payload};
  rx_valid=total<5?(8'hff<<(5-total)):8'hff;rx_sop=1;rx_eop=eop;tick();
  @(negedge clk);rx_valid=0;rx_sop=0;rx_eop=0;
 endtask
 task continuation(input int count,input int base,input bit eop);
  logic [255:0] payload;
  payload=0;for(int i=0;i<count;i++)payload[255-i*32-:32]=wire_word(32'h11000000+base+i);
  @(negedge clk);rx_data=payload;rx_valid=8'hff<<(8-count);rx_sop=0;rx_eop=eop;tick();
  @(negedge clk);rx_valid=0;rx_eop=0;
 endtask
 initial begin
  repeat(3)tick();@(negedge clk);rst_n=1;
  request_valid=1;#1;
  assert(request_ready&&!request_error&&tx_sop&&tx_eop&&tx_valid==8'hf0)else $fatal(1,"request handshake");
  assert(tx_data[255:224]==32'h20000001&&tx_data[223:192]==32'h01005a0f)else $fatal(1,"request header 0/1");
  tx_wait=1;#1;assert(!request_ready&&tx_valid==8'hf0)else $fatal(1,"request stall");
  tx_wait=0;length_dw=64;#1;assert(request_ready&&tx_data[255:224]==32'h20000040
   &&tx_data[223:192]==32'h01005aff)else $fatal(1,"64 DWORD request");
  host_address[11:0]=12'hfc0;#1;assert(request_error&&!request_ready&&tx_valid==0)
   else $fatal(1,"4 KiB crossing request");
  host_address=64'h12345678_9abcdef0;
  host_address[0]=1;#1;assert(request_error&&!request_ready&&tx_valid==0)else $fatal(1,"unaligned request");
  request_valid=0;host_address[0]=0;

  // A 64-DWORD CplD arrives as 5 + seven lots of 8 + 3 payload DWORDs.
  first_beat(64,0,'h10,0);consume(5,0,'h10);
  continuation(8,5,0);consume(8,5,'h24);
  continuation(8,13,0);consume(8,13,'h44);
  continuation(8,21,0);consume(8,21,'h64);
  continuation(8,29,0);consume(8,29,'h04);
  continuation(8,37,0);consume(8,37,'h24);
  continuation(8,45,0);consume(8,45,'h44);
  continuation(8,53,0);consume(8,53,'h64);
  continuation(3,61,1);consume(3,61,'h04);

  // A held output chunk must backpressure the following raw beat.
  first_beat(6,0,'h20,0);wait(completion_valid);
  @(negedge clk);rx_data={wire_word(32'h11000005),224'b0};rx_valid=8'h80;rx_eop=1;#1;
  assert(rx_wait)else $fatal(1,"continuation backpressure");
  completion_ready=1;tick();@(negedge clk);completion_ready=0;#1;
  assert(!rx_wait)else $fatal(1,"backpressure release");
  tick();@(negedge clk);rx_valid=0;rx_eop=0;consume(1,5,'h34);

  // Unsupported-request Completion is surfaced as an error chunk.
  @(negedge clk);rx_data={{8'h0a,24'b0},{16'h0200,3'b001,1'b0,12'd4},
    {expected_requester,expected_tag,8'h10},160'b0};rx_valid=8'he0;rx_sop=1;rx_eop=1;tick();
  @(negedge clk);rx_valid=0;rx_sop=0;rx_eop=0;wait(completion_valid);#1;
  assert(completion_error&&!completion_has_data&&completion_status==1)else $fatal(1,"UR completion");
  completion_ready=1;tick();

  // Starting a new DMA command discards a held chunk and partial packet.
  @(negedge clk);completion_ready=0;clear=1;tick();@(negedge clk);clear=0;#1;
  assert(!completion_valid&&!rx_wait)else $fatal(1,"completion receiver clear");
  first_beat(6,0,'h20,0);wait(completion_valid);
  @(negedge clk);clear=1;tick();@(negedge clk);clear=0;#1;
  assert(!completion_valid&&!rx_wait)else $fatal(1,"partial packet clear");
  $display("DMA TLP codec: MRd64 and CplD up to 64 DWORDs, 4 KiB guard, stalls and errors PASS");$finish;
 end
 initial begin #500000;$fatal(1,"timeout");end
endmodule
