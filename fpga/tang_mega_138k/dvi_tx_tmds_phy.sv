// SPDX-License-Identifier: Apache-2.0
// Derived from Sipeed TangMega-138K-example; see THIRD_PARTY_NOTICES.md.
// Copyright [2024] [Shenzhen Sipeed Technology Co,Ltd.]
// Modified by tarusake, 2026.
// Gowin-specific TMDS serializer boundary. The encoder remains the Sipeed IP.
// Match the vendor/Sipeed reference PHY: OSER10 reset is registered in the
// pixel-clock domain. RESET is an asynchronous primitive control, not a
// serializer data input; its path is excluded from data timing in the SDC.
module dvi_tx_tmds_phy(
 input wire pixel_clock, ddr_bit_clock, reset,
 input wire [9:0] data,
 output wire [1:0] tmds_lane
);
 (* syn_preserve=1, syn_keep=1 *) reg reset_reg = 1'b1;
 always @(posedge pixel_clock)
  reset_reg <= reset;
 wire dq_tmds;
 OSER10 tmds_serdes_inst0(
  .Q(dq_tmds),.D0(data[0]),.D1(data[1]),.D2(data[2]),.D3(data[3]),.D4(data[4]),
  .D5(data[5]),.D6(data[6]),.D7(data[7]),.D8(data[8]),.D9(data[9]),
  .PCLK(pixel_clock),.FCLK(ddr_bit_clock),.RESET(reset_reg));
 ELVDS_OBUF tmds_bufds_isnt0(.I(dq_tmds),.O(tmds_lane[1]),.OB(tmds_lane[0]));
endmodule
