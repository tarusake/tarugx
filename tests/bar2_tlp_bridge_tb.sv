// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module bar2_tlp_bridge_tb;
 logic clk=0,rst=0;always #5 clk=~clk;
 logic [15:0] completer_id=16'h0100;
 logic rx_valid=0,rx_ready,tx_valid,tx_ready=0;
 logic [31:0] rx_header_dw0=0,rx_header_dw1=0,rx_header_dw2=0,rx_header_dw3=0,rx_payload_dw0=0,rx_payload_dw1=0;
 logic [31:0] tx_header_dw0,tx_header_dw1,tx_header_dw2,tx_payload_dw0;
 logic unsupported,malformed,req_valid,req_ready=0,req_write;
 logic [31:0] req_addr,rsp_rdata=0;logic [63:0] req_wdata;logic [7:0] req_wstrb;
 logic rsp_valid=0,rsp_ready,rsp_error=0;
 tarugx_bar2_tlp_bridge dut(.*);
 task tick;@(posedge clk);#1;endtask
 task send(input int h0,input int address);
  @(negedge clk);assert(rx_ready) else $fatal;
  rx_valid=1;rx_header_dw0=h0;rx_header_dw1='h1234560f;
  rx_header_dw2=address;rx_payload_dw0='hcafebabe;
  tick();@(negedge clk);rx_valid=0;
 endtask
 task send_two(input int address);
  @(negedge clk);assert(rx_ready) else $fatal;
  rx_valid=1;rx_header_dw0='h40000002;rx_header_dw1='h123456ff;
  rx_header_dw2=address;rx_payload_dw0='hcafebabe;rx_payload_dw1='h89abcdef;
  tick();@(negedge clk);rx_valid=0;
 endtask
 task memory(input bit wr,input bit err);
  repeat(3) begin tick();assert(req_valid && req_write==wr && req_addr=='h12340 && req_wstrb==15 && !rx_ready && !tx_valid) else $fatal;end
  @(negedge clk);req_ready=1;tick();@(negedge clk);req_ready=0;
  repeat(4) begin tick();assert(!rx_ready && !tx_valid && !req_valid && rsp_ready) else $fatal;end
  @(negedge clk);rsp_valid=1;rsp_rdata='hdeadbeef;rsp_error=err;
  tick();@(negedge clk);rsp_valid=0;rsp_error=0;
 endtask
 task complete(input bit data,input bit[2:0] status);
  tick();
  repeat(3) begin
   assert(tx_valid && !rx_ready && tx_header_dw1[15:13]==status && tx_header_dw2[31:8]==24'h123456) else $fatal;
   assert(tx_header_dw0==(data?'h4a000001:'h0a000000)) else $fatal;
   if(data) assert(tx_payload_dw0=='hdeadbeef && tx_header_dw2[6:0]=='h40 && tx_header_dw1[11:0]==4) else $fatal;
   tick();
  end
  @(negedge clk);tx_ready=1;tick();@(negedge clk);tx_ready=0;
 endtask
 task memory_two;
  tick();
  assert(req_valid && req_write && req_addr=='h12340 && req_wdata==64'h89abcdefcafebabe && req_wstrb==255) else $fatal;
  @(negedge clk);req_ready=1;tick();@(negedge clk);req_ready=0;
  @(negedge clk);rsp_valid=1;tick();@(negedge clk);rsp_valid=0;
  assert(rx_ready && !tx_valid) else $fatal;
 endtask
 initial begin
  tick();@(negedge clk);rst=1;
  send('h40000001,'h80412340);memory(1,0);assert(rx_ready && !tx_valid) else $fatal;
  send_two('h80412340);memory_two();
  send('h00000001,'h80412340);memory(0,0);complete(1,0);
  send('h00000001,'h80412340);memory(0,1);complete(0,4);
  send('h00000002,'h80412340);assert(!req_valid) else $fatal;complete(0,1);
  send('h40000002,'h80412340);tick();assert(rx_ready && !req_valid && !tx_valid) else $fatal;
  send('h40000001,'h80412340);@(negedge clk);rst=0;tick();assert(rx_ready && !req_valid && !tx_valid) else $fatal;
  $display("BAR2: posted ordering, stalled requests/completions, tags, CA/UR and reset PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
