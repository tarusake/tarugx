// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module render_queue_dispatcher_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic dispatch_valid=0,dispatch_ready;logic [401:0] dispatch_command=0;
 logic legacy_fill_start=0,legacy_blit_start=0,legacy_triangle_start=0;
 logic fill_ready=1,fill_done=0,fill_error=0;
 logic blit_ready=1,blit_done=0,blit_error=0,fill_start,blit_start,completion_valid,completion_error;
 logic triangle_ready=1,triangle_done=0,triangle_error=0,triangle_start;
 logic [127:0] fill_command;logic [159:0] blit_command;
 logic [367:0] triangle_command;
 tarugx_render_queue_dispatcher dut(.*);
 task tick;@(posedge clk);#1;endtask
 task offer(input int opcode,input logic [367:0] payload);
  @(negedge clk);dispatch_command={payload,32'h12345678,2'(opcode)};dispatch_valid=1;
 endtask
 task finish_offer;tick();@(negedge clk);dispatch_valid=0;endtask
 initial begin
  repeat(3)tick();@(negedge clk);rst_n=1;
  // A legacy fill pulse gets priority and keeps the queued fill pending.
  offer(1,368'(160'h11223344_aabbccdd_01020304_55667788));legacy_fill_start=1;#1;
  assert(!dispatch_ready&&!fill_start)else $fatal(1,"legacy fill priority");
  tick();@(negedge clk);legacy_fill_start=0;#1;
  assert(dispatch_ready&&fill_start&&fill_command==128'h11223344_aabbccdd_01020304_55667788)else $fatal(1,"fill dispatch");
  finish_offer();assert(!completion_valid)else $fatal(1,"premature fill completion");
  @(negedge clk);fill_done=1;fill_error=0;#1;assert(completion_valid&&!completion_error)else $fatal(1,"fill completion");
  tick();@(negedge clk);fill_done=0;
  offer(2,368'(160'hffeeddcc_bbaa9988_77665544_33221100_12345678));#1;
  assert(dispatch_ready&&blit_start&&blit_command==160'hffeeddcc_bbaa9988_77665544_33221100_12345678)else $fatal(1,"blit dispatch");
  finish_offer();@(negedge clk);blit_done=1;blit_error=1;#1;
  assert(completion_valid&&completion_error)else $fatal(1,"blit error completion");
  tick();@(negedge clk);blit_done=0;blit_error=0;
  offer(3,368'(272'hfedcba98_01234567_89abcdef_11223344_55667788_99aabbcc_ddeeff00_1234));#1;
  assert(dispatch_ready&&triangle_start&&triangle_command==
   368'(272'hfedcba98_01234567_89abcdef_11223344_55667788_99aabbcc_ddeeff00_1234))
   else $fatal(1,"triangle dispatch");
  finish_offer();assert(!completion_valid)else $fatal(1,"premature triangle completion");
  @(negedge clk);triangle_done=1;triangle_error=0;#1;
  assert(completion_valid&&!completion_error)else $fatal(1,"triangle completion");
  tick();@(negedge clk);triangle_done=0;
  // Invalid work is consumed and retired with an error without engine starts.
  offer(0,0);#1;assert(dispatch_ready&&!fill_start&&!blit_start&&!triangle_start)else $fatal(1,"invalid dispatch");
  finish_offer();#1;assert(completion_valid&&completion_error)else $fatal(1,"invalid completion");
  tick();
  $display("Render queue dispatcher: legacy priority, fill/blit/triangle routing and invalid opcode error PASS");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
