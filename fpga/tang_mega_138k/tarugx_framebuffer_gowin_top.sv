// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
module tarugx_framebuffer_gowin_top #(parameter bit PCIE_RECOVERY_EXPERIMENT = 1'b0) (
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
    wire recovery_reset, recovery_fired, recovery_inhibited;
    wire [55:0] reset_scope;
    wire core_rst_n = rst_n & ~recovery_reset;

    logic rx_sop, rx_eop, rx_wait, rx_masknp, tx_sop, tx_eop, tx_wait, link_up;
    logic [7:0] rx_valid, rx_error, tx_valid;
    logic [255:0] rx_data, tx_data;
    logic [5:0] rx_bar_decode;
    logic [12:0] cfg_busdev;
    logic [4:0] ltssm;
    wire [31:0] display_ctrl, display_color;
    wire active_buffer;
    wire [31:0] active_fb_base;
    wire vblank_toggle;
    wire msi_req, msi_status, msi_ack;
    wire [1:0] msi_enable;

    SerDes_Top u_pcie (
`ifdef TARUGX_PCIE_PHY_RECOVERY
        // The independent, one-shot Veryl controller also resets the PHY.
        // Its timer continues even when this reset deasserts CMU ready.
        .por_n_i(~recovery_reset),
`elsif TARUGX_PCIE_POR_RELEASE_TEST
        // Warm-reference diagnostic only: test the generated PHY reset input.
        // This is not the late-REFCLK startup sequence or a cold-boot fix.
        .por_n_i(1'b1),
`endif
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
`ifndef TARUGX_PCIE_NO_MSI
        ,.PCIE_Controller_Top_pcie_tl_int_status_i(msi_status),
        .PCIE_Controller_Top_pcie_tl_int_req_i(msi_req),
        .PCIE_Controller_Top_pcie_tl_int_msinum_i(5'b0),
        .PCIE_Controller_Top_pcie_tl_int_ack_o(msi_ack)
`endif
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
    wire common_reset_n = core_rst_n & pll_lock & video_lock & calibrated;
    (* async_reg = "true" *) reg [1:0] cpu_reset=0, mem_reset=0, pixel_reset=0;
    always @(posedge tlp_clk or negedge common_reset_n)
        if (!common_reset_n) cpu_reset<=0; else cpu_reset<={cpu_reset[0],1'b1};
    always @(posedge ui_clk or negedge common_reset_n)
        if (!common_reset_n) mem_reset<=0; else mem_reset<={mem_reset[0],1'b1};
    always @(posedge pixel_clk or negedge common_reset_n)
        if (!common_reset_n) pixel_reset<=0; else pixel_reset<={pixel_reset[0],1'b1};
    wire [31:0] committed_base, committed_stride;
    wire fb_req_valid, fb_req_ready, fb_req_write, fb_rsp_valid, fb_rsp_ready;
    wire [31:0] fb_req_addr;
    wire [255:0] fb_req_wdata;
    wire [31:0] fb_req_wstrb;
    wire [32:0] cpu_response;
    wire app_rx_sop, app_rx_eop, app_rx_wait, app_rx_masknp;
    wire [7:0] app_rx_valid, app_rx_error;
    wire [255:0] app_rx_data;
    wire [5:0] app_rx_bar;
`ifdef TARUGX_PCIE_ROM_DIAGNOSTIC
    wire app_rx_busy;
    wire app_rom_hit;
`ifdef TARUGX_PCIE_AUTO_ROM
    wire [19:0] rx_raw_tag={u_pcie.tl_rx_bardec0_w,u_pcie.PCIE_Controller_Top_pcie_tl_rx_bardec_i};
    localparam AUTO_ROM = 1;
`else
    wire [19:0] rx_raw_tag=20'b0;
    localparam AUTO_ROM = 0;
`endif
    tarugx_tlp_rx_buffer u_rx_buffer (
        .clk(tlp_clk), .rst_n(cpu_reset[1]),
        .in_sop(rx_sop), .in_eop(rx_eop), .in_valid(rx_valid), .in_data(rx_data),
        .in_bar(rx_bar_decode), .in_error(rx_error), .in_wait(rx_wait),
        .in_tag(rx_raw_tag), .out_tag_match(app_rom_hit),
        .out_sop(app_rx_sop), .out_eop(app_rx_eop), .out_valid(app_rx_valid), .out_data(app_rx_data),
        .out_bar(app_rx_bar), .out_error(app_rx_error), .out_wait(app_rx_wait), .busy(app_rx_busy)
    );
    assign rx_masknp=app_rx_masknp | app_rx_busy;
`else
    assign app_rx_sop=rx_sop, app_rx_eop=rx_eop, app_rx_valid=rx_valid, app_rx_data=rx_data;
    assign app_rx_bar=rx_bar_decode, app_rx_error=rx_error;
    assign rx_wait=app_rx_wait, rx_masknp=app_rx_masknp;
`endif
    wire [7:0] core_rx_valid, core_tx_valid;
    wire core_rx_wait, core_rx_masknp, core_tx_wait, core_tx_sop, core_tx_eop;
    wire [255:0] core_tx_data;
    wire [127:0] fill_command;
    wire fill_start, fill_busy, fill_done, fill_error;
    wire [159:0] blit_command;
    wire blit_start, blit_busy, blit_done, blit_error;
    wire [271:0] triangle_command;
    wire triangle_start, triangle_busy, triangle_done, triangle_error;
    wire [31:0] triangle_writes;
    wire [305:0] queue_command;
    wire queue_submit, queue_ready, queue_active, queue_error, queue_clear_error;
    wire render_irq_event;
    wire [2:0] queue_level;
    wire [31:0] queue_completed, queue_error_fence;
`ifdef TARUGX_PCIE_ROM_DIAGNOSTIC
    wire rom_en, rom_diagnostic_error;
    wire [13:0] rom_addr;
    wire [31:0] rom_data;
    tarugx_option_rom_diagnostic #(.AUTO_ROM(AUTO_ROM)) u_rom (
        .clk(tlp_clk), .rst_n(cpu_reset[1]), .completer_id({cfg_busdev,3'b000}),
        .rom_hit(app_rom_hit),
        .rx_sop(app_rx_sop), .rx_eop(app_rx_eop), .rx_valid(app_rx_valid), .rx_data(app_rx_data),
        .rx_bar(app_rx_bar), .rx_error(app_rx_error), .rx_wait(app_rx_wait), .rx_masknp(app_rx_masknp),
        .core_rx_valid(core_rx_valid), .core_rx_wait(core_rx_wait), .core_rx_masknp(core_rx_masknp),
        .core_tx_valid(core_tx_valid), .core_tx_sop(core_tx_sop), .core_tx_eop(core_tx_eop),
        .core_tx_data(core_tx_data), .core_tx_wait(core_tx_wait),
        .tx_wait(tx_wait), .tx_valid(tx_valid), .tx_sop(tx_sop), .tx_eop(tx_eop), .tx_data(tx_data),
        .rom_en(rom_en), .rom_addr(rom_addr), .rom_data(rom_data), .diagnostic_error(rom_diagnostic_error)
    );
    tarugx_option_rom_storage #(.INIT_FILE("__TARUGX_ROM_MEM__")) u_option_rom (
        .clk(tlp_clk), .en(rom_en), .addr(rom_addr), .data(rom_data)
    );
`else
    assign core_rx_valid=app_rx_valid;
    assign app_rx_wait=core_rx_wait, app_rx_masknp=core_rx_masknp;
    assign core_tx_wait=tx_wait;
    assign tx_valid=core_tx_valid, tx_sop=core_tx_sop, tx_eop=core_tx_eop, tx_data=core_tx_data;
`endif
    tarugx_tarugx_framebuffer_top #(
`ifdef TARUGX_VIDEO_1080P
        .GPU_VERSION(32'h0001_0003)
`else
        .GPU_VERSION(32'h0001_0001)
`endif
    ) u_core (
        .tlp_clk(tlp_clk), .tlp_rst_n(cpu_reset[1]),
        .completer_id({cfg_busdev, 3'b000}), .link_up(link_up), .perst_n(pcie_rstn),
        .clock_ready(clock_ready), .lane_status(lane_status), .ltssm(ltssm),
        .rx_sop(app_rx_sop), .rx_eop(app_rx_eop), .rx_valid(core_rx_valid), .rx_data(app_rx_data),
        .rx_bar_decode(app_rx_bar), .rx_error(app_rx_error), .rx_wait(core_rx_wait),
        .rx_masknp(core_rx_masknp), .tx_wait(core_tx_wait), .tx_sop(core_tx_sop), .tx_eop(core_tx_eop),
        .tx_valid(core_tx_valid), .tx_data(core_tx_data), .led_status(led), .uart_tx_o(),
        .display_ctrl(display_ctrl), .display_color(display_color),
        .committed_base(committed_base), .committed_stride(committed_stride),
        .fb_req_valid(fb_req_valid), .fb_req_ready(fb_req_ready), .fb_req_write(fb_req_write),
        .fb_req_addr(fb_req_addr), .fb_req_wdata(fb_req_wdata), .fb_req_wstrb(fb_req_wstrb),
        .fb_rsp_valid(fb_rsp_valid), .fb_rsp_ready(fb_rsp_ready),
        .fb_rsp_rdata(cpu_response[31:0]), .fb_rsp_error(cpu_response[32]),
        .active_buffer(active_buffer), .active_fb_base(active_fb_base),
        .vblank_toggle(vblank_toggle),
        .irq_enable(msi_enable),
        .fill_busy(fill_busy), .fill_done(fill_done), .fill_error(fill_error),
        .fill_command(fill_command), .fill_start(fill_start),
        .blit_busy(blit_busy), .blit_done(blit_done), .blit_error(blit_error),
        .blit_command(blit_command), .blit_start(blit_start),
        .triangle_busy(triangle_busy), .triangle_done(triangle_done),
        .triangle_error(triangle_error), .triangle_writes(triangle_writes),
        .triangle_command(triangle_command), .triangle_start(triangle_start),
        .queue_ready(queue_ready), .queue_level(queue_level), .queue_active(queue_active),
        .queue_completed(queue_completed), .queue_error(queue_error),
        .queue_error_fence(queue_error_fence), .queue_command(queue_command),
        .queue_submit(queue_submit), .queue_clear_error(queue_clear_error)
    );
    Tarugx_Video_PLL u_video_pll (
        .clkin(sys_clk_p), .clkout0(pixel_clk), .clkout1(serial_clk), .lock(video_lock)
    );
    wire [25:0] video_settings;
`ifdef TARUGX_VIDEO_LINK_TEST
    assign video_settings = 26'b0;
`else
    tarugx_display_config_cdc u_display_cdc (
        .src_clk(tlp_clk), .src_rst_n(cpu_reset[1]),
        .dst_clk(pixel_clk), .dst_rst_n(pixel_reset[1]),
        .src_config({display_ctrl[1:0], display_color[23:0]}),
        .dst_config(video_settings)
    );
`endif
    wire video_de, video_hsync, video_vsync;
    wire [23:0] video_rgb;
    wire scan_video_de, scan_video_hsync, scan_video_vsync;
    wire [23:0] scan_video_rgb;
    wire underflow, config_error;
    wire mem_active_buffer;
    wire [31:0] mem_active_fb_base;
    wire pixel_vblank_toggle, scan_vblank_toggle;
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
    wire [23:0] mem_addr;
    wire [255:0] mem_wdata;
    wire [31:0] mem_wstrb;
    wire mem_rsp_valid;
    wire mem_rsp_ready;
    wire [255:0] mem_rsp_data;
    wire mem_rsp_error;
    wire scan_valid;
    wire scan_urgent;
    wire scan_ready;
    wire [23:0] scan_addr;
    wire scan_rsp_valid;
    wire scan_rsp_ready;
    wire [255:0] scan_rsp_data;
    wire scan_rsp_error;
`ifdef TARUGX_PIPELINED_MEMORY
    wire queue_dispatch_valid, queue_dispatch_ready, queue_completion_valid, queue_completion_error;
    wire [305:0] queue_dispatch_command;
    wire queued_fill_start, queued_blit_start, queued_triangle_start;
    wire fill_cdc_ready, blit_cdc_ready, triangle_cdc_ready;
    wire [127:0] queued_fill_command;
    wire [159:0] queued_blit_command;
    wire [271:0] queued_triangle_command;
    tarugx_render_command_queue u_render_queue (
        .clk(tlp_clk), .rst_n(cpu_reset[1]),
        .enqueue_valid(queue_submit), .enqueue_ready(queue_ready), .enqueue_command(queue_command),
        .dispatch_valid(queue_dispatch_valid), .dispatch_ready(queue_dispatch_ready),
        .dispatch_command(queue_dispatch_command),
        .completion_valid(queue_completion_valid), .completion_error(queue_completion_error),
        .clear_error(queue_clear_error), .level(queue_level), .active(queue_active),
        .completed_fence(queue_completed), .error_sticky(queue_error), .error_fence(queue_error_fence)
    );
    assign render_irq_event=queue_completion_valid;
    tarugx_render_queue_dispatcher u_render_dispatcher (
        .clk(tlp_clk), .rst_n(cpu_reset[1]),
        .dispatch_valid(queue_dispatch_valid), .dispatch_ready(queue_dispatch_ready),
        .dispatch_command(queue_dispatch_command),
        .legacy_fill_start(fill_start), .legacy_blit_start(blit_start),
        .legacy_triangle_start(triangle_start),
        .fill_ready(fill_cdc_ready), .fill_done(fill_done), .fill_error(fill_error),
        .blit_ready(blit_cdc_ready), .blit_done(blit_done), .blit_error(blit_error),
        .triangle_ready(triangle_cdc_ready), .triangle_done(triangle_done),
        .triangle_error(triangle_error),
        .fill_start(queued_fill_start), .fill_command(queued_fill_command),
        .blit_start(queued_blit_start), .blit_command(queued_blit_command),
        .triangle_start(queued_triangle_start), .triangle_command(queued_triangle_command),
        .completion_valid(queue_completion_valid), .completion_error(queue_completion_error)
    );
    wire fill_cmd_valid, fill_cmd_ready, fill_engine_done, fill_engine_error;
    wire [127:0] fill_mem_command;
    wire fill_mem_valid, fill_mem_ready, fill_mem_rsp_valid, fill_mem_rsp_ready, fill_mem_rsp_error;
    wire [23:0] fill_mem_addr;
    wire [255:0] fill_mem_wdata;
    wire [31:0] fill_mem_wstrb;
    wire blit_cmd_valid, blit_cmd_ready, blit_engine_done, blit_engine_error;
    wire [159:0] blit_mem_command;
    wire blit_mem_valid, blit_mem_ready, blit_mem_write;
    wire blit_mem_rsp_valid, blit_mem_rsp_ready, blit_mem_rsp_error;
    wire [23:0] blit_mem_addr;
    wire [255:0] blit_mem_wdata, blit_mem_rsp_data;
    wire [31:0] blit_mem_wstrb;
    wire triangle_cmd_valid, triangle_cmd_ready, triangle_engine_done, triangle_engine_error;
    wire [271:0] triangle_mem_command;
    wire triangle_mem_valid, triangle_mem_ready;
    wire triangle_mem_rsp_valid, triangle_mem_rsp_ready, triangle_mem_rsp_error;
    wire [23:0] triangle_mem_addr;
    wire [255:0] triangle_mem_wdata;
    wire [31:0] triangle_mem_wstrb, triangle_engine_writes;
    reg [31:0] triangle_writes_meta, triangle_writes_sync;
    tarugx_fill_command_cdc u_fill_cdc (
        .src_clk(tlp_clk), .src_rst_n(cpu_reset[1]),
        .src_valid(fill_start || queued_fill_start), .src_ready(fill_cdc_ready),
        .src_command(fill_start ? fill_command : queued_fill_command),
        .src_busy(fill_busy), .src_done(fill_done), .src_error(fill_error),
        .dst_clk(ui_clk), .dst_rst_n(mem_reset[1]),
        .dst_valid(fill_cmd_valid), .dst_ready(fill_cmd_ready), .dst_command(fill_mem_command),
        .dst_done(fill_engine_done), .dst_error(fill_engine_error)
    );
    tarugx_fill_engine #(
`ifdef TARUGX_VIDEO_1080P
        .PIXEL_BYTES(2)
`else
        .PIXEL_BYTES(4)
`endif
    ) u_fill_engine (
        .clk(ui_clk), .rst_n(mem_reset[1]),
        .cmd_valid(fill_cmd_valid), .cmd_ready(fill_cmd_ready),
        .cmd_addr(fill_mem_command[31:0]), .cmd_stride(fill_mem_command[63:32]),
        .cmd_width(fill_mem_command[79:64]), .cmd_height(fill_mem_command[95:80]),
        .cmd_color(fill_mem_command[127:96]),
        .busy(), .done(fill_engine_done), .error(fill_engine_error),
        .mem_valid(fill_mem_valid), .mem_ready(fill_mem_ready), .mem_addr(fill_mem_addr),
        .mem_wdata(fill_mem_wdata), .mem_wstrb(fill_mem_wstrb),
        .mem_rsp_valid(fill_mem_rsp_valid), .mem_rsp_ready(fill_mem_rsp_ready),
        .mem_rsp_error(fill_mem_rsp_error)
    );
    tarugx_render_command_cdc #(.WIDTH(160)) u_blit_cdc (
        .src_clk(tlp_clk), .src_rst_n(cpu_reset[1]),
        .src_valid(blit_start || queued_blit_start), .src_ready(blit_cdc_ready),
        .src_command(blit_start ? blit_command : queued_blit_command),
        .src_busy(blit_busy), .src_done(blit_done), .src_error(blit_error),
        .dst_clk(ui_clk), .dst_rst_n(mem_reset[1]),
        .dst_valid(blit_cmd_valid), .dst_ready(blit_cmd_ready), .dst_command(blit_mem_command),
        .dst_done(blit_engine_done), .dst_error(blit_engine_error)
    );
    tarugx_blit_engine #(
`ifdef TARUGX_VIDEO_1080P
        .PIXEL_BYTES(2)
`else
        .PIXEL_BYTES(4)
`endif
    ) u_blit_engine (
        .clk(ui_clk), .rst_n(mem_reset[1]),
        .cmd_valid(blit_cmd_valid), .cmd_ready(blit_cmd_ready),
        .cmd_src_addr(blit_mem_command[31:0]), .cmd_dst_addr(blit_mem_command[63:32]),
        .cmd_src_stride(blit_mem_command[95:64]), .cmd_dst_stride(blit_mem_command[127:96]),
        .cmd_width(blit_mem_command[143:128]), .cmd_height(blit_mem_command[159:144]),
        .busy(), .done(blit_engine_done), .error(blit_engine_error),
        .mem_valid(blit_mem_valid), .mem_ready(blit_mem_ready), .mem_write(blit_mem_write),
        .mem_addr(blit_mem_addr), .mem_wdata(blit_mem_wdata), .mem_wstrb(blit_mem_wstrb),
        .mem_rsp_valid(blit_mem_rsp_valid), .mem_rsp_ready(blit_mem_rsp_ready),
        .mem_rsp_data(blit_mem_rsp_data), .mem_rsp_error(blit_mem_rsp_error)
    );
    tarugx_render_command_cdc #(.WIDTH(272)) u_triangle_cdc (
        .src_clk(tlp_clk), .src_rst_n(cpu_reset[1]),
        .src_valid(triangle_start || queued_triangle_start), .src_ready(triangle_cdc_ready),
        .src_command(triangle_start ? triangle_command : queued_triangle_command),
        .src_busy(triangle_busy), .src_done(triangle_done), .src_error(triangle_error),
        .dst_clk(ui_clk), .dst_rst_n(mem_reset[1]),
        .dst_valid(triangle_cmd_valid), .dst_ready(triangle_cmd_ready),
        .dst_command(triangle_mem_command),
        .dst_done(triangle_engine_done), .dst_error(triangle_engine_error)
    );
    tarugx_triangle_engine u_triangle_engine (
        .clk(ui_clk), .rst_n(mem_reset[1]),
        .cmd_valid(triangle_cmd_valid), .cmd_ready(triangle_cmd_ready),
        .cmd_base(triangle_mem_command[31:0]), .cmd_stride(triangle_mem_command[63:32]),
        .cmd_width(triangle_mem_command[79:64]), .cmd_height(triangle_mem_command[95:80]),
        .cmd_x0(triangle_mem_command[119:96]), .cmd_y0(triangle_mem_command[143:120]),
        .cmd_x1(triangle_mem_command[167:144]), .cmd_y1(triangle_mem_command[191:168]),
        .cmd_x2(triangle_mem_command[215:192]), .cmd_y2(triangle_mem_command[239:216]),
        .cmd_color(triangle_mem_command[271:240]),
        .busy(), .done(triangle_engine_done), .error(triangle_engine_error),
        .writes(triangle_engine_writes),
        .mem_valid(triangle_mem_valid), .mem_ready(triangle_mem_ready),
        .mem_addr(triangle_mem_addr), .mem_wdata(triangle_mem_wdata),
        .mem_wstrb(triangle_mem_wstrb), .mem_rsp_valid(triangle_mem_rsp_valid),
        .mem_rsp_ready(triangle_mem_rsp_ready), .mem_rsp_error(triangle_mem_rsp_error)
    );
    always @(posedge tlp_clk or negedge cpu_reset[1]) begin
        if (!cpu_reset[1]) begin
            triangle_writes_meta <= 32'b0;
            triangle_writes_sync <= 32'b0;
        end else begin
            triangle_writes_meta <= triangle_engine_writes;
            triangle_writes_sync <= triangle_writes_meta;
        end
    end
    assign triangle_writes = triangle_writes_sync;
    localparam PIPELINED_MEMORY=1;
    tarugx_framebuffer_memory_pipeline u_memory_path (
`else
    assign fill_busy=1'b0, fill_done=fill_start, fill_error=fill_start;
    assign blit_busy=1'b0, blit_done=blit_start, blit_error=blit_start;
    assign triangle_busy=1'b0, triangle_done=triangle_start, triangle_error=triangle_start;
    assign triangle_writes=32'b0;
    assign queue_ready=1'b0, queue_level=3'b0, queue_active=1'b0;
    assign queue_completed=32'b0, queue_error=1'b0, queue_error_fence=32'b0;
    assign render_irq_event=1'b0;
    localparam PIPELINED_MEMORY=0;
    tarugx_framebuffer_memory_path u_memory_path (
`endif
        .cpu_clk(tlp_clk), .cpu_rst_n(cpu_reset[1]),
        .cpu_valid(fb_req_valid), .cpu_ready(fb_req_ready),
        .cpu_request({fb_req_write, fb_req_addr, fb_req_wdata, fb_req_wstrb}),
        .cpu_rsp_valid(fb_rsp_valid), .cpu_rsp_ready(fb_rsp_ready), .cpu_response(cpu_response),
        .mem_clk(ui_clk), .mem_rst_n(mem_reset[1]),
        .scan_valid(scan_valid),
        .scan_urgent(scan_urgent),
        .scan_ready(scan_ready),
        .scan_addr(scan_addr),
        .scan_rsp_valid(scan_rsp_valid),
        .scan_rsp_ready(scan_rsp_ready),
        .scan_rsp_data(scan_rsp_data),
        .scan_rsp_error(scan_rsp_error),
`ifdef TARUGX_PIPELINED_MEMORY
        .fill_valid(fill_mem_valid), .fill_ready(fill_mem_ready), .fill_addr(fill_mem_addr),
        .fill_wdata(fill_mem_wdata), .fill_wstrb(fill_mem_wstrb),
        .fill_rsp_valid(fill_mem_rsp_valid), .fill_rsp_ready(fill_mem_rsp_ready),
        .fill_rsp_error(fill_mem_rsp_error),
        .blit_valid(blit_mem_valid), .blit_ready(blit_mem_ready), .blit_write(blit_mem_write),
        .blit_addr(blit_mem_addr), .blit_wdata(blit_mem_wdata), .blit_wstrb(blit_mem_wstrb),
        .blit_rsp_valid(blit_mem_rsp_valid), .blit_rsp_ready(blit_mem_rsp_ready),
        .blit_rsp_data(blit_mem_rsp_data), .blit_rsp_error(blit_mem_rsp_error),
        .triangle_valid(triangle_mem_valid), .triangle_ready(triangle_mem_ready),
        .triangle_addr(triangle_mem_addr), .triangle_wdata(triangle_mem_wdata),
        .triangle_wstrb(triangle_mem_wstrb), .triangle_rsp_valid(triangle_mem_rsp_valid),
        .triangle_rsp_ready(triangle_mem_rsp_ready), .triangle_rsp_error(triangle_mem_rsp_error),
`endif
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
`ifdef TARUGX_VIDEO_LINK_TEST
    // Link-only images remove DDR scanout so timing and hardware tests isolate
    // the selected pixel clock, serializer and physical HDMI path.
    assign scan_valid = 1'b0;
    assign scan_addr = 24'b0;
    assign scan_rsp_ready = 1'b1;
    assign scan_urgent = 1'b0;
    assign underflow = 1'b0;
    assign config_error = 1'b0;
    assign mem_active_buffer = 1'b0;
    assign mem_active_fb_base = 32'b0;
    assign fault_snapshot = 64'b0;
`else
`ifdef TARUGX_PIPELINED_MEMORY
    tarugx_scanout_pipeline_engine #(
`else
    tarugx_scanout_engine #(
`endif
`ifdef TARUGX_VIDEO_1080P
        .VIDEO_1080P(1'b1)
`else
        .VIDEO_1080P(1'b0)
`endif
    ) u_scanout (
        .mem_clk(ui_clk), .mem_rst_n(mem_reset[1]),
        .enable(memory_settings[64]),
        .fb_base(memory_settings[63:32]), .fb_stride(memory_settings[31:0]),
        .config_error(config_error), .active_buffer(mem_active_buffer),
        .active_fb_base(mem_active_fb_base),
        .scan_urgent(scan_urgent), .pixel_clk(pixel_clk), .pixel_rst_n(pixel_reset[1]),
        .settings(video_settings),
        .de(scan_video_de), .hsync(scan_video_hsync), .vsync(scan_video_vsync),
        .rgb(scan_video_rgb), .underflow(underflow), .vblank_toggle(scan_vblank_toggle),
        .fault_snapshot(fault_snapshot),
        .mem_valid(scan_valid),
        .mem_ready(scan_ready),
        .mem_addr(scan_addr),
        .mem_rsp_valid(scan_rsp_valid),
        .mem_rsp_ready(scan_rsp_ready),
        .mem_rsp_data(scan_rsp_data),
        .mem_rsp_error(scan_rsp_error)
    );
`endif
`ifdef TARUGX_VIDEO_LINK_TEST
    wire pattern_vblank_toggle;
`ifdef TARUGX_VIDEO_720P
    tarugx_video_test_pattern_720p u_720p_pattern (
        .clk(pixel_clk), .rst_n(pixel_reset[1]),
        .de(video_de), .hsync(video_hsync), .vsync(video_vsync),
        .rgb(video_rgb), .vblank_toggle(pattern_vblank_toggle)
    );
`else
    tarugx_video_test_pattern_1080p u_1080p_pattern (
        .clk(pixel_clk), .rst_n(pixel_reset[1]),
        .de(video_de), .hsync(video_hsync), .vsync(video_vsync),
        .rgb(video_rgb), .vblank_toggle(pattern_vblank_toggle)
    );
`endif
    assign pixel_vblank_toggle = pattern_vblank_toggle;
`else
    assign video_de = scan_video_de;
    assign video_hsync = scan_video_hsync;
    assign video_vsync = scan_video_vsync;
    assign video_rgb = scan_video_rgb;
    assign pixel_vblank_toggle = scan_vblank_toggle;
`endif
    tarugx_display_config_cdc #(.WIDTH(1)) u_active_buffer_cdc (
        .src_clk(ui_clk), .src_rst_n(mem_reset[1]),
        .dst_clk(tlp_clk), .dst_rst_n(cpu_reset[1]),
        .src_config(mem_active_buffer), .dst_config(active_buffer)
    );
    tarugx_display_config_cdc #(.WIDTH(32)) u_active_fb_base_cdc (
        .src_clk(ui_clk), .src_rst_n(mem_reset[1]),
        .dst_clk(tlp_clk), .dst_rst_n(cpu_reset[1]),
        .src_config(mem_active_fb_base), .dst_config(active_fb_base)
    );
`ifdef TARUGX_VIDEO_LINK_TEST
    assign vblank_toggle = 1'b0;
`else
    tarugx_display_config_cdc #(.WIDTH(1)) u_vblank_toggle_cdc (
        .src_clk(pixel_clk), .src_rst_n(pixel_reset[1]),
        .dst_clk(tlp_clk), .dst_rst_n(cpu_reset[1]),
        .src_config(pixel_vblank_toggle), .dst_config(vblank_toggle)
    );
`endif
`ifndef TARUGX_PCIE_NO_MSI
    tarugx_vblank_msi u_vblank_msi (
        .clk(tlp_clk), .rst_n(cpu_reset[1]), .enable(msi_enable),
        .vblank_toggle(vblank_toggle), .render_done(render_irq_event),
        .interrupt_ack(msi_ack), .interrupt_req(msi_req),
        .interrupt_status(msi_status)
    );
`endif
    tarugx_ddr3_wrapper #(.PIPELINED_MEMORY(PIPELINED_MEMORY)) u_ddr (
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
    // D<DDR PLL><calibration><link><underflow>; FR04 preserves the FR03 low byte:
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
    (* async_reg="true" *) reg [1:0] pcie_debug_reset=0;
    always @(posedge tlp_clk or negedge debug_reset_n)
        if (!debug_reset_n) pcie_debug_reset<=0;
        else pcie_debug_reset<={pcie_debug_reset[0],1'b1};
    generate if (PCIE_RECOVERY_EXPERIMENT) begin : recovery_experiment
        tarugx_pcie_reset_scope u_reset_scope (
            .clk(debug_clk), .rst_n(debug_reset_n),
            .raw({recovery_reset, core_rst_n, u_pcie.PCIE_Controller_Top_pcie_pmac_rstn_o,
                  u_pcie.q0_pmac_ln_rstn, u_pcie.q0_ahb_rstn, u_pcie.upar_rst}),
            .snapshot(reset_scope)
        );
        tarugx_pcie_startup_recovery u_recovery (
            .clk(tlp_clk), .rst_n(pcie_debug_reset[1]),
            .ready_async(clock_ready[1]), .perst_async(pcie_rstn), .link_up(link_up),
            .reset_request(recovery_reset), .fired(recovery_fired),
            .inhibited(recovery_inhibited)
        );
    end else begin : recovery_disabled
        assign reset_scope=56'b0;
        assign recovery_reset=1'b0;
        assign recovery_fired=1'b0;
        assign recovery_inhibited=1'b0;
    end endgenerate
    wire [95:0] pcie_diagnostic;
    tarugx_pcie_debug_monitor u_pcie_debug (
        .src_clk(tlp_clk), .src_rst_n(pcie_debug_reset[1]),
        .dbg_clk(debug_clk), .dbg_rst_n(debug_reset_n), .ltssm(ltssm),
        .raw_flags({core_rst_n,pcie_rstn,clock_ready,lane_status,link_up}),
        .snapshot(pcie_diagnostic)
    );
    (* async_reg="true" *) reg [2:0] recovery_meta=0, recovery_sync=0;
    always @(posedge debug_clk or negedge debug_reset_n)
        if (!debug_reset_n) begin recovery_meta<=0; recovery_sync<=0; end
        else begin
            recovery_meta<={recovery_reset,recovery_inhibited,recovery_fired};
            recovery_sync<=recovery_meta;
        end
`ifdef TARUGX_PCIE_PHY_RECOVERY
    localparam [31:0] UART_VERSION = 32'h46523037; // FR07: PHY + core reset
`else
    localparam [31:0] UART_VERSION = PCIE_RECOVERY_EXPERIMENT ? 32'h46523036 : 32'h46523034;
`endif
`ifdef TARUGX_PCIE_ROM_PROBE
    wire [255:0] rom_probe_src, rom_probe_dbg;
`ifdef TARUGX_PCIE_RAW_BAR_PROBE
    // Generated netlist confirms SOP/data/valid/BAR and TL clock pass through.
    // Capture all 20 hard-macro BARDEC bits; do not assign meanings yet.
    wire [19:0] raw_bar_decode = {u_pcie.tl_rx_bardec0_w,
                                 u_pcie.PCIE_Controller_Top_pcie_tl_rx_bardec_i};
    wire [255:0] rom_probe_data = {rx_data[255:128],12'b0,raw_bar_decode,96'b0};
    localparam PROBE_REPLACE_UNMATCHED = 1;
`else
    wire [255:0] rom_probe_data = rx_data;
    localparam PROBE_REPLACE_UNMATCHED = 0;
`endif
    tarugx_pcie_rx_probe #(.REPLACE_UNMATCHED(PROBE_REPLACE_UNMATCHED)) u_rom_probe (
        .clk(tlp_clk), .rst_n(pcie_debug_reset[1]), .rx_sop(rx_sop), .rx_eop(rx_eop),
        .rx_wait(rx_wait), .rx_valid(rx_valid), .rx_error(rx_error),
        .rx_bar(rx_bar_decode), .rx_data(rom_probe_data), .snapshot(rom_probe_src)
    );
    tarugx_display_config_cdc #(.WIDTH(256), .SEND_UNCHANGED(1)) u_rom_probe_cdc (
        .src_clk(tlp_clk), .src_rst_n(pcie_debug_reset[1]),
        .dst_clk(debug_clk), .dst_rst_n(debug_reset_n),
        .src_config(rom_probe_src), .dst_config(rom_probe_dbg)
    );
`ifdef TARUGX_PCIE_AUTO_ROM
    // Passive observation of project-owned Veryl signals, never vendor IP internals.
    // The FIFO level and arbitration-wait fields remain valid with pipelined DDR;
    // serial request/response latency fields do not and are not exported by FR17.
    wire [183:0] scanout_perf_src;
    tarugx_scanout_debug_monitor u_scanout_debug (
        .mem_clk(ui_clk), .mem_rst_n(mem_reset[1]),
        .pixel_clk(pixel_clk), .pixel_rst_n(pixel_reset[1]),
        .push(u_scanout.push_valid && u_scanout.push_ready),
        .pop(u_scanout.pop_valid && u_scanout.pop_ready), .underflow(underflow),
        .scan_valid(scan_valid), .scan_ready(scan_ready),
        .scan_rsp_valid(scan_rsp_valid), .scan_rsp_ready(scan_rsp_ready),
        .mem_valid(mem_valid), .mem_ready(mem_ready), .mem_write(mem_write),
        .mem_rsp_valid(mem_rsp_valid), .mem_rsp_ready(mem_rsp_ready),
        .snapshot(scanout_perf_src)
    );
`ifdef TARUGX_PIPELINED_MEMORY
    // FR17: keep PCIe recovery/fault data and use the former reset-scope area
    // for pipeline-safe scanout wait/FIFO diagnostics. Layout after version:
    // max_wait[16], reserved[32], frozen/armed/pad/min/max[24], reserved[8].
    wire [39:0] pipeline_scanout_src = {
        scanout_perf_src[183:168], scanout_perf_src[23:0]
    };
    wire [39:0] pipeline_scanout_dbg;
    tarugx_display_config_cdc #(.WIDTH(40)) u_scanout_debug_cdc (
        .src_clk(ui_clk), .src_rst_n(mem_reset[1]),
        .dst_clk(debug_clk), .dst_rst_n(debug_reset_n),
        .src_config(pipeline_scanout_src), .dst_config(pipeline_scanout_dbg)
    );
    wire [79:0] pipeline_scanout_debug = {
        pipeline_scanout_dbg[39:24], 32'b0, pipeline_scanout_dbg[23:0], 8'b0
    };
    wire [287:0] uart_snapshot = {32'h46523137,pipeline_scanout_debug,5'b0,recovery_sync,pcie_diagnostic,debug_fault,debug_aux_sync};
`else
    wire [183:0] scanout_perf_dbg;
    tarugx_display_config_cdc #(.WIDTH(184), .SEND_UNCHANGED(1)) u_scanout_debug_cdc (
        .src_clk(ui_clk), .src_rst_n(mem_reset[1]),
        .dst_clk(debug_clk), .dst_rst_n(debug_reset_n),
        .src_config(scanout_perf_src), .dst_config(scanout_perf_dbg)
    );
    wire [287:0] uart_snapshot = {32'h46523134,scanout_perf_dbg,debug_fault,debug_aux_sync};
`endif
`elsif TARUGX_PCIE_RAW_BAR_PROBE
    wire [287:0] uart_snapshot = {32'h46523130,rom_probe_dbg}; // FR10: raw BARDEC
`elsif TARUGX_PCIE_ROM_DIAGNOSTIC
    wire [287:0] uart_snapshot = {32'h46523039,rom_probe_dbg}; // FR09: software-configured ROM
`else
    wire [287:0] uart_snapshot = {32'h46523038,rom_probe_dbg}; // FR08: passive RX probe
`endif
`else
    wire [287:0] uart_snapshot = {UART_VERSION,24'b0,reset_scope,5'b0,recovery_sync,pcie_diagnostic,debug_fault,debug_aux_sync};
`endif
    tarugx_ddr_debug_uart #(.CLKS_PER_BIT(91), .FRAME_GAP_CYCLES(2625000)) u_status_uart (
        .clk(debug_clk), .rst_n(debug_reset_n), .status(status_sync),
        .snapshot(uart_snapshot), .tx(uart_tx)
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
