`timescale 1ns/1ps
module pcie_reset_scope_tb;
  bit clk=0;
  always #47 clk=~clk;
  logic rst_n=0;
  logic [5:0] raw=0;
  wire [55:0] snapshot;
  tarugx_pcie_reset_scope dut(.*);
  task automatic settle;
    repeat(8) @(negedge clk);
  endtask
  initial begin
    #200;rst_n=1;#13;raw=6'h1e;settle();
    assert(snapshot[55:48]==0 && snapshot[7:0]==8'h1e) else $fatal(1,"idle");
    #13;raw=6'h22;settle();
    assert(snapshot[55:48]==1 && snapshot[47:40]==8'h1e) else $fatal(1,"trigger/pre sample");
    assert(snapshot[39:32]==2 && snapshot[31:24]==8'h1d) else $fatal(1,"raw levels during pulse");
    #13;raw=6'h23;settle();
    assert(snapshot[39:32]==3 && snapshot[31:24]==8'h1d) else $fatal(1,"during union");
    #13;raw=6'h1e;settle();
    #13;raw=6'h1f;settle();
    assert(snapshot==56'h031e031d1f011f) else $fatal(1,"after union/byte layout");
    raw=6'h20;settle();raw=0;settle();
    assert(snapshot[47:24]==24'h1e031d) else $fatal(1,"first pulse preserved");
    rst_n=0;#100;assert(snapshot==0) else $fatal(1,"reset");
    $display("PASS: reset scope captures raw levels before/during/after first pulse");
    $finish;
  end
  initial begin #100000;$fatal(1,"timeout");end
endmodule
