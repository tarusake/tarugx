// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module memory_self_test_tb;
  logic clk=0,rst_n=0,start=0,calibrated=0;
  always #5 clk=~clk;
  logic busy,done,failed;
  logic [23:0] fail_addr,mem_addr;
  logic [255:0] fail_data;
  logic fail_error;
  logic mem_valid,mem_ready=0,mem_write;
  logic [255:0] mem_wdata,mem_rsp_data=0;
  logic [31:0] mem_wstrb;
  logic mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
  logic [255:0] ram[16];
  tarugx_memory_self_test dut(.*);
  task tick; @(posedge clk); #1; endtask
  task run_test(input int fault);
    int count,index;
    logic [255:0] held;
    @(negedge clk); rst_n=0; start=0; calibrated=0; mem_ready=0; mem_rsp_valid=0;
    tick(); @(negedge clk); rst_n=1; start=1;
    repeat(3) begin tick(); assert(!busy && !mem_valid && !done) else $fatal; end
    @(negedge clk); calibrated=1; tick(); start=0; count=0;
    while(!done) begin
      assert(mem_valid && busy) else $fatal;
      index=int'(mem_addr)>>5;
      assert(index==(count%16) && mem_write==(count<16)) else $fatal(1,"ordering");
      assert(mem_wstrb=='1) else $fatal;
      held=mem_wdata;
      repeat(3) begin tick(); assert(mem_valid && mem_wdata==held) else $fatal(1,"stall"); end
      if(mem_write) ram[index]=mem_wdata;
      @(negedge clk); mem_ready=1; tick();
      @(negedge clk); mem_ready=0;
      repeat(2) begin tick(); assert(!mem_valid && busy) else $fatal; end
      @(negedge clk);
      mem_rsp_data=ram[index];
      if(fault==1 && count==19) mem_rsp_data[101]=~mem_rsp_data[101];
      mem_rsp_error=(fault==2 && count==2);
      mem_rsp_valid=1;
      assert(mem_rsp_ready) else $fatal;
      tick(); @(negedge clk); mem_rsp_valid=0; mem_rsp_error=0;
      count++;
    end
    assert(!busy && !mem_valid && failed==(fault!=0)) else $fatal;
    if(fault==0) assert(count==32) else $fatal;
    if(fault==1) begin
      assert(count==20 && fail_addr==96 && !fail_error) else $fatal;
      assert((fail_data ^ ram[3]) == (256'b1 << 101)) else $fatal(1,"failure capture");
    end
    if(fault==2) assert(count==3 && fail_addr==64 && fail_error) else $fatal;
    start=1; repeat(3) begin tick(); assert(done && !mem_valid) else $fatal(1,"unexpected restart"); end
  endtask
  initial begin
    run_test(0); run_test(1); run_test(2);
    $display("Memory self-test: calibration gate, stalls, full readback, corruption and backend error PASS");
    $finish;
  end
  initial begin #100000; $fatal(1,"timeout"); end
endmodule
