// SPDX-License-Identifier: Apache-2.0
// Derived from Sipeed TangMega-138K-example; see THIRD_PARTY_NOTICES.md.
// Copyright [2024] [Shenzhen Sipeed Technology Co,Ltd.]
// Modified by tarusake, 2026.
// Pipelined DVI TMDS encoder for the 148 MHz pixel domain.
module dvi_tx_tmds_enc(
    input  wire       clock,
    input  wire       reset,
    input  wire       den,
    input  wire [7:0] data,
    input  wire [1:0] ctrl,
    output reg  [9:0] tmds
);
    function automatic [3:0] count_ones(input [7:0] value);
        integer i;
        begin
            count_ones = 0;
            for (i = 0; i < 8; i = i + 1)
                count_ones = count_ones + value[i];
        end
    endfunction

    reg [7:0] data_reg;
    reg       den_reg;
    reg [1:0] ctrl_reg;
    wire      use_xnor = (count_ones(data_reg) > 4) ||
                         ((count_ones(data_reg) == 4) && !data_reg[0]);
    wire [8:0] qm_next;
    assign qm_next[0] = data_reg[0];
    assign qm_next[1] = use_xnor ? ~(qm_next[0] ^ data_reg[1]) : qm_next[0] ^ data_reg[1];
    assign qm_next[2] = use_xnor ? ~(qm_next[1] ^ data_reg[2]) : qm_next[1] ^ data_reg[2];
    assign qm_next[3] = use_xnor ? ~(qm_next[2] ^ data_reg[3]) : qm_next[2] ^ data_reg[3];
    assign qm_next[4] = use_xnor ? ~(qm_next[3] ^ data_reg[4]) : qm_next[3] ^ data_reg[4];
    assign qm_next[5] = use_xnor ? ~(qm_next[4] ^ data_reg[5]) : qm_next[4] ^ data_reg[5];
    assign qm_next[6] = use_xnor ? ~(qm_next[5] ^ data_reg[6]) : qm_next[5] ^ data_reg[6];
    assign qm_next[7] = use_xnor ? ~(qm_next[6] ^ data_reg[7]) : qm_next[6] ^ data_reg[7];
    assign qm_next[8] = !use_xnor;

    reg [8:0] qm_reg;
    reg       qm_den;
    reg [1:0] qm_ctrl;
    reg signed [8:0] disparity;
    reg signed [8:0] next_disparity;
    reg [9:0] encoded;
    wire [3:0] qm_ones = count_ones(qm_reg[7:0]);
    wire signed [8:0] balance = $signed({4'b0, qm_ones, 1'b0}) - 9'sd8;

    always @* begin
        encoded = 10'b0;
        next_disparity = disparity;
        if (!qm_den) begin
            next_disparity = 0;
            case (qm_ctrl)
                2'b00: encoded = 10'b1101010100;
                2'b01: encoded = 10'b0010101011;
                2'b10: encoded = 10'b0101010100;
                default: encoded = 10'b1010101011;
            endcase
        end else if ((disparity == 0) || (qm_ones == 4)) begin
            encoded = {~qm_reg[8], qm_reg[8], qm_reg[8] ? qm_reg[7:0] : ~qm_reg[7:0]};
            next_disparity = disparity + (qm_reg[8] ? balance : -balance);
        end else if (((disparity > 0) && (balance > 0)) ||
                     ((disparity < 0) && (balance < 0))) begin
            encoded = {1'b1, qm_reg[8], ~qm_reg[7:0]};
            next_disparity = disparity + (qm_reg[8] ? 9'sd2 : 9'sd0) - balance;
        end else begin
            encoded = {1'b0, qm_reg[8], qm_reg[7:0]};
            next_disparity = disparity + balance - (qm_reg[8] ? 9'sd0 : 9'sd2);
        end
    end

    always @(posedge clock) begin
        if (reset) begin
            data_reg <= 0;
            den_reg <= 0;
            ctrl_reg <= 0;
            qm_reg <= 0;
            qm_den <= 0;
            qm_ctrl <= 0;
            disparity <= 0;
            tmds <= 0;
        end else begin
            data_reg <= data;
            den_reg <= den;
            ctrl_reg <= ctrl;
            qm_reg <= qm_next;
            qm_den <= den_reg;
            qm_ctrl <= ctrl_reg;
            disparity <= next_disparity;
            tmds <= encoded;
        end
    end
endmodule
