// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module triangle_rasterizer_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cmd_valid=0,cmd_ready;
 logic signed [23:0] cmd_x0=0,cmd_y0=0,cmd_x1=0,cmd_y1=0,cmd_x2=0,cmd_y2=0;
 logic [15:0] cmd_width=0,cmd_height=0;logic [31:0] cmd_color=0;
 logic busy,done,error,fragment_valid,fragment_ready=0;
 logic [15:0] fragment_x,fragment_y;logic [31:0] fragment_color;
 bit expected[0:255],seen[0:255];int active_width,active_height,fragments;
 tarugx_triangle_rasterizer dut(.*);

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
  int cycles=0;logic [15:0] held_x,held_y;logic [31:0] held_color;bit holding=0;
  build_expected(x0,y0,x1,y1,x2,y2,width,height);
  active_width=width;active_height=height;fragments=0;
  @(negedge clk);assert(cmd_ready)else $fatal(1,"command not ready");
  cmd_x0=24'(x0);cmd_y0=24'(y0);cmd_x1=24'(x1);cmd_y1=24'(y1);
  cmd_x2=24'(x2);cmd_y2=24'(y2);cmd_width=16'(width);cmd_height=16'(height);
  cmd_color=32'h12ab34cd;cmd_valid=1;tick();@(negedge clk);cmd_valid=0;
  while(!done)begin
   fragment_ready=(cycles%3)!=0;
   if(fragment_valid&&!fragment_ready)begin
    if(holding)assert(fragment_x==held_x&&fragment_y==held_y&&fragment_color==held_color)
     else $fatal(1,"fragment changed under backpressure");
    held_x=fragment_x;held_y=fragment_y;held_color=fragment_color;holding=1;
   end else if(fragment_valid&&fragment_ready)begin
    holding=0;
    assert(int'(fragment_x)<active_width && int'(fragment_y)<active_height)
     else $fatal(1,"fragment outside target %0d,%0d",fragment_x,fragment_y);
    assert(fragment_color==32'h12ab34cd)else $fatal(1,"color changed");
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
  command(0,0,0,4*256,4*256,0,4,4);
  command(-4*256,-4*256,6*256,0,0,6*256,4,4);
  command(320,320,1856,512,512,1664,8,8);
  command(256,256,512,512,768,768,8,8);
  command(0,0,256,0,0,256,0,8,1);
  @(negedge clk);cmd_x0=0;cmd_y0=0;cmd_x1=1024;cmd_y1=0;cmd_x2=0;cmd_y2=1024;
  cmd_width=8;cmd_height=8;cmd_valid=1;tick();@(negedge clk);cmd_valid=0;
  while(!fragment_valid)tick();fragment_ready=0;tick();@(negedge clk);rst_n=0;tick();
  assert(cmd_ready&&!busy&&!done&&!fragment_valid)else $fatal(1,"reset failed");
  $display("Triangle rasterizer: winding, top-left coverage, clipping, subpixels, stalls and reset PASS");
  $finish;
 end
 initial begin #1000000;$fatal(1,"timeout");end
endmodule
