// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module render_rgb565_tb;
 logic clk=0,rst_n=0; always #5 clk=~clk;
 logic fill_valid=0,fill_ready,fill_busy,fill_done,fill_error;
 logic [31:0] fill_addr=0,fill_stride=0,fill_color=0;
 logic [15:0] fill_width=0,fill_height=0;
 logic fv,fr=0; logic [23:0] fa; logic [255:0] fd; logic [31:0] fs;
 logic frr=0,frdy,fre=0;
 tarugx_fill_engine #(.PIXEL_BYTES(2)) fill(
  .clk,.rst_n,.cmd_valid(fill_valid),.cmd_ready(fill_ready),.cmd_addr(fill_addr),
  .cmd_stride(fill_stride),.cmd_width(fill_width),.cmd_height(fill_height),
  .cmd_color(fill_color),.busy(fill_busy),.done(fill_done),.error(fill_error),
  .mem_valid(fv),.mem_ready(fr),.mem_addr(fa),.mem_wdata(fd),.mem_wstrb(fs),
  .mem_rsp_valid(frr),.mem_rsp_ready(frdy),.mem_rsp_error(fre));

 logic blit_valid=0,blit_ready,blit_busy,blit_done,blit_error;
 logic [31:0] src=0,dst=0,ss=0,ds=0; logic [15:0] bw=0,bh=0;
 logic bv,br=0,bwrite; logic [23:0] ba; logic [255:0] bd,brd=0; logic [31:0] bs;
 logic brr=0,brrdy,bre=0;
 tarugx_blit_engine #(.PIXEL_BYTES(2)) blit(
  .clk,.rst_n,.cmd_valid(blit_valid),.cmd_ready(blit_ready),
  .cmd_src_addr(src),.cmd_dst_addr(dst),.cmd_src_stride(ss),.cmd_dst_stride(ds),
  .cmd_width(bw),.cmd_height(bh),.busy(blit_busy),.done(blit_done),.error(blit_error),
  .mem_valid(bv),.mem_ready(br),.mem_write(bwrite),.mem_addr(ba),
  .mem_wdata(bd),.mem_wstrb(bs),.mem_rsp_valid(brr),.mem_rsp_ready(brrdy),
  .mem_rsp_data(brd),.mem_rsp_error(bre));

 byte unsigned memory[0:4095]; int fill_requests=0,blit_reads=0,blit_writes=0;
 task tick; @(posedge clk); #1; endtask
 task service_fill;
  while(!fill_done) begin
   if(fv) begin
    logic [23:0] a; logic [255:0] d; logic [31:0] s;
    a=fa;d=fd;s=fs; @(negedge clk);fr=1;tick();@(negedge clk);fr=0;
    for(int i=0;i<32;i++)if(s[i])memory[int'(a)+i]=d[i*8+:8];
    @(negedge clk);frr=1;tick();@(negedge clk);frr=0;fill_requests++;
   end else tick();
  end
 endtask
 task service_blit;
  while(!blit_done) begin
   if(bv) begin
    logic w; logic [23:0] a; logic [255:0] d; logic [31:0] s;
    w=bwrite;a=ba;d=bd;s=bs;
    if(!w)for(int i=0;i<32;i++)brd[i*8+:8]=memory[int'(a)+i];
    @(negedge clk);br=1;tick();@(negedge clk);br=0;
    if(w)begin for(int i=0;i<32;i++)if(s[i])memory[int'(a)+i]=d[i*8+:8];blit_writes++;end
    else blit_reads++;
    @(negedge clk);brr=1;tick();@(negedge clk);brr=0;
   end else tick();
  end
 endtask
 function automatic logic [15:0] pixel(input int a); return {memory[a+1],memory[a]}; endfunction
 initial begin
  foreach(memory[i])memory[i]=8'ha5;
  repeat(3)tick();@(negedge clk);rst_n=1;
  fill_addr=2;fill_stride=40;fill_width=17;fill_height=2;fill_color=32'h00ff0000;fill_valid=1;
  tick();@(negedge clk);fill_valid=0;service_fill();
  assert(!fill_error&&fill_requests==4)else $fatal(1,"RGB565 fill requests");
  for(int y=0;y<2;y++)for(int x=0;x<17;x++)assert(pixel(2+y*40+x*2)==16'hf800)else $fatal(1,"RGB565 fill data");
  src=2;dst=1026;ss=40;ds=40;bw=17;bh=2;blit_valid=1;
  tick();@(negedge clk);blit_valid=0;service_blit();
  assert(!blit_error&&blit_reads==blit_writes)else $fatal(1,"RGB565 blit requests");
  for(int y=0;y<2;y++)for(int x=0;x<17;x++)assert(pixel(1026+y*40+x*2)==16'hf800)else $fatal(1,"RGB565 blit data");
  $display("RGB565 render: partial fill and unaligned 2D blit PASS");$finish;
 end
 initial begin #1000000;$fatal(1,"timeout");end
endmodule
