// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
module tarugx_gowin_top (
    input  logic       sys_clk_p,
    input  logic       pcie_rstn,
    input  logic       rst_n,
    output logic [3:0] led,
    output logic       uart_tx,
    output wire        tmds_clk_p_0, tmds_clk_n_0,
    output wire [2:0]  tmds_d_p_0, tmds_d_n_0
);
    logic tlp_clk;
    logic pll_100m_clk;
    logic pll_200m_clk;
    logic pll_lock;

    Gowin_PLL u_tlp_pll (
        .lock(pll_lock), .clkout0(pll_100m_clk), .clkout1(pll_200m_clk),
        .clkin(sys_clk_p)
    );

    // Preserve the clock topology used by Sipeed's known-working standard-Dock
    // design.  The PCIe TLP clock is the PLL's 200 MHz output divided by the
    // dedicated CLKDIV primitive, rather than the nominally equivalent 100 MHz
    // PLL output.
    CLKDIV #(
        .DIV_MODE("2")
    ) u_tlp_div2 (
        .CLKOUT(tlp_clk), .HCLKIN(pll_200m_clk), .RESETN(1'b1), .CALIB(1'b0)
    );

    // Match Sipeed's standard-Dock reference: the hard PCIe block is released
    // by the board reset. PERST# remains observable at the package pin but is
    // not inserted into the hard-IP reset path.
    wire core_rst_n = rst_n;

    logic rx_sop, rx_eop, rx_wait, rx_masknp, tx_sop, tx_eop, tx_wait, link_up;
    logic [7:0] rx_valid, rx_error, tx_valid;
    logic [255:0] rx_data, tx_data;
    logic [5:0] rx_bar_decode;
    logic [12:0] cfg_busdev;
    logic [4:0] ltssm;
    wire [31:0] display_ctrl, display_color;

    SerDes_Top u_pcie (
        .PCIE_Controller_Top_pcie_tl_rx_sop_o(rx_sop),
        .PCIE_Controller_Top_pcie_tl_rx_eop_o(rx_eop),
        .PCIE_Controller_Top_pcie_tl_rx_data_o(rx_data),
        .PCIE_Controller_Top_pcie_tl_rx_valid_o(rx_valid),
        .PCIE_Controller_Top_pcie_tl_rx_bardec_o(rx_bar_decode),
        .PCIE_Controller_Top_pcie_tl_rx_err_o(rx_error),
        .PCIE_Controller_Top_pcie_tl_tx_wait_o(tx_wait),
        .PCIE_Controller_Top_pcie_ltssm_o(ltssm),
        .PCIE_Controller_Top_pcie_tl_cfg_busdev_o(cfg_busdev),
        .PCIE_Controller_Top_pcie_linkup_o(link_up),
        .PCIE_Controller_Top_pcie_rstn_i(core_rst_n),
        .PCIE_Controller_Top_pcie_tl_clk_i(tlp_clk),
        .PCIE_Controller_Top_pcie_tl_rx_wait_i(rx_wait),
        .PCIE_Controller_Top_pcie_tl_rx_masknp_i(rx_masknp),
        .PCIE_Controller_Top_pcie_tl_tx_sop_i(tx_sop),
        .PCIE_Controller_Top_pcie_tl_tx_eop_i(tx_eop),
        .PCIE_Controller_Top_pcie_tl_tx_data_i(tx_data),
        .PCIE_Controller_Top_pcie_tl_tx_valid_i(tx_valid),
        .PCIE_Controller_Top_pcie_tl_drp_addr_i(24'b0),
        .PCIE_Controller_Top_pcie_tl_drp_wrdata_i(32'b0),
        .PCIE_Controller_Top_pcie_tl_drp_strb_i(8'b0),
        .PCIE_Controller_Top_pcie_tl_drp_wr_i(1'b0),
        .PCIE_Controller_Top_pcie_tl_drp_rd_i(1'b0)
    );

    // These generated SerDes status nets are intentionally observed only in
    // this Gowin-specific wrapper. They distinguish a missing PCIe reference
    // clock from higher-level LTSSM failures during board bring-up.
    wire [1:0] clock_ready = {
        u_pcie.q0_fabric_cmu1_ok_o,
        u_pcie.q0_fabric_cmu_ok_o
    };
    // Polling.Active diagnostics: distinguish a valid electrical reference
    // clock from successful 8b/10b comma/K-code lock on lane 0.
    wire [1:0] lane_status = {
        u_pcie.q0_lane0_k_lock,
        u_pcie.q0_fabric_pma_cm0_dr_refclk_det_o
    };

    tarugx_tarugx_top u_core (
        .tlp_clk(tlp_clk), .tlp_rst_n(core_rst_n),
        .completer_id({cfg_busdev, 3'b000}), .link_up(link_up), .perst_n(pcie_rstn),
        .clock_ready(clock_ready), .lane_status(lane_status), .ltssm(ltssm),
        .rx_sop(rx_sop), .rx_eop(rx_eop), .rx_valid(rx_valid), .rx_data(rx_data),
        .rx_bar_decode(rx_bar_decode), .rx_error(rx_error), .rx_wait(rx_wait),
        .rx_masknp(rx_masknp), .tx_wait(tx_wait), .tx_sop(tx_sop), .tx_eop(tx_eop),
        .tx_valid(tx_valid), .tx_data(tx_data), .led_status(led), .uart_tx_o(uart_tx),
        .display_ctrl(display_ctrl), .display_color(display_color)
    );

    wire pixel_clk, serial_clk, video_lock;
    Tarugx_Video_PLL u_video_pll (
        .clkin(sys_clk_p), .clkout0(pixel_clk), .clkout1(serial_clk), .lock(video_lock)
    );
    wire video_reset_n = rst_n & video_lock & pll_lock;
    (* async_reg = "true" *) reg [1:0] video_src_reset = 0, video_dst_reset = 0;
    always @(posedge tlp_clk or negedge video_reset_n)
        if (!video_reset_n) video_src_reset <= 0;
        else video_src_reset <= {video_src_reset[0], 1'b1};
    always @(posedge pixel_clk or negedge video_reset_n)
        if (!video_reset_n) video_dst_reset <= 0;
        else video_dst_reset <= {video_dst_reset[0], 1'b1};
    wire [25:0] video_settings;
    tarugx_display_config_cdc u_display_cdc (
        .src_clk(tlp_clk), .src_rst_n(video_src_reset[1]),
        .dst_clk(pixel_clk), .dst_rst_n(video_dst_reset[1]),
        .src_config({display_ctrl[1:0], display_color[23:0]}),
        .dst_config(video_settings)
    );
    wire video_de, video_hsync, video_vsync;
    wire [23:0] video_rgb;
    tarugx_display_controller u_display (
        .clk(pixel_clk), .rst_n(video_dst_reset[1]), .settings(video_settings),
        .de(video_de), .hsync(video_hsync), .vsync(video_vsync), .rgb(video_rgb)
    );
    dvi_tx_top u_dvi (
        .pixel_clock(pixel_clk), .ddr_bit_clock(serial_clk), .reset(!video_dst_reset[1]),
        .den(video_de), .hsync(video_hsync), .vsync(video_vsync), .pixel_data(video_rgb),
        .tmds_clk({tmds_clk_p_0,tmds_clk_n_0}),
        .tmds_d0({tmds_d_p_0[0],tmds_d_n_0[0]}),
        .tmds_d1({tmds_d_p_0[1],tmds_d_n_0[1]}),
        .tmds_d2({tmds_d_p_0[2],tmds_d_n_0[2]})
    );
endmodule
