`timescale 1ns/1ps
module display_cdc_tb;
    reg src_clk=0, dst_clk=0, src_rst_n=0, dst_rst_n=0;
    reg [25:0] src_config=0;
    wire [25:0] dst_config;
    always #5 src_clk = !src_clk;
    always #19.85 dst_clk = !dst_clk;
    tarugx_display_config_cdc dut(.*);
    always @(negedge dst_clk) if (dst_rst_n) begin
        assert(dst_config == 0 || dst_config == 26'h3123456 ||
               dst_config == 26'h3abcdef || dst_config == 26'h355aa55)
            else $fatal(1, "torn configuration %h", dst_config);
    end
    initial begin
        #100; src_rst_n=1; dst_rst_n=1;
        @(negedge src_clk); src_config=26'h3123456;
        #500; assert(dst_config == 26'h3123456) else $fatal;
        @(negedge src_clk); src_config=26'h3abcdef;
        @(negedge src_clk); src_config=26'h355aa55;
        #1000; assert(dst_config == 26'h355aa55) else $fatal;
        #3; src_rst_n=0; dst_rst_n=0;
        #100; assert(dst_config == 0) else $fatal;
        src_rst_n=1; dst_rst_n=1;
        #1000; assert(dst_config == 26'h355aa55) else $fatal;
        $display("CDC asynchronous clocks, update and reset: PASS");
        $finish;
    end
endmodule
