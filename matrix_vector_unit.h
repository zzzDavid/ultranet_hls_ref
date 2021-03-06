#pragma once

#include <ap_int.h>
#include <hls_stream.h>
using namespace hls;
#include "function.h"

#ifndef __SYNTHESIS__
#include <iostream>
#include <iomanip>
#include <fstream>

// functions used for op tracing in csim
template<
    unsigned W_BIT,
	unsigned IN_BIT,
	unsigned M_BIT,
	unsigned SIMD>
ap_int<M_BIT> simd_mul_optrace(
	ap_uint<SIMD*W_BIT> weights,
	ap_uint<SIMD*IN_BIT> in,
    unsigned rep, unsigned PEid,
    std::ofstream &ofs
){
	ap_int<M_BIT> accumulation = 0;
	for (unsigned p = 0; p < SIMD; p++) {
		#pragma HLS loop_tripcount min=8 max=8
		ap_int<W_BIT> temp_w = weights( (p+1)*W_BIT-1, p*W_BIT );
		ap_uint<IN_BIT> temp_in = in( (p+1)*IN_BIT-1, p*IN_BIT );
		ap_int<W_BIT + IN_BIT> result = temp_w * temp_in;
        ofs << rep << ", " << PEid << ", " << p << ": ";
        ofs << accumulation << " += " << result << " = ";
        ofs << temp_w << " * " << temp_in << "\n";
		accumulation += result;
	}
	return accumulation;
}

template <
    unsigned MAT_ROW,		// 展开后的k × k × in_ch
    unsigned MAT_COL,		// 展开后的out_ch

    unsigned IN_BIT,
    unsigned OUT_BIT,		//

    unsigned W_BIT,
    unsigned M_BIT,			// 乘累加后的计算结果的值

    unsigned INC_BIT,		// 激活等差数列 的步长
    unsigned BIAS_BIT,		//

    unsigned SIMD,
    unsigned PE,
    unsigned L_SHIFT,
    unsigned VECT_NUMS>
void matrix_vector_act_unit_optrace(
	stream<ap_uint<SIMD*IN_BIT> >& vec,
	const ap_uint<SIMD*W_BIT> weights[PE][(MAT_ROW/SIMD)*(MAT_COL/PE)],
	const ap_int<INC_BIT> inc[PE][MAT_COL/PE],
	const ap_int<BIAS_BIT> bias[PE][MAT_COL/PE],
	stream<ap_uint<PE*OUT_BIT> >& out,
	const unsigned reps,
    std::ofstream &ofs
){

	const unsigned INPUT_FOLD = MAT_ROW/SIMD;
	const unsigned OUTPUT_FOLD = MAT_COL/PE;

	const unsigned total_reps = INPUT_FOLD * OUTPUT_FOLD * VECT_NUMS * reps;


	// 需要保存一行数据
	ap_uint<SIMD*IN_BIT> row_store[INPUT_FOLD];
#pragma HLS RESOURCE variable=row_store core=RAM_2P_BRAM

	// 用来保存累加结果
	unsigned in_fold_cnt = 0;			// 输入折叠计数
	unsigned out_fold_cnt = 0;			// 输出折叠计数
	unsigned tile = 0;

	// 一次 读入的数据 需要保存 in_ch * k * k长度的数据
	ap_uint<SIMD*IN_BIT> temp_vec;
	// 累加结果 这里需要初始化为0
	ap_int<M_BIT> acc[PE];

    ofs << "Op Trace --\n";
    ofs << "input data point, PEid <--> Och, MAC simd <--> Ich: operation\n";
	for (unsigned rep = 0; rep < total_reps; rep++) {
		#pragma HLS loop_tripcount min=180*320 max=360*640
		#pragma HLS PIPELINE II=1

		// 这里是在第一次输出之前 就度完了数据，之后一直用
		// 在输出折叠第一次计算时读
		if (out_fold_cnt == 0) {
			temp_vec = vec.read();
			row_store[in_fold_cnt] = temp_vec;
		}
		else {
			temp_vec = row_store[in_fold_cnt];
		}

		// 初始化累加结果
		if(in_fold_cnt == 0) {
			for(unsigned p=0; p < PE; p ++) {
				#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
				acc[p] = 0;
			}
		}

		// 主要计算单元 这里用UNROLL展开 期望用单周期实现计算
		// PE 并行计算
		for (unsigned p = 0; p < PE; p++) {
			#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
			// 读 W 子块
			ap_uint<SIMD*W_BIT> temp_mat = weights[p][tile];
			// SIMD 并行
			acc[p] += simd_mul_optrace<W_BIT, IN_BIT, M_BIT, SIMD>(temp_mat, temp_vec, rep, p, ofs);
		}

		// 计数逻辑 和输出处理
		tile ++;
		if(++ in_fold_cnt == INPUT_FOLD) {
			in_fold_cnt = 0;
			ap_uint<PE*M_BIT> out_buf;
			// PE 列计算完成 可以输出
			for(unsigned p=0; p < PE; p ++) {
				#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
				out_buf((p+1)*OUT_BIT-1, p*OUT_BIT) = bn_qurelu<M_BIT, OUT_BIT, INC_BIT, BIAS_BIT, IN_BIT, W_BIT, L_SHIFT>(acc[p], inc[p][out_fold_cnt], bias[p][out_fold_cnt]);
			}
			out.write(out_buf);
			// 完整的一次矩阵向量计算
			if(++ out_fold_cnt == OUTPUT_FOLD) {
				out_fold_cnt = 0;
				tile = 0;
			}

		}
	}  // end for

}


#endif

/**
 *  simd 乘
 * 使用 逻辑查找表
 * SIMD multiplier
 * use LUT
 */
template <	unsigned W_BIT,
			unsigned IN_BIT,
			unsigned M_BIT,
			unsigned SIMD>
ap_int<M_BIT> simd_mul_lut(
	ap_uint<SIMD*W_BIT> weights,
	ap_uint<SIMD*IN_BIT> in)
{
	ap_int<M_BIT> accumulation = 0;

	for (unsigned p = 0; p < SIMD; p++) {
		#pragma HLS loop_tripcount min=8 max=8
#pragma HLS UNROLL
		ap_int<W_BIT> temp_w = weights( (p+1)*W_BIT-1, p*W_BIT );
		ap_uint<IN_BIT> temp_in = in( (p+1)*IN_BIT-1, p*IN_BIT );
		ap_int<W_BIT + IN_BIT> result = temp_w * temp_in;
#pragma HLS RESOURCE variable=result core=Mul_LUT
		accumulation += result;
	}
	return accumulation;
}

/**
 *  simd 乘
 *  由 编译器自动选择使用 dsp 或者 lut
 *  SIMD multiplier
 *  let the complier automatically choose between DSP or LUT
 */
template <	unsigned W_BIT,
			unsigned IN_BIT,
			unsigned M_BIT,
			unsigned SIMD>
ap_int<M_BIT> simd_mul(
	ap_uint<SIMD*W_BIT> weights,
	ap_uint<SIMD*IN_BIT> in)
{
	ap_int<M_BIT> accumulation = 0;

	for (unsigned p = 0; p < SIMD; p++) {
		#pragma HLS loop_tripcount min=8 max=8
#pragma HLS UNROLL
		ap_int<W_BIT> temp_w = weights( (p+1)*W_BIT-1, p*W_BIT );
		ap_uint<IN_BIT> temp_in = in( (p+1)*IN_BIT-1, p*IN_BIT );
		ap_int<W_BIT + IN_BIT> result = temp_w * temp_in;
// #pragma HLS RESOURCE variable=result core=Mul_LUT
		accumulation += result;
	}
	return accumulation;
}

/**
 * 矩阵向量计算单元
 * matrix-vector calculation unit
 *
 */
template <	unsigned MAT_ROW,		// 展开后的k × k × in_ch   | flattened k x k x in_ch
			unsigned MAT_COL,		// 展开后的out_ch          | flattened out_ch
			unsigned IN_BIT,
			unsigned W_BIT,
			unsigned M_BIT,			// 乘累加后的计算结果的值   |  the bitwidth of the MAC result
			unsigned SIMD,
			unsigned PE,
			unsigned VECT_NUMS>
void matrix_vector_unit(
	stream<ap_uint<SIMD*IN_BIT> >& vec,
	const ap_uint<SIMD*W_BIT> weights[PE][(MAT_ROW/SIMD)*(MAT_COL/PE)],
	stream<ap_uint<PE*M_BIT> >& out,
	const unsigned reps = 1)
{

	const unsigned INPUT_FOLD = MAT_ROW/SIMD;
	const unsigned OUTPUT_FOLD = MAT_COL/PE;

	const unsigned total_reps = INPUT_FOLD * OUTPUT_FOLD * VECT_NUMS * reps;
	// const unsigned total_reps = 18;
	// 需要保存一行数据
    // need to buffer one row
	ap_uint<SIMD*IN_BIT> row_store[INPUT_FOLD];
#pragma HLS RESOURCE variable=row_store core=RAM_2P_BRAM

	// 用来保存累加结果
// 	ap_uint<M_BIT> result_vec[PE];
// #pragma HLS ARRAY_PARTITION variable=result_vec complete dim=0
	unsigned in_fold_cnt = 0;			// 输入折叠计数
	unsigned out_fold_cnt = 0;			// 输出折叠计数
	unsigned tile = 0;

	// 一次 读入的数据 需要保存 in_ch * k * k长度的数据
	ap_uint<SIMD*IN_BIT> temp_vec;
	// 累加结果 这里需要初始化为0

	// TODO
	ap_int<M_BIT> acc[PE];

	for (unsigned rep = 0; rep < total_reps; rep++) {
		#pragma HLS loop_tripcount min=180*320 max=360*640
#pragma HLS PIPELINE II=1

		// 这里是在第一次输出之前 就度完了数据，之后一直用
		// 在输出折叠第一次计算时读
		if (out_fold_cnt == 0) {
			temp_vec = vec.read();
			row_store[in_fold_cnt] = temp_vec;
		}
		else {
			temp_vec = row_store[in_fold_cnt];
		}

		// 初始化累加结果
		if(in_fold_cnt == 0) {
			for(unsigned p=0; p < PE; p ++) {
				#pragma HLS loop_tripcount min=1 max=1
#pragma HLS UNROLL
				acc[p] = 0;
			}
		}

		// 主要计算单元 这里用UNROLL展开 期望用单周期实现计算
		// PE 并行计算
		for (unsigned p = 0; p < PE; p++) {
			#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
			// 读 W 子块
			ap_uint<SIMD*W_BIT> temp_mat = weights[p][tile];
			// SIMD 并行
			acc[p] += simd_mul<W_BIT, IN_BIT, M_BIT, SIMD>( temp_mat, temp_vec );
		}

		// 计数逻辑 和输出处理
		tile ++;
		if(++ in_fold_cnt == INPUT_FOLD) {
			in_fold_cnt = 0;
			ap_uint<PE*M_BIT> out_buf;
			// PE 列计算完成 可以输出
			for(unsigned p=0; p < PE; p ++) {
				#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
				out_buf((p+1)*M_BIT-1, p*M_BIT) = acc[p];
				// acc[p] = 0;
			}
			out.write(out_buf);
			// 完整的一次矩阵向量计算
			if(++ out_fold_cnt == OUTPUT_FOLD) {
				out_fold_cnt = 0;
				tile = 0;
			}

		}
	}  // end for

}

/**
 * 矩阵向量计算单元
 * 同时进行量化激活处理
 */
template <	unsigned MAT_ROW,		// 展开后的k × k × in_ch
			unsigned MAT_COL,		// 展开后的out_ch

			unsigned IN_BIT,
			unsigned OUT_BIT,		//

			unsigned W_BIT,
			unsigned M_BIT,			// 乘累加后的计算结果的值

			unsigned INC_BIT,		// 激活等差数列 的步长
			unsigned BIAS_BIT,		//

			unsigned SIMD,
			unsigned PE,
			unsigned L_SHIFT,
			unsigned VECT_NUMS>
void matrix_vector_act_unit(
	stream<ap_uint<SIMD*IN_BIT> >& vec,
	const ap_uint<SIMD*W_BIT> weights[PE][(MAT_ROW/SIMD)*(MAT_COL/PE)],
	const ap_int<INC_BIT> inc[PE][MAT_COL/PE],
	const ap_int<BIAS_BIT> bias[PE][MAT_COL/PE],
	stream<ap_uint<PE*OUT_BIT> >& out,
	const unsigned reps = 1)
{

	const unsigned INPUT_FOLD = MAT_ROW/SIMD;
	const unsigned OUTPUT_FOLD = MAT_COL/PE;

	const unsigned total_reps = INPUT_FOLD * OUTPUT_FOLD * VECT_NUMS * reps;


	// 需要保存一行数据
	ap_uint<SIMD*IN_BIT> row_store[INPUT_FOLD];
#pragma HLS RESOURCE variable=row_store core=RAM_2P_BRAM

	// 用来保存累加结果
	unsigned in_fold_cnt = 0;			// 输入折叠计数
	unsigned out_fold_cnt = 0;			// 输出折叠计数
	unsigned tile = 0;

	// 一次 读入的数据 需要保存 in_ch * k * k长度的数据
	ap_uint<SIMD*IN_BIT> temp_vec;
	// 累加结果 这里需要初始化为0
	ap_int<M_BIT> acc[PE];

	for (unsigned rep = 0; rep < total_reps; rep++) {
		#pragma HLS loop_tripcount min=180*320 max=360*640
#pragma HLS PIPELINE II=1

		// 这里是在第一次输出之前 就度完了数据，之后一直用
		// 在输出折叠第一次计算时读
		if (out_fold_cnt == 0) {
			temp_vec = vec.read();
			row_store[in_fold_cnt] = temp_vec;
		}
		else {
			temp_vec = row_store[in_fold_cnt];
		}

		// 初始化累加结果
		if(in_fold_cnt == 0) {
			for(unsigned p=0; p < PE; p ++) {
#pragma HLS UNROLL
#pragma HLS loop_tripcount min=1 max=2
				acc[p] = 0;
			}
		}

		// 主要计算单元 这里用UNROLL展开 期望用单周期实现计算
		// PE 并行计算
		for (unsigned p = 0; p < PE; p++) {
			#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
			// 读 W 子块
			ap_uint<SIMD*W_BIT> temp_mat = weights[p][tile];
			// SIMD 并行
			acc[p] += simd_mul<W_BIT, IN_BIT, M_BIT, SIMD>( temp_mat, temp_vec );
		}

		// 计数逻辑 和输出处理
		tile ++;
		if(++ in_fold_cnt == INPUT_FOLD) {
			in_fold_cnt = 0;
			ap_uint<PE*M_BIT> out_buf;
			// PE 列计算完成 可以输出
			for(unsigned p=0; p < PE; p ++) {
				#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
				out_buf((p+1)*OUT_BIT-1, p*OUT_BIT) = bn_qurelu<M_BIT, OUT_BIT, INC_BIT, BIAS_BIT, IN_BIT, W_BIT, L_SHIFT>(acc[p], inc[p][out_fold_cnt], bias[p][out_fold_cnt]);
			}
			out.write(out_buf);
			// 完整的一次矩阵向量计算
			if(++ out_fold_cnt == OUTPUT_FOLD) {
				out_fold_cnt = 0;
				tile = 0;
			}

		}
	}  // end for

}

/**
 * 矩阵向量计算单元
 * 使用 lut 计算
 *
 */
template <	unsigned MAT_ROW,		// 展开后的k × k × in_ch
			unsigned MAT_COL,		// 展开后的out_ch
			unsigned IN_BIT,
			unsigned W_BIT,
			unsigned M_BIT,			// 乘累加后的计算结果的值
			unsigned SIMD,
			unsigned PE,
			unsigned VECT_NUMS>
void matrix_vector_unit_lut(
	stream<ap_uint<SIMD*IN_BIT> >& vec,
	const ap_uint<SIMD*W_BIT> weights[PE][(MAT_ROW/SIMD)*(MAT_COL/PE)],
	stream<ap_uint<PE*M_BIT> >& out,
	const unsigned reps = 1)
{

	const unsigned INPUT_FOLD = MAT_ROW/SIMD;
	const unsigned OUTPUT_FOLD = MAT_COL/PE;

	const unsigned total_reps = INPUT_FOLD * OUTPUT_FOLD * VECT_NUMS * reps;
	// const unsigned total_reps = 18;
	// 需要保存一行数据
	ap_uint<SIMD*IN_BIT> row_store[INPUT_FOLD];
#pragma HLS RESOURCE variable=row_store core=RAM_2P_BRAM

	// 用来保存累加结果
	unsigned in_fold_cnt = 0;			// 输入折叠计数
	unsigned out_fold_cnt = 0;			// 输出折叠计数
	unsigned tile = 0;

	// 一次 读入的数据 需要保存 in_ch * k * k长度的数据
	ap_uint<SIMD*IN_BIT> temp_vec;
	// 累加结果 这里需要初始化为0

	ap_int<M_BIT> acc[PE];


	for (unsigned rep = 0; rep < total_reps; rep++) {
		#pragma HLS loop_tripcount min=180*320 max=360*640
#pragma HLS PIPELINE II=1

		// 这里是在第一次输出之前 就度完了数据，之后一直用
		// 在输出折叠第一次计算时读
		if (out_fold_cnt == 0) {
			temp_vec = vec.read();
			row_store[in_fold_cnt] = temp_vec;
		}
		else {
			temp_vec = row_store[in_fold_cnt];
		}


		// 初始化累加结果
		if(in_fold_cnt == 0) {
			for(unsigned p=0; p < PE; p ++) {
				#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
				acc[p] = 0;
			}
		}

		// 主要计算单元 这里用UNROLL展开 期望用单周期实现计算
		// PE 并行计算
		for (unsigned p = 0; p < PE; p++) {
			#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
			// 读 W 子块
			ap_uint<SIMD*W_BIT> temp_mat = weights[p][tile];
			// SIMD 并行
			acc[p] += simd_mul_lut<W_BIT, IN_BIT, M_BIT, SIMD>( temp_mat, temp_vec );
		}

		// 计数逻辑 和输出处理
		tile ++;
		if(++ in_fold_cnt == INPUT_FOLD) {
			in_fold_cnt = 0;
			ap_uint<PE*M_BIT> out_buf;
			// PE 列计算完成 可以输出
			for(unsigned p=0; p < PE; p ++) {
				#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
				out_buf((p+1)*M_BIT-1, p*M_BIT) = acc[p];
				// acc[p] = 0;
			}
			out.write(out_buf);
			// 完整的一次矩阵向量计算
			if(++ out_fold_cnt == OUTPUT_FOLD) {
				out_fold_cnt = 0;
				tile = 0;
			}

		}
	}  // end for

}

/**
 * 矩阵向量计算单元
 * 同时进行量化激活处理
 * 使用 lut 计算
 */
template <	unsigned MAT_ROW,		// 展开后的k × k × in_ch
			unsigned MAT_COL,		// 展开后的out_ch

			unsigned IN_BIT,
			unsigned OUT_BIT,		//

			unsigned W_BIT,
			unsigned M_BIT,			// 乘累加后的计算结果的值

			unsigned INC_BIT,		// 激活等差数列 的步长
			unsigned BIAS_BIT,		//

			unsigned SIMD,
			unsigned PE,
			unsigned L_SHIFT,
			unsigned VECT_NUMS>
void matrix_vector_act_unit_lut(
	stream<ap_uint<SIMD*IN_BIT> >& vec,
	const ap_uint<SIMD*W_BIT> weights[PE][(MAT_ROW/SIMD)*(MAT_COL/PE)],
	const ap_uint<INC_BIT> inc[PE][MAT_COL/PE],
	const ap_int<BIAS_BIT> bias[PE][MAT_COL/PE],
	stream<ap_uint<PE*OUT_BIT> >& out,
	const unsigned reps = 1)
{

	const unsigned INPUT_FOLD = MAT_ROW/SIMD;
	const unsigned OUTPUT_FOLD = MAT_COL/PE;

	const unsigned total_reps = INPUT_FOLD * OUTPUT_FOLD * VECT_NUMS * reps;


	// 需要保存一行数据
	ap_uint<SIMD*IN_BIT> row_store[INPUT_FOLD];
#pragma HLS RESOURCE variable=row_store core=RAM_2P_BRAM

	// 用来保存累加结果
	unsigned in_fold_cnt = 0;			// 输入折叠计数
	unsigned out_fold_cnt = 0;			// 输出折叠计数
	unsigned tile = 0;

	// 一次 读入的数据 需要保存 in_ch * k * k长度的数据
	ap_uint<SIMD*IN_BIT> temp_vec;
	// 累加结果 这里需要初始化为0
	ap_int<M_BIT> acc[PE];

	for (unsigned rep = 0; rep < total_reps; rep++) {
		#pragma HLS loop_tripcount min=180*320 max=360*640
#pragma HLS PIPELINE II=1

		// 这里是在第一次输出之前 就度完了数据，之后一直用
		// 在输出折叠第一次计算时读
		if (out_fold_cnt == 0) {
			temp_vec = vec.read();
			row_store[in_fold_cnt] = temp_vec;
		}
		else {
			temp_vec = row_store[in_fold_cnt];
		}

		// 初始化累加结果
		if(in_fold_cnt == 0) {
			for(unsigned p=0; p < PE; p ++) {
				#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
				acc[p] = 0;
			}
		}

		// 主要计算单元 这里用UNROLL展开 期望用单周期实现计算
		// PE 并行计算
		for (unsigned p = 0; p < PE; p++) {
			#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
			// 读 W 子块
			ap_uint<SIMD*W_BIT> temp_mat = weights[p][tile];
			// SIMD 并行
			acc[p] += simd_mul_lut<W_BIT, IN_BIT, M_BIT, SIMD>( temp_mat, temp_vec );
		}

		// 计数逻辑 和输出处理
		tile ++;
		if(++ in_fold_cnt == INPUT_FOLD) {
			in_fold_cnt = 0;
			ap_uint<PE*M_BIT> out_buf;
			// PE 列计算完成 可以输出
			for(unsigned p=0; p < PE; p ++) {
				#pragma HLS loop_tripcount min=1 max=2
#pragma HLS UNROLL
				out_buf((p+1)*OUT_BIT-1, p*OUT_BIT) = bn_qurelu<M_BIT, OUT_BIT, INC_BIT, BIAS_BIT, IN_BIT, W_BIT, L_SHIFT>(acc[p], inc[p][out_fold_cnt], bias[p][out_fold_cnt]);
			}
			out.write(out_buf);
			// 完整的一次矩阵向量计算
			if(++ out_fold_cnt == OUTPUT_FOLD) {
				out_fold_cnt = 0;
				tile = 0;
			}

		}
	}  // end for

}

