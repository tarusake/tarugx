// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module memory_arbiter_pipelined_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cpu_ready,cpu_rsp_valid,cpu_rsp_error,fill_ready,fill_rsp_valid,fill_rsp_error;
 logic blit_ready,blit_rsp_valid,blit_rsp_error;logic [255:0] blit_rsp_data;
 logic out_valid,out_ready=1,out_write,out_rsp_valid=0,out_rsp_ready,out_rsp_error=0;
 logic [21:0] out_addr;logic [255:0] out_wdata,out_rsp_data=0;logic [31:0] out_wstrb;
 tarugx_render_memory_arbiter dut(
  .clk,.rst_n,.cpu_valid(1'b1),.cpu_ready,.cpu_write(1'b1),.cpu_addr(22'h100),
  .cpu_wdata({8{32'hc0c0c0c0}}),.cpu_wstrb(32'hffffffff),
  .cpu_rsp_valid,.cpu_rsp_ready(1'b1),.cpu_rsp_data(),.cpu_rsp_error,
  .fill_valid(1'b1),.fill_ready,.fill_addr(22'h300),.fill_wdata({8{32'hf1f1f1f1}}),
  .fill_wstrb(32'h0f0f0f0f),.fill_rsp_valid,.fill_rsp_ready(1'b1),.fill_rsp_error,
  .blit_valid(1'b1),.blit_ready,.blit_write(1'b0),.blit_addr(22'h500),
  .blit_wdata('0),.blit_wstrb('0),.blit_rsp_valid,.blit_rsp_ready(1'b1),
  .blit_rsp_data,.blit_rsp_error,
  .out_valid,.out_ready,.out_write,.out_addr,.out_wdata,.out_wstrb,
  .out_rsp_valid,.out_rsp_ready,.out_rsp_data,.out_rsp_error);
 int cpu_requests=0,fill_requests=0,blit_requests=0,cpu_responses=0,fill_responses=0,blit_responses=0,delay=0;
 always @(posedge clk)if(rst_n)begin
  if(out_valid&&out_ready)begin
   if(out_addr==22'h100)begin assert(out_write&&out_wstrb==32'hffffffff);cpu_requests++;end
   else if(out_addr==22'h300)begin assert(out_write&&out_wstrb==32'h0f0f0f0f);fill_requests++;end
   else begin assert(out_addr==22'h500&&!out_write&&out_wstrb==0);blit_requests++;end
   delay<=2;
  end
  if(delay!=0)begin delay<=delay-1;if(delay==1)out_rsp_valid<=1;end
  if(out_rsp_valid&&out_rsp_ready)out_rsp_valid<=0;
  if(cpu_rsp_valid)begin assert(!cpu_rsp_error);cpu_responses++;end
  if(fill_rsp_valid)begin assert(!fill_rsp_error);fill_responses++;end
  if(blit_rsp_valid)begin assert(!blit_rsp_error&&blit_rsp_data==out_rsp_data);blit_responses++;end
 end
 initial begin
  repeat(3)@(posedge clk);rst_n=1;
  wait(cpu_responses>=20&&fill_responses>=20&&blit_responses>=20);
  assert(cpu_requests==cpu_responses&&fill_requests==fill_responses&&blit_requests==blit_responses)else $fatal(1,"ownership");
  assert(cpu_requests-fill_requests inside {[-1:1]}&&fill_requests-blit_requests inside {[-1:1]})else $fatal(1,"fairness");
  $display("Render pre-arbiter: CPU/fill/blit fairness, read/write routing and response ownership PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
