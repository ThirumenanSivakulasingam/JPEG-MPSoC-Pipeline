module jpeg_dct_ci (
    input  wire        clk,
    input  wire        clk_en,
    input  wire        reset,
    input  wire        reset_req,
    input  wire        start,
    input  wire [7:0]  n,
    input  wire [31:0] dataa,
    input  wire [31:0] datab,
    output reg  [31:0] result,
    output reg         done
);
    localparam signed [15:0] C1 = 16'sd1420;
    localparam signed [15:0] C2 = 16'sd1338;
    localparam signed [15:0] C3 = 16'sd1204;
    localparam signed [15:0] C5 = 16'sd805;
    localparam signed [15:0] C6 = 16'sd554;
    localparam signed [15:0] C7 = 16'sd283;

    reg signed [15:0] x [0:7];
    reg signed [15:0] y [0:7];
    reg        pending_complete;
    reg [31:0] next_result;

    integer i;

    function [31:0] signext16;
        input signed [15:0] value;
        begin
            signext16 = {{16{value[15]}}, value};
        end
    endfunction

    task compute_row;
        integer x0, x1, x2, x3, x4, x5, x6, x7, x8;
        begin
            x8 = $signed(x[0]) + $signed(x[7]);
            x0 = $signed(x[0]) - $signed(x[7]);
            x7 = $signed(x[1]) + $signed(x[6]);
            x1 = $signed(x[1]) - $signed(x[6]);
            x6 = $signed(x[2]) + $signed(x[5]);
            x2 = $signed(x[2]) - $signed(x[5]);
            x5 = $signed(x[3]) + $signed(x[4]);
            x3 = $signed(x[3]) - $signed(x[4]);

            x4 = x8 + x5;
            x8 = x8 - x5;
            x5 = x7 + x6;
            x7 = x7 - x6;

            y[0] = x4 + x5;
            y[4] = x4 - x5;
            y[2] = (x8 * C2 + x7 * C6) >>> 10;
            y[6] = (x8 * C6 - x7 * C2) >>> 10;
            y[7] = (x0 * C7 - x1 * C5 + x2 * C3 - x3 * C1) >>> 10;
            y[5] = (x0 * C5 - x1 * C1 + x2 * C7 + x3 * C3) >>> 10;
            y[3] = (x0 * C3 - x1 * C7 - x2 * C1 - x3 * C5) >>> 10;
            y[1] = (x0 * C1 + x1 * C3 + x2 * C5 + x3 * C7) >>> 10;
        end
    endtask

    task compute_col;
        integer x0, x1, x2, x3, x4, x5, x6, x7, x8;
        begin
            x8 = $signed(x[0]) + $signed(x[7]);
            x0 = $signed(x[0]) - $signed(x[7]);
            x7 = $signed(x[1]) + $signed(x[6]);
            x1 = $signed(x[1]) - $signed(x[6]);
            x6 = $signed(x[2]) + $signed(x[5]);
            x2 = $signed(x[2]) - $signed(x[5]);
            x5 = $signed(x[3]) + $signed(x[4]);
            x3 = $signed(x[3]) - $signed(x[4]);

            x4 = x8 + x5;
            x8 = x8 - x5;
            x5 = x7 + x6;
            x7 = x7 - x6;

            y[0] = (x4 + x5) >>> 3;
            y[4] = (x4 - x5) >>> 3;
            y[2] = (x8 * C2 + x7 * C6) >>> 13;
            y[6] = (x8 * C6 - x7 * C2) >>> 13;
            y[7] = (x0 * C7 - x1 * C5 + x2 * C3 - x3 * C1) >>> 13;
            y[5] = (x0 * C5 - x1 * C1 + x2 * C7 + x3 * C3) >>> 13;
            y[3] = (x0 * C3 - x1 * C7 - x2 * C1 - x3 * C5) >>> 13;
            y[1] = (x0 * C1 + x1 * C3 + x2 * C5 + x3 * C7) >>> 13;
        end
    endtask

    always @(posedge clk or posedge reset) begin
        if (reset) begin
            done <= 1'b0;
            result <= 32'd0;
            next_result <= 32'd0;
            pending_complete <= 1'b0;
            for (i = 0; i < 8; i = i + 1) begin
                x[i] <= 16'sd0;
                y[i] <= 16'sd0;
            end
        end else begin
            done <= 1'b0;

            if (clk_en) begin
                if (pending_complete) begin
                    result <= next_result;
                    done <= 1'b1;
                    pending_complete <= 1'b0;
                end else if (start) begin
                    next_result <= 32'd0;

                    case (n)
                        8'd0: begin
                            x[0] <= dataa[15:0];
                            x[1] <= dataa[31:16];
                        end
                        8'd1: begin
                            x[2] <= dataa[15:0];
                            x[3] <= dataa[31:16];
                        end
                        8'd2: begin
                            x[4] <= dataa[15:0];
                            x[5] <= dataa[31:16];
                        end
                        8'd3: begin
                            x[6] <= dataa[15:0];
                            x[7] <= dataa[31:16];
                        end
                        8'd4: begin
                            compute_row();
                        end
                        8'd5: begin
                            compute_col();
                        end
                        8'd8: begin
                            next_result <= signext16(y[0]);
                        end
                        8'd9: begin
                            next_result <= signext16(y[1]);
                        end
                        8'd10: begin
                            next_result <= signext16(y[2]);
                        end
                        8'd11: begin
                            next_result <= signext16(y[3]);
                        end
                        8'd12: begin
                            next_result <= signext16(y[4]);
                        end
                        8'd13: begin
                            next_result <= signext16(y[5]);
                        end
                        8'd14: begin
                            next_result <= signext16(y[6]);
                        end
                        8'd15: begin
                            next_result <= signext16(y[7]);
                        end
                        default: begin
                            next_result <= 32'd0;
                        end
                    endcase

                    pending_complete <= 1'b1;
                end
            end
        end
    end
    wire _unused_reset_req = reset_req;
    wire _unused_datab = datab[0];
endmodule
