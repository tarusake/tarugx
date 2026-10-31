`timescale 1ns/1ps
module ddr_debug_uart_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic [3:0] status=4'b1100;
 logic [287:0] snapshot={32'h46523032,248'b0,8'hfd};
 wire tx;
 tarugx_ddr_debug_uart #(.CLKS_PER_BIT(4),.FRAME_GAP_CYCLES(100)) dut(.*);
 task receive(output byte value);
  @(negedge tx);
  repeat(2) @(posedge clk);#1;assert(!tx) else $fatal(1,"start");
  for(int i=0;i<8;i++) begin repeat(4) @(posedge clk);#1;value[i]=tx;end
  repeat(4) @(posedge clk);#1;assert(tx) else $fatal(1,"stop");
 endtask
 string expected;byte received;
 initial begin
  #30;@(negedge clk);rst_n=1;
  for(int frame=0;frame<2;frame++) begin
   expected=$sformatf("D1100X%072h\r\n",snapshot);
   expected=expected.toupper();
   assert(expected.len()==80) else $fatal;
   for(int i=0;i<80;i++) begin
    receive(received);
    assert(received==expected[i]) else $fatal(1,"frame %0d byte %0d expected %h actual %h",frame,i,expected[i],received);
    // Inputs may change mid-frame; all fields must retain the first-byte snapshot.
    if(i==0) begin
     @(negedge clk);status=4'b0011;snapshot='1;
    end
   end
   repeat(50) begin @(posedge clk);#1;assert(tx) else $fatal(1,"missing inter-frame gap");end
   @(negedge clk);status=4'b1100;snapshot={32'h46523032,248'b0,8'h21};
  end
  @(negedge clk);rst_n=0;#1;assert(tx) else $fatal(1,"idle reset");
  $display("Diagnostic UART: two complete 80-byte frames, uppercase snapshot and 8N1 PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
