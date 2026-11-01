// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module framebuffer_commit_tb;
 logic tlp_clk=0,tlp_rst_n=0;always #5 tlp_clk=~tlp_clk;
 logic [15:0] completer_id=16'h0100;
 logic link_up=0,perst_n=1;logic [1:0] clock_ready=0,lane_status=0;logic [4:0] ltssm=0;
 logic rx_sop=1,rx_eop=1;logic [7:0] rx_valid=0,rx_error=0;
 logic [255:0] rx_data=0;logic [5:0] rx_bar_decode=1;
 logic rx_wait,rx_masknp,tx_wait=0,tx_sop,tx_eop;logic [7:0] tx_valid;logic [255:0] tx_data;
 logic [3:0] led_status;logic uart_tx_o;
 logic [31:0] display_ctrl,display_color,committed_base,committed_stride;
 logic fb_req_valid,fb_req_ready=0,fb_req_write,fb_rsp_valid=0,fb_rsp_ready,fb_rsp_error=0;
 logic [31:0] fb_req_addr,fb_req_wdata,fb_rsp_rdata=0;logic [3:0] fb_req_wstrb;
 tarugx_tarugx_framebuffer_top dut(.*);
 logic dst_clk=0,dst_rst_n=0;always #7 dst_clk=~dst_clk;
 wire [64:0] dst_config;
 tarugx_display_config_cdc #(.WIDTH(65)) cdc (
  .src_clk(tlp_clk),.src_rst_n(tlp_rst_n),.dst_clk(dst_clk),.dst_rst_n(dst_rst_n),
  .src_config({display_ctrl[0],committed_base,committed_stride}),.dst_config(dst_config));
 task write_reg(input int address,input int value);
  @(negedge tlp_clk);
  rx_valid=8'hf0;rx_data={32'h40000001,32'h12345a0f,address,value[7:0],value[15:8],value[23:16],value[31:24],128'b0};
  @(posedge tlp_clk);#1;assert(!rx_wait) else $fatal;
  @(negedge tlp_clk);rx_valid=0;
 endtask
 always @(negedge dst_clk) if(dst_rst_n) begin
  assert(dst_config==0 || dst_config==65'h00000000000000a00 ||
   dst_config==65'h00000100000000a20 || dst_config==65'h10000100000000a20)
   else $fatal(1,"torn committed configuration %h",dst_config);
 end
 initial begin
  #30;@(negedge tlp_clk);tlp_rst_n=1;dst_rst_n=1;
  write_reg('h1c,4096);write_reg('h20,2592);
  assert(committed_base==0 && committed_stride==2560) else $fatal;
  write_reg('h24,1);@(posedge tlp_clk);#1;
  assert(committed_base==4096 && committed_stride==2592) else $fatal;
  write_reg('h1c,8192);#300;
  assert(committed_base==4096 && dst_config==65'h00000100000000a20) else $fatal;
  write_reg('h10,1);#300;
  assert(dst_config==65'h10000100000000a20) else $fatal;
  @(negedge tlp_clk);tlp_rst_n=0;dst_rst_n=0;#10;
  assert(committed_base==0 && committed_stride==2560 && dst_config==0) else $fatal;
  $display("Framebuffer commit: atomic base/stride snapshot, 65-bit CDC, enable and reset PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
