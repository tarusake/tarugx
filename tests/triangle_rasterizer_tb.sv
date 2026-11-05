// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module triangle_rasterizer_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic abort=0,cmd_valid=0,cmd_ready;
 logic signed [23:0] cmd_x0=0,cmd_y0=0,cmd_x1=0,cmd_y1=0,cmd_x2=0,cmd_y2=0;
 logic [15:0] cmd_width=0,cmd_height=0;logic [31:0] cmd_color=0;
 logic [31:0] cmd_gradient_r=0,cmd_gradient_g=0,cmd_gradient_b=0;
 logic [15:0] cmd_depth=0;
 logic signed [23:0] cmd_gradient_z_x=0,cmd_gradient_z_y=0;
 logic busy,done,error,fragment_valid,fragment_ready=0;
 logic [15:0] fragment_x,fragment_y,fragment_depth;logic [31:0] fragment_color;
 bit expected[0:255],seen[0:255];int active_width,active_height,fragments;
 logic [31:0] test_color=32'h12ab34cd;
 logic [31:0] test_gradient_r=0,test_gradient_g=0,test_gradient_b=0;
 logic [15:0] test_depth=16'h4321;
 logic signed [23:0] test_gradient_z_x=0,test_gradient_z_y=0;
 tarugx_triangle_rasterizer dut(.*);

 function automatic [7:0] expected_channel(
  input logic [7:0] base,input logic [31:0] gradient,
  input logic [15:0] x,y);
  integer signed value,dx,dy;
  dx=$signed({{16{gradient[15]}},gradient[15:0]});
  dy=$signed({{16{gradient[31]}},gradient[31:16]});
  value=(integer'(base)<<<7)+dx*x+dy*y;
  if(value<0)expected_channel=0;
  else if(value>32640)expected_channel=8'hff;
  else expected_channel=8'(value>>>7);
 endfunction
 function automatic [31:0] expected_color(
  input logic [15:0] x,y);
  expected_color={test_color[31:24],
   expected_channel(test_color[23:16],test_gradient_r,x,y),
   expected_channel(test_color[15:8],test_gradient_g,x,y),
   expected_channel(test_color[7:0],test_gradient_b,x,y)};
 endfunction
 function automatic [15:0] expected_depth(
  input logic [15:0] x,y);
  longint signed value;
  value=(longint'(test_depth)<<<8)
   +longint'($signed(test_gradient_z_x))*x
   +longint'($signed(test_gradient_z_y))*y;
  if(value<0)expected_depth=0;
  else if(value>=16776960)expected_depth=16'hffff;
  else expected_depth=16'(value>>>8);
 endfunction

 function automatic longint signed edge_fn(
  input integer signed ax,ay,bx,by,px,py);
  edge_fn=(longint'(bx)-longint'(ax))*(longint'(py)-longint'(ay))
         -(longint'(by)-longint'(ay))*(longint'(px)-longint'(ax));
 endfunction
 function automatic bit top_left(input integer signed ax,ay,bx,by);
  top_left=(by-ay)<0 || ((by-ay)==0 && (bx-ax)>0);
 endfunction
 task tick;@(posedge clk);#1;endtask

 task build_expected(
  input integer signed x0,y0,x1,y1,x2,y2,input int width,height);
  integer signed tx,ty;longint signed area,e0,e1,e2;bit i0,i1,i2;
  foreach(expected[i])begin expected[i]=0;seen[i]=0;end
  area=edge_fn(x0,y0,x1,y1,x2,y2);
  if(area<0)begin tx=x1;ty=y1;x1=x2;y1=y2;x2=tx;y2=ty;end
  i0=top_left(x0,y0,x1,y1);i1=top_left(x1,y1,x2,y2);i2=top_left(x2,y2,x0,y0);
  if(area!=0)for(int y=0;y<height;y++)for(int x=0;x<width;x++)begin
   e0=edge_fn(x0,y0,x1,y1,x*256+128,y*256+128);
   e1=edge_fn(x1,y1,x2,y2,x*256+128,y*256+128);
   e2=edge_fn(x2,y2,x0,y0,x*256+128,y*256+128);
   expected[y*width+x]=(e0>0||(e0==0&&i0))&&(e1>0||(e1==0&&i1))&&(e2>0||(e2==0&&i2));
  end
 endtask

 task automatic command(
  input integer signed x0,y0,x1,y1,x2,y2,input int width,height,
  input bit expect_error=0);
  int cycles=0;logic [15:0] held_x,held_y,held_depth;
  logic [31:0] held_color;bit holding=0;
  build_expected(x0,y0,x1,y1,x2,y2,width,height);
  active_width=width;active_height=height;fragments=0;
  @(negedge clk);assert(cmd_ready)else $fatal(1,"command not ready");
  cmd_x0=24'(x0);cmd_y0=24'(y0);cmd_x1=24'(x1);cmd_y1=24'(y1);
  cmd_x2=24'(x2);cmd_y2=24'(y2);cmd_width=16'(width);cmd_height=16'(height);
  cmd_color=test_color;cmd_gradient_r=test_gradient_r;
  cmd_gradient_g=test_gradient_g;cmd_gradient_b=test_gradient_b;
  cmd_depth=test_depth;cmd_gradient_z_x=test_gradient_z_x;
  cmd_gradient_z_y=test_gradient_z_y;
  cmd_valid=1;tick();@(negedge clk);cmd_valid=0;
  while(!done)begin
   fragment_ready=(cycles%3)!=0;
   if(fragment_valid&&!fragment_ready)begin
    if(holding)assert(fragment_x==held_x&&fragment_y==held_y
     &&fragment_color==held_color&&fragment_depth==held_depth)
     else $fatal(1,"fragment changed under backpressure");
    held_x=fragment_x;held_y=fragment_y;held_color=fragment_color;
    held_depth=fragment_depth;holding=1;
   end else if(fragment_valid&&fragment_ready)begin
    holding=0;
    assert(int'(fragment_x)<active_width && int'(fragment_y)<active_height)
     else $fatal(1,"fragment outside target %0d,%0d",fragment_x,fragment_y);
    assert(fragment_color==expected_color(fragment_x,fragment_y))
     else $fatal(1,"color mismatch %0d,%0d expected=%08x got=%08x",
      fragment_x,fragment_y,expected_color(fragment_x,fragment_y),fragment_color);
    assert(fragment_depth==expected_depth(fragment_x,fragment_y))
     else $fatal(1,"depth mismatch %0d,%0d expected=%04x got=%04x",
      fragment_x,fragment_y,expected_depth(fragment_x,fragment_y),fragment_depth);
    assert(expected[int'(fragment_y)*active_width+int'(fragment_x)])
     else $fatal(1,"unexpected fragment %0d,%0d",fragment_x,fragment_y);
    assert(!seen[int'(fragment_y)*active_width+int'(fragment_x)])
     else $fatal(1,"duplicate fragment %0d,%0d",fragment_x,fragment_y);
    seen[int'(fragment_y)*active_width+int'(fragment_x)]=1;fragments++;
   end
   tick();cycles++;if(cycles>2000)$fatal(1,"command timeout");
  end
  fragment_ready=0;
  assert(error==expect_error)else $fatal(1,"error mismatch expected=%0d got=%0d",expect_error,error);
  if(!expect_error)for(int y=0;y<height;y++)for(int x=0;x<width;x++)
   assert(seen[y*width+x]==expected[y*width+x])
    else $fatal(1,"coverage mismatch %0d,%0d expected=%0d seen=%0d",x,y,expected[y*width+x],seen[y*width+x]);
  tick();assert(cmd_ready&&!busy&&!done)else $fatal(1,"completion did not return idle");
 endtask

 initial begin
  repeat(3)tick();@(negedge clk);rst_n=1;
  command(0,0,4*256,0,0,4*256,4,4);
  test_color=32'h12000010;
  test_gradient_r=32'h00001000;
  test_gradient_g=32'h10000000;
  test_gradient_b=32'h08000800;
  command(0,0,4*256,0,0,4*256,4,4);
  test_color=32'h12ab34cd;
  test_gradient_r=0;test_gradient_g=0;test_gradient_b=0;
  test_depth=16'd100;test_gradient_z_x=24'sd256;test_gradient_z_y=24'sd512;
  command(0,0,4*256,0,0,4*256,4,4);
  test_depth=16'hfffe;test_gradient_z_x=24'sd512;test_gradient_z_y=0;
  command(0,0,4*256,0,0,4*256,4,4);
  test_depth=16'd1;test_gradient_z_x=-24'sd512;test_gradient_z_y=0;
  command(0,0,4*256,0,0,4*256,4,4);
  test_depth=16'h4321;test_gradient_z_x=0;test_gradient_z_y=0;
  command(0,0,0,4*256,4*256,0,4,4);
  command(-4*256,-4*256,6*256,0,0,6*256,4,4);
  command(320,320,1856,512,512,1664,8,8);
  command(256,256,512,512,768,768,8,8);
  command(0,0,256,0,0,256,0,8,1);
  @(negedge clk);cmd_x0=0;cmd_y0=0;cmd_x1=1024;cmd_y1=0;cmd_x2=0;cmd_y2=1024;
  cmd_width=8;cmd_height=8;cmd_valid=1;tick();@(negedge clk);cmd_valid=0;
  while(!fragment_valid)tick();fragment_ready=0;tick();@(negedge clk);rst_n=0;tick();
  assert(cmd_ready&&!busy&&!done&&!fragment_valid)else $fatal(1,"reset failed");
  $display("Triangle rasterizer: coverage, color/depth interpolation, clipping, stalls and reset PASS");
  $finish;
 end
 initial begin #1000000;$fatal(1,"timeout");end
endmodule
