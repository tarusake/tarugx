// SPDX-License-Identifier: Apache-2.0
`timescale 1ns/1ps
module dvi_tx_tmds_enc_tb;
    logic clock = 0, reset = 1, den = 0;
    logic [7:0] data = 0;
    logic [1:0] ctrl = 0;
    logic [9:0] tmds;
    integer signed ref_disparity = 0;
    logic [9:0] expected_0, expected_1;
    integer active_edges = 0;

    always #5 clock = ~clock;
    dvi_tx_tmds_enc dut(.*);

    function automatic integer ones(input logic [7:0] value);
        integer i;
        begin
            ones = 0;
            for (i = 0; i < 8; i++) ones += value[i];
        end
    endfunction

    task automatic reference_encode(
        input logic enabled, input logic [7:0] value, input logic [1:0] control,
        output logic [9:0] result
    );
        logic use_xnor;
        logic [8:0] qm;
        integer i, count, balance;
        begin
            if (!enabled) begin
                ref_disparity = 0;
                case (control)
                    0: result = 10'b1101010100;
                    1: result = 10'b0010101011;
                    2: result = 10'b0101010100;
                    3: result = 10'b1010101011;
                endcase
            end else begin
                use_xnor = (ones(value) > 4) ||
                           ((ones(value) == 4) && !value[0]);
                qm[0] = value[0];
                for (i = 1; i < 8; i++)
                    qm[i] = use_xnor ? ~(qm[i-1] ^ value[i]) : qm[i-1] ^ value[i];
                qm[8] = !use_xnor;
                count = ones(qm[7:0]);
                balance = 2 * count - 8;
                if ((ref_disparity == 0) || (count == 4)) begin
                    result = {~qm[8], qm[8], qm[8] ? qm[7:0] : ~qm[7:0]};
                    ref_disparity += qm[8] ? balance : -balance;
                end else if (((ref_disparity > 0) && (balance > 0)) ||
                             ((ref_disparity < 0) && (balance < 0))) begin
                    result = {1'b1, qm[8], ~qm[7:0]};
                    ref_disparity += (qm[8] ? 2 : 0) - balance;
                end else begin
                    result = {1'b0, qm[8], qm[7:0]};
                    ref_disparity += balance - (qm[8] ? 0 : 2);
                end
            end
        end
    endtask

    always @(posedge clock) begin
        logic [9:0] current_expected;
        if (!reset) begin
            reference_encode(den, data, ctrl, current_expected);
            #1;
            if (active_edges >= 2)
                assert(tmds == expected_1)
                    else $fatal(1, "TMDS mismatch edge=%0d got=%b expected=%b",
                                active_edges, tmds, expected_1);
            expected_1 = expected_0;
            expected_0 = current_expected;
            active_edges++;
        end
    end

    initial begin
        repeat (3) @(negedge clock);
        reset = 0;
        for (integer c = 0; c < 4; c++) begin
            den = 0; ctrl = c; @(negedge clock);
        end
        den = 1;
        for (integer value = 0; value < 256; value++) begin
            data = value; @(negedge clock);
        end
        den = 0; ctrl = 0;
        repeat (4) @(negedge clock);
        $display("Pipelined TMDS encoder: all byte values and control symbols PASS");
        $finish;
    end
endmodule
