// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module render_command_queue_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic enqueue_valid=0,enqueue_ready;logic [401:0] enqueue_command=0;
 logic dispatch_valid,dispatch_ready=0;logic [401:0] dispatch_command;
 logic completion_valid=0,completion_error=0,clear_error=0;logic [2:0] level;logic active;
 logic [31:0] completed_fence,error_fence;logic error_sticky;
 tarugx_render_command_queue dut(.*);
 task tick;@(posedge clk);#1;endtask
 function automatic logic [401:0] command(input int opcode,input int fence,input int payload);
  return {368'(payload),32'(fence),2'(opcode)};
 endfunction
 task enqueue(input int opcode,input int fence,input int payload);
  @(negedge clk);assert(enqueue_ready)else $fatal(1,"queue not ready");
  enqueue_command=command(opcode,fence,payload);enqueue_valid=1;tick();
  @(negedge clk);enqueue_valid=0;
 endtask
 task dispatch(input int opcode,input int fence,input int payload);
  while(!dispatch_valid)tick();
  assert(dispatch_command==command(opcode,fence,payload))else $fatal(1,"dispatch order/data");
  @(negedge clk);dispatch_ready=1;tick();@(negedge clk);dispatch_ready=0;
  assert(active)else $fatal(1,"command did not become active");
 endtask
 task complete(input bit failed);
  @(negedge clk);completion_error=failed;completion_valid=1;tick();
  @(negedge clk);completion_valid=0;completion_error=0;
  assert(!active)else $fatal(1,"completion did not retire active command");
 endtask
 logic [401:0] held;
 initial begin
  repeat(3)tick();@(negedge clk);rst_n=1;
  // Fill all waiting slots and verify backpressure plus a stable stalled head.
  enqueue(1,10,'h10);enqueue(2,11,'h11);enqueue(1,12,'h12);enqueue(2,13,'h13);
  assert(level==4&&!enqueue_ready&&dispatch_valid)else $fatal(1,"full status");
  held=dispatch_command;repeat(3)begin tick();assert(dispatch_valid&&dispatch_command==held)else $fatal(1,"stalled head changed");end
  dispatch(1,10,'h10);assert(level==3&&!dispatch_valid)else $fatal(1,"active gating");
  // The fifth waiting command is accepted while the first executes.
  enqueue(1,14,'h14);assert(level==4&&!enqueue_ready)else $fatal(1,"active plus four queued");
  complete(0);assert(completed_fence==10&&!error_sticky)else $fatal(1,"successful fence");
  dispatch(2,11,'h11);complete(1);
  assert(completed_fence==11&&error_sticky&&error_fence==11)else $fatal(1,"error fence");
  dispatch(1,12,'h12);complete(0);dispatch(2,13,'h13);complete(0);dispatch(1,14,'h14);complete(0);
  assert(level==0&&!dispatch_valid&&completed_fence==14)else $fatal(1,"queue drain");
  // A completion with no active command must not corrupt fence state.
  @(negedge clk);completion_valid=1;completion_error=1;tick();@(negedge clk);completion_valid=0;completion_error=0;
  assert(completed_fence==14&&error_fence==11)else $fatal(1,"spurious completion");
  @(negedge clk);clear_error=1;tick();@(negedge clk);clear_error=0;
  assert(!error_sticky&&error_fence==0)else $fatal(1,"error clear");
  $display("Render command queue: FIFO order, backpressure, active gating and fence/error retirement PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
