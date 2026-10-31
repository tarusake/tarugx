`timescale 1ns/1ps
module memory_request_cdc_tb;
 logic src_clk=0,dst_clk=0,src_rst_n=0,dst_rst_n=0;
 always #5 src_clk=~src_clk;
 always #7 dst_clk=~dst_clk;
 logic src_valid=0,src_ready,src_rsp_valid,src_rsp_ready=0;
 logic [68:0] src_request=0;
 logic [32:0] src_response;
 logic dst_valid,dst_ready=0,dst_rsp_valid=0,dst_rsp_ready;
 logic [68:0] dst_request;
 logic [32:0] dst_response=0;
 tarugx_memory_request_cdc dut(.*);
 initial begin
  repeat(3) @(negedge src_clk);
  src_rst_n=1;dst_rst_n=1;
  for(int n=0;n<100;n++) begin
   @(negedge src_clk);assert(src_ready) else $fatal;
   src_request={5'(n),32'(n*123),32'(~n)};src_valid=1;
   @(negedge src_clk);src_valid=0;
   wait(dst_valid);
   repeat(n%4+1) begin
    @(negedge dst_clk);assert(dst_valid && dst_request==src_request && !src_ready) else $fatal(1,"request hold/CDC");
   end
   dst_ready=1;
   @(negedge dst_clk);dst_ready=0;
   repeat(n%5) @(negedge dst_clk);
   assert(dst_rsp_ready && !dst_valid) else $fatal;
   dst_response={1'(n),32'(n^32'hdeadbeef)};dst_rsp_valid=1;
   @(negedge dst_clk);dst_rsp_valid=0;
   wait(src_rsp_valid);
   repeat(4) begin
    @(negedge src_clk);assert(src_response==dst_response && src_rsp_valid && !src_ready) else $fatal(1,"response hold/CDC");
   end
   src_rsp_ready=1;
   @(negedge src_clk);src_rsp_ready=0;
  end
  @(negedge src_clk);src_valid=1;
  @(negedge src_clk);src_valid=0;src_rst_n=0;dst_rst_n=0;
  repeat(3) @(negedge src_clk);
  assert(!src_rsp_valid && !dst_valid) else $fatal;
  src_rst_n=1;dst_rst_n=1;
  repeat(5) @(negedge src_clk);
  assert(src_ready && !src_rsp_valid && !dst_valid) else $fatal;
  $display("Memory CDC: 100 asynchronous request/reply transfers, backpressure and common reset PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
