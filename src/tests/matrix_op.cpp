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





#define BOOST_TEST_MODULE example
#include <boost/test/included/unit_test.hpp>
#include <boost/test/floating_point_comparison.hpp>
#include <boost/assign.hpp>
#include <list>
using namespace boost::assign;

#include <cuv/tools/cuv_general.hpp>
#include <cuv/basics/tensor.hpp>
#include <cuv/convert/convert.hpp>
#include <cuv/matrix_ops/matrix_ops.hpp>
#include <cuv/tensor_ops/rprop.hpp>
#include <cuv/tools/cuv_test.hpp>
#include <cuv/random/random.hpp>

using namespace cuv;

struct MyConfig {
	static const int dev = CUDA_TEST_DEVICE;
	MyConfig()   { 
		printf("Testing on device=%d\n",dev);
		initCUDA(dev); 
		initialize_mersenne_twister_seeds();
	}
	~MyConfig()  { exitCUDA();  }
};

BOOST_GLOBAL_FIXTURE( MyConfig );

struct Fix{
	static const int n;
	static const int N;
	static const int big_images = 384*384*2;
	tensor<float,dev_memory_space,column_major> a,b,u,v,w,d_reduce_big;
	tensor<float,host_memory_space,column_major> s,t,r,x,z,h_reduce_big;
	Fix()
	:   a(1,n),b(1,n),u(extents[n][n]),v(extents[n][n]),w(extents[n][n]), d_reduce_big(extents[32][big_images])
	,   s(1,n),t(1,n),r(extents[n][n]),x(extents[n][n]),z(extents[n][n]), h_reduce_big(extents[32][big_images])
	{
	}
	~Fix(){
	}
};

const int Fix::n=128;
const int Fix::N=Fix::n * Fix::n;
template<class VT2, class VT, class ML>
std::pair<tensor<VT2,host_memory_space>*,    // host result
	 tensor<VT2,host_memory_space>*>   // dev  result
test_reduce(
	int dim,
	tensor<VT,dev_memory_space,ML>&   d_mat,
	cuv::reduce_functor rf
){
	tensor<VT,host_memory_space,ML> h_mat(d_mat.shape());
	convert(h_mat,d_mat);

	unsigned int len = d_mat.shape()[0];
	if(dim==0) len = d_mat.shape()[1];
	tensor<VT2,host_memory_space>* v_host1= new tensor<VT2,host_memory_space> (len);
	tensor<VT2,host_memory_space>* v_host2= new tensor<VT2,host_memory_space> (len);
	tensor<VT2,dev_memory_space>   v_dev(len);
	if(dim==0){
		reduce_to_row(*v_host1, h_mat,rf);
		reduce_to_row( v_dev,   d_mat,rf);
	}else if(dim==1){
		reduce_to_col(*v_host1, h_mat,rf);
		reduce_to_col( v_dev,   d_mat,rf);
	}
	convert(*v_host2,v_dev);
	return std::make_pair(v_host1, v_host2);
}


BOOST_FIXTURE_TEST_SUITE( s, Fix )


BOOST_AUTO_TEST_CASE( vec_ops_unary1 )
{
	apply_scalar_functor(v, SF_EXP);
	//apply_scalar_functor(v, SF_EXACT_EXP);
	apply_scalar_functor(x, SF_EXP);
	//apply_scalar_functor(x, SF_EXACT_EXP);
}

BOOST_AUTO_TEST_CASE( binary_operators )
{
  tensor<float,dev_memory_space,column_major> j(extents[32][32]);
  tensor<float,dev_memory_space,column_major> k(extents[32][32]);
  j = k = 1.f;
  const tensor<float,dev_memory_space,column_major>& j_ = j;
  const tensor<float,dev_memory_space,column_major>& k_ = k;
  const tensor<float,dev_memory_space,column_major> l = j_+k_;
  for(int i=0;i<32*32;i++){
	  BOOST_CHECK_EQUAL(l[i], 2.f);
  }
}

BOOST_AUTO_TEST_CASE( vec_ops_binary1 )
{
	sequence(v);
	w=v.copy();
	sequence(a);
	//apply_scalar_functor(v,SF_ADD,1);
	v+= float(1.0);
	for(int i=0;i<N;i++){
		BOOST_CHECK_EQUAL(v[i], i + 1);
	}
	//apply_binary_functor(v,w, BF_ADD);
	a=v.copy();
	a=v+w;
	v=a;
	for(int i=0;i<N;i++){
		BOOST_CHECK_EQUAL(v[i], i + i + 1);
	}
}

BOOST_AUTO_TEST_CASE( vec_ops_binary2 )
{
	apply_binary_functor(v,w, BF_AXPY, 2.f);
}

BOOST_AUTO_TEST_CASE( vec_ops_copy )
{
	// generate data
	sequence(w);

	// copy data from v to w
	// copy(v,w);
	v=w.copy();
    BOOST_CHECK_NE(v.ptr(), w.ptr());
	for(int i=0;i<N;i++){
		BOOST_CHECK_EQUAL(v[i],w[i]);
	}
}

BOOST_AUTO_TEST_CASE( vec_ops_unary_add )
{
	sequence(v);
	apply_scalar_functor(v,SF_ADD,3.8f);
	for(int i=0;i<N;i++){
		BOOST_CHECK_EQUAL(v[i], i+3.8f);
	}
}
BOOST_AUTO_TEST_CASE( vec_ops_axpby )
{
	// generate data
	sequence(w);
	sequence(v);
	apply_scalar_functor(v,SF_ADD, 1.f);
	BOOST_CHECK_EQUAL(w[0],0);
	BOOST_CHECK_EQUAL(v[0],1);

	// copy data from v to w
	apply_binary_functor(v,w,BF_AXPBY, 2.f,3.f);
	for(int i=0;i<N;i++){
		BOOST_CHECK_EQUAL(v[i], 2*(i+1) + 3*i );
	}
}


BOOST_AUTO_TEST_CASE( vec_ops_0ary1 )
{
	// test sequence
	sequence(v);
	for(int i=0;i<N;i++){
		BOOST_CHECK_EQUAL(v[i], i);
	}

	// test fill
	w = 1.f;
	for(int i=0;i<N;i++){
		BOOST_CHECK_EQUAL(w[i], 1);
	}
}

BOOST_AUTO_TEST_CASE( vec_ops_norms )
{
	sequence(v);
	float f1 = norm1(v), f1_ = 0;
	float f2 = norm2(v), f2_ = 0;
	for(int i=0;i<N;i++){
		f2_ += v[i]*v[i];
		f1_ += fabs(v[i]);
	}
	f2_ = sqrt(f2_);
	BOOST_CHECK_CLOSE((float)f1,(float)f1_,0.01f);
	BOOST_CHECK_CLOSE((float)f2,(float)f2_,0.01f);
}


BOOST_AUTO_TEST_CASE( mat_op_mm )
{
	sequence(v);     apply_scalar_functor(v, SF_MULT, 0.01f);
	sequence(w);     apply_scalar_functor(w, SF_MULT, 0.01f);
	sequence(x);     apply_scalar_functor(x, SF_MULT, 0.01f);
	sequence(z);     apply_scalar_functor(z, SF_MULT, 0.01f);
	prod(u,v,w,'n','t');
	prod(r,x,z,'n','t');

	tensor<float,host_memory_space,column_major> u2(u.shape());
	convert(u2,u);
	for(int i=0;i<u2.shape()[0];i++){
		for(int j=0;j<u2.shape()[0];j++){
			BOOST_CHECK_CLOSE( (float)u2(i,j), (float)r(i,j), 0.01 );
		}
	}
}

BOOST_AUTO_TEST_CASE( mat_op_rm_prod )
{
	int m = 234;
	int n = 314;
	int k = 413;

	tensor<float,host_memory_space,row_major> hA(extents[m][k]);
	tensor<float,host_memory_space,row_major> hB(extents[k][n]);
	tensor<float,host_memory_space,row_major> hC(extents[m][n]);

	tensor<float,dev_memory_space,row_major> dA(extents[m][k]);
	tensor<float,dev_memory_space,row_major> dB(extents[k][n]);
	tensor<float,dev_memory_space,row_major> dC(extents[m][n]);

	sequence(hA);     apply_scalar_functor(hA, SF_MULT, 0.01f);
	sequence(hB);     apply_scalar_functor(hB, SF_MULT, 0.01f);
	sequence(hC);     apply_scalar_functor(hC, SF_MULT, 0.01f);

	sequence(dA);     apply_scalar_functor(dA, SF_MULT, 0.01f);
	sequence(dB);     apply_scalar_functor(dB, SF_MULT, 0.01f);
	sequence(dC);     apply_scalar_functor(dC, SF_MULT, 0.01f);

	prod(hC,hA,hB,'n','n');
	prod(dC,dA,dB,'n','n');

	tensor<float,host_memory_space,row_major> c2(dC.shape());
	convert(c2,dC);

	for(int i=0;i<m;i++){
		for(int j=0;j<n;j++){
			BOOST_CHECK_CLOSE( (float)hC(i,j), (float)c2(i,j), 0.01 );
		}
	}
}

BOOST_AUTO_TEST_CASE( mat_op_mmdim1 )
{
	sequence(a);     apply_scalar_functor(a, SF_MULT, 0.01f);
	sequence(w);     apply_scalar_functor(w, SF_MULT, 0.01f);
	sequence(s);     apply_scalar_functor(s, SF_MULT, 0.01f);
	sequence(z);     apply_scalar_functor(z, SF_MULT, 0.01f);
	prod(b,a,w,'n','t');
	prod(t,s,z,'n','t');

	tensor<float,host_memory_space,column_major> b2(b.shape());
	convert(b2,b);

	for(int i=0;i<z.shape()[0];i++) {
		float val = 0.0f;
		for(int j=0;j<z.shape()[1];j++) {
			val += s(0,j) * z(i,j);
		}
		BOOST_CHECK_CLOSE( (float)b2(0,i), (float)val, 0.01 );
		BOOST_CHECK_CLOSE( (float)t(0,i), (float)val, 0.01 );
	}
}

BOOST_AUTO_TEST_CASE( mat_op_mat_plus_row )
{
	sequence(v); sequence(w);
	sequence(x); sequence(z);
	tensor<float,dev_memory_space>   v_vec(n); sequence(v_vec);
	tensor<float,host_memory_space>  x_vec(n); sequence(x_vec);
	matrix_plus_row(v,v_vec);
	matrix_plus_row(x,x_vec);
	for(int i=0;i<n;i++){
		for(int j=0;j<n;j++){
			BOOST_CHECK_CLOSE((float)v(i,j), (float)x(i,j), 0.01);
			BOOST_CHECK_CLOSE((float)v(i,j), w(i,j)+v_vec[j], 0.01);
			BOOST_CHECK_CLOSE((float)x(i,j), z(i,j)+x_vec[j], 0.01);
		}
	}

}


BOOST_AUTO_TEST_CASE( mat_op_middle )
{
    unsigned int nImg = 100;
    unsigned int nChan = 20;
    unsigned int npix_x = 10;
    // 2nd dim
    {
        cuv::tensor<float,cuv::dev_memory_space, row_major> src(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> dst(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> v(cuv::extents[nChan]);

        cuv::tensor<float,cuv::host_memory_space, row_major> src_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> dst_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> temp_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> v_h(cuv::extents[nChan]);

        fill_rnd_uniform(src_h);
        fill_rnd_uniform(v_h);
        v = v_h;
        src = src_h;

        dst = 0.f;
        dst_h = 0.f;

        matrix_op_vec(dst, src,v,1,BF_ADD);
        matrix_op_vec(dst_h, src_h,v_h,1,BF_ADD);

        temp_h = dst;
        BOOST_CHECK_CLOSE(1.f,1.f + cuv::norm1(temp_h - dst_h), 0.01);
        
    }
    // 3rd dim
    {
        cuv::tensor<float,cuv::dev_memory_space, row_major> src(cuv::extents[nImg][npix_x][nChan][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> dst(cuv::extents[nImg][npix_x][nChan][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> v(cuv::extents[nChan]);

        cuv::tensor<float,cuv::host_memory_space, row_major> src_h(cuv::extents[nImg][npix_x][nChan][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> dst_h(cuv::extents[nImg][npix_x][nChan][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> temp_h(cuv::extents[nImg][npix_x][nChan][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> v_h(cuv::extents[nChan]);

        fill_rnd_uniform(src_h);
        fill_rnd_uniform(v_h);
        v = v_h;
        src = src_h;

        dst = 0.f;
        dst_h = 0.f;

        matrix_op_vec(dst, src,v,2,BF_ADD);
        matrix_op_vec(dst_h, src_h,v_h,2,BF_ADD);

        temp_h = dst;
        BOOST_CHECK_CLOSE(1.f,1.f + cuv::norm1(temp_h - dst_h), 0.01);
        
    }
    // column major
    {
        cuv::tensor<float,cuv::dev_memory_space, column_major> src(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, column_major> dst(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> v(cuv::extents[nChan]);

        cuv::tensor<float,cuv::host_memory_space, column_major> src_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, column_major> dst_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, column_major> temp_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> v_h(cuv::extents[nChan]);

        fill_rnd_uniform(src_h);
        fill_rnd_uniform(v_h);
        v = v_h;
        src = src_h;

        dst = 0.f;
        dst_h = 0.f;

        matrix_op_vec(dst, src,v,1,BF_ADD);
        matrix_op_vec(dst_h, src_h,v_h,1,BF_ADD);

        temp_h = dst;
        BOOST_CHECK_CLOSE(1.f,1.f + cuv::norm1(temp_h - dst_h), 0.01);
        
    }

}

BOOST_AUTO_TEST_CASE( mat_op_vec )
{
    unsigned int nImg = 100;
    unsigned int nChan = 20;
    unsigned int npix_x = 10;
    {
        cuv::tensor<float,cuv::host_memory_space, row_major> src_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> dst1_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> dst2_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> dst3_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> dst4_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> v1_h(cuv::extents[nImg]);
        cuv::tensor<float,cuv::host_memory_space, row_major> v2_h(cuv::extents[nChan]);
        cuv::tensor<float,cuv::host_memory_space, row_major> v3_h(cuv::extents[npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> v4_h(cuv::extents[npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> src(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> dst1(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> dst2(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> dst3(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> dst4(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> v1(cuv::extents[nImg]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> v2(cuv::extents[nChan]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> v3(cuv::extents[npix_x]);
        cuv::tensor<float,cuv::dev_memory_space, row_major> v4(cuv::extents[npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> temp1_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> temp2_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> temp3_h(cuv::extents[nImg][nChan][npix_x][npix_x]);
        cuv::tensor<float,cuv::host_memory_space, row_major> temp4_h(cuv::extents[nImg][nChan][npix_x][npix_x]);

        sequence(src_h);
        sequence(v1_h);
        sequence(v2_h);
        sequence(v3_h);
        sequence(v4_h);
        src = src_h;
        v1 = v1_h;
        v2 = v2_h;
        v3 = v3_h;
        v4 = v4_h;
        dst1_h = 0.f;
        dst2_h = 0.f;
        dst3_h = 0.f;
        dst4_h = 0.f;
        dst1 = 0.f;
        dst2 = 0.f;
        dst3 = 0.f;
        dst4 = 0.f;

        matrix_op_vec(dst1_h, src_h, v1_h, 0, BF_ADD);
        matrix_op_vec(dst2_h, src_h, v2_h, 1, BF_ADD);
        matrix_op_vec(dst3_h, src_h, v3_h, 2, BF_ADD);
        matrix_op_vec(dst4_h, src_h, v4_h, 3, BF_ADD);
        matrix_op_vec(dst1, src, v1, 0, BF_ADD);
        matrix_op_vec(dst2, src, v2, 1, BF_ADD);
        matrix_op_vec(dst3, src, v3, 2, BF_ADD);
        matrix_op_vec(dst4, src, v4, 3, BF_ADD);

        for (int q=0; q < nImg; q++){
            for (int w=0; w < nChan; w++){
                for (int e=0; e < npix_x; e++){
                    for (int r=0; r < npix_x; r++){
                        float src_part = q*nChan*npix_x*npix_x + w*npix_x*npix_x + e*npix_x + r;
                        BOOST_CHECK_CLOSE((float) src_part
                                + q , (float) dst1_h(q,w,e,r), 0.01);
                        BOOST_CHECK_CLOSE((float) src_part
                                + w , (float) dst2_h(q,w,e,r), 0.01);
                        BOOST_CHECK_CLOSE((float) src_part
                                + e , (float) dst3_h(q,w,e,r), 0.01);
                        BOOST_CHECK_CLOSE((float) src_part
                                + r , (float) dst4_h(q,w,e,r), 0.01);
                    }
                }
            }
        }

        temp1_h = dst1;
        temp2_h = dst2;
        temp3_h = dst3;
        temp4_h = dst4;

        BOOST_CHECK_CLOSE(1.f,1.f + cuv::norm1(temp1_h - dst1_h), 0.01);
        BOOST_CHECK_CLOSE(1.f,1.f + cuv::norm1(temp2_h - dst2_h), 0.01);
        BOOST_CHECK_CLOSE(1.f,1.f + cuv::norm1(temp3_h - dst3_h), 0.01);
        BOOST_CHECK_CLOSE(1.f,1.f + cuv::norm1(temp4_h - dst4_h), 0.01);
    }
}
BOOST_AUTO_TEST_CASE( mat_op_mat_bf2nd )
{
        sequence(v); sequence(w);
        sequence(x); sequence(z);
        tensor<float,dev_memory_space>   v_vec(n); sequence(v_vec);
        tensor<float,host_memory_space>  x_vec(n); sequence(x_vec);
        matrix_op_vec(v, v,v_vec,1,BF_2ND);
        matrix_op_vec(x, x,x_vec,1,BF_2ND);

        // v = 2 v + 1.5 (v + v_vec)

        for(int i=0;i<n;i++){
            for(int j=0;j<n;j++){
                BOOST_CHECK_CLOSE((float)v(i,j), (float)x(i,j), 0.01);
                BOOST_CHECK_CLOSE((float)v(i,j), (float)v_vec[j], 0.01);
                BOOST_CHECK_CLOSE((float)x(i,j), (float)x_vec[j], 0.01);
            }
        }
}

BOOST_AUTO_TEST_CASE( mat_op_mat_plus_row_fact )
{
	{   // both factors
        sequence(v); sequence(w);
        sequence(x); sequence(z);
        tensor<float,dev_memory_space>   v_vec(n); sequence(v_vec);
        tensor<float,host_memory_space>  x_vec(n); sequence(x_vec);
        matrix_op_vec(v, v,v_vec,1,BF_ADD, 1.5f, 2.f);
        matrix_op_vec(x, x,x_vec,1,BF_ADD, 1.5f, 2.f);


        // v = 2 v + 1.5 (v + v_vec)

        for(int i=0;i<n;i++){
            for(int j=0;j<n;j++){
                BOOST_CHECK_CLOSE((float)v(i,j), (float)x(i,j), 0.01);
                BOOST_CHECK_CLOSE((float)v(i,j), 2.f * w(i,j) + 1.5 * (w(i,j) + v_vec[j]), 0.01);
                BOOST_CHECK_CLOSE((float)x(i,j), 2.f * z(i,j) + 1.5 * (z(i,j) + x_vec[j]), 0.01);
            }
        }
	}

	{   // only factNew
        sequence(v); sequence(w);
        sequence(x); sequence(z);
        tensor<float,dev_memory_space>   v_vec(n); sequence(v_vec);
        tensor<float,host_memory_space>  x_vec(n); sequence(x_vec);
        matrix_op_vec(v, v,v_vec,1,BF_ADD, 1.5f);
        matrix_op_vec(x, x,x_vec,1,BF_ADD, 1.5f);


        // v = 1.5 (v + v_vec)

        for(int i=0;i<n;i++){
            for(int j=0;j<n;j++){
                BOOST_CHECK_CLOSE((float)v(i,j), (float)x(i,j), 0.01);
                BOOST_CHECK_CLOSE((float)v(i,j), 1.5 * (w(i,j) + v_vec[j]), 0.01);
                BOOST_CHECK_CLOSE((float)x(i,j), 1.5 * (z(i,j) + x_vec[j]), 0.01);
            }
        }
	}

	{   // only factOld
        sequence(v); sequence(w);
        sequence(x); sequence(z);
        tensor<float,dev_memory_space>   v_vec(n); sequence(v_vec);
        tensor<float,host_memory_space>  x_vec(n); sequence(x_vec);
        matrix_op_vec(v, v,v_vec,1,BF_ADD, 1.0f, 2.f);
        matrix_op_vec(x, x,x_vec,1,BF_ADD, 1.0f, 2.f);


        // v = 2 v + (v + v_vec)

        for(int i=0;i<n;i++){
            for(int j=0;j<n;j++){
                BOOST_CHECK_CLOSE((float)v(i,j), (float)x(i,j), 0.01);
                BOOST_CHECK_CLOSE((float)v(i,j), 2.f * w(i,j) + (w(i,j) + v_vec[j]), 0.01);
                BOOST_CHECK_CLOSE((float)x(i,j), 2.f * z(i,j) + (z(i,j) + x_vec[j]), 0.01);
            }
        }
	}

}
BOOST_AUTO_TEST_CASE( mat_op_mat_reduce_to_row_3d )
{
    tensor<float,host_memory_space> R(extents[5][4][3]);

    tensor<float,dev_memory_space>  Md(extents[5][4][3]);
    tensor<float,host_memory_space> Mh(extents[5][4][3]);
    tensor<float,dev_memory_space>  vd(extents[3]);
    tensor<float,host_memory_space> vh(extents[3]);

    sequence(R);
    sequence(Md);
    sequence(Mh);
    sequence(vd);
    sequence(vh);

	reduce_to_row(vd,Md);
	reduce_to_row(vh,Mh);
    for(int k=0;k<3;k++){
        float sum = 0.f;
        BOOST_CHECK_CLOSE((float)vd(k), (float)vh(k), 0.01);
        for(int i=0;i<5;i++){
            for(int j=0;j<4;j++){
                sum += Mh(i,j,k);
            }
        }
        BOOST_CHECK_CLOSE((float)vh(k), sum, 0.01);
	}
}
BOOST_AUTO_TEST_CASE( mat_op_mat_reduce_to_col_3d )
{
    tensor<float,host_memory_space> R(extents[5][4][3]);

    tensor<float,dev_memory_space>  Md(extents[5][4][3]);
    tensor<float,host_memory_space> Mh(extents[5][4][3]);
    tensor<float,dev_memory_space>  vd(extents[5]);
    tensor<float,host_memory_space> vh(extents[5]);

    sequence(R);
    sequence(Md);
    sequence(Mh);
    sequence(vd);
    sequence(vh);

	reduce_to_col(vd,Md);
	reduce_to_col(vh,Mh);
	for(int i=0;i<5;i++){
        BOOST_CHECK_CLOSE((float)vd(i), (float)vh(i), 0.01);
        float sum = 0.f;
        for(int j=0;j<4;j++){
            for(int k=0;k<3;k++){
                sum += Mh(i,j,k);
            }
        }
        BOOST_CHECK_CLOSE((float)vh(i), sum, 0.01);
	}
}
BOOST_AUTO_TEST_CASE( mat_op_mat_plus_row_3d )
{
    tensor<float,host_memory_space> R(extents[5][4][3]);

    tensor<float,dev_memory_space>  Md(extents[5][4][3]);
    tensor<float,host_memory_space> Mh(extents[5][4][3]);
    tensor<float,dev_memory_space>  vd(extents[3]);
    tensor<float,host_memory_space> vh(extents[3]);

    sequence(R);
    sequence(Md);
    sequence(Mh);
    sequence(vd);
    sequence(vh);

	matrix_plus_row(Md,vd);
	matrix_plus_row(Mh,vh);
	for(int i=0;i<5;i++){
        for(int j=0;j<4;j++){
            for(int k=0;k<3;k++){
                BOOST_CHECK_CLOSE((float)Md(i,j,k), (float)Mh(i,j,k), 0.01);
                BOOST_CHECK_CLOSE((float)Mh(i,j,k), (float)R(i,j,k) + (float)vh(k), 0.01);
            }
        }
	}
}
BOOST_AUTO_TEST_CASE( mat_op_mat_plus_col_3d )
{
    tensor<float,host_memory_space> R(extents[5][4][3]);

    tensor<float,dev_memory_space>  Md(extents[5][4][3]);
    tensor<float,host_memory_space> Mh(extents[5][4][3]);
    tensor<float,dev_memory_space>  vd(extents[5]);
    tensor<float,host_memory_space> vh(extents[5]);

    sequence(R);
    sequence(Md);
    sequence(Mh);
    sequence(vd);
    sequence(vh);

	matrix_plus_col(Md,vd);
	matrix_plus_col(Mh,vh);
	for(int i=0;i<5;i++){
        for(int j=0;j<4;j++){
            for(int k=0;k<3;k++){
                BOOST_CHECK_CLOSE((float)Md(i,j,k), (float)Mh(i,j,k), 0.01);
                BOOST_CHECK_CLOSE((float)Mh(i,j,k), (float)R(i,j,k) + (float)vh(i), 0.01);
            }
        }
	}
}
BOOST_AUTO_TEST_CASE( mat_op_mat_plus_col )
{
	sequence(v); sequence(w);
	sequence(x); sequence(z);
	tensor<float,dev_memory_space>   v_vec(n); sequence(v_vec);
	tensor<float,host_memory_space>  x_vec(n); sequence(x_vec);
	matrix_plus_col(v,v_vec);
	matrix_plus_col(x,x_vec);
	for(int i=0;i<n;i++){
		for(int j=0;j<n;j++){
			BOOST_CHECK_CLOSE((float)v(i,j), (float)x(i,j), 0.01);
			BOOST_CHECK_CLOSE((float)v(i,j), w(i,j)+v_vec[i], 0.01);
			BOOST_CHECK_CLOSE((float)x(i,j), z(i,j)+x_vec[i], 0.01);
		}
	}

}

BOOST_AUTO_TEST_CASE( mat_op_mat_plus_vec_row_major )
{
	tensor<float,dev_memory_space,row_major> V(v.shape()); sequence(V);
	tensor<float,host_memory_space,row_major> X(x.shape()); sequence(X);
	tensor<float,dev_memory_space,row_major> W(v.shape()); sequence(W);
	tensor<float,host_memory_space,row_major> Z(x.shape()); sequence(Z);
	tensor<float,dev_memory_space>   v_vec(n); sequence(v_vec);
	tensor<float,host_memory_space>  x_vec(n); sequence(x_vec);
	matrix_plus_col(V,v_vec);
	matrix_plus_col(X,x_vec);
	for(int i=0;i<n;i++){
		for(int j=0;j<n;j++){
			BOOST_CHECK_CLOSE((float)V(i,j), (float)X(i,j), 0.01);
			BOOST_CHECK_CLOSE((float)V(i,j), W(i,j)+v_vec[i], 0.01);
			BOOST_CHECK_CLOSE((float)X(i,j), Z(i,j)+x_vec[i], 0.01);
		}
	}

}

/*
BOOST_AUTO_TEST_CASE( mat_op_big_reduce_to_col )
{
	sequence(d_reduce_big);
	sequence(h_reduce_big);
	tensor<float,dev_memory_space>  v_col(32); sequence(v_col);
	tensor<float,host_memory_space> x_col(32); sequence(x_col);
	reduce_to_col(v_col,d_reduce_big,RF_ADD,1.f,0.5f);
	reduce_to_col(x_col,h_reduce_big,RF_ADD,1.f,0.5f);
	for(int i=0;i<32;i++){
		float v_correct = 0;
		for(int j=0;j<big_images;j++)
			v_correct += d_reduce_big(i,j);
		BOOST_CHECK_CLOSE(v_correct,v_col[i],0.01);
		BOOST_CHECK_CLOSE(v_col[i],x_col[i],0.01);
	}
}
*/


BOOST_AUTO_TEST_CASE( mat_op_divide_col )
{
	sequence(v);
	sequence(x);
	sequence(z);
	tensor<float,dev_memory_space>  v_col(n); sequence(v_col); apply_scalar_functor(v_col, SF_ADD, 1.0f);
	tensor<float,host_memory_space> x_col(n); sequence(x_col); apply_scalar_functor(x_col, SF_ADD, 1.0f);

	matrix_divide_col(v, v_col);
	matrix_divide_col(x, x_col);

	for(int i=0;i<n;i++)
		for(int j=0; j<n; j++) {
			BOOST_CHECK_CLOSE((float)v(i,j),(float)x(i,j),0.01);
			BOOST_CHECK_CLOSE((float)x(i,j),z(i,j)/x_col[i],0.01);
		}
}


/*
BOOST_AUTO_TEST_CASE( mat_op_reduce_big_rm_to_row )
{
	tensor<float,dev_memory_space,row_major> dA(32, 1179648);
	tensor<float,dev_memory_space> dV(1179648);
	tensor<float,host_memory_space,row_major> hA(32, 1179648);
	tensor<float,host_memory_space> hV(1179648);

	sequence(dA);
	sequence(dV);
	sequence(hA);
	sequence(hV);

	reduce_to_row(dV,dA,RF_ADD, 1.0f, 1.0f);
	reduce_to_row(hV,hA,RF_ADD, 1.0f, 1.0f);

	tensor<float,host_memory_space> hV2(dV.size());
	convert(hV2, dV);

	for(int i=0;i<1179648;i++){
		BOOST_CHECK_CLOSE(hV[i],hV2[i],0.01);
	}

}
*/

BOOST_AUTO_TEST_CASE( mat_op_view )
{
	tensor<float,host_memory_space,column_major>* h2 = blockview(x,(int)0,(int)n,(int)1,(int)2);
	tensor<float,dev_memory_space,column_major>*  d2 = blockview(v,(int)0,(int)n,(int)1,(int)2);
	sequence(x);
	sequence(v);
	BOOST_CHECK_EQUAL(h2->shape()[0], x.shape()[0]);
	BOOST_CHECK_EQUAL(d2->shape()[0], x.shape()[0]);
	BOOST_CHECK_EQUAL(h2->shape()[1], 2);
	BOOST_CHECK_EQUAL(d2->shape()[1], 2);
	for(int i=0;i<n;i++)
		for(int j=0;j<2;j++){
			BOOST_CHECK_CLOSE((float)(*h2)(i,j),(float)(*d2)(i,j),0.01);
			BOOST_CHECK_CLOSE((float)(*h2)(i,j),(float)x(i, j+1),0.01);
		}
}

template<class T, class M, class R>
void test_matrix_transpose(unsigned int n, unsigned int m){
    tensor<T,M,R> src(extents[n][m]), dst(extents[m][n]);
    dst = (T)0;
    //sequence(src);
    fill_rnd_uniform(src);
    transpose(dst,src);
    for(unsigned int i = 0;i<n;i++)
        for (unsigned int j = 0; j < m; ++j)
        {
            BOOST_CHECK_EQUAL(src(i,j),dst(j,i));
        }
}


BOOST_AUTO_TEST_CASE( mat_op_transpose )
{
for(unsigned int iter=0;iter<1;iter++){
    int nm[] = {2,8,15,24,176};
    for(unsigned int ni = 0; ni<5;ni++)
        for(unsigned int mi = 0; mi<5;mi++){
            //unsigned int n= nm[ni];
            //unsigned int m= nm[mi];
            unsigned int n = 1 + drand48()*50;
            unsigned int m = 1 + drand48()*50;
            //std::cout << "Transpose: "<<n << ", "<<m<<std::endl;
            test_matrix_transpose<float,host_memory_space,column_major>(n,m);
            test_matrix_transpose<float,host_memory_space,column_major>(m,n);
            test_matrix_transpose<float,host_memory_space,row_major>(n,m);
            test_matrix_transpose<float,host_memory_space,row_major>(m,n);

            test_matrix_transpose<float,dev_memory_space,column_major>(n,m);
            test_matrix_transpose<float,dev_memory_space,column_major>(m,n);
            test_matrix_transpose<float,dev_memory_space,row_major>(n,m);
            test_matrix_transpose<float,dev_memory_space,row_major>(m,n);
        }
}
}


BOOST_AUTO_TEST_CASE( logaddexp_reduce ){
	const int n = 25;
	const int m = 1;

	tensor<float,host_memory_space> A(extents[m][n]);
	tensor<float,host_memory_space> r(extents[m]);
	sequence(A);
	A /= 25.f;
	reduce_to_col(r,A,RF_LOGADDEXP);
	double sum = 0;
	for(int i=0;i<25;i++){
		sum += exp(((double)i)/25.0);
	}
	//sum = log(sum);
	BOOST_CHECK_CLOSE(sum,exp((float)r[0]), 0.01);
}
BOOST_AUTO_TEST_CASE( all_reduce )
{
	const int n = 270;
	const int m = 270;

	std::list<reduce_functor> rf_arg;
	std::list<reduce_functor> rf_val;
	std::list<reduce_functor> rf_rp; // reduced precision
	rf_arg += RF_ARGMAX, RF_ARGMIN;
	rf_val += RF_ADD, RF_MAX, RF_MIN, RF_LOGADDEXP;
	rf_rp  += RF_LOGADDEXP, RF_MULT;

	for(int dim=0;dim<2;dim++){ 
	if(1){ // column-major
		std::cout << "Column Major"<<std::endl;
		tensor<float,dev_memory_space,column_major>  dA(extents[n][m]);
		fill_rnd_uniform(dA);
		dA *= 2.f;

		for(std::list<reduce_functor>::iterator it=rf_arg.begin(); it!=rf_arg.end(); it++)
		{   std::cout << "Functor: "<<(*it)<<std::endl;
			std::pair<tensor<unsigned int,host_memory_space>*,
				tensor<unsigned int,host_memory_space>*> p = test_reduce<unsigned int>(dim,dA,*it);
			for(unsigned int i=0; i<m; i++) {
				BOOST_CHECK_EQUAL((*p.first)[i], (*p.second)[i]);
			}
			delete p.first; delete p.second;
		}
		for(std::list<reduce_functor>::iterator it=rf_val.begin(); it!=rf_val.end(); it++)
		{   std::cout << "Functor: "<<(*it)<<std::endl;
		    std::pair<tensor<float,host_memory_space>*,
				tensor<float,host_memory_space>*> p = test_reduce<float>(dim,dA,*it);
			const float prec = find(rf_rp.begin(), rf_rp.end(), *it)==rf_rp.end() ? 0.1f : 4.5f;
			for(unsigned int i=0; i<m; i++) {
				BOOST_CHECK_CLOSE((float)(*p.first)[i], (float)(*p.second)[i],prec);
			}
			delete p.first; delete p.second;
		}
	}
	if(1){ // row-major
		std::cout << "Row Major"<<std::endl;
		tensor<float,dev_memory_space,row_major>  dA(extents[n][m]);
		fill_rnd_uniform(dA);
		dA *= 2.f;

		for(std::list<reduce_functor>::iterator it=rf_arg.begin(); it!=rf_arg.end(); it++)
		{   std::cout << "Functor: "<<(*it)<<std::endl;
		    std::pair<tensor<unsigned int,host_memory_space>*,
				tensor<unsigned int,host_memory_space>*> p = test_reduce<unsigned int>(dim,dA,*it);
			for(unsigned int i=0; i<m; i++) {
				BOOST_CHECK_EQUAL((*p.first)[i], (*p.second)[i]);
			}
			delete p.first; delete p.second;
		}
		for(std::list<reduce_functor>::iterator it=rf_val.begin(); it!=rf_val.end(); it++)
		{   std::cout << "Functor: "<<(*it)<<std::endl;
		    std::pair<tensor<float,host_memory_space>*,
				tensor<float,host_memory_space>*> p = test_reduce<float>(dim,dA,*it);
			const float prec = find(rf_rp.begin(), rf_rp.end(), *it)==rf_rp.end() ? 0.1f : 4.5f;
			for(unsigned int i=0; i<m; i++) {
				BOOST_CHECK_CLOSE((float)(*p.first)[i], (float)(*p.second)[i], prec);
			}
			delete p.first; delete p.second;
		}
	}
	}
}
BOOST_AUTO_TEST_SUITE_END()
