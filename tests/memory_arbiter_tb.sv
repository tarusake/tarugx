`timescale 1ns/1ps
module memory_arbiter_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cpu_valid=0,cpu_ready,cpu_write=1,cpu_rsp_valid,cpu_rsp_ready=0,cpu_rsp_error;
 logic [21:0] cpu_addr=32,scan_addr=64,mem_addr;
 logic [255:0] cpu_wdata=256'h123, cpu_rsp_data,scan_rsp_data,mem_wdata,mem_rsp_data=256'h987;
 logic [31:0] cpu_wstrb='hff00,mem_wstrb;
 logic scan_valid=0,scan_ready,scan_rsp_valid,scan_rsp_ready=0,scan_rsp_error;
 logic mem_valid,mem_ready=0,mem_write,mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
 tarugx_memory_arbiter dut(.*);
 task tick;@(posedge clk);#1;endtask
 initial begin
  tick();@(negedge clk);rst_n=1;cpu_valid=1;scan_valid=1;
  for(int n=0;n<20;n++) begin
   bit scan;scan=(n%2==0);
   #1;assert(scan_ready==scan && cpu_ready==!scan) else $fatal(1,"fairness");
   tick();@(negedge clk);
   repeat(3) begin
    assert(mem_valid && !cpu_ready && !scan_ready && mem_addr==(scan?64:32) && mem_write==!scan) else $fatal;
    assert(mem_wdata==(scan?0:cpu_wdata) && mem_wstrb==(scan?0:cpu_wstrb)) else $fatal;
    tick();
   end
   @(negedge clk);mem_ready=1;tick();@(negedge clk);mem_ready=0;
   mem_rsp_valid=1;mem_rsp_error=1'(n);
   repeat(3) begin
    tick();assert(scan_rsp_valid==scan && cpu_rsp_valid==!scan && !mem_rsp_ready) else $fatal;
    assert((scan?scan_rsp_data:cpu_rsp_data)==mem_rsp_data) else $fatal;
    assert((scan?scan_rsp_error:cpu_rsp_error)==mem_rsp_error) else $fatal;
   end
   @(negedge clk);cpu_rsp_ready=!scan;scan_rsp_ready=scan;tick();
   @(negedge clk);mem_rsp_valid=0;cpu_rsp_ready=0;scan_rsp_ready=0;
  end
  cpu_valid=0;scan_valid=0;rst_n=0;tick();assert(!mem_valid && !cpu_rsp_valid && !scan_rsp_valid) else $fatal;
  $display("Memory arbiter: contention fairness, stable stalls, response ownership and reset PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
