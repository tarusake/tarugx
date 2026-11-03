// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
`timescale 1ns/1ps
module pcie_dma_read_engine_tb;
 logic clk=0,rst_n=0;always #5 clk=~clk;
 logic cmd_valid=0,cmd_ready;logic [63:0] cmd_host_address=0;logic [31:0] cmd_vram_address=0,cmd_length_bytes=0;
 logic busy,done,error,mrd_valid,mrd_ready=0;logic [3:0] error_code;logic [63:0] mrd_host_address;logic [6:0] mrd_length_dw;logic [7:0] mrd_tag;
 logic [7:0] active_valid;logic [63:0] active_tags;
 logic cpl_valid=0,cpl_ready,cpl_has_data=0,cpl_error=0;logic [3:0] cpl_length_dw=0;logic [6:0] cpl_lower_address=0;logic [7:0] cpl_tag=0;
 logic cpl_end=1;
 logic [255:0] cpl_data=0;logic fb_req_valid,fb_req_ready=0;logic [31:0] fb_req_addr;logic [255:0] fb_req_wdata;
 logic [31:0] fb_req_wstrb;logic fb_rsp_valid=0,fb_rsp_ready,fb_rsp_error=0;
 logic fb_window_full;
 logic slot_full_sample,completion_busy_sample;
 tarugx_pcie_dma_read_engine #(.TIMEOUT_CYCLES(100)) dut(.*);
 logic [31:0] vram[0:1023];int request_count=0,write_count=0;
 task tick;@(posedge clk);#1;endtask
 task command(input logic [63:0] host,input int vram_addr,input int bytes);
  @(negedge clk);assert(cmd_ready)else $fatal(1,"command ready");
  cmd_host_address=host;cmd_vram_address=vram_addr;cmd_length_bytes=bytes;cmd_valid=1;tick();
  @(negedge clk);cmd_valid=0;
 endtask
 task completion(input int count,input int base,input logic [7:0] tag,input logic [6:0] lower,input bit failed=0);
  @(negedge clk);cpl_data=0;
  for(int i=0;i<count;i++)cpl_data[i*32+:32]=32'hd0000000+base+i;
  cpl_length_dw=4'(count);cpl_lower_address=lower;cpl_tag=tag;cpl_has_data=1;cpl_error=failed;cpl_valid=1;
  #1;
  while(!cpl_ready)tick();tick();@(negedge clk);cpl_valid=0;cpl_error=0;
 endtask
 task automatic service_writes(input int count,input int expected_base,input bit fail_last=0);
  int i=0;int transaction=0;int words;
  while(i<count)begin
   while(!fb_req_valid)tick();
   repeat(transaction%2)begin tick();assert(fb_req_valid)else $fatal(1,"write request dropped");end
   words = $countones(fb_req_wstrb)/4;
   assert(words>=1&&words<=8
          &&fb_req_wstrb==(32'hffff_ffff>>((8-words)*4))
          &&fb_req_wdata[31:0]==32'hd0000000+expected_base+i)else $fatal(1,"write shape");
   for(int word=0;word<words;word++)begin
    assert(fb_req_wdata[word*32+:32]==32'hd0000000+expected_base+i+word)else $fatal(1,"burst write data");
    vram[(fb_req_addr>>2)+word]=fb_req_wdata[word*32+:32];
   end
   @(negedge clk);fb_req_ready=1;tick();@(negedge clk);fb_req_ready=0;
   @(negedge clk);fb_rsp_error=fail_last&&(i+words>=count);fb_rsp_valid=1;tick();
   @(negedge clk);fb_rsp_valid=0;fb_rsp_error=0;write_count+=words;i+=words;transaction++;
  end
 endtask
 task automatic service_writes_deferred(input int count,input int expected_base);
  int i=0;int transactions=0;int words;
  while(i<count)begin
   while(!fb_req_valid)tick();
   words = $countones(fb_req_wstrb)/4;
   assert(fb_req_wdata[31:0]==32'hd0000000+expected_base+i)else $fatal(1,"deferred write data");
   @(negedge clk);fb_req_ready=1;tick();@(negedge clk);fb_req_ready=0;
   i+=words;transactions++;
  end
  assert(transactions==2&&dut.fb_outstanding==4'(transactions))else $fatal(1,"writes were not pipelined");
  repeat(transactions)begin
   @(negedge clk);fb_rsp_valid=1;tick();@(negedge clk);fb_rsp_valid=0;
  end
 endtask
 int expected_lengths[0:1]='{1,11};logic [7:0] issued_tags[0:1];logic [6:0] issued_lowers[0:1];int base=0;
 initial begin
  repeat(3)tick();@(negedge clk);rst_n=1;
  command(64'h10000000_00000ffc,4,48);
  for(int req=0;req<2;req++)begin
   while(!mrd_valid)tick();
   assert(mrd_length_dw==7'(expected_lengths[req])&&mrd_host_address==64'h10000000_00000ffc+base*4)else $fatal(1,"4 KiB request split %0d",req);
   issued_tags[req]=mrd_tag;issued_lowers[req]=mrd_host_address[6:0];
   repeat(req%2)begin tick();assert(mrd_valid)else $fatal(1,"request dropped");end
   @(negedge clk);mrd_ready=1;tick();@(negedge clk);mrd_ready=0;request_count++;
   base+=expected_lengths[req];
  end
   assert(active_valid==8'b00000011&&active_tags[7:0]==issued_tags[0]&&active_tags[15:8]==issued_tags[1])
   else $fatal(1,"two active tags");
  completion(8,1,issued_tags[1],issued_lowers[1]);service_writes(8,1);
  completion(3,9,issued_tags[1],issued_lowers[1]+7'd32);service_writes(3,9);
  completion(1,0,issued_tags[0],issued_lowers[0]);service_writes(1,0);
  wait(done);tick();
  if(error||busy||request_count!=2||write_count!=12)
   $display("DMA completion state error=%0d code=%0d busy=%0d requests=%0d words=%0d slots=%02x received1=%0d chunk=%0d left=%0d flush=%0d fb=%0d remaining=%0d request=%0d completion=%0d",
    error,error_code,busy,request_count,write_count,dut.slot_valid,dut.slot_received[1],
    dut.chunk_pending,dut.chunk_left,dut.chunk_flush,dut.fb_outstanding,
    dut.issue_remaining_dw,dut.request_pending,dut.completion_pending);
  assert(!error&&!busy&&request_count==2&&write_count==12)else $fatal(1,"DMA completion");
  for(int i=0;i<12;i++)assert(vram[i+1]==32'hd0000000+i)else $fatal(1,"VRAM word %0d",i);
  // Eight aligned 256-byte requests must be in flight before any Completion.
  begin logic [7:0] tags[0:7];
   command(0,0,2048);
   for(int req=0;req<8;req++)begin
    while(!mrd_valid)tick();
    assert(mrd_length_dw==64&&mrd_host_address==64'(req*256))else $fatal(1,"eight outstanding request");
    tags[req]=mrd_tag;
    @(negedge clk);mrd_ready=1;tick();@(negedge clk);mrd_ready=0;
   end
   assert(active_valid==8'hff)else $fatal(1,"eight active tags");
   for(int req=7;req>=0;req--)begin
    for(int chunk=0;chunk<8;chunk++)begin
     completion(8,req*64+chunk*8,tags[req],7'(req*256+chunk*32));
     service_writes(8,req*64+chunk*8);
    end
   end
   wait(done);tick();assert(!error&&!busy)else $fatal(1,"eight outstanding completion");
  end
  command(0,4,32);while(!mrd_valid)tick();
  begin logic [7:0] tag;logic [6:0] lower;tag=mrd_tag;lower=mrd_host_address[6:0];
   @(negedge clk);mrd_ready=1;tick();@(negedge clk);mrd_ready=0;
   completion(8,32,tag,lower);service_writes_deferred(8,32);wait(done);
   assert(!error&&!busy)else $fatal(1,"deferred write completion");tick();end
  command(3,0,4);wait(done);assert(error&&error_code==1)else $fatal(1,"unaligned host");tick();
  command(0,'hfffffc,8);wait(done);assert(error&&error_code==1)else $fatal(1,"VRAM bounds");tick();
  command(0,0,6);wait(done);assert(error&&error_code==1)else $fatal(1,"length alignment");tick();
  command(0,0,4);while(!mrd_valid)tick();
  begin logic [7:0] tag;logic [6:0] lower;tag=mrd_tag;lower=mrd_host_address[6:0];
   @(negedge clk);mrd_ready=1;tick();@(negedge clk);mrd_ready=0;
   completion(1,0,tag,lower,1);wait(done);assert(error&&error_code==3)else $fatal(1,"completion error");tick();end
  command(0,0,4);while(!mrd_valid)tick();@(negedge clk);mrd_ready=1;tick();@(negedge clk);mrd_ready=0;
  wait(done);assert(error&&!busy&&error_code==8)else $fatal(1,"completion timeout");
  $display("PCIe DMA read engine: eight 256-byte requests, 4 KiB/chunk splits, eight outstanding VRAM writes, errors and timeout PASS");$finish;
 end
 initial begin #1000000;$fatal(1,"test timeout");end
endmodule
