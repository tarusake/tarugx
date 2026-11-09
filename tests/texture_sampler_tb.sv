// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
module texture_sampler_tb;
 logic clk=0,rst_n=0,clear=0,sample_valid=0,sample_ready,color_valid,color_ready=0;
 logic signed [23:0] sample_u=0,sample_v=0;
 logic [31:0] texture_base=32'h1000,texture_stride=32'd16,color;
 logic [15:0] texture_width=8,texture_height=4;
 logic busy,fault,mem_valid,mem_ready=0,mem_write,mem_rsp_valid=0,mem_rsp_ready,mem_rsp_error=0;
 logic [23:0] mem_addr;logic [255:0] mem_wdata,mem_rsp_data=0;logic [31:0] mem_wstrb;
 tarugx_texture_sampler dut(.*);
 always #5 clk=~clk;
 task tick;@(posedge clk);#1;endtask
 task configure;
  clear=1;tick();clear=0;tick();
  assert(sample_ready&&!fault&&!busy)else $fatal(1,"configuration reset");
 endtask
 task sample(input logic signed [23:0] u,input logic signed [23:0] v,
             input logic [23:0] expected_addr,
             input integer lane,input logic [15:0] texel,input logic [31:0] expected_color);
  sample_u=u;sample_v=v;sample_valid=1;tick();sample_valid=0;
  wait(mem_valid);#1;
  assert(mem_addr==expected_addr&&!mem_write&&mem_wstrb==0&&mem_wdata==0)
   else $fatal(1,"texture read request addr=%h",mem_addr);
  mem_ready=1;tick();mem_ready=0;
  assert(mem_rsp_ready&&!color_valid)else $fatal(1,"texture response wait");
  mem_rsp_data=0;mem_rsp_data[lane*16+:16]=texel;mem_rsp_valid=1;tick();mem_rsp_valid=0;
  assert(color_valid&&color==expected_color)else $fatal(1,"texture color %h",color);
  tick();assert(color_valid&&color==expected_color)else $fatal(1,"texture output stability");
  color_ready=1;tick();color_ready=0;assert(sample_ready)else $fatal(1,"texture completion");
 endtask
 initial begin
  repeat(3)tick();rst_n=1;configure();
  // (3.75, 2.5) floors to texel (3,2): 0x1000 + 2*16 + 3*2.
  sample(24'sh0003c0,24'sh000280,24'h001020,3,16'hf81f,32'hffff00ff);
  // Clamp negative U and too-large V to (0,3).
  sample(-24'sh000100,24'sh000900,24'h001020,8,16'h07e0,32'hff00ff00);
  mem_ready=1;sample_u=0;sample_v=0;sample_valid=1;tick();sample_valid=0;
  wait(mem_valid);tick();
  assert(mem_rsp_ready)else $fatal(1,"texture response must remain ready");
  mem_rsp_error=1;mem_rsp_valid=1;tick();mem_rsp_valid=0;mem_rsp_error=0;
  assert(fault&&!sample_ready)else $fatal(1,"texture response error");
  configure();texture_stride=8;configure();sample_valid=1;tick();sample_valid=0;tick();
  assert(fault)else $fatal(1,"invalid texture shape");
  $display("Texture sampler: nearest RGB565, clamp, stalls, bounds and error recovery PASS");
  $finish;
 end
endmodule
