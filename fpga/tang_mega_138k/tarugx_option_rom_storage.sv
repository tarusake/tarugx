// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
// FPGA storage mapping only; protocol and diagnostic controls are Veryl.
// 1-cycle synchronous read, initialized from the validated 64 KiB image.
module tarugx_option_rom_storage #(
    parameter INIT_FILE = ""
) (
    input wire clk, input wire en, input wire [13:0] addr,
    output reg [31:0] data
);
    reg [31:0] memory [0:16383];
    initial $readmemh(INIT_FILE, memory);
    always @(posedge clk) if (en) data <= memory[addr];
endmodule
