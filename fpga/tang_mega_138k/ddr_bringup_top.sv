// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
module ddr_bringup_top(
 input wire clk, rst_n, output wire uart_tx,
 output wire [14:0] ddr_addr, output wire [2:0] ddr_bank,
 output wire ddr_cs,ddr_ras,ddr_cas,ddr_we,ddr_ck,ddr_ck_n,
 output wire ddr_cke,ddr_odt,ddr_reset_n,
 output wire [3:0] ddr_dm,
 inout wire [31:0] ddr_dq, inout wire [3:0] ddr_dqs,ddr_dqs_n
);
 wire memory_clk,pll_stop,pll_lock,ui_clk,ddr_rst,calibrated;
 Gowin_PLL u_pll(.clkin(clk),.init_clk(clk),.reset(!rst_n),
  .enclk0(1'b1),.enclk1(1'b1),.enclk2(pll_stop),
  .clkout0(),.clkout1(),.clkout2(memory_clk),.lock(pll_lock));
 wire cmd_ready,cmd_en,wr_data_rdy,wr_data_en,wr_data_end,rd_data_valid,rd_data_end;
 wire [2:0] cmd; wire [28:0] addr;
 wire [255:0] wr_data,rd_data; wire [31:0] wr_data_mask;
 DDR3MI u_ddr3(
  .clk(clk),.memory_clk(memory_clk),.pll_stop(pll_stop),.pll_lock(pll_lock),.rst_n(rst_n),
  .clk_out(ui_clk),.ddr_rst(ddr_rst),.init_calib_complete(calibrated),
  .cmd_ready(cmd_ready),.cmd(cmd),.cmd_en(cmd_en),.addr(addr),
  .wr_data_rdy(wr_data_rdy),.wr_data(wr_data),.wr_data_en(wr_data_en),
  .wr_data_end(wr_data_end),.wr_data_mask(wr_data_mask),
  .rd_data(rd_data),.rd_data_valid(rd_data_valid),.rd_data_end(rd_data_end),
  .sr_req(1'b0),.ref_req(1'b0),.sr_ack(),.ref_ack(),.burst(1'b1),
  .O_ddr_addr(ddr_addr),.O_ddr_ba(ddr_bank),.O_ddr_cs_n(ddr_cs),
  .O_ddr_ras_n(ddr_ras),.O_ddr_cas_n(ddr_cas),.O_ddr_we_n(ddr_we),
  .O_ddr_clk(ddr_ck),.O_ddr_clk_n(ddr_ck_n),.O_ddr_cke(ddr_cke),
  .O_ddr_odt(ddr_odt),.O_ddr_reset_n(ddr_reset_n),.O_ddr_dqm(ddr_dm),
  .IO_ddr_dq(ddr_dq),.IO_ddr_dqs(ddr_dqs),.IO_ddr_dqs_n(ddr_dqs_n));
 wire run_reset_n=rst_n && calibrated;
 (* async_reg="true" *) reg [1:0] reset_sync=0;
 always @(posedge ui_clk or negedge run_reset_n)
  if(!run_reset_n) reset_sync<=0; else reset_sync<={reset_sync[0],1'b1};
 wire mem_valid,mem_ready,mem_write,mem_rsp_valid,mem_rsp_ready,mem_rsp_error;
 wire [23:0] mem_addr,fail_addr; wire [255:0] mem_wdata,mem_rsp_data;
 wire [31:0] mem_wstrb; wire busy,done,failed,fail_error; wire [255:0] fail_data;
 tarugx_memory_self_test test_core(.clk(ui_clk),.rst_n(reset_sync[1]),
  .start(1'b1),.calibrated(calibrated),.busy(busy),.done(done),.failed(failed),.fail_addr(fail_addr),
  .fail_data(fail_data),.fail_error(fail_error),
  .mem_valid(mem_valid),.mem_ready(mem_ready),.mem_write(mem_write),.mem_addr(mem_addr),
  .mem_wdata(mem_wdata),.mem_wstrb(mem_wstrb),.mem_rsp_valid(mem_rsp_valid),
  .mem_rsp_ready(mem_rsp_ready),.mem_rsp_data(mem_rsp_data),.mem_rsp_error(mem_rsp_error));
 tarugx_ddr3_native_adapter adapter(.clk(ui_clk),.rst_n(reset_sync[1]),.calibrated(calibrated),
  .mem_valid(mem_valid),.mem_ready(mem_ready),.mem_write(mem_write),.mem_addr(mem_addr),
  .mem_wdata(mem_wdata),.mem_wstrb(mem_wstrb),.mem_rsp_valid(mem_rsp_valid),
  .mem_rsp_ready(mem_rsp_ready),.mem_rsp_data(mem_rsp_data),.mem_rsp_error(mem_rsp_error),
  .cmd_ready(cmd_ready),.cmd(cmd),.cmd_en(cmd_en),.addr(addr),
  .wr_data_rdy(wr_data_rdy),.wr_data(wr_data),.wr_data_en(wr_data_en),
  .wr_data_end(wr_data_end),.wr_data_mask(wr_data_mask),
  .rd_data(rd_data),.rd_data_valid(rd_data_valid),.rd_data_end(rd_data_end));
 (* async_reg="true" *) reg [3:0] status_meta=0,status_sync=0;
 always @(posedge clk or negedge rst_n)
  if(!rst_n) begin status_meta<=0;status_sync<=0;end
  else begin status_meta<={pll_lock,calibrated,done,failed};status_sync<=status_meta;end
 // Failure payload remains held until common reset. Snapshot only after synced done.
 reg [287:0] failure_snapshot=0;
 always @(posedge clk or negedge rst_n)
  if(!rst_n) failure_snapshot<=0;
  else if(status_sync[1]) failure_snapshot<={9'b0,fail_error,fail_addr,fail_data};
 tarugx_ddr_debug_uart debug_uart(.clk(clk),.rst_n(rst_n),.status(status_sync),
  .snapshot(failure_snapshot),.tx(uart_tx));
endmodule
