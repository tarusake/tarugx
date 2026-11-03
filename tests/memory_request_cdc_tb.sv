// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module memory_request_cdc_tb;
 logic src_clk=0,dst_clk=0,src_rst_n=0,dst_rst_n=0;
 always #5 src_clk=~src_clk;
 always #7 dst_clk=~dst_clk;
 logic src_valid=0,src_ready,src_rsp_valid,src_rsp_ready=0;
 logic [104:0] src_request=0;
 logic [32:0] src_response;
 logic dst_valid,dst_ready=0,dst_rsp_valid=0,dst_rsp_ready;
 logic [104:0] dst_request;
 logic [32:0] dst_response=0;
 tarugx_memory_request_cdc #(.REQUEST_WIDTH(105),.DEPTH(8)) dut(.*);
 function automatic logic [104:0] request_for(input logic [31:0] value);
  return {value[8:0],32'b0,value,~value};
 endfunction
 initial begin
  repeat(3) @(negedge src_clk);
  src_rst_n=1;dst_rst_n=1;
  for(int base=0;base<96;base+=8) begin
   // Fill the request FIFO before returning any response.  The old mailbox
   // cannot pass this test because it permits only one request in flight.
   for(int n=0;n<8;n++) begin
    @(negedge src_clk);while(!src_ready)@(negedge src_clk);
    src_request=request_for(base+n);src_valid=1;
    @(negedge src_clk);src_valid=0;
   end
   for(int n=0;n<8;n++) begin
    wait(dst_valid);
    assert(dst_request==request_for(base+n))
     else $fatal(1,"request FIFO order");
    repeat(n%3)begin @(negedge dst_clk);assert(dst_valid)else $fatal(1,"request hold");end
    @(negedge dst_clk);dst_ready=1;@(negedge dst_clk);dst_ready=0;
   end
   for(int n=0;n<8;n++) begin
    @(negedge dst_clk);while(!dst_rsp_ready)@(negedge dst_clk);
    dst_response={1'((base+n)&1),32'((base+n)^32'hdeadbeef)};dst_rsp_valid=1;
    @(negedge dst_clk);dst_rsp_valid=0;
   end
   for(int n=0;n<8;n++) begin
    wait(src_rsp_valid);
    assert(src_response=={1'((base+n)&1),32'((base+n)^32'hdeadbeef)})
     else $fatal(1,"response FIFO order");
    repeat(n%3)begin @(negedge src_clk);assert(src_rsp_valid)else $fatal(1,"response hold");end
    @(negedge src_clk);src_rsp_ready=1;@(negedge src_clk);src_rsp_ready=0;
   end
  end
  @(negedge src_clk);src_valid=1;
  @(negedge src_clk);src_valid=0;src_rst_n=0;dst_rst_n=0;
  repeat(3) @(negedge src_clk);
  assert(!src_rsp_valid && !dst_valid) else $fatal;
  src_rst_n=1;dst_rst_n=1;
  repeat(5) @(negedge src_clk);
  assert(src_ready && !src_rsp_valid && !dst_valid) else $fatal;
  $display("Memory CDC: eight in-flight ordered requests/replies, backpressure and common reset PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
