module tarugx_ddr3_wrapper(
 input wire clk, rst_n,
 output wire ui_clk, pll_lock, calibrated,
 input wire mem_reset_n,
 input wire mem_valid, mem_write, mem_rsp_ready,
 output wire mem_ready, mem_rsp_valid, mem_rsp_error,
 input wire [21:0] mem_addr,
 input wire [255:0] mem_wdata,
 input wire [31:0] mem_wstrb,
 output wire [255:0] mem_rsp_data,
 output wire [14:0] ddr_addr, output wire [2:0] ddr_bank,
 output wire ddr_cs,ddr_ras,ddr_cas,ddr_we,ddr_ck,ddr_ck_n,
 output wire ddr_cke,ddr_odt,ddr_reset_n,
 output wire [3:0] ddr_dm,
 inout wire [31:0] ddr_dq, inout wire [3:0] ddr_dqs,ddr_dqs_n
);
 wire memory_clk,pll_stop,ddr_rst;
 Tarugx_DDR_PLL u_pll(.clkin(clk),.init_clk(clk),.reset(!rst_n),
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
 tarugx_ddr3_native_adapter adapter(.clk(ui_clk),.rst_n(mem_reset_n),.calibrated(calibrated),
  .mem_valid(mem_valid),.mem_ready(mem_ready),.mem_write(mem_write),.mem_addr(mem_addr),
  .mem_wdata(mem_wdata),.mem_wstrb(mem_wstrb),.mem_rsp_valid(mem_rsp_valid),
  .mem_rsp_ready(mem_rsp_ready),.mem_rsp_data(mem_rsp_data),.mem_rsp_error(mem_rsp_error),
  .cmd_ready(cmd_ready),.cmd(cmd),.cmd_en(cmd_en),.addr(addr),
  .wr_data_rdy(wr_data_rdy),.wr_data(wr_data),.wr_data_en(wr_data_en),
  .wr_data_end(wr_data_end),.wr_data_mask(wr_data_mask),
  .rd_data(rd_data),.rd_data_valid(rd_data_valid),.rd_data_end(rd_data_end));
endmodule
