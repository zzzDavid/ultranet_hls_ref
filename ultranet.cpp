// #define DEBUG

#ifndef __SYNTHESIS__
#include <iostream>
#include <fstream>
using namespace std;

#endif

#include <stdint.h>
#include <ap_int.h>
#include <hls_video.h>
#include "ultranet.h"
#include "stream_tools.h"
#include "function.h"
#include "sliding_window_unit.h"
#include "matrix_vector_unit.h"
#include "config.h"
#include "param.h"
#include "conv2d.h"
#include "pool2d.h"
#include "bn_qrelu2d.h"

void stream_to_mat (hls::stream<ap_uint<24> >&in,
		 hls::Mat<IN_IMAGE_HEIGHT, IN_IMAGE_WIDTH, HLS_8UC3> & raw_img) {

	for (int i=0; i<IN_IMAGE_HEIGHT; i++) {
        #pragma HLS loop_tripcount min=120 max=360
		for (int j=0; j<IN_IMAGE_WIDTH; j++) {
            #pragma HLS loop_tripcount min=320 max=640
            #pragma HLS pipeline II = 1
            hls::Scalar<3, ap_uint<8> > pix;
            ap_uint<24> in_data = in.read();
            for (unsigned int p=0; p < 3; p ++) {

                pix.val[p] = in_data(8*p+7, 8*p);
            }
			raw_img << pix;
		}
	}

}

void mat_to_stream (hls::Mat<RESIZE_IMAGE_HEIGHT, RESIZE_IMAGE_WIDTH, HLS_8UC3> & resize_img,
                    hls::stream<ap_uint<24> > & out ) {

	for (int i=0; i<RESIZE_IMAGE_HEIGHT; i++) {
        #pragma HLS loop_tripcount min=120 max=360
		for (int j=0; j<RESIZE_IMAGE_WIDTH; j++) {
            #pragma HLS loop_tripcount min=320 max=640
            #pragma HLS pipeline II = 1
            hls::Scalar<3, ap_uint<8> > pix;
            resize_img >> pix;
            ap_uint<24> out_data;
            for (unsigned int p=0; p < 3; p ++) {
                out_data(8*p+7, 8*p) = pix.val[p];
            }
            out.write(out_data);
		}
	}

}

void resize(hls::stream<ap_uint<24> > &in, hls::stream<ap_uint<24> > & out) {
    #pragma HLS dataflow
    hls::Mat<IN_IMAGE_HEIGHT, IN_IMAGE_WIDTH, HLS_8UC3> raw_img;
    #pragma HLS STREAM variable=raw_img depth=128 dim=1
    hls::Mat<RESIZE_IMAGE_HEIGHT, RESIZE_IMAGE_WIDTH, HLS_8UC3> resize_img;
    #pragma HLS STREAM variable=resize_img depth=128 dim=1
    stream_to_mat(in, raw_img);
    // hls::Resize(raw_img, resize_img, HLS_INTER_LINEAR);
    hls::Resize_opr_linear(raw_img, resize_img);
    mat_to_stream(resize_img, out);
}

void resize_batch(hls::stream<ap_uint<24> > &in, hls::stream<ap_uint<24> > & out, unsigned int reps) {
    for (unsigned int rep=0; rep < reps; rep ++) {
        #pragma HLS loop_tripcount min=1 max=1
        resize(in, out);
    }
}

// function used to dump per-layer results
#ifndef __SYNTHESIS__
template<unsigned H, unsigned W, unsigned C, unsigned B>
void dump_results(
    hls::stream<ap_uint<C * B> > &result_in_stream,
    hls::stream<ap_uint<C * B> > &result_continue,
    ap_uint<B> result_dump[H][W][C]
) {
    for (size_t i = 0; i < H * W; i++) {
        #pragma HLS loop_tripcount min=180*320 max=360*640
        ap_uint<C * B> in = result_in_stream.read();
        result_continue.write(in);
        for (size_t ch = 0; ch < C; ch++) {
            ap_uint<B> val = in(B * (ch+1) - 1, B*ch);
            size_t h = i / W;
            size_t w = i % W;
            result_dump[h][w][ch] = val;
        }
    }
}
#endif

void do_compute(
#ifndef __SYNTHESIS__
    // ap_uint<CONV_0_IN_BIT> before_resize[IN_IMAGE_HEIGHT][IN_IMAGE_WIDTH][3],
    // ap_uint<CONV_0_IN_BIT> after_resize[RESIZE_IMAGE_HEIGHT][RESIZE_IMAGE_WIDTH][3],
    // ap_uint<CONV_0_OUT_BIT> conv0_out[CONV_0_OFM_ROW][CONV_0_OFM_COL][CONV_0_OFM_CH],
    // ap_uint<CONV_0_OUT_BIT> pool0_out[CONV_1_IFM_ROW][CONV_1_IFM_COL][CONV_1_IFM_CH],
    // ap_uint<CONV_1_OUT_BIT> conv1_out[CONV_1_OFM_ROW][CONV_1_OFM_COL][CONV_1_OFM_CH],
    // ap_uint<CONV_1_OUT_BIT> pool1_out[CONV_2_IFM_ROW][CONV_2_IFM_COL][CONV_2_IFM_CH],
    // ap_uint<CONV_2_OUT_BIT> conv2_out[CONV_2_OFM_ROW][CONV_2_OFM_COL][CONV_2_OFM_CH],
    // ap_uint<CONV_2_OUT_BIT> pool2_out[CONV_3_IFM_ROW][CONV_3_IFM_COL][CONV_3_IFM_CH],
    // ap_uint<CONV_3_OUT_BIT> conv3_out[CONV_3_OFM_ROW][CONV_3_OFM_COL][CONV_3_OFM_CH],
    // ap_uint<CONV_3_OUT_BIT> pool3_out[CONV_4_IFM_ROW][CONV_4_IFM_COL][CONV_4_IFM_CH],
    // ap_uint<CONV_4_OUT_BIT> conv4_out[CONV_4_OFM_ROW][CONV_4_OFM_COL][CONV_4_OFM_CH],
    // ap_uint<CONV_5_OUT_BIT> conv5_out[CONV_5_OFM_ROW][CONV_5_OFM_COL][CONV_5_OFM_CH],
    // ap_uint<CONV_6_OUT_BIT> conv6_out[CONV_6_OFM_ROW][CONV_6_OFM_COL][CONV_6_OFM_CH],
    // ap_uint<CONV_7_OUT_BIT> conv7_out[CONV_7_OFM_ROW][CONV_7_OFM_COL][CONV_7_OFM_CH],
#endif
    stream<my_ap_axis >& in,
    stream<my_ap_axis >& out,
    const unsigned int reps
) {
    #pragma HLS DATAFLOW

    // hls::stream<ap_uint<128> > in_stream_extract("in_stream_extract");
    // #pragma HLS STREAM variable=in_stream_extract depth=16 dim=1
    // hls::stream<ap_uint<128 * 3> > in_stream0("in_stream0");
    // #pragma HLS STREAM variable=in_stream0 depth=16 dim=1
    hls::stream<ap_uint<64> > in_stream_extract("in_stream_extract");
    #pragma HLS STREAM variable=in_stream_extract depth=16 dim=1
    hls::stream<ap_uint<64 * 3> > in_stream0("in_stream0");
    #pragma HLS STREAM variable=in_stream0 depth=16 dim=1

	hls::stream<ap_uint<CONV_0_IN_BIT * CONV_0_IFM_CH> > in_stream1("in_stream1");
    #pragma HLS STREAM variable=in_stream1 depth=16 dim=1
    hls::stream<ap_uint<CONV_0_IN_BIT * CONV_0_IFM_CH> > in_stream2("in_stream2");
    #pragma HLS STREAM variable=in_stream2 depth=16 dim=1
    hls::stream<ap_uint<CONV_0_OUT_BIT * CONV_0_OFM_CH> > conv_0_out("conv_0_out");
    #pragma HLS STREAM variable=conv_0_out depth=128 dim=1
    hls::stream<ap_uint<CONV_0_OUT_BIT * CONV_0_OFM_CH> > pool_0_out("pool_0_out");
    #pragma HLS STREAM variable=pool_0_out depth=128 dim=1
    hls::stream<ap_uint<CONV_1_OUT_BIT * CONV_1_OFM_CH> > conv_1_out("conv_1_out");
    #pragma HLS STREAM variable=conv_1_out depth=128 dim=1
    hls::stream<ap_uint<CONV_1_OUT_BIT * CONV_1_OFM_CH> > pool_1_out("pool_1_out");
    #pragma HLS STREAM variable=pool_1_out depth=128 dim=1
    hls::stream<ap_uint<CONV_2_OUT_BIT * CONV_2_OFM_CH> > conv_2_out("conv_2_out");
    #pragma HLS STREAM variable=conv_2_out depth=128 dim=1
    hls::stream<ap_uint<CONV_2_OUT_BIT * CONV_2_OFM_CH> > pool_2_out("pool_2_out");
    #pragma HLS STREAM variable=pool_2_out depth=128 dim=1
    hls::stream<ap_uint<CONV_3_OUT_BIT * CONV_3_OFM_CH> > conv_3_out("conv_3_out");
    #pragma HLS STREAM variable=conv_3_out depth=128 dim=1
    hls::stream<ap_uint<CONV_3_OUT_BIT * CONV_3_OFM_CH> > pool_3_out("pool_3_out");
    #pragma HLS STREAM variable=pool_3_out depth=128 dim=1
    hls::stream<ap_uint<CONV_4_OUT_BIT * CONV_4_OFM_CH> > conv_4_out("conv_4_out");
    #pragma HLS STREAM variable=conv_4_out depth=128 dim=1
    hls::stream<ap_uint<CONV_5_OUT_BIT * CONV_5_OFM_CH> > conv_5_out("conv_5_out");
    #pragma HLS STREAM variable=conv_5_out depth=128 dim=1
    hls::stream<ap_uint<CONV_6_OUT_BIT * CONV_6_OFM_CH> > conv_6_out("conv_6_out");
    #pragma HLS STREAM variable=conv_6_out depth=128 dim=1
    hls::stream<ap_uint<CONV_7_OUT_BIT * CONV_7_OFM_CH> > conv_7_out("conv_7_out");
    #pragma HLS STREAM variable=conv_7_out depth=128 dim=1
    hls::stream<ap_uint<CONV_8_IN_BIT * CONV_8_SIMD> > conv_8_in("conv_8_in");
    #pragma HLS STREAM variable=conv_8_in depth=64 dim=1
    hls::stream<ap_uint<32 * CONV_8_PE> > conv_8_out("conv_8_out");
    #pragma HLS STREAM variable=conv_8_out depth=64 dim=1

// #ifndef __SYNTHESIS__
//     hls::stream<ap_uint<CONV_0_IN_BIT * CONV_0_IFM_CH> > in_stream1_dup("in_stream1_dup");
//     hls::stream<ap_uint<CONV_0_IN_BIT * CONV_0_IFM_CH> > in_stream2_dup("in_stream2_dup");
//     hls::stream<ap_uint<CONV_0_OUT_BIT * CONV_0_OFM_CH> > conv_0_out_dup("conv_0_out_dup");
//     hls::stream<ap_uint<CONV_0_OUT_BIT * CONV_0_OFM_CH> > pool_0_out_dup("pool_0_out_dup");
//     hls::stream<ap_uint<CONV_1_OUT_BIT * CONV_1_OFM_CH> > conv_1_out_dup("conv_1_out_dup");
//     hls::stream<ap_uint<CONV_1_OUT_BIT * CONV_1_OFM_CH> > pool_1_out_dup("pool_1_out_dup");
//     hls::stream<ap_uint<CONV_2_OUT_BIT * CONV_2_OFM_CH> > conv_2_out_dup("conv_2_out_dup");
//     hls::stream<ap_uint<CONV_2_OUT_BIT * CONV_2_OFM_CH> > pool_2_out_dup("pool_2_out_dup");
//     hls::stream<ap_uint<CONV_3_OUT_BIT * CONV_3_OFM_CH> > conv_3_out_dup("conv_3_out_dup");
//     hls::stream<ap_uint<CONV_3_OUT_BIT * CONV_3_OFM_CH> > pool_3_out_dup("pool_3_out_dup");
//     hls::stream<ap_uint<CONV_4_OUT_BIT * CONV_4_OFM_CH> > conv_4_out_dup("conv_4_out_dup");
//     hls::stream<ap_uint<CONV_5_OUT_BIT * CONV_5_OFM_CH> > conv_5_out_dup("conv_5_out_dup");
//     hls::stream<ap_uint<CONV_6_OUT_BIT * CONV_6_OFM_CH> > conv_6_out_dup("conv_6_out_dup");
//     hls::stream<ap_uint<CONV_7_OUT_BIT * CONV_7_OFM_CH> > conv_7_out_dup("conv_7_out_dup");
// #endif

    // const unsigned int num_per_rep = IN_IMAGE_HEIGHT * IN_IMAGE_WIDTH * 3 * 4 / 64;
    // ExtractPixels<128, num_per_rep> (in, in_stream_extract, reps);
    // StreamingDataWidthConverter_Batch<128, 128 * 3, num_per_rep>(in_stream_extract, in_stream0, reps);
	// StreamingDataWidthConverter_Batch<128 * 3, CONV_0_IN_BIT * CONV_0_IFM_CH, num_per_rep / 3> (in_stream0, in_stream1, reps);

    const unsigned int num_per_rep = IN_IMAGE_HEIGHT * IN_IMAGE_WIDTH * 3 * 8 / 64;
    ExtractPixels<64, num_per_rep> (in, in_stream_extract, reps);
    StreamingDataWidthConverter_Batch<64, 64 * 3, num_per_rep>(in_stream_extract, in_stream0, reps);
	StreamingDataWidthConverter_Batch<64 * 3, CONV_0_IN_BIT * CONV_0_IFM_CH, num_per_rep / 3> (in_stream0, in_stream1, reps);

#ifndef __SYNTHESIS__
    cout << "in_stream1 size " << in_stream1.size() << endl;
    // dump_results<IN_IMAGE_HEIGHT, IN_IMAGE_WIDTH, 3, CONV_0_IN_BIT>(in_stream1, in_stream1_dup, before_resize);
#endif

    resize_batch(
#ifndef __SYNTHESIS__
        in_stream1_dup,
#else
        in_stream1,
#endif
        in_stream2, reps);

#ifndef __SYNTHESIS__
    cout << "in_stream2 size " << in_stream2.size() << endl;
    // dump_results<RESIZE_IMAGE_HEIGHT, RESIZE_IMAGE_WIDTH, 3, CONV_0_IN_BIT>(in_stream2, in_stream2_dup, after_resize);
#endif

#ifndef __SYNTHESIS__
    // ofstream ofs("xcel_conv0_optrace.log");
#endif

    // conv3x3_bn_act_optrace<
    conv3x3_bn_act<
        CONV_0_IFM_ROW,
        CONV_0_IFM_COL,
        CONV_0_IFM_CH,
        CONV_0_IN_BIT,

        CONV_0_OFM_CH,
        CONV_0_OUT_BIT,

        CONV_0_W_BIT,
        32,
        CONV_0_INC_BIT,
        CONV_0_BIAS_BIT,

        CONV_0_SIMD,
        CONV_0_PE,
        CONV_0_L_SHIFT
    >(
        in_stream2,
        conv_0_w,
        conv_0_inc,
        conv_0_bias,
        conv_0_out,
        reps
        // ofs
    );
#ifndef __SYNTHESIS__
    // ofs.close();
    cout << "conv_0_out size " << conv_0_out.size() << endl;
    // dump_results<CONV_0_OFM_ROW, CONV_0_OFM_COL, CONV_0_OFM_CH, CONV_0_OUT_BIT>(conv_0_out, conv_0_out_dup, conv0_out);
#endif

    max_pool2d<
        2,
        CONV_0_OFM_ROW,
        CONV_0_OFM_COL,
        CONV_0_OFM_CH,
        CONV_0_OUT_BIT
    >(
        conv_0_out,
        pool_0_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "pool_0_out size " << pool_0_out.size() << endl;
    // dump_results<CONV_1_IFM_ROW, CONV_1_IFM_COL, CONV_1_IFM_CH, CONV_0_OUT_BIT>(pool_0_out, pool_0_out_dup, pool0_out);
#endif


    conv3x3_bn_act<
        CONV_1_IFM_ROW,
        CONV_1_IFM_COL,
        CONV_1_IFM_CH,
        CONV_1_IN_BIT,

        CONV_1_OFM_CH,
        CONV_1_OUT_BIT,

        CONV_1_W_BIT,
        32,
        CONV_1_INC_BIT,
        CONV_1_BIAS_BIT,

        CONV_1_SIMD,
        CONV_1_PE,
        CONV_1_L_SHIFT
    >(
        pool_0_out,
        conv_1_w,
        conv_1_inc,
        conv_1_bias,
        conv_1_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "conv_1_out size " << conv_1_out.size() << endl;
    // dump_results<CONV_1_OFM_ROW, CONV_1_OFM_COL, CONV_1_OFM_CH, CONV_1_OUT_BIT>(conv_1_out, conv_1_out_dup, conv1_out);
#endif

    max_pool2d<
        2,
        CONV_1_OFM_ROW,
        CONV_1_OFM_COL,
        CONV_1_OFM_CH,
        CONV_1_OUT_BIT
    >(
        conv_1_out,
        pool_1_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "pool_1_out size " << pool_1_out.size() << endl;
    // dump_results<CONV_2_IFM_ROW, CONV_2_IFM_COL, CONV_2_IFM_CH, CONV_1_OUT_BIT>(pool_1_out, pool_1_out_dup, pool1_out);
#endif

    conv3x3_bn_act<
        CONV_2_IFM_ROW,
        CONV_2_IFM_COL,
        CONV_2_IFM_CH,
        CONV_2_IN_BIT,

        CONV_2_OFM_CH,
        CONV_2_OUT_BIT,

        CONV_2_W_BIT,
        32,
        CONV_2_INC_BIT,
        CONV_2_BIAS_BIT,

        CONV_2_SIMD,
        CONV_2_PE,
        CONV_2_L_SHIFT
    >(
        pool_1_out,
        conv_2_w,
        conv_2_inc,
        conv_2_bias,
        conv_2_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "conv_2_out size " << conv_2_out.size() << endl;
    // dump_results<CONV_2_OFM_ROW, CONV_2_OFM_COL, CONV_2_OFM_CH, CONV_2_OUT_BIT>(conv_2_out, conv_2_out_dup, conv2_out);
#endif

    max_pool2d<
        2,
        CONV_2_OFM_ROW,
        CONV_2_OFM_COL,
        CONV_2_OFM_CH,
        CONV_2_OUT_BIT
    >(
        conv_2_out,
        pool_2_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "pool_2_out size " << pool_2_out.size() << endl;
    // dump_results<CONV_3_IFM_ROW, CONV_3_IFM_COL, CONV_3_IFM_CH, CONV_2_OUT_BIT>(pool_2_out, pool_2_out_dup, pool2_out);
#endif

    conv3x3_bn_act<
        CONV_3_IFM_ROW,
        CONV_3_IFM_COL,
        CONV_3_IFM_CH,
        CONV_3_IN_BIT,

        CONV_3_OFM_CH,
        CONV_3_OUT_BIT,

        CONV_3_W_BIT,
        32,
        CONV_3_INC_BIT,
        CONV_3_BIAS_BIT,

        CONV_3_SIMD,
        CONV_3_PE,
        CONV_3_L_SHIFT
    >(
        pool_2_out,
        conv_3_w,
        conv_3_inc,
        conv_3_bias,
        conv_3_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "conv_3_out size " << conv_3_out.size() << endl;
    // dump_results<CONV_3_OFM_ROW, CONV_3_OFM_COL, CONV_3_OFM_CH, CONV_3_OUT_BIT>(conv_3_out, conv_3_out_dup, conv3_out);
#endif

    max_pool2d<
        2,
        CONV_3_OFM_ROW,
        CONV_3_OFM_COL,
        CONV_3_OFM_CH,
        CONV_3_OUT_BIT
    >(
        conv_3_out,
        pool_3_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "pool_3_out size " << pool_3_out.size() << endl;
    // dump_results<CONV_4_IFM_ROW, CONV_4_IFM_COL, CONV_4_IFM_CH, CONV_3_OUT_BIT>(pool_3_out, pool_3_out_dup, pool3_out);
#endif


    conv3x3_bn_act<
        CONV_4_IFM_ROW,
        CONV_4_IFM_COL,
        CONV_4_IFM_CH,
        CONV_4_IN_BIT,

        CONV_4_OFM_CH,
        CONV_4_OUT_BIT,

        CONV_4_W_BIT,
        32,
        CONV_4_INC_BIT,
        CONV_4_BIAS_BIT,

        CONV_4_SIMD,
        CONV_4_PE,
        CONV_4_L_SHIFT
    >(
        pool_3_out,
        conv_4_w,
        conv_4_inc,
        conv_4_bias,
        conv_4_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "conv_4_out size " << conv_4_out.size() << endl;
    // dump_results<CONV_4_OFM_ROW, CONV_4_OFM_COL, CONV_4_OFM_CH, CONV_4_OUT_BIT>(conv_4_out, conv_4_out_dup, conv4_out);
#endif

    conv3x3_bn_act<
        CONV_5_IFM_ROW,
        CONV_5_IFM_COL,
        CONV_5_IFM_CH,
        CONV_5_IN_BIT,

        CONV_5_OFM_CH,
        CONV_5_OUT_BIT,

        CONV_5_W_BIT,
        32,
        CONV_5_INC_BIT,
        CONV_5_BIAS_BIT,

        CONV_5_SIMD,
        CONV_5_PE,
        CONV_5_L_SHIFT
    >(
        conv_4_out,
        conv_5_w,
        conv_5_inc,
        conv_5_bias,
        conv_5_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "conv_5_out size " << conv_5_out.size() << endl;
    // dump_results<CONV_5_OFM_ROW, CONV_5_OFM_COL, CONV_5_OFM_CH, CONV_5_OUT_BIT>(conv_5_out, conv_5_out_dup, conv5_out);
#endif

    conv3x3_bn_act<
        CONV_6_IFM_ROW,
        CONV_6_IFM_COL,
        CONV_6_IFM_CH,
        CONV_6_IN_BIT,

        CONV_6_OFM_CH,
        CONV_6_OUT_BIT,

        CONV_6_W_BIT,
        32,
        CONV_6_INC_BIT,
        CONV_6_BIAS_BIT,

        CONV_6_SIMD,
        CONV_6_PE,
        CONV_6_L_SHIFT
    >(
        conv_5_out,
        conv_6_w,
        conv_6_inc,
        conv_6_bias,
        conv_6_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "conv_6_out size " << conv_6_out.size() << endl;
    // dump_results<CONV_6_OFM_ROW, CONV_6_OFM_COL, CONV_6_OFM_CH, CONV_6_OUT_BIT>(conv_6_out, conv_6_out_dup, conv6_out);
#endif


    conv3x3_bn_act<
        CONV_7_IFM_ROW,
        CONV_7_IFM_COL,
        CONV_7_IFM_CH,
        CONV_7_IN_BIT,

        CONV_7_OFM_CH,
        CONV_7_OUT_BIT,

        CONV_7_W_BIT,
        32,
        CONV_7_INC_BIT,
        CONV_7_BIAS_BIT,

        CONV_7_SIMD,
        CONV_7_PE,
        CONV_7_L_SHIFT
    >(
        conv_6_out,
        conv_7_w,
        conv_7_inc,
        conv_7_bias,
        conv_7_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "conv_7_out size " << conv_7_out.size() << endl;
    // dump_results<CONV_7_OFM_ROW, CONV_7_OFM_COL, CONV_7_OFM_CH, CONV_7_OUT_BIT>(conv_7_out, conv_7_out_dup, conv7_out);
#endif


    StreamingDataWidthConverter_Batch<
        CONV_7_OFM_CH * CONV_7_OUT_BIT,
        CONV_8_IN_BIT * CONV_8_SIMD,
        CONV_7_OFM_ROW * CONV_7_OFM_COL
    >(
        conv_7_out,
        conv_8_in,
        reps
    );

    conv1x1 <
        CONV_8_IFM_ROW,
        CONV_8_IFM_COL,
        CONV_8_IFM_CH,
        CONV_8_IN_BIT,
        CONV_8_OFM_CH,

        CONV_8_W_BIT,
        32,

        CONV_8_SIMD,
        CONV_8_PE
    >(
        conv_8_in,
        conv_8_w,
        conv_8_out,
        reps
    );
#ifndef __SYNTHESIS__
    cout << "conv_8_out size " << conv_8_out.size() << endl;
#endif

    AddLast<CONV_8_OFM_ROW*CONV_8_OFM_COL*CONV_8_OFM_CH/2>(
        conv_8_out,
        out, reps
    );

}

#ifdef __SYNTHESIS__
void ultranet(stream<my_ap_axis >& in, stream<my_ap_axis >& out, const unsigned int reps) {

#pragma HLS INTERFACE axis register both port=out
#pragma HLS INTERFACE axis register both port=in
#pragma HLS INTERFACE s_axilite port=reps bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

#pragma HLS ARRAY_PARTITION variable = conv_0_w complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_0_inc complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_0_bias complete dim = 1

#pragma HLS ARRAY_PARTITION variable = conv_1_w complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_1_inc complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_1_bias complete dim = 1

#pragma HLS ARRAY_PARTITION variable = conv_2_w complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_2_inc complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_2_bias complete dim = 1

#pragma HLS ARRAY_PARTITION variable = conv_3_w complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_3_inc complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_3_bias complete dim = 1

#pragma HLS ARRAY_PARTITION variable = conv_4_w complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_4_inc complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_4_bias complete dim = 1

#pragma HLS ARRAY_PARTITION variable = conv_5_w complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_5_inc complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_5_bias complete dim = 1

#pragma HLS ARRAY_PARTITION variable = conv_6_w complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_6_inc complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_6_bias complete dim = 1

#pragma HLS ARRAY_PARTITION variable = conv_7_w complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_7_inc complete dim = 1
#pragma HLS ARRAY_PARTITION variable = conv_7_bias complete dim = 1

#pragma HLS ARRAY_PARTITION variable = conv_8_w complete dim = 1

    do_compute(in, out, reps);
}
#endif
