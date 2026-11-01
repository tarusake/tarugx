module pcie_rx_probe_tb;
logic clk=0;always #5 clk=~clk;
logic rst_n=0,rx_sop=0,rx_eop=1,rx_wait=0;
logic [7:0] rx_valid=0,rx_error=0;
logic [5:0] rx_bar=0;
logic [255:0] rx_data=0,snapshot;
tarugx_pcie_rx_probe dut(.*);
task automatic send(input logic [5:0] bar,input logic [31:0] header,input bit wait_rx);
 @(negedge clk);rx_sop=1;rx_valid=8'he0;rx_bar=bar;rx_data={header,224'b0};rx_wait=wait_rx;
 @(negedge clk);rx_sop=0;rx_valid=0;rx_wait=0;
endtask
initial begin
 repeat(2)@(negedge clk);rst_n=1;
 send(1,32'h12345678,1);assert(snapshot==0);
 send(1,32'h12345678,0);assert(snapshot[159:128]==32'h12345678 && snapshot[255:224]==1);
 send(4,32'h87654321,0);assert(snapshot[159:128]==32'h12345678);
 rx_error=8'h10;send(0,32'hdeadbeef,0);
 assert(snapshot[159:128]==32'hdeadbeef && snapshot[223:192]==1 && snapshot[167:160]==8'h10);
 assert(snapshot[191:184]==8'h0f);
 send(0,32'h00000001,0);assert(snapshot[159:128]==32'hdeadbeef && snapshot[223:192]==2);
 @(negedge clk);rst_n=0;@(negedge clk);assert(snapshot==0);rst_n=1;
 send(0,32'h20000010,0);assert(snapshot[159:128]==32'h20000010);
 $display("PASS: accepted SOP, first capture, unmatched priority, lock, error, reset");$finish;
end
initial begin #10000;$fatal(1,"timeout");end
endmodule
