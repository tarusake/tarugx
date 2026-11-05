// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module memory_arbiter_scan_priority_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic scan_urgent=1;
 logic cpu_ready,cpu_rsp_valid,cpu_rsp_error,scan_ready,scan_rsp_valid,scan_rsp_error;
 logic [255:0] cpu_rsp_data,scan_rsp_data;
 logic mem_valid,mem_ready=1,mem_write,mem_rsp_valid,mem_rsp_ready;
 logic [23:0] mem_addr;logic [255:0] mem_wdata,mem_rsp_data=0;logic [31:0] mem_wstrb;
 int pending=0,accepted=0,urgent_scan=0,urgent_cpu=0;
 int scan_run=0,cpu_run=0,max_scan_run=0,max_cpu_run=0;
 tarugx_memory_arbiter_pipelined dut(
  .clk,.rst_n,.cpu_valid(1'b1),.cpu_ready,.cpu_write(1'b1),.cpu_addr(24'h200),
  .cpu_wdata('0),.cpu_wstrb('1),.cpu_rsp_valid,.cpu_rsp_ready(1'b1),.cpu_rsp_data,.cpu_rsp_error,
  .scan_valid(1'b1),.scan_urgent,.scan_ready,.scan_addr(24'h100),
  .scan_rsp_valid,.scan_rsp_ready(1'b1),.scan_rsp_data,.scan_rsp_error,
  .mem_valid,.mem_ready,.mem_write,.mem_addr,.mem_wdata,.mem_wstrb,
  .mem_rsp_valid,.mem_rsp_ready,.mem_rsp_data,.mem_rsp_error(1'b0));
 assign mem_rsp_valid=pending!=0;
 always @(posedge clk)if(rst_n)begin
  if(mem_valid&&mem_ready)begin
   accepted++;
   if(mem_addr==24'h100)begin
    assert(!mem_write)else $fatal(1,"scan write");
    scan_run++;cpu_run=0;if(scan_run>max_scan_run)max_scan_run=scan_run;
    if(scan_urgent)urgent_scan++;
   end else begin
    assert(mem_addr==24'h200&&mem_write)else $fatal(1,"CPU routing");
    cpu_run++;scan_run=0;if(cpu_run>max_cpu_run)max_cpu_run=cpu_run;
    if(scan_urgent)urgent_cpu++;
   end
  end
  case({mem_valid&&mem_ready,mem_rsp_valid&&mem_rsp_ready})
   2'b10:pending++;
   2'b01:pending--;
   default:;
  endcase
  assert(pending>=0&&pending<=8)else $fatal(1,"pending %0d",pending);
  assert(!cpu_rsp_error&&!scan_rsp_error)else $fatal(1,"response error");
 end
 initial begin
  repeat(3)@(posedge clk);rst_n=1;
  wait(accepted>=40);
  assert(urgent_scan>=29&&urgent_cpu>=9)else
   $fatal(1,"urgent ratio scan=%0d cpu=%0d",urgent_scan,urgent_cpu);
  assert(max_scan_run==3&&max_cpu_run==1)else
   $fatal(1,"urgent runs scan=%0d cpu=%0d",max_scan_run,max_cpu_run);
  scan_urgent=0;scan_run=0;cpu_run=0;max_scan_run=0;max_cpu_run=0;
  wait(accepted>=100);
  assert(max_cpu_run==4&&max_scan_run==1)else
   $fatal(1,"buffered runs scan=%0d cpu=%0d",max_scan_run,max_cpu_run);
  $display("Pipelined memory arbiter: urgent 3:1 scan priority, buffered 1:4 fairness and ownership PASS");
  $finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
