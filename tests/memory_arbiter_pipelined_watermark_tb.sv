// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module memory_arbiter_pipelined_watermark_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cpu_valid=0,cpu_ready,cpu_rsp_valid,cpu_rsp_error;
 logic scan_valid=0,scan_urgent=0,scan_ready,scan_rsp_valid,scan_rsp_error;
 logic mem_valid,mem_rsp_valid=0,mem_rsp_ready;logic [23:0] mem_addr;
 logic [255:0] mem_rsp_data=0;
 tarugx_memory_arbiter_pipelined dut(
  .clk,.rst_n,.cpu_valid,.cpu_ready,.cpu_write(1'b1),.cpu_addr(24'hc00001),
  .cpu_wdata('1),.cpu_wstrb('1),.cpu_rsp_valid,.cpu_rsp_ready(1'b1),
  .cpu_rsp_data(),.cpu_rsp_error,.scan_valid,.scan_urgent,.scan_ready,
  .scan_addr(24'h500001),.scan_rsp_valid,.scan_rsp_ready(1'b1),
  .scan_rsp_data(),.scan_rsp_error,.mem_valid,.mem_ready(1'b1),.mem_write(),
  .mem_addr,.mem_wdata(),.mem_wstrb(),.mem_rsp_valid,.mem_rsp_ready,
  .mem_rsp_data,.mem_rsp_error(1'b0));
 int phase=0,scan_grants=0,cpu_grants=0,run_length=0,max_run=0;
 always @(posedge clk)if(rst_n)begin
  mem_rsp_valid<=mem_valid;
  if(mem_valid)mem_rsp_data<={232'b0,mem_addr};
  if(scan_valid&&scan_ready)begin
   scan_grants++;run_length++;
   if(run_length>max_run)max_run=run_length;
  end
  if(cpu_valid&&cpu_ready)begin
   cpu_grants++;
   if(phase==1)assert(run_length==8)else $fatal(1,"urgent burst length %0d",run_length);
   run_length=0;
  end
  if(cpu_rsp_valid)begin assert(!cpu_rsp_error);end
  else if(scan_rsp_valid)begin assert(!scan_rsp_error);end
 end
 initial begin
  int phase2_scan,phase3_scan;
  repeat(3)@(posedge clk);rst_n=1;
  phase=1;scan_valid=1;scan_urgent=1;cpu_valid=1;
  wait(cpu_grants==3);
  assert(scan_grants==24&&max_run==8)else $fatal(1,"urgent fairness scan=%0d max=%0d",scan_grants,max_run);
  phase=2;scan_urgent=0;phase2_scan=scan_grants;
  wait(cpu_grants==9);
  assert(scan_grants>phase2_scan)else $fatal(1,"nonurgent scan progress");
  phase=3;cpu_valid=0;scan_urgent=1;phase3_scan=scan_grants;
  wait(scan_grants>=phase3_scan+20);
  scan_valid=0;repeat(8)@(posedge clk);
  $display("Pipelined watermark arbitration: urgent burst=8, CPU progress, round robin, scan-only progress PASS");
  $finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
