`timescale 1ns/1ps

module encoder_tb;

reg [15:0] v;
wire [15:0] out;

encoder dut (.v(v), .out(out));

integer i;

initial begin
    v = 16'b0;
    #10;

    for (i = 0; i < 16; i = i + 1) begin
        v = (1 << (i+1)) - 1;
        #10;
    end

    $finish;
end

initial begin
    $dumpfile("encoder_tb.vcd");
    $dumpvars(0, encoder_tb);
end

endmodule
