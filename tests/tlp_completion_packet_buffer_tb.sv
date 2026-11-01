`timescale 1ns/1ps
module tlp_completion_packet_buffer_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic in_valid=0,in_ready,in_sop=0,in_eop=0;
 logic [31:0] in_data=0;
 logic tx_wait=1,tx_sop,tx_eop,idle,framing_error;
 logic [7:0] tx_valid;logic [255:0] tx_data;
 tarugx_tlp_completion_packet_buffer dut(.*);
 task tick;@(posedge clk);#1;endtask
 task put(input logic[31:0] data,input bit sop,input bit eop);
  @(negedge clk);assert(in_ready) else $fatal;in_valid=1;in_data=data;in_sop=sop;in_eop=eop;
  tick();@(negedge clk);in_valid=0;
 endtask
 task good;
  put(32'h4a000001,1,0);put(32'h01000004,0,0);put(32'h12345a00,0,0);put(32'h12345678,0,1);
  assert(tx_valid==8'hf0 && tx_sop && tx_eop && tx_data[255:128]==128'h4a0000010100000412345a0078563412) else $fatal(1,"valid packet");
  repeat(3) begin tick();assert(!in_ready && tx_valid==8'hf0) else $fatal;end
  @(negedge clk);tx_wait=0;tick();@(negedge clk);tx_wait=1;assert(idle && tx_valid==0) else $fatal;
 endtask
 initial begin
  tick();@(negedge clk);rst_n=1;
  put(32'h4a000021,1,0);assert(framing_error && tx_valid==0) else $fatal;
  put(0,0,1);assert(idle) else $fatal;good();
  put(32'h4a000001,1,0);put(0,0,1);assert(framing_error && tx_valid==0 && idle) else $fatal;good();
  put(32'h0a000000,1,0);put(0,0,0);put(0,0,0);assert(framing_error && tx_valid==0) else $fatal;
  put(0,0,1);assert(idle) else $fatal;good();
  put(32'h4a000001,1,0);rst_n=0;tick();assert(idle && tx_valid==0) else $fatal;
  @(negedge clk);rst_n=1;good();
  put(32'h0a000000,1,0);put(0,0,0);put(0,0,1);assert(tx_valid==8'he0) else $fatal;
  rst_n=0;tick();assert(idle && tx_valid==0) else $fatal;
  $display("Completion packet buffer malformed/oversize/drain/reset PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
