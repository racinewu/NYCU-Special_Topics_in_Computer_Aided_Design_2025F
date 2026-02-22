//Verilog HDL for "ADC", "encoder" "functional"
module encoder #(parameter WIDTH = 16) (v, out);

input      [WIDTH - 1:0] v;
output reg [WIDTH - 1:0] out;

integer i;
reg found;

always @(*) begin
    out = {WIDTH{1'b0}};
    found = 1'b0;

    for (i = WIDTH - 1; i >= 0; i = i - 1) begin
        if (v[i] && !found) begin
            out[i] = 1'b1;
            found = 1'b1;
        end
    end

    if (v == {WIDTH{1'b0}})
        out[0] = 1'b0;
end

endmodule
