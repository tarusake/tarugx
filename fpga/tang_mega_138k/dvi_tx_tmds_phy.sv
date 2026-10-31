// Gowin-specific TMDS serializer boundary. The encoder remains the Sipeed IP.
// Assert reset asynchronously; release it on the serializer's fast clock,
// instead of driving OSER10 RESET from a pixel-clock register.
module dvi_tx_tmds_phy(
 input wire pixel_clock, ddr_bit_clock, reset,
 input wire [9:0] data,
 output wire [1:0] tmds_lane
);
 (* async_reg="true" *) reg [2:0] reset_release = 0;
 always @(posedge ddr_bit_clock or posedge reset)
  if (reset) reset_release <= 0;
  else reset_release <= {reset_release[1:0],1'b1};
 wire dq_tmds;
 OSER10 tmds_serdes_inst0(
  .Q(dq_tmds),.D0(data[0]),.D1(data[1]),.D2(data[2]),.D3(data[3]),.D4(data[4]),
  .D5(data[5]),.D6(data[6]),.D7(data[7]),.D8(data[8]),.D9(data[9]),
  .PCLK(pixel_clock),.FCLK(ddr_bit_clock),.RESET(!reset_release[2]));
 ELVDS_OBUF tmds_bufds_isnt0(.I(dq_tmds),.O(tmds_lane[1]),.OB(tmds_lane[0]));
endmodule
