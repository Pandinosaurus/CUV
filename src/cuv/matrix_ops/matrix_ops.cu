//*LB*
// Copyright (c) 2010, University of Bonn, Institute for Computer Science VI
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
//  * Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//  * Neither the name of the University of Bonn 
//    nor the names of its contributors may be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//*LE*


#include <stdexcept>
#include <cublas.h>
#include <cblas.h>
#include <stdio.h>
#include <float.h>
#include <limits>

#include <thrust/functional.h>

#include <cuv/tools/cuv_general.hpp>
#include <3rd_party/CudaConv/nvmatrix.cuh>
#include <cuv/matrix_ops/matrix_ops.hpp>
#include <cuv/tensor_ops/functors.hpp>

#ifdef __CDT_PARSER__
#define __global__
#define __shared__
#endif

#define PITCH(PTR,PITCH,Y,X) ((typeof(PTR))((PTR) + (PITCH)*(Y)) + (X))
#define CVT_TRANSPOSE(c) \
	(CBLAS_TRANSPOSE)(((c) == 'N' || (c) == 'n') ? CblasNoTrans : \
	 ((c) == 'T' || (c) == 't') ? CblasTrans : \
	 ((c) == 'C' || (c) == 'c') ? CblasConjTrans : \
	 -1)
	/* (mg)the idea is to place the blocks under each other starting at the upper left in the matrix. their threads
	 * add up multiples of their x position (indicated by ty - see above) in shared memory. then we have a 2-dim
	 * array in shared memory that corresponds in size to the block.
	 *
	 * in each block we now have to add up the elements of each row in the shared memory to get the final value. this is done
	 * by logarithmically adding up the elements:
	 * in the first step the second half (in x direction) of the blocks add their values to the first halfs memory locations in
	 * shared memory - then this is repeated for the first half of the threads. a short example for blockDim.y=8 and blockDim.x=1:
	 *
	 * 1st iter(offset=4): a1 a2 a3 a4 | b1 b2 b3 b4
	 *
	 * 2nd iter(offset=2): (a1+b1) (a2+b2) | (a3+b3) (a4+b4)
	 *
	 * 3rd iter(offset=1): ( (a1+b1) + (a3+b3)  ) | ( (a2+b2) +  (a4+b4) )
	 *
	 * 4th iter(offset=0): ( (a1+b1) + (a3+b3)  ) + ( (a2+b2) +  (a4+b4) )
	 *
	 * tx indicates the y-dimension in the matrix; ty indicates the x-dimension in the matrix
	 */

// "coalesced transpose" with no bank conflicts, example from SDK
// potential speedup by 5 possible for "fine-grained transpose"
template<int BLOCK_SIZE, class T, class I>
__global__
void transpose_kernel(T* dst, const T* src, I width, I height,
		I dpitch, I spitch) {
	const I bx = blockIdx.x * blockDim.x;
	const I by = blockIdx.y * blockDim.y;
	I tx = bx + threadIdx.x;
	I ty = by + threadIdx.y;

	__shared__
	T shared[BLOCK_SIZE][BLOCK_SIZE + 1];

	if (tx < width && ty < height) {
		shared[threadIdx.y][threadIdx.x] = *PITCH(src,spitch,ty,tx);
	}
	__syncthreads();

	tx = by + threadIdx.x;
	ty = bx + threadIdx.y;

	if (tx < height && ty < width) {
		*PITCH(dst,dpitch,ty,tx)
		/*dst[(bx + threadIdx.y) * height + by + threadIdx.x]*/
				= shared[threadIdx.x][threadIdx.y];
	}
}
template<int BLOCK_SIZE, class T, class I>
__global__ void transposeNoBankConflicts(T *dst, const T *src,
	       	I width, I height,
		I dpitch, I spitch)
{
	const I BLOCK_ROWS = BLOCK_SIZE;
	__shared__ float tile[BLOCK_SIZE][BLOCK_SIZE+1];

	int xIndex = blockIdx.x * BLOCK_SIZE + threadIdx.x;
	int yIndex = blockIdx.y * BLOCK_SIZE + threadIdx.y;  
	if(xIndex > width) return;

	const T* srcp = PITCH(src,spitch,yIndex,xIndex);

	for (int i=0; i<BLOCK_SIZE; i+=BLOCK_ROWS) {
		tile[threadIdx.y+i][threadIdx.x] = 
			yIndex+i<height ?  *PITCH(srcp,spitch,i,0):0;
	}

	xIndex = blockIdx.y * BLOCK_SIZE + threadIdx.x;
	yIndex = blockIdx.x * BLOCK_SIZE + threadIdx.y;

	T*       dstp = PITCH(dst,dpitch,yIndex,xIndex);
	__syncthreads();

	for (int i=0; i<BLOCK_SIZE; i+=BLOCK_ROWS) {
		if(yIndex+i<width)
			*PITCH(dstp,dpitch,i,0) = tile[threadIdx.x][threadIdx.y+i];
	}
}
namespace cuv {
template<class __value_type, class __memory_space_type, class __index_type>
tensor<__value_type , __memory_space_type,column_major>*blockview(
		tensor<__value_type,__memory_space_type,column_major>& matrix,
				__index_type start_rows,
				__index_type num_rows,
				__index_type start_cols,
				__index_type num_cols,
				column_major
				) {
                        cuvAssert(matrix.ndim()==2);
			cuvAssert(start_rows==0);
			cuvAssert(num_rows==matrix.shape(0))
			return new tensor<__value_type,__memory_space_type,column_major>(indices[index_range(0,num_rows)][index_range(0,num_cols)], matrix.ptr()+matrix.shape(0)*start_cols);
		}

template<class __value_type, class __memory_space_type, class __index_type>
tensor<__value_type,__memory_space_type,row_major>* blockview(
		tensor<__value_type,__memory_space_type,row_major>& matrix,
		__index_type start_rows,
		__index_type num_rows,
		__index_type start_cols,
		__index_type num_cols,
		row_major
) {
        cuvAssert(matrix.ndim()==2);
	cuvAssert(start_cols==0);
	cuvAssert(num_cols==matrix.shape(1))
	return new tensor<__value_type,__memory_space_type,row_major>(indices[index_range(0,num_rows)][index_range(0,num_cols)],matrix.ptr()+matrix.shape(1)*start_rows);
}
template<class __value_type, class __memory_space_type, class __memory_layout, class __index_type>
tensor<__value_type,__memory_space_type,__memory_layout>* blockview(
		tensor<__value_type,__memory_space_type,__memory_layout> & matrix,
		__index_type start_rows,
		__index_type num_rows ,
		__index_type start_cols,
		__index_type num_cols) {
	return blockview(matrix,start_rows,num_rows,start_cols,num_cols, __memory_layout());
}



/// column major blas3
template<>
void prod(tensor<float,dev_memory_space,column_major>& dst,
		const tensor<float,dev_memory_space,column_major>& A,
		const tensor<float,dev_memory_space,column_major>& B,
		char transA,
		char transB,
		const float& factAB,
		const float& factC) {
        cuvAssert(dst.ndim()==2);
        cuvAssert(A.ndim()==2);
        cuvAssert(B.ndim()==2);
	int m = (transA=='t' ? A.shape(1) : A.shape(0));
	int k1 = (transA=='t' ? A.shape(0) : A.shape(1));
	int k2 = (transB=='t' ? B.shape(1) : B.shape(0));
	int n = (transB=='t' ? B.shape(0) : B.shape(1));

	cuvAssert(dst.shape(0) == m);
	cuvAssert(dst.shape(1) == n);
	cuvAssert(k1 == k2);
	cuvAssert(A.ptr());
	cuvAssert(B.ptr());
	cuvAssert(dst.ptr());

	cublasSgemm(transA, transB, m, n, k1, factAB, A.ptr(), A.shape(0),B.ptr(), B.shape(0), factC, dst.ptr(), dst.shape(0));
	cuvAssert( cublasGetError() == CUBLAS_STATUS_SUCCESS );
	cuvSafeCall(cudaThreadSynchronize());
}

template<>
void prod(tensor<float,host_memory_space,column_major>& dst,
		const tensor<float,host_memory_space,column_major>& A,
		const tensor<float,host_memory_space,column_major>& B,
		char transA,
		char transB,
		const float& factAB,
		const float& factC) {
        cuvAssert(dst.ndim()==2);
        cuvAssert(A.ndim()==2);
        cuvAssert(B.ndim()==2);

	int m = (transA=='t' ? A.shape(1) : A.shape(0));
	int k1 = (transA=='t' ? A.shape(0) : A.shape(1));
	int k2 = (transB=='t' ? B.shape(1) : B.shape(0));
	int n = (transB=='t' ? B.shape(0) : B.shape(1));

	cuvAssert(dst.shape(0) == m);
	cuvAssert(dst.shape(1) == n);
	cuvAssert(k1 == k2);
	cuvAssert(A.ptr() != NULL);
	cuvAssert(B.ptr() != NULL);
	cuvAssert(dst.ptr());

#if 1 /* CBLAS */
	cblas_sgemm(
			CblasColMajor,
			CVT_TRANSPOSE(transA),
			CVT_TRANSPOSE(transB), m, n, k1,
			factAB, A.ptr(), A.shape(0),B.ptr(), B.shape(0), factC, dst.ptr(), dst.shape(0));
#else /* naive */
	for(int i=0; i<A.shape(0);i++)
	for(int j=0; j<B.shape(1); j++) {
		float f=0;
		for(int k=0;k<A.shape(1);k++) {
			f += A(i,k)*B(k,j);
		}
		dst.set(i,j,f);
	}
#endif
}
/// row major blas3
template<>
void prod(tensor<float,dev_memory_space,row_major>& dst,
		const tensor<float,dev_memory_space,row_major>& A,
		const tensor<float,dev_memory_space,row_major>& B,
		char transA,
		char transB,
		const float& factAB,
		const float& factC) {
        cuvAssert(dst.ndim()==2);
        cuvAssert(A.ndim()==2);
        cuvAssert(B.ndim()==2);
	// we use column major prod and just exchange width and height
	int m = (transB=='t' ? B.shape(0) : B.shape(1));
	int k1 = (transB=='t' ? B.shape(1) : B.shape(0));
	int k2 = (transA=='t' ? A.shape(0) : A.shape(1));
	int n = (transA=='t' ? A.shape(1) : A.shape(0));

	cuvAssert(dst.shape(0) == n);
	cuvAssert(dst.shape(1) == m);
	cuvAssert(k1 == k2);
	cuvAssert(A.ptr());
	cuvAssert(B.ptr());
	cuvAssert(dst.ptr());
	cublasSgemm(transB, transA, m, n, k1, factAB, B.ptr(), B.shape(1),A.ptr(), A.shape(1), factC, dst.ptr(), dst.shape(1));

	cuvAssert( cublasGetError() == CUBLAS_STATUS_SUCCESS );
	cuvSafeCall(cudaThreadSynchronize());
}

template<>
void prod(tensor<float,host_memory_space,row_major>& dst,
		const tensor<float,host_memory_space,row_major>& A,
		const tensor<float,host_memory_space,row_major>& B,
		char transA,
		char transB,
		const float& factAB,
		const float& factC) {
        cuvAssert(dst.ndim()==2);
        cuvAssert(A.ndim()==2);
        cuvAssert(B.ndim()==2);
	int m = (transA=='t' ? A.shape(1) : A.shape(0));
	int k1 = (transA=='t' ? A.shape(0) : A.shape(1));
	int k2 = (transB=='t' ? B.shape(1) : B.shape(0));
	int n = (transB=='t' ? B.shape(0) : B.shape(1));

	cuvAssert(dst.shape(0) == m);
	cuvAssert(dst.shape(1) == n);
	cuvAssert(k1 == k2);
	cuvAssert(A.ptr() != NULL);
	cuvAssert(B.ptr() != NULL);
	cuvAssert(dst.ptr());

	cblas_sgemm(
			CblasRowMajor,
			CVT_TRANSPOSE(transA),
			CVT_TRANSPOSE(transB), m, n, k1,
			factAB, A.ptr(), A.shape(1),B.ptr(), B.shape(1), factC, dst.ptr(), dst.shape(1));
}

template<bool UseFactNew, bool UseFactOld, class V, class I, class V2, class OP>
__global__
void matrix_plus_vector_kernel_column_major(V* Dst, const V*Src, const V2* v,I w,I h, OP op, float factNew, float factOld) {
	int tid = blockDim.x*blockIdx.x + threadIdx.x;
	if(tid>h) return;
	V2 tid_v = v[tid];
	for(int i=tid;i<w;i++)
        if(!UseFactOld && !UseFactNew)
            Dst[i] = op(Src[i],tid_v);
        else if(!UseFactOld && UseFactNew)
            Dst[i] = op(Src[i],tid_v)*factNew;
        else if(UseFactOld && !UseFactNew)
            Dst[i] = factOld * Dst[i] + op(Src[i],tid_v);
        else if(UseFactOld && UseFactNew)
            Dst[i] = factOld * Dst[i] + op(Src[i],tid_v)*factNew;
}
template<bool UseFactNew, bool UseFactOld, class V, class I, class V2, class OP>
__global__
void matrix_plus_vector_kernel_column_major2 (V *Dst, const V* Src, const V2* v, I h, I w, OP op, float factNew, float factOld) {
	const unsigned int idx = __mul24(blockIdx.x , blockDim.x) + threadIdx.x;
	const unsigned int numThreads = __mul24(blockDim.x , gridDim.x);

	int stop = w*h;
	for (unsigned int i = idx; i < stop; i += numThreads)
        if(!UseFactNew && !UseFactOld)
            Dst[i] = op(Src[i] , v[i % h]);
        else if(!UseFactNew && UseFactOld)
            Dst[i] = factOld * Dst[i] + op(Src[i] , v[i % h]);
        else if(UseFactNew && !UseFactOld)
            Dst[i] = factNew * op(Src[i] , v[i % h]);
        else if(UseFactNew && UseFactOld)
            Dst[i] = factOld * Dst[i] + factNew * op(Src[i] , v[i % h]);
}
template<bool UseFactNew, bool UseFactOld, class V, class I, class V2, class OP>
__global__
void matrix_plus_vector_kernel_row_major (V *Dst, const V* Src, const V2* v, I h, I w, OP op, float factNew, float factOld) {
	__shared__ V scalar;
	for(unsigned int baseidx = blockIdx.x; baseidx < h; baseidx += gridDim.x) {
		if (threadIdx.x == 0) {
			scalar = (V) v[baseidx];
		}
		__syncthreads();
		for (unsigned int i = threadIdx.x; i < w; i += blockDim.x) {
			const unsigned int k = baseidx * w + i;
            if(!UseFactOld && !UseFactNew)
                Dst[k] = op(Src[k] , scalar);
            else if(!UseFactOld && UseFactNew)
                Dst[k] = op(Src[k] , scalar) * factNew;
            else if(UseFactOld && !UseFactNew)
                Dst[k] = Dst[k] * factOld + op(Src[k] , scalar);
            else if(UseFactOld && UseFactNew)
                Dst[k] = Dst[k] * factOld + op(Src[k] , scalar) * factNew;
		}
		__syncthreads(); // necessary, otherwise the threads use different values of scalar!
	}
}

namespace matrix_op_col_impl {
	template<class V, class V2, class OP>
	void matrix_op_col(tensor<V,dev_memory_space,row_major>& Dst, const tensor<V,dev_memory_space,row_major>& Src, const tensor<V2,dev_memory_space>& v, const OP& op, float factNew, float factOld) {
		cuvAssert(Src.shape(0) == v.size());
        unsigned int other_dim = Src.size()/Src.shape(0);
		const unsigned int num_threads = min(512,other_dim);
		const unsigned int num_blocks  = min(1024,Src.shape(0));
        if(factNew == 1.f && factOld == 0.f)
            matrix_plus_vector_kernel_row_major<false,false><<<num_blocks,num_threads>>>(Dst.ptr(),Src.ptr(), v.ptr(), Src.shape(0), other_dim, op, factNew, factOld);
        else if(factNew == 1.f)
            matrix_plus_vector_kernel_row_major<false,true><<<num_blocks,num_threads>>>(Dst.ptr(),Src.ptr(), v.ptr(), Src.shape(0), other_dim, op, factNew, factOld);
        else if(factOld == 0.f)
            matrix_plus_vector_kernel_row_major<true,false><<<num_blocks,num_threads>>>(Dst.ptr(),Src.ptr(), v.ptr(), Src.shape(0), other_dim, op, factNew, factOld);
        else
            matrix_plus_vector_kernel_row_major<true,true><<<num_blocks,num_threads>>>(Dst.ptr(),Src.ptr(), v.ptr(), Src.shape(0), other_dim, op, factNew, factOld);
		cuvSafeCall(cudaThreadSynchronize());
	}
	template<class V, class V2, class OP>
	void matrix_op_col(tensor<V,dev_memory_space,column_major>& Dst, const tensor<V,dev_memory_space,column_major>& Src, const tensor<V2,dev_memory_space>& v, const OP& op, float factNew, float factOld) {
		cuvAssert(Src.shape(0) == v.size());
        unsigned int other_dim = Src.size()/Src.shape(0);
		const unsigned int num_threads = 512;
		const unsigned int num_blocks  = min(512,(int)ceil((float)Src.size() / num_threads));
        if(factNew == 1.f && factOld == 0.f)
            matrix_plus_vector_kernel_column_major2<false,false><<<num_blocks,num_threads>>>(Dst.ptr(), Src.ptr(), v.ptr(), Src.shape(0), other_dim, op, factNew, factOld);
        else if(                  factOld == 0.f)
            matrix_plus_vector_kernel_column_major2<true,false><<<num_blocks,num_threads>>>(Dst.ptr(), Src.ptr(), v.ptr(), Src.shape(0), other_dim, op, factNew, factOld);
        else if(factNew == 1.f                  )
            matrix_plus_vector_kernel_column_major2<false,true><<<num_blocks,num_threads>>>(Dst.ptr(), Src.ptr(), v.ptr(), Src.shape(0), other_dim, op, factNew, factOld);
        else // if(factNew == 1.f && factOld == 0.f)
            matrix_plus_vector_kernel_column_major2<true,true><<<num_blocks,num_threads>>>(Dst.ptr(), Src.ptr(), v.ptr(), Src.shape(0), other_dim, op, factNew, factOld);
		cuvSafeCall(cudaThreadSynchronize());
	}
	template<class V, class V2, class OP>
	void matrix_op_col(tensor<V,host_memory_space,column_major>& Dst, const tensor<V,host_memory_space,column_major>& Src, const tensor<V2,host_memory_space>& v, const OP& op, float factNew, float factOld) {
		cuvAssert(Src.shape(0) == v.size());
        unsigned int other_dim = Src.size()/Src.shape(0);
		const V2* v_ptr = v.ptr();
		const V * Src_ptr = Src.ptr();
		V * Dst_ptr = Dst.ptr();
        unsigned int Srcshape0 = Src.shape(0);

        // move src ptr, dst ptr and vptr at the same time

        if(factOld == 0.f){
            if(factNew == 1.f)
                for(int j=0;j<other_dim;j++) {
                    v_ptr = v.ptr();
                    for(int i=0;i<Srcshape0;i++,Src_ptr++,v_ptr++,Dst_ptr++)
                        *Dst_ptr = op(*Src_ptr,*v_ptr);
                }
            else
                for(int j=0;j<other_dim;j++) {
                    v_ptr = v.ptr();
                    for(int i=0;i<Srcshape0;i++,Src_ptr++,v_ptr++,Dst_ptr++)
                        *Dst_ptr = factNew * op(*Src_ptr,*v_ptr);
                }
        }else{
            if(factNew == 1.f)
                for(int j=0;j<other_dim;j++) {
                    v_ptr = v.ptr();
                    for(int i=0;i<Srcshape0;i++,Src_ptr++,v_ptr++,Dst_ptr++)
                        *Dst_ptr = *Dst_ptr * factOld + op(*Src_ptr,*v_ptr);
                }
            else
                for(int j=0;j<other_dim;j++) {
                    v_ptr = v.ptr();
                    for(int i=0;i<Srcshape0;i++,Src_ptr++,v_ptr++,Dst_ptr++)
                        *Dst_ptr = *Dst_ptr * factOld + factNew * op(*Src_ptr,*v_ptr);
                }
        }
	}
	template<class V, class V2, class OP>
	void matrix_op_col(tensor<V,host_memory_space,row_major>& Dst, const tensor<V,host_memory_space,row_major>& Src, const tensor<V2,host_memory_space>& v, const OP& op, float factNew, float factOld) {
		cuvAssert(Src.shape(0) == v.size());
        unsigned int other_dim = Src.size()/Src.shape(0);
		const V2* v_ptr = v.ptr();
		const V * Src_ptr = Src.ptr();
		V * Dst_ptr = Dst.ptr();
        unsigned int Srcshape0 = Src.shape(0);

        // in this version, we only move along src+dst horizontally, but keep pos in column vector v constant

        if(factOld == 0.f){
            if(factNew == 1.f )
                for(int i=0;i<Srcshape0;i++, v_ptr++) {
                    for(int j=0;j<other_dim;j++)
                        *Dst_ptr++ = op(*Src_ptr++,*v_ptr);
                }
            else
                for(int i=0;i<Srcshape0;i++, v_ptr++) {
                    for(int j=0;j<other_dim;j++)
                        *Dst_ptr++ = *Dst_ptr * factOld + factNew * op(*Src_ptr++,*v_ptr);
                }
        }else{
            if(factNew == 1.f)
                for(int i=0;i<Srcshape0;i++, v_ptr++) {
                    for(int j=0;j<other_dim;j++)
                        *Dst_ptr++ = *Dst_ptr * factOld + op(*Src_ptr++,*v_ptr);
                }
            else
                for(int i=0;i<Srcshape0;i++, v_ptr++) {
                    for(int j=0;j<other_dim;j++)
                        *Dst_ptr++ = *Dst_ptr * factOld + factNew * op(*Src_ptr++,*v_ptr);
                }
        }
	}
	// ====================  row ======================
	template<class V, class V2, class T, class M, class OP>
	void matrix_op_row(tensor<V,T,M>& Dst, const tensor<V,T,M>& Src, const tensor<V2,T>& v, const OP& op, float factNew, float factOld) {
		cuvAssert(Src.shape(Src.ndim()-1) == v.size());
		matrix_op_col(*(transposed_view(Dst)), *(transposed_view(Src)),v,op, factNew, factOld);
	}

	// ====================  middle ======================
    template<bool UseFactNew, bool UseFactOld,class T, class OP>
       __global__
       void matrix_op_middle_kernel(T* dst, const T* src, const T* v, OP op, const unsigned int dim0, const unsigned int dim1, const unsigned int dim2, float factNew, float factOld){
           unsigned int line = blockIdx.x;
           unsigned int init_offset = line * dim2 * dim1;
           T* dst0;
           const T* src0;
           unsigned int offset_dim0 = gridDim.x * dim2 * dim1;

           for(unsigned int f = 0; f < dim1; f++){
              dst0 = dst + init_offset;
              src0 = src + init_offset;
              line = blockIdx.x;

              T el = v[f];
              unsigned int offset_dim1 = f*dim2;
              for(; line < dim0; line += gridDim.x){
                  /*unsigned int item = threadIdx.x;*/
                  /*for(; item < dim2; item += blockDim.x){*/
                      /*unsigned int index = offset_dim1 + item;*/
                  for(unsigned int index = offset_dim1 + threadIdx.x;
                          index < dim2 + offset_dim1;
                          index += blockDim.x){

                      if(!UseFactOld && !UseFactNew)
                          dst0[index] = op(src0[index], el);
                      else if(!UseFactOld && UseFactNew)
                          dst0[index] = op(src0[index],el)*factNew;
                      else if(UseFactOld && !UseFactNew)
                          dst0[index] = factOld * dst0[index] + op(src0[index],el);
                      else if(UseFactOld && UseFactNew)
                          dst0[index] = factOld * dst0[index] + op(src0[index],el)*factNew;
                  }
                  dst0 += offset_dim0;
                  src0 += offset_dim0;
              }
           }
       }

    template<bool UseFactNew, bool UseFactOld,class T, class OP>
        void matrix_op_middle_host(T* dst, const T* src, const T* v, OP op,  unsigned int dim0, unsigned int dim1, unsigned int dim2, float factNew, float factOld){
            T* dst0;
            const T* src0;
            dst0 = dst;
            src0 = src;
            for(unsigned int f = 0; f < dim1; f++){
                unsigned int offset_dim1 = f * dim2;
                for(unsigned int d0 = 0; d0 < dim0; d0++){
                    unsigned int offset = d0 * dim1 * dim2 + offset_dim1;
                    for(unsigned int d2 = 0; d2 < dim2; ++d2){
                        T el = v[f];
                        unsigned int index = offset + d2;
                        if(!UseFactOld && !UseFactNew)
                            dst0[index] = op(src0[index], el);
                        else if(!UseFactOld && UseFactNew)
                            dst0[index] = op(src0[index],el)*factNew;
                        else if(UseFactOld && !UseFactNew)
                            dst0[index] = factOld * dst0[index] + op(src0[index],el);
                        else if(UseFactOld && UseFactNew)
                            dst0[index] = factOld * dst0[index] + op(src0[index],el)*factNew;
                    }
                }
            }
        }

    template<class V,class M, class T, class OP>
        void matrix_op_middle(tensor<V,M,T>& dst, const tensor<V,M,T>& src, const tensor<V,M,row_major>& v, unsigned int dim, const OP& op, float factNew, float factOld){
            assert(dst.ndim() == src.ndim());
            assert(v.ndim() == 1);
            assert(v.shape(0) == src.shape(dim));

            unsigned int dim0 = 1;
            unsigned int dim1 = src.shape(dim);
            unsigned int dim2 = 1;
            for(unsigned int i = 0; i < src.ndim(); i++){
                if(i < dim)
                    dim0 *= src.shape(i);
                else if(i > dim)
                    dim2 *= src.shape(i);
            }

            if(IsSame<M,host_memory_space>::Result::value){
                if(IsSame<T,row_major>::Result::value){
                    if(factNew == 1.f && factOld == 0.f)
                        matrix_op_middle_host<false,false>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else if(                  factOld == 0.f)
                        matrix_op_middle_host<true,false>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else if(factNew == 1.f                  )
                        matrix_op_middle_host<false,true>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else // if(factNew == 1.f && factOld == 0.f)
                        matrix_op_middle_host<true,true>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                }else{
                    // in the case of column mayor, only dim0 and dim2 are swiched
                    unsigned int temp = dim2;
                    dim2 = dim0;
                    dim0 = temp;

                    if(factNew == 1.f && factOld == 0.f)
                        matrix_op_middle_host<false,false>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else if(                  factOld == 0.f)
                        matrix_op_middle_host<true,false>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else if(factNew == 1.f                  )
                        matrix_op_middle_host<false,true>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else // if(factNew == 1.f && factOld == 0.f)
                        matrix_op_middle_host<true,true>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                }
            }else{
                // device: run kernel
                unsigned int num_threads = min(512, int(32 * ceil(dim2 / 32. )));

                unsigned int num_blocks  = min(1024,dim0);

                if(IsSame<T,row_major>::Result::value){
                    if(factNew == 1.f && factOld == 0.f)
                        matrix_op_middle_kernel<false,false><<<num_blocks,num_threads>>>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else if(                  factOld == 0.f)
                        matrix_op_middle_kernel<true,false><<<num_blocks,num_threads>>>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else if(factNew == 1.f                  )
                        matrix_op_middle_kernel<false,true><<<num_blocks,num_threads>>>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else // if(factNew == 1.f && factOld == 0.f)
                        matrix_op_middle_kernel<true,true><<<num_blocks,num_threads>>>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                }else{
                    // in the case of column mayor, only dim0 and dim2 are swiched
                    unsigned int temp = dim2;
                    dim2 = dim0;
                    dim0 = temp;

                    if(factNew == 1.f && factOld == 0.f)
                        matrix_op_middle_kernel<false,false><<<num_blocks,num_threads>>>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else if(                  factOld == 0.f)
                        matrix_op_middle_kernel<true,false><<<num_blocks,num_threads>>>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else if(factNew == 1.f                  )
                        matrix_op_middle_kernel<false,true><<<num_blocks,num_threads>>>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                    else // if(factNew == 1.f && factOld == 0.f)
                        matrix_op_middle_kernel<true,true><<<num_blocks,num_threads>>>(dst.ptr(), src.ptr(), v.ptr(), op, dim0, dim1, dim2, factNew, factOld);
                }

                cuvSafeCall(cudaThreadSynchronize());
            }
        }

}

template<class V, class V2, class M, class L>
    void matrix_op_vec(tensor<V,M,L>& Dst, const tensor<V,M,L>& Src, const tensor<V2,M>& v, int axis, BinaryFunctor bf, float factNew, float factOld, int n_params, float param0, float param1){
        if(axis == Src.ndim()-1)
            switch(bf){
                case BF_1ST:
                    matrix_op_col_impl::matrix_op_row(Dst, Src,v, bf_1st<V,V,V2>(), factNew, factOld);
                    break;
                case BF_2ND:
                    matrix_op_col_impl::matrix_op_row(Dst, Src,v, bf_2nd<V,V,V2>(), factNew, factOld);
                    break;
                case BF_ADD:
                    matrix_op_col_impl::matrix_op_row(Dst, Src,v, thrust::plus<V>(), factNew, factOld);
                    break;
                case BF_MULT:
                    matrix_op_col_impl::matrix_op_row(Dst, Src,v, thrust::multiplies<V>(), factNew, factOld);
                    break;
                case BF_DIV:
                    matrix_op_col_impl::matrix_op_row(Dst, Src,v, thrust::divides<V>(), factNew, factOld);
                    break;
                default:
                    throw std::runtime_error("matrix_op_row: binary functor not implemented.");
            }
        else if(axis == 0)
            switch(bf){
                case BF_1ST:
                    matrix_op_col_impl::matrix_op_col(Dst, Src,v, bf_1st<V,V,V2>(), factNew, factOld);
                    break;
                case BF_2ND:
                    matrix_op_col_impl::matrix_op_col(Dst, Src,v, bf_2nd<V,V,V2>(), factNew, factOld);
                    break;
                case BF_ADD:
                    matrix_op_col_impl::matrix_op_col(Dst, Src,v, thrust::plus<V>(), factNew, factOld);
                    break;
                case BF_MULT:
                    matrix_op_col_impl::matrix_op_col(Dst, Src,v, thrust::multiplies<V>(), factNew, factOld);
                    break;
                case BF_DIV:
                    matrix_op_col_impl::matrix_op_col(Dst, Src,v, thrust::divides<V>(), factNew, factOld);
                    break;
                default:
                    throw std::runtime_error("matrix_op_row: binary functor not implemented.");
            }
        else {
            switch(bf){
                case BF_1ST:
                    matrix_op_col_impl::matrix_op_middle(Dst, Src,v, axis,  bf_1st<V,V,V2>(), factNew, factOld);
                    break;
                case BF_2ND:
                    matrix_op_col_impl::matrix_op_middle(Dst, Src,v, axis,  bf_2nd<V,V,V2>(), factNew, factOld);
                    break;
                case BF_ADD:
                    matrix_op_col_impl::matrix_op_middle(Dst, Src,v,axis, thrust::plus<V>(), factNew, factOld);
                    break;
                case BF_MULT:
                    matrix_op_col_impl::matrix_op_middle(Dst, Src,v, axis,thrust::multiplies<V>(), factNew, factOld);
                    break;
                case BF_DIV:
                    matrix_op_col_impl::matrix_op_middle(Dst, Src,v,axis, thrust::divides<V>(), factNew, factOld);
                    break;
                default:
                    throw std::runtime_error("matrix_op_row: binary functor not implemented.");
            }
        }
    }

// ====================  col ======================
template<class __value_type, class __memory_space_type, class __memory_layout_type>
void matrix_plus_col(tensor<__value_type,__memory_space_type,__memory_layout_type>& A, const tensor<__value_type,__memory_space_type>& v) {
   /*matrix_op_col_impl::matrix_op_col(A,A,v, thrust::plus<__value_type>());*/
   matrix_op_vec(A, A, v, 0, BF_ADD);
}
template<class __value_type, class __memory_space_type, class __memory_layout_type>
void matrix_times_col(tensor<__value_type,__memory_space_type,__memory_layout_type>& A, const tensor<__value_type,__memory_space_type>& v) {
	/*matrix_op_col_impl::matrix_op_col(A,A,v, thrust::multiplies<__value_type>());*/
    matrix_op_vec(A, A, v, 0, BF_MULT);
}
template<class __value_type, class __memory_space_type, class __memory_layout_type>
void matrix_divide_col(tensor<__value_type,__memory_space_type,__memory_layout_type>& A, const tensor<__value_type,__memory_space_type>& v) {
	/*matrix_op_col_impl::matrix_op_col(A,A,v, thrust::divides<__value_type>());*/
    matrix_op_vec(A, A, v, 0, BF_DIV);
}
// ====================  row ======================
template<class __value_type, class __memory_space_type, class __memory_layout_type>
void matrix_plus_row(tensor<__value_type,__memory_space_type,__memory_layout_type>& A, const tensor<__value_type,__memory_space_type>& v) {
   /*matrix_op_col_impl::matrix_op_row(A,A,v, thrust::plus<__value_type>());*/
   matrix_op_vec(A, A, v, A.ndim()-1, BF_ADD);
}
template<class __value_type, class __memory_space_type, class __memory_layout_type>
void matrix_times_row(tensor<__value_type,__memory_space_type,__memory_layout_type>& A, const tensor<__value_type,__memory_space_type>& v) {
	/*matrix_op_col_impl::matrix_op_row(A,A,v, thrust::multiplies<__value_type>());*/
    matrix_op_vec(A, A, v, A.ndim()-1, BF_MULT);
}
template<class __value_type, class __memory_space_type, class __memory_layout_type>
void matrix_divide_row(tensor<__value_type,__memory_space_type,__memory_layout_type>& A, const tensor<__value_type,__memory_space_type>& v) {
	/*matrix_op_col_impl::matrix_op_row(A,A,v, thrust::divides<__value_type>());*/
    matrix_op_vec(A, A, v, A.ndim()-1, BF_DIV);
}

namespace transpose_impl{
	template<class V>
	void transpose(tensor<V, dev_memory_space, column_major>& dst,
			 const tensor<V, dev_memory_space, column_major>& src) {
                cuvAssert(dst.ndim()==2);
                cuvAssert(src.ndim()==2);
		cuvAssert(dst.shape(1) == src.shape(0));
		cuvAssert(dst.shape(0) == src.shape(1));
                typedef typename tensor<V, dev_memory_space, column_major>::index_type I;
		const I width = src.shape(0);
		const I height = src.shape(1);
		static const int BLOCK_SIZE = 16;
		const int numBlocksX = ceil((float)width / BLOCK_SIZE);
		const int numBlocksY = ceil((float)height / BLOCK_SIZE);
		dim3 gridSize(numBlocksX, numBlocksY, 1);
		dim3 blockSize(BLOCK_SIZE, BLOCK_SIZE, 1);
		transpose_kernel<BLOCK_SIZE><<<gridSize, blockSize>>>(dst.ptr(), src.ptr(), width, height,dst.stride(1),src.stride(1));
		cuvSafeCall(cudaThreadSynchronize());
	}

	template<class V>
	void transpose(tensor<V,dev_memory_space,row_major>& dst,
			 const tensor<V,dev_memory_space,row_major>& src) {
                cuvAssert(dst.ndim()==2);
                cuvAssert(src.ndim()==2);
		cuvAssert(dst.shape(1) == src.shape(0));
		cuvAssert(dst.shape(0) == src.shape(1));
                typedef typename tensor<V, dev_memory_space, row_major>::index_type I;
		const I width = src.shape(1);
		const I height = src.shape(0);
		static const int BLOCK_SIZE = 16;
		const int numBlocksX = ceil((float)width / BLOCK_SIZE);
		const int numBlocksY = ceil((float)height / BLOCK_SIZE);
		dim3 gridSize(numBlocksX, numBlocksY, 1);
		dim3 blockSize(BLOCK_SIZE, BLOCK_SIZE, 1);
		transpose_kernel<BLOCK_SIZE><<<gridSize, blockSize>>>(dst.ptr(), src.ptr(), width, height,dst.stride(0),src.stride(0));
		cuvSafeCall(cudaThreadSynchronize());
	}

	template<class V>
	void transpose(tensor<V,host_memory_space,column_major>& dst,
			 const tensor<V,host_memory_space,column_major>& src) {
                cuvAssert(dst.ndim()==2);
                cuvAssert(src.ndim()==2);
		cuvAssert(dst.shape(1) == src.shape(0));
		cuvAssert(dst.shape(0) == src.shape(1));
		V* dst_ptr = dst.ptr();
		const V* src_ptr = src.ptr();
		for(int i=0; i<dst.shape(1); i++) {
			for(int j=0; j<dst.shape(0); j++) {
				*dst_ptr++ = src_ptr[j*src.shape(0)];
			}
			src_ptr++;
		}
	}

	template<class V>
	void transpose(tensor<V,host_memory_space,row_major>& dst,
			 const tensor<V,host_memory_space,row_major>& src) {
                cuvAssert(dst.ndim()==2);
                cuvAssert(src.ndim()==2);
		cuvAssert(dst.shape(1) == src.shape(0));
		cuvAssert(dst.shape(0) == src.shape(1));
		V* dst_ptr = dst.ptr();
		const V* src_ptr = src.ptr();
		for(int i=0; i<dst.shape(0); i++) {
			for(int j=0; j<dst.shape(1); j++) {
				*dst_ptr++ = src_ptr[j*src.shape(1)];
			}
			src_ptr++;
		}
	}
} // namespace transpose_impl

template<class __value_type, class __memory_space_type, class __memory_layout_type>
void transpose(tensor<__value_type,__memory_space_type, __memory_layout_type>& dst, const tensor<__value_type,__memory_space_type, __memory_layout_type>& src){
	transpose_impl::transpose(dst,src);
}

template<class V, class T, class M>
cuv::tensor<V,T,typename other_memory_layout<M>::type> * transposed_view_p(cuv::tensor<V,T,M>&  src){
        std::vector<unsigned int> shape = src.shape();
        std::reverse(shape.begin(), shape.end());
	return new tensor<V,T,typename other_memory_layout<M>::type>(shape,src.ptr());
}

template<class V, class T, class M>
const cuv::tensor<V,T,typename other_memory_layout<M>::type> * transposed_view_p(const cuv::tensor<V,T,M>&  src){
        std::vector<unsigned int> shape = src.shape();
        std::reverse(shape.begin(), shape.end());
	return new tensor<V,T,typename other_memory_layout<M>::type>(shape,const_cast<V*>(src.ptr()));
}

#define INSTANTIATE_MOV(V1,V2,M) \
  template void matrix_op_vec(tensor<V1,dev_memory_space,M>&, const tensor<V1,dev_memory_space,M>&, const tensor<V2,dev_memory_space>&, int, BinaryFunctor, float,float, int, float,float);   \
  template void matrix_op_vec(tensor<V1,host_memory_space,M>&, const tensor<V1,host_memory_space,M>&, const tensor<V2,host_memory_space>&, int, BinaryFunctor, float,float, int, float,float);   \

#define INSTANTIATE_MV(V1,V2,M) \
  template void matrix_plus_col(tensor<V1,dev_memory_space,M>&, const tensor<V2,dev_memory_space>&);   \
  template void matrix_plus_col(tensor<V1,host_memory_space,M>&, const tensor<V2,host_memory_space>&); \
  template void matrix_times_col(tensor<V1,dev_memory_space,M>&, const tensor<V2,dev_memory_space>&);  \
  template void matrix_times_col(tensor<V1,host_memory_space,M>&, const tensor<V2,host_memory_space>&); \
  template void matrix_divide_col(tensor<V1,dev_memory_space,M>&, const tensor<V2,dev_memory_space>&);  \
  template void matrix_divide_col(tensor<V1,host_memory_space,M>&, const tensor<V2,host_memory_space>&); \
  template void matrix_plus_row(tensor<V1,dev_memory_space,M>&, const tensor<V2,dev_memory_space>&);   \
  template void matrix_plus_row(tensor<V1,host_memory_space,M>&, const tensor<V2,host_memory_space>&); \
  template void matrix_times_row(tensor<V1,dev_memory_space,M>&, const tensor<V2,dev_memory_space>&);  \
  template void matrix_times_row(tensor<V1,host_memory_space,M>&, const tensor<V2,host_memory_space>&); \
  template void matrix_divide_row(tensor<V1,dev_memory_space,M>&, const tensor<V2,dev_memory_space>&);  \
  template void matrix_divide_row(tensor<V1,host_memory_space,M>&, const tensor<V2,host_memory_space>&);


#define INSTANTIATE_BLOCKVIEW(V,M,I) \
  template tensor<V,host_memory_space,M>* blockview(tensor<V,host_memory_space,M>&,I,I,I,I); \
  template tensor<V,dev_memory_space,M>* blockview(tensor<V,dev_memory_space,M>&,I,I,I,I);

#define INSTANTIATE_TRANSPOSE(V,M) \
  template void transpose(tensor<V, host_memory_space, M>&, const tensor<V, host_memory_space, M>&); \
  template void transpose(tensor<V, dev_memory_space , M>&, const tensor<V, dev_memory_space , M>&);

#define INSTANTIATE_TRANSPOSED_VIEW(V) \
  template tensor<V,host_memory_space,other_memory_layout<column_major>::type >* transposed_view_p(tensor<V,host_memory_space,column_major>&);\
  template tensor<V,host_memory_space,other_memory_layout<row_major>::type >* transposed_view_p(tensor<V,host_memory_space,row_major>&);\
  template tensor<V,dev_memory_space,other_memory_layout<column_major>::type >* transposed_view_p(tensor<V,dev_memory_space,column_major>&);\
  template tensor<V,dev_memory_space,other_memory_layout<row_major>::type >* transposed_view_p(tensor<V,dev_memory_space,row_major>&);\
  template const tensor<V,host_memory_space,other_memory_layout<column_major>::type >* transposed_view_p(const tensor<V,host_memory_space,column_major>&);\
  template const tensor<V,host_memory_space,other_memory_layout<row_major>::type >* transposed_view_p(const tensor<V,host_memory_space,row_major>&);\
  template const tensor<V,dev_memory_space,other_memory_layout<column_major>::type >* transposed_view_p(const tensor<V,dev_memory_space,column_major>&);\
  template const tensor<V,dev_memory_space,other_memory_layout<row_major>::type >* transposed_view_p(const tensor<V,dev_memory_space,row_major>&);

INSTANTIATE_TRANSPOSE(float,column_major);
INSTANTIATE_TRANSPOSE(float,row_major);
INSTANTIATE_TRANSPOSE(int,column_major);
INSTANTIATE_TRANSPOSE(int,row_major);
INSTANTIATE_TRANSPOSE(unsigned char,column_major);
INSTANTIATE_TRANSPOSE(unsigned char,row_major);

INSTANTIATE_TRANSPOSED_VIEW(float);
INSTANTIATE_TRANSPOSED_VIEW(int);
INSTANTIATE_TRANSPOSED_VIEW(unsigned int);
INSTANTIATE_TRANSPOSED_VIEW(char);
INSTANTIATE_TRANSPOSED_VIEW(unsigned char);

INSTANTIATE_MOV(float, float, column_major);
INSTANTIATE_MOV(float, float, row_major);

INSTANTIATE_MV(float, float, column_major);
INSTANTIATE_MV(float, float, row_major);
/*INSTANTIATE_MV(float, unsigned char, column_major);*/
/*INSTANTIATE_MV(float, unsigned char, row_major);*/

INSTANTIATE_BLOCKVIEW(float,column_major,int);
INSTANTIATE_BLOCKVIEW(float,row_major,int);


}; // cuv
