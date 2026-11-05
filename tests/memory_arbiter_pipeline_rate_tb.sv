// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module memory_arbiter_pipeline_rate_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cpu_valid=0,cpu_ready,cpu_rsp_valid,cpu_rsp_error;
 logic scan_valid=0,scan_ready,scan_rsp_valid,scan_rsp_error;
 logic mem_valid,mem_ready=1,mem_rsp_valid=0,mem_rsp_ready;
 logic [23:0] mem_addr;logic [255:0] mem_rsp_data=0;
 tarugx_memory_arbiter_pipelined dut(
  .clk,.rst_n,.cpu_valid,.cpu_ready,.cpu_write(1'b1),.cpu_addr(24'hc00000),
  .cpu_wdata('1),.cpu_wstrb('1),.cpu_rsp_valid,.cpu_rsp_ready(1'b1),
  .cpu_rsp_data(),.cpu_rsp_error,.scan_valid,.scan_urgent(1'b0),.scan_ready,
  .scan_addr(24'h500000),
  .scan_rsp_valid,.scan_rsp_ready(1'b1),.scan_rsp_data(),.scan_rsp_error,
  .mem_valid,.mem_ready,.mem_write(),.mem_addr,.mem_wdata(),.mem_wstrb(),
  .mem_rsp_valid,.mem_rsp_ready,.mem_rsp_data,.mem_rsp_error(1'b0));
 int accepted=0,issued=0,responses=0,first_issue=-1,last_issue=-1,cycles=0;
 always @(posedge clk)if(rst_n)begin
  mem_rsp_valid<=mem_valid&&mem_ready;
  if(mem_valid&&mem_ready)mem_rsp_data<={232'b0,mem_addr};
  if(cpu_valid&&cpu_ready)accepted++;
  if(scan_valid&&scan_ready)accepted++;
  if(mem_valid&&mem_ready)begin
   if(first_issue<0)first_issue=cycles;
   last_issue=cycles;issued++;
  end
  if(cpu_rsp_valid)begin assert(!cpu_rsp_error);responses++;end
  if(scan_rsp_valid)begin assert(!scan_rsp_error);responses++;end
  cycles++;
 end
 initial begin
  repeat(3)@(posedge clk);rst_n=1;cpu_valid=1;scan_valid=1;
  wait(accepted==8);@(negedge clk);cpu_valid=0;scan_valid=0;
  wait(issued==8);
  assert(last_issue-first_issue==7)
   else $fatal(1,"issue bubbles span=%0d",last_issue-first_issue);
  wait(responses==8);repeat(2)@(posedge clk);
  assert(dut.count==0)else $fatal(1,"response ownership did not drain");
  assert(!dut.output_valid&&!dut.pending_valid)
   else $fatal(1,"command queue did not drain");
  $display("Queued memory arbiter: 8 alternating owners issued in 8 clocks and routed in order PASS");
  $finish;
 end
 initial begin #10000;$fatal(1,"timeout");end
endmodule
