// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module framebuffer_request_arbiter_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cpu_req_valid=0,cpu_req_ready,cpu_req_write=0;logic [31:0] cpu_req_addr=0,cpu_rsp_rdata;
 logic [63:0] cpu_req_wdata=0;logic [7:0] cpu_req_wstrb=0;logic cpu_rsp_valid,cpu_rsp_ready=0,cpu_rsp_error;
 logic dma_req_valid=0,dma_req_ready;logic [31:0] dma_req_addr=0;logic [255:0] dma_req_wdata=0;
 logic [31:0] dma_req_wstrb=0;logic dma_rsp_valid,dma_rsp_ready=0,dma_rsp_error;
 logic out_req_valid,out_req_ready=0,out_req_write;logic [31:0] out_req_addr,out_rsp_rdata=0;
 logic [255:0] out_req_wdata;logic [31:0] out_req_wstrb;logic out_rsp_valid=0,out_rsp_ready,out_rsp_error=0;
 tarugx_framebuffer_request_arbiter dut(.*);
 task tick;@(posedge clk);#1;endtask
 task check(input logic condition,input string message);if(!condition)$fatal(1,"%s",message);endtask
 task response(input logic [31:0] data,input logic error);
  @(negedge clk);while(!out_rsp_ready)@(negedge clk);
  out_rsp_rdata=data;out_rsp_error=error;out_rsp_valid=1;tick();
  @(negedge clk);out_rsp_valid=0;
 endtask
 initial begin
  repeat(3)tick();@(negedge clk);rst_n=1;
  // A tie selects CPU first; DMA remains pending and is accepted on the next
  // cycle without waiting for the CPU response.
  cpu_req_valid=1;cpu_req_addr=32'h104;cpu_req_wdata=64'h1111;cpu_req_wstrb=8'h0f;
  dma_req_valid=1;dma_req_addr=32'h200;dma_req_wdata=256'h2222;dma_req_wstrb=32'hf0;
  #1;check(cpu_req_ready&&!dma_req_ready,"CPU must win first tie");tick();
  check(out_req_valid&&out_req_addr==32'h104&&!out_req_write&&out_req_wstrb==32'hf0,
        "CPU request hold/routing");
  @(negedge clk);cpu_req_valid=0;out_req_ready=1;tick();
  check(out_req_valid&&out_req_addr==32'h200&&out_req_write&&out_req_wdata==256'h2222,
        "pending DMA was not pipelined");
  @(negedge clk);dma_req_valid=0;tick();@(negedge clk);out_req_ready=0;
  // Responses remain ordered and are routed to their original owners.
  response(32'hcafebabe,0);check(cpu_rsp_valid&&!dma_rsp_valid&&cpu_rsp_rdata==32'hcafebabe,"CPU owner");
  repeat(2)begin tick();check(cpu_rsp_valid,"CPU response stall");end
  @(negedge clk);cpu_rsp_ready=1;tick();@(negedge clk);cpu_rsp_ready=0;
  response(0,1);check(dma_rsp_valid&&!cpu_rsp_valid&&dma_rsp_error,"DMA owner/error");
  @(negedge clk);dma_rsp_ready=1;tick();@(negedge clk);dma_rsp_ready=0;
  check(dut.count==0&&!dut.issuing,"initial owners did not retire");
  // Fill all eight ownership slots before returning any response.
  out_req_ready=1;
  for(int n=0;n<8;n++)begin
   dma_req_addr=32'h1000+n*32;dma_req_wdata=256'(n);dma_req_wstrb=32'hffffffff;dma_req_valid=1;
   #1;
   while(!dma_req_ready)tick();tick();@(negedge clk);dma_req_valid=0;
  end
  tick();@(negedge clk);out_req_ready=0;check(!out_req_valid,"request pipeline did not drain");
  dma_rsp_ready=1;
  for(int n=0;n<8;n++)begin response(0,0);tick();check(!dma_rsp_error,"pipelined DMA response");end
  @(negedge clk);dma_rsp_ready=0;
  $display("Framebuffer request arbiter: fairness, eight in-flight owners, ordered responses and stalls PASS");$finish;
 end
 initial begin #200000;$fatal(1,"timeout");end
endmodule
