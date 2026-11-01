// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module scanout_debug_monitor_tb;
 logic mem_clk=0,pixel_clk=0,mem_rst_n=0,pixel_rst_n=0;
 always #5 mem_clk=~mem_clk; always #17 pixel_clk=~pixel_clk;
 logic push=0,pop=0,underflow=0;
 logic scan_valid=0,scan_ready=0,scan_rsp_valid=0,scan_rsp_ready=1;
 logic mem_valid=0,mem_ready=0,mem_write=0,mem_rsp_valid=0,mem_rsp_ready=1;
 logic [183:0] snapshot;
 tarugx_scanout_debug_monitor dut(.*);
 task tick; @(posedge mem_clk);#1; endtask
 initial begin
  tick();@(negedge mem_clk);mem_rst_n=1;pixel_rst_n=1;
  push=1;repeat(300)tick();@(negedge mem_clk);push=0;repeat(4)tick();
  assert(snapshot[9:0]==300 && snapshot[22]) else $fatal(1,"prefill level %h",snapshot);
  @(negedge pixel_clk);pop=1;repeat(100)@(posedge pixel_clk);@(negedge pixel_clk);pop=0;
  repeat(6)tick();
  assert(snapshot[19:10]==200 && snapshot[9:0]==300) else $fatal(1,"Gray level %h",snapshot);
  // Four arbitration stall cycles, followed by a three-cycle scan response.
  @(negedge mem_clk);scan_valid=1;repeat(4)tick();
  @(negedge mem_clk);scan_ready=1;tick();
  @(negedge mem_clk);scan_valid=0;scan_ready=0;mem_valid=1;mem_ready=1;tick();
  @(negedge mem_clk);mem_valid=0;mem_ready=0;tick();
  @(negedge mem_clk);scan_rsp_valid=1;mem_rsp_valid=1;tick();
  @(negedge mem_clk);scan_rsp_valid=0;mem_rsp_valid=0;
  assert(snapshot[183:168]==4 && snapshot[167:152]==3 && snapshot[151:136]==2)
    else $fatal(1,"latency %h",snapshot);
  assert(snapshot[119:88]==1 && snapshot[87:56]==1 && snapshot[55:24]==4) else $fatal(1,"counts");
  // Write response includes a cycle of response backpressure.
  mem_write=1;mem_valid=1;mem_ready=1;tick();
  @(negedge mem_clk);mem_valid=0;mem_ready=0;mem_rsp_valid=1;mem_rsp_ready=0;tick();
  @(negedge mem_clk);mem_rsp_ready=1;tick();
  @(negedge mem_clk);mem_rsp_valid=0;
  assert(snapshot[135:120]==2) else $fatal(1,"write latency");
  // Exercise pointer wrap with a bounded queue; 200 entries remain.
  repeat(10)begin
   push=1;repeat(100)tick();@(negedge mem_clk);push=0;
   @(negedge pixel_clk);pop=1;repeat(100)@(posedge pixel_clk);@(negedge pixel_clk);pop=0;
   repeat(6)tick();
  end
  assert(dut.level==200)else $fatal(1,"pointer wrap");
  @(negedge mem_clk);scan_valid=1;repeat(65540)tick();
  assert(snapshot[183:168]==65535) else $fatal(1,"saturating wait");
  @(negedge pixel_clk);underflow=1;repeat(6)tick();
  assert(snapshot[23])else $fatal(1,"freeze CDC");
  begin logic [183:0] held; held=snapshot; repeat(20)tick();
   assert(snapshot==held)else $fatal(1,"frozen snapshot changed");end
  @(negedge mem_clk);mem_rst_n=0;pixel_rst_n=0;tick();
  assert(snapshot==184'(1023<<10))else $fatal(1,"reset");
  $display("Scanout monitor: Gray level, wrap, latency, stalls, saturation, freeze and reset PASS");$finish;
 end
 initial begin #2000000;$fatal(1,"timeout");end
endmodule
