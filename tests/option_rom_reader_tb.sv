// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
module option_rom_reader_tb;
  logic clk=0; always #5 clk=~clk;
  logic rst_n=0, req_valid=0, req_ready, req_write=0;
  logic [31:0] req_addr=0, rsp_data, rom_data=0;
  logic rsp_valid, rsp_ready=0, rsp_error, rom_en;
  logic [13:0] rom_addr;
  logic [31:0] memory [0:16383];
  int reads=0;
  tarugx_option_rom_reader dut(.*);
  initial $readmemh("build/uefi/TaruGxGop.mem",memory);
  always @(posedge clk) if(rom_en) begin rom_data<=memory[rom_addr]; reads<=reads+1; end
  task automatic access(input logic [31:0] address, input bit wr, input bit error_expected);
    int before_reads;
    before_reads=reads;
    @(negedge clk); assert(req_ready);req_addr=address;req_write=wr;req_valid=1;
    @(negedge clk);req_valid=0;req_addr=32'hdeadbeef;req_write=0;
    while(!rsp_valid) @(negedge clk);
    assert(rsp_error==error_expected) else $fatal(1,"error response");
    assert(reads-before_reads==(error_expected?0:1)) else $fatal(1,"storage access count");
    repeat(5) begin
      assert(rsp_valid && !req_ready && !rom_en);
      assert(rsp_data==(error_expected?32'b0:memory[address>>2])) else $fatal(1,"ROM data");
      @(negedge clk);
    end
    rsp_ready=1;@(negedge clk);rsp_ready=0;assert(req_ready && !rsp_valid);
  endtask
  initial begin
    repeat(2) @(negedge clk);rst_n=1;
    access(0,0,0);access(32'h200,0,0);access(32'hfffc,0,0);
    for(int i=0;i<16384;i++)access(i*4,0,0);
    access(32'h10000,0,1);access(3,0,1);access(0,1,1);
    @(negedge clk);req_valid=1;req_addr=0;
    @(negedge clk);req_valid=0;rst_n=0;
    @(negedge clk);assert(!rsp_valid && !rom_en);rst_n=1;
    access(32'h204,0,0);
    $display("PASS: ROM contents, range/alignment, read-only, backpressure, pending reset");$finish;
  end
  initial begin #5000000; $fatal(1,"timeout"); end
endmodule
