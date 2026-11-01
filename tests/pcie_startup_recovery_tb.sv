// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module pcie_startup_recovery_tb;
  bit clk=0;
  always #5 clk=~clk;
  logic rst_n=0, ready_async=0, perst_async=0, link_up=0;
  wire reset_request, fired, inhibited;
  tarugx_pcie_startup_recovery #(.WAIT_CYCLES(8),.PULSE_CYCLES(3)) dut(.*);
  task automatic ticks(input int n);
    repeat(n) @(negedge clk);
  endtask
  task automatic restart;
    rst_n=0;ready_async=0;perst_async=0;link_up=0;
    ticks(2);rst_n=1;ticks(2);
  endtask
  initial begin
    restart();
    ticks(30);assert(!fired && !reset_request) else $fatal(1,"no readiness");
    ready_async=1;ticks(30);assert(!fired) else $fatal(1,"PERST hold");
    perst_async=1;ticks(5);ready_async=0;ticks(4);
    assert(!fired) else $fatal(1,"unstable readiness");
    ready_async=1;
    wait(reset_request); @(negedge clk);
    assert(fired && !inhibited) else $fatal(1,"fired status");
    ready_async=0;perst_async=0;
    ticks(2);assert(reset_request) else $fatal(1,"pulse shortened");
    ticks(1);assert(!reset_request) else $fatal(1,"pulse too long");
    ready_async=1;perst_async=1;ticks(40);
    assert(fired && !reset_request) else $fatal(1,"repeated reset");
    restart();ready_async=1;perst_async=1;link_up=1;ticks(3);link_up=0;
    ticks(40);assert(inhibited && !fired) else $fatal(1,"reset after established link");
    restart();ready_async=1;perst_async=1;
    // A link assertion on the deadline must take priority over starting reset.
    wait(dut.wait_count==7);@(negedge clk);link_up=1;
    ticks(2);assert(!fired && inhibited) else $fatal(1,"deadline race");
    $display("PASS: readiness debounce, PERST gate, pulse width, one shot, link inhibit");
    $finish;
  end
  initial begin #100000; $fatal(1,"timeout"); end
endmodule
