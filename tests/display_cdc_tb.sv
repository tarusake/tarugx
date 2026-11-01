`timescale 1ns/1ps
module display_cdc_tb;
    reg src_clk=0, dst_clk=0, src_rst_n=0, dst_rst_n=0;
    reg [25:0] src_config=0;
    wire [25:0] dst_config;
    always #5 src_clk = !src_clk;
    always #19.85 dst_clk = !dst_clk;
    tarugx_display_config_cdc dut(.*);
    wire [255:0] periodic_config;
    tarugx_display_config_cdc #(.WIDTH(256), .SEND_UNCHANGED(1)) periodic (
        .src_clk, .dst_clk, .src_rst_n, .dst_rst_n,
        .src_config({22'b0, {9{src_config}}}), .dst_config(periodic_config)
    );
    always @(negedge dst_clk) if (dst_rst_n) begin
        assert(periodic_config == 0 ||
               periodic_config == {22'b0, {9{26'h3123456}}} ||
               periodic_config == {22'b0, {9{26'h3abcdef}}} ||
               periodic_config == {22'b0, {9{26'h355aa55}}})
            else $fatal(1, "torn periodic snapshot");
    end
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
        #1000; assert(periodic_config == {22'b0, {9{26'h355aa55}}}) else $fatal;
        assert(dst_config == 26'h355aa55) else $fatal;
        #3; src_rst_n=0; dst_rst_n=0;
        #100; assert(periodic_config == 0) else $fatal;
        assert(dst_config == 0) else $fatal;
        src_rst_n=1; dst_rst_n=1;
        #1000; assert(periodic_config == {22'b0, {9{26'h355aa55}}}) else $fatal;
        assert(dst_config == 26'h355aa55) else $fatal;
        $display("CDC asynchronous clocks, update and reset: PASS");
        $finish;
    end
endmodule
