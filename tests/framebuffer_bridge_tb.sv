`timescale 1ns/1ps
module framebuffer_bridge_tb;
  logic clk=0, rst_n=0;
  always #5 clk=~clk;
  logic req_valid=0,req_ready,req_write=0;
  logic [31:0] req_addr=0,req_wdata=0;
  logic [3:0] req_wstrb=0;
  logic rsp_valid,rsp_ready=0,rsp_error;
  logic [31:0] rsp_rdata;
  logic mem_valid,mem_ready=0,mem_write;
  logic [21:0] mem_addr;
  logic [255:0] mem_wdata;
  logic [31:0] mem_wstrb;
  logic mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
  logic [255:0] mem_rsp_data=0;
  byte unsigned model[32];
  tarugx_framebuffer_bridge dut(.*);
  task tick; @(posedge clk); #1; endtask
  task send(input bit wr,input int addr,input int data,input bit [3:0] be);
    @(negedge clk);
    assert(req_ready) else $fatal(1,"request not ready");
    req_valid=1; req_write=wr; req_addr=addr; req_wdata=data; req_wstrb=be;
    tick(); @(negedge clk); req_valid=0;
  endtask
  task reply(input int expected,input bit error);
    assert(rsp_valid && rsp_rdata==expected && rsp_error==error)
      else $fatal(1,"response mismatch: %h error=%b",rsp_rdata,rsp_error);
    repeat(3) begin
      tick();
      assert(rsp_valid && !req_ready && !mem_valid && rsp_rdata==expected && rsp_error==error)
        else $fatal(1,"response changed under backpressure");
    end
    @(negedge clk); rsp_ready=1; tick();
    @(negedge clk); rsp_ready=0;
    assert(req_ready && !rsp_valid) else $fatal;
  endtask
  task backend(input bit wr,input int addr,input bit immediate,input bit error);
    logic [255:0] saved_data;
    logic [31:0] saved_strobe;
    saved_data=mem_wdata; saved_strobe=mem_wstrb;
    repeat(3) begin
      assert(mem_valid && mem_write==wr && mem_addr==22'(addr & 'h3fffe0)) else $fatal;
      assert(mem_wdata==saved_data && mem_wstrb==saved_strobe && !req_ready) else $fatal;
      tick();
    end
    // Model bytes independently of the DWORD lane packing.
    if(wr) for(int b=0;b<32;b++) if(mem_wstrb[b]) model[b]=mem_wdata[b*8+:8];
    for(int b=0;b<32;b++) mem_rsp_data[b*8+:8]=model[b];
    @(negedge clk); mem_ready=1; mem_rsp_error=error; mem_rsp_valid=immediate;
    tick(); @(negedge clk); mem_ready=0; mem_rsp_valid=0;
    if(!immediate) begin
      repeat(4) begin tick(); assert(!req_ready && !mem_valid && !rsp_valid) else $fatal; end
      @(negedge clk); mem_rsp_valid=1;
      assert(mem_rsp_ready) else $fatal;
      tick(); @(negedge clk); mem_rsp_valid=0;
    end
  endtask
  initial begin
    int value,expected;
    repeat(2) tick(); @(negedge clk); rst_n=1;
    for(int lane=0;lane<8;lane++) for(int mask=0;mask<16;mask++) begin
      for(int b=0;b<32;b++) model[b]=8'ha5;
      value='h12345678 ^ (lane * 'h01010101);
      send(1,32'h3fffe0+lane*4,value,4'(mask));
      if(mask!=0) begin
        assert(mem_wstrb==(32'(mask) << (lane*4))) else $fatal(1,"strobe mapping");
        backend(1,'h3fffe0+lane*4,1'(mask),0);
      end else assert(!mem_valid) else $fatal(1,"zero strobe emitted write");
      reply(0,0);
      for(int b=0;b<32;b++) begin
        if(b/4==lane && (((mask >> (b%4)) & 1)!=0))
          assert(model[b]==8'(value >> ((b%4)*8))) else $fatal(1,"enabled byte");
        else assert(model[b]==8'ha5) else $fatal(1,"disabled byte modified");
      end
      expected=0;
      for(int b=0;b<4;b++) expected |= int'(model[lane*4+b]) << (b*8);
      send(0,'h3fffe0+lane*4,0,0);
      backend(0,'h3fffe0+lane*4,1'(lane),0); reply(expected,0);
    end
    send(1,2,0,15); reply(0,1);
    send(0,'h400000,0,0); reply(0,1);
    send(1,0,123,15); backend(1,0,0,1); reply(0,1);
    send(0,0,0,0); backend(0,0,1,1); reply(123,1);
    send(1,0,456,15);
    @(negedge clk); rst_n=0; tick();
    assert(!mem_valid && !rsp_valid) else $fatal(1,"reset did not cancel pending request");
    @(negedge clk); rst_n=1; tick(); assert(req_ready) else $fatal;
    $display("Framebuffer: byte masks, lanes, stalls, ordering, errors and reset PASS");
    $finish;
  end
  initial begin #1000000; $fatal(1,"timeout"); end
endmodule
