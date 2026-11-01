module tarugx_framebuffer_gowin_top (
    input  logic       sys_clk_p,
    input  logic       pcie_rstn,
    input  logic       rst_n,
    output logic [3:0] led,
    output logic       uart_tx,
    output wire        tmds_clk_p_0, tmds_clk_n_0,
    output wire [2:0]  tmds_d_p_0, tmds_d_n_0,
    output wire [14:0] ddr_addr,
    output wire [2:0] ddr_bank,
    output wire ddr_cs, ddr_ras, ddr_cas, ddr_we, ddr_ck, ddr_ck_n,
    output wire ddr_cke, ddr_odt, ddr_reset_n,
    output wire [3:0] ddr_dm,
    inout wire [31:0] ddr_dq,
    inout wire [3:0] ddr_dqs, ddr_dqs_n
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
        .PCIE_Controller_Top_pcie_tl_drp_rd_i(1'b0),
        .PCIE_Controller_Top_pcie_tl_int_status_i(1'b0),
        .PCIE_Controller_Top_pcie_tl_int_req_i(1'b0),
        .PCIE_Controller_Top_pcie_tl_int_msinum_i(5'b0),
        .PCIE_Controller_Top_pcie_tl_int_ack_o()
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

    wire ui_clk, ddr_pll_lock, calibrated;
    wire pixel_clk, serial_clk, video_lock;
    wire common_reset_n = rst_n & pll_lock & video_lock & calibrated;
    (* async_reg = "true" *) reg [1:0] cpu_reset=0, mem_reset=0, pixel_reset=0;
    always @(posedge tlp_clk or negedge common_reset_n)
        if (!common_reset_n) cpu_reset<=0; else cpu_reset<={cpu_reset[0],1'b1};
    always @(posedge ui_clk or negedge common_reset_n)
        if (!common_reset_n) mem_reset<=0; else mem_reset<={mem_reset[0],1'b1};
    always @(posedge pixel_clk or negedge common_reset_n)
        if (!common_reset_n) pixel_reset<=0; else pixel_reset<={pixel_reset[0],1'b1};
    wire [31:0] committed_base, committed_stride;
    wire fb_req_valid, fb_req_ready, fb_req_write, fb_rsp_valid, fb_rsp_ready;
    wire [31:0] fb_req_addr, fb_req_wdata;
    wire [3:0] fb_req_wstrb;
    wire [32:0] cpu_response;
    tarugx_tarugx_framebuffer_top u_core (
        .tlp_clk(tlp_clk), .tlp_rst_n(cpu_reset[1]),
        .completer_id({cfg_busdev, 3'b000}), .link_up(link_up), .perst_n(pcie_rstn),
        .clock_ready(clock_ready), .lane_status(lane_status), .ltssm(ltssm),
        .rx_sop(rx_sop), .rx_eop(rx_eop), .rx_valid(rx_valid), .rx_data(rx_data),
        .rx_bar_decode(rx_bar_decode), .rx_error(rx_error), .rx_wait(rx_wait),
        .rx_masknp(rx_masknp), .tx_wait(tx_wait), .tx_sop(tx_sop), .tx_eop(tx_eop),
        .tx_valid(tx_valid), .tx_data(tx_data), .led_status(led), .uart_tx_o(),
        .display_ctrl(display_ctrl), .display_color(display_color),
        .committed_base(committed_base), .committed_stride(committed_stride),
        .fb_req_valid(fb_req_valid), .fb_req_ready(fb_req_ready), .fb_req_write(fb_req_write),
        .fb_req_addr(fb_req_addr), .fb_req_wdata(fb_req_wdata), .fb_req_wstrb(fb_req_wstrb),
        .fb_rsp_valid(fb_rsp_valid), .fb_rsp_ready(fb_rsp_ready),
        .fb_rsp_rdata(cpu_response[31:0]), .fb_rsp_error(cpu_response[32])
    );

    Tarugx_Video_PLL u_video_pll (
        .clkin(sys_clk_p), .clkout0(pixel_clk), .clkout1(serial_clk), .lock(video_lock)
    );
    wire [25:0] video_settings;
    tarugx_display_config_cdc u_display_cdc (
        .src_clk(tlp_clk), .src_rst_n(cpu_reset[1]),
        .dst_clk(pixel_clk), .dst_rst_n(pixel_reset[1]),
        .src_config({display_ctrl[1:0], display_color[23:0]}),
        .dst_config(video_settings)
    );
    wire video_de, video_hsync, video_vsync;
    wire [23:0] video_rgb;
    wire underflow, config_error;
    wire [63:0] fault_snapshot;
    wire [64:0] memory_settings;
    tarugx_display_config_cdc #(.WIDTH(65)) u_memory_config_cdc (
        .src_clk(tlp_clk), .src_rst_n(cpu_reset[1]),
        .dst_clk(ui_clk), .dst_rst_n(mem_reset[1]),
        .src_config({display_ctrl[0], committed_base, committed_stride}),
        .dst_config(memory_settings)
    );
    wire mem_valid;
    wire mem_ready;
    wire mem_write;
    wire [21:0] mem_addr;
    wire [255:0] mem_wdata;
    wire [31:0] mem_wstrb;
    wire mem_rsp_valid;
    wire mem_rsp_ready;
    wire [255:0] mem_rsp_data;
    wire mem_rsp_error;
    wire scan_valid;
    wire scan_ready;
    wire [21:0] scan_addr;
    wire scan_rsp_valid;
    wire scan_rsp_ready;
    wire [255:0] scan_rsp_data;
    wire scan_rsp_error;
    tarugx_framebuffer_memory_path u_memory_path (
        .cpu_clk(tlp_clk), .cpu_rst_n(cpu_reset[1]),
        .cpu_valid(fb_req_valid), .cpu_ready(fb_req_ready),
        .cpu_request({fb_req_write, fb_req_addr, fb_req_wdata, fb_req_wstrb}),
        .cpu_rsp_valid(fb_rsp_valid), .cpu_rsp_ready(fb_rsp_ready), .cpu_response(cpu_response),
        .mem_clk(ui_clk), .mem_rst_n(mem_reset[1]),
        .scan_valid(scan_valid),
        .scan_ready(scan_ready),
        .scan_addr(scan_addr),
        .scan_rsp_valid(scan_rsp_valid),
        .scan_rsp_ready(scan_rsp_ready),
        .scan_rsp_data(scan_rsp_data),
        .scan_rsp_error(scan_rsp_error),
        .mem_valid(mem_valid),
        .mem_ready(mem_ready),
        .mem_write(mem_write),
        .mem_addr(mem_addr),
        .mem_wdata(mem_wdata),
        .mem_wstrb(mem_wstrb),
        .mem_rsp_valid(mem_rsp_valid),
        .mem_rsp_ready(mem_rsp_ready),
        .mem_rsp_data(mem_rsp_data),
        .mem_rsp_error(mem_rsp_error)
    );
    tarugx_scanout_engine u_scanout (
        .mem_clk(ui_clk), .mem_rst_n(mem_reset[1]), .enable(memory_settings[64]),
        .fb_base(memory_settings[63:32]), .fb_stride(memory_settings[31:0]),
        .config_error(config_error), .pixel_clk(pixel_clk), .pixel_rst_n(pixel_reset[1]),
        .settings(video_settings), .de(video_de), .hsync(video_hsync), .vsync(video_vsync),
        .rgb(video_rgb), .underflow(underflow), .fault_snapshot(fault_snapshot),
        .mem_valid(scan_valid),
        .mem_ready(scan_ready),
        .mem_addr(scan_addr),
        .mem_rsp_valid(scan_rsp_valid),
        .mem_rsp_ready(scan_rsp_ready),
        .mem_rsp_data(scan_rsp_data),
        .mem_rsp_error(scan_rsp_error)
    );
    tarugx_ddr3_wrapper u_ddr (
        .clk(sys_clk_p), .rst_n(rst_n), .ui_clk(ui_clk), .pll_lock(ddr_pll_lock),
        .calibrated(calibrated), .mem_reset_n(mem_reset[1]),
        .mem_valid(mem_valid),
        .mem_ready(mem_ready),
        .mem_write(mem_write),
        .mem_addr(mem_addr),
        .mem_wdata(mem_wdata),
        .mem_wstrb(mem_wstrb),
        .mem_rsp_valid(mem_rsp_valid),
        .mem_rsp_ready(mem_rsp_ready),
        .mem_rsp_data(mem_rsp_data),
        .mem_rsp_error(mem_rsp_error),
        .ddr_addr(ddr_addr),
        .ddr_bank(ddr_bank),
        .ddr_cs(ddr_cs),
        .ddr_ras(ddr_ras),
        .ddr_cas(ddr_cas),
        .ddr_we(ddr_we),
        .ddr_ck(ddr_ck),
        .ddr_ck_n(ddr_ck_n),
        .ddr_cke(ddr_cke),
        .ddr_odt(ddr_odt),
        .ddr_reset_n(ddr_reset_n),
        .ddr_dm(ddr_dm),
        .ddr_dq(ddr_dq),
        .ddr_dqs(ddr_dqs),
        .ddr_dqs_n(ddr_dqs_n)
    );
    // UG306: GW5AST-138 OSC = nominal 210 MHz / 20 = 10.5 MHz.
    // Keep diagnostics alive even when the external clock or rst_n is absent.
    wire debug_clk;
    OSC #(.FREQ_DIV(20)) u_debug_osc (.OSCOUT(debug_clk));
    reg [1:0] debug_por=0;
    always @(posedge debug_clk) debug_por <= {debug_por[0],1'b1};
    wire debug_reset_n=debug_por[1];
    reg [7:0] sys_counter=0;
    always @(posedge sys_clk_p) sys_counter<=sys_counter+8'd1;
    (* async_reg="true" *) reg [1:0] sys_toggle_sync=0;
    reg previous_toggle=0, clock_toggled=0;
    reg [9:0] clock_idle=0;
    always @(posedge debug_clk or negedge debug_reset_n)
        if(!debug_reset_n) begin
            sys_toggle_sync<=0; previous_toggle<=0; clock_toggled<=0; clock_idle<=0;
        end else begin
            sys_toggle_sync<={sys_toggle_sync[0],sys_counter[7]};
            previous_toggle<=sys_toggle_sync[1];
            if(sys_toggle_sync[1]!=previous_toggle) begin
                clock_toggled<=1; clock_idle<=0;
            end else if(!(&clock_idle)) clock_idle<=clock_idle+10'd1;
        end
    wire system_clock_present=clock_toggled && !(&clock_idle);
    // D<DDR PLL><calibration><link><underflow>; FR03 low byte:
    // [7] video PLL, [6] PCIe PLL, [5] rst_n, [4:2] CPU/UI/pixel reset release,
    // [1] invalid scanout config, [0] external system-clock activity.
    (* async_reg="true" *) reg [3:0] status_meta=0, status_sync=0;
    (* async_reg="true" *) reg [7:0] debug_aux_meta=0, debug_aux_sync=0;
    // Bundled data held at source; capture after synchronized sticky flag.
    reg fault_seen=0;
    reg [63:0] debug_fault=0;
    always @(posedge debug_clk or negedge debug_reset_n)
        if (!debug_reset_n) begin fault_seen<=0; debug_fault<=0; end
        else if (!status_sync[0]) begin fault_seen<=0; debug_fault<=0; end
        else if (!fault_seen) begin fault_seen<=1; debug_fault<=fault_snapshot; end
    always @(posedge debug_clk or negedge debug_reset_n)
        if (!debug_reset_n) begin
            status_meta<=0; status_sync<=0; debug_aux_meta<=0; debug_aux_sync<=0;
        end else begin
            status_meta<={ddr_pll_lock,calibrated,link_up,underflow}; status_sync<=status_meta;
            debug_aux_meta<={video_lock,pll_lock,rst_n,cpu_reset[1],mem_reset[1],
                            pixel_reset[1],config_error,system_clock_present};
            debug_aux_sync<=debug_aux_meta;
        end
    tarugx_ddr_debug_uart #(.CLKS_PER_BIT(91), .FRAME_GAP_CYCLES(2625000)) u_status_uart (
        .clk(debug_clk), .rst_n(debug_reset_n), .status(status_sync),
        .snapshot({32'h46523033,184'b0,debug_fault,debug_aux_sync}), .tx(uart_tx)
    );
    dvi_tx_top u_dvi (
        .pixel_clock(pixel_clk), .ddr_bit_clock(serial_clk), .reset(!pixel_reset[1]),
        .den(video_de), .hsync(video_hsync), .vsync(video_vsync), .pixel_data(video_rgb),
        .tmds_clk({tmds_clk_p_0,tmds_clk_n_0}),
        .tmds_d0({tmds_d_p_0[0],tmds_d_n_0[0]}),
        .tmds_d1({tmds_d_p_0[1],tmds_d_n_0[1]}),
        .tmds_d2({tmds_d_p_0[2],tmds_d_n_0[2]})
    );
endmodule
