`timescale 1ns/1ps
module pcie_debug_monitor_tb;
  bit src_clk=0, dbg_clk=0, run_src=1;
  logic src_rst_n=0, dbg_rst_n=0;
  logic [4:0] ltssm=0;
  logic [6:0] raw_flags=0;
  wire [95:0] snapshot;
  always #5 if (run_src) src_clk=~src_clk;
  always #47 dbg_clk=~dbg_clk;
  tarugx_pcie_debug_monitor dut(.*);
  task automatic settle;
    repeat(30) @(negedge dbg_clk);
  endtask
  initial begin
    #200; src_rst_n=1; dbg_rst_n=1;
    raw_flags=7'h7f;
    settle();
    assert(snapshot[95:64]==1 && snapshot[63:56]==0) else $fatal(1,"initial LTSSM");
    assert(snapshot[15:8]==8'hff) else $fatal(1,"heartbeat/live flags");
    @(negedge src_clk); ltssm=31;
    settle();
    assert(snapshot[95:64]==32'h80000001 && snapshot[63:56]==1 && snapshot[31:16]==16'h001f)
      else $fatal(1,"coherent transition snapshot");
    // Only CMU0 falls: CMU1 stays high (detect bitwise-mask regression).
    raw_flags[3]=0;
    settle();
    assert(snapshot[47:40]==1) else $fatal(1,"single CMU falling edge");
    @(negedge src_clk); run_src=0;
    raw_flags[5]=0; raw_flags[0]=0;
    repeat(1100) @(negedge dbg_clk);
    assert(!snapshot[15] && snapshot[55:48]==1 && snapshot[39:32]==1)
      else $fatal(1,"independent debug domain while source stopped");
    assert(snapshot[95:64]==32'h80000001 && snapshot[7:0]==8'hff)
      else $fatal(1,"retained history");
    run_src=1; raw_flags=7'h7f;
    settle();
    assert(snapshot[15]) else $fatal(1,"heartbeat recovery");
    for(int i=0;i<260;i++) begin
      @(negedge src_clk); ltssm=(i%2==0)?5'd1:5'd2;
      raw_flags=0; repeat(6) @(negedge dbg_clk);
      raw_flags=7'h7f; repeat(6) @(negedge dbg_clk);
    end
    settle();
    assert(snapshot[63:32]==32'hffffffff) else $fatal(1,"saturating counters");
    src_rst_n=0; dbg_rst_n=0; #200;
    assert(snapshot==0) else $fatal(1,"reset");
    $display("PASS: PCIe diagnostics CDC, clock loss, edges, history and saturation");
    $finish;
  end
  initial begin #10000000; $fatal(1,"timeout"); end
endmodule
