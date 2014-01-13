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
#include <cuv/tools/cuv_general.hpp>
#include <cuv/basics/tensor.hpp>
using namespace cuv;

struct MyConfig {
	static const int dev = CUDA_TEST_DEVICE;
	MyConfig()   { 
		printf("Testing on device=%d\n",dev);
		initCUDA(dev); 
	}
	~MyConfig()  { exitCUDA();  }
};

BOOST_GLOBAL_FIXTURE( MyConfig );

struct Fix{
	Fix()
	{
	}
	~Fix(){
	}
};

BOOST_FIXTURE_TEST_SUITE( s, Fix )

/** 
 * @test
 * @brief create tensor
 */
BOOST_AUTO_TEST_CASE( create_tensor )
{
	// column_major
	tensor<float,host_memory_space,column_major> m(extents[2][3][4]);
	BOOST_CHECK_EQUAL(24,m.size());
	BOOST_CHECK_EQUAL(2ul,m.shape()[0]);
	BOOST_CHECK_EQUAL(3ul,m.shape()[1]);
	BOOST_CHECK_EQUAL(4ul,m.shape()[2]);

	BOOST_CHECK_EQUAL(0ul,m.index_of(extents[0][0][0]));  // column major test
	BOOST_CHECK_EQUAL(1ul,m.index_of(extents[1][0][0]));
	BOOST_CHECK_EQUAL(2ul,m.index_of(extents[0][1][0]));


	// row_major
	tensor<float,host_memory_space,row_major> n(extents[2][3][4]);
	BOOST_CHECK_EQUAL(24,m.size());
	BOOST_CHECK_EQUAL(2ul,n.shape()[0]);
	BOOST_CHECK_EQUAL(3ul,n.shape()[1]);
	BOOST_CHECK_EQUAL(4ul,n.shape()[2]);

	BOOST_CHECK_EQUAL(0ul,n.index_of(extents[0][0][0]));  // row major test
	BOOST_CHECK_EQUAL(1ul,n.index_of(extents[0][0][1]));
	BOOST_CHECK_EQUAL(2ul,n.index_of(extents[0][0][2]));
	BOOST_CHECK_EQUAL(4ul,n.index_of(extents[0][1][0]));
}

BOOST_AUTO_TEST_CASE( tensor_data_access )
{
	tensor<float,host_memory_space,column_major> m(extents[2][3][4]);
	tensor<float,host_memory_space,row_major>    n(extents[2][3][4]);

	tensor<float,host_memory_space,column_major> o(extents[2][3][4]);
	tensor<float,host_memory_space,row_major>    p(extents[2][3][4]);
	for (int i = 0; i < 2; ++i) {
		for (int j = 0; j < 3; ++j) {
			for (int k = 0; k < 4; ++k) {
				m(i,j,k)=i*j+k;
				n(i,j,k)=i*j+k;

				o(i,j,k)=i*j+k;
				p(i,j,k)=i*j+k;
			}
		}
	}
	BOOST_CHECK_EQUAL(1*2+3,m(1,2,3));
	BOOST_CHECK_EQUAL(1*2+3,n(1,2,3));
	BOOST_CHECK_EQUAL(1*2+3,o(1,2,3));
	BOOST_CHECK_EQUAL(1*2+3,p(1,2,3));

	BOOST_CHECK_EQUAL(1*2+3-1,--p(1,2,3));
	BOOST_CHECK_EQUAL(1*2+3,  p(1,2,3)+=1);
}

BOOST_AUTO_TEST_CASE( tensor_assignment )
{
	tensor<float,host_memory_space,column_major> m(extents[2][3][4]);
	tensor<float,host_memory_space,column_major> n(extents[2][3][4]);

	tensor<float,host_memory_space,column_major> o(extents[2][3][4]);

	for (int i = 0; i < 2*3*4; ++i)
		m[i] = i;
	n = m;
	o = m;

	tensor<float,host_memory_space,column_major> s(n);
	tensor<float,dev_memory_space,column_major> t(n);

	for (int i = 0; i < 2*3*4; ++i){
		BOOST_CHECK_EQUAL(m[i], i);
		BOOST_CHECK_EQUAL(n[i], i);
		BOOST_CHECK_EQUAL(o[i], i);
		BOOST_CHECK_EQUAL(s[i], i);
		BOOST_CHECK_EQUAL(t[i], i);
	}


}

BOOST_AUTO_TEST_CASE( tensor_copy ) {
    boost::shared_ptr<allocator> allocator(new pooled_cuda_allocator("tensor_copy"));
    tensor<float, host_memory_space> x(extents[4][5][6], allocator);
    for (int i = 0; i < 4 * 5 * 6; i++) {
        x[i] = i;
    }

    tensor<float, host_memory_space> y = x.copy();
    BOOST_CHECK_NE(x.ptr(), y.ptr());

    for (int i = 0; i < 4; i++) {
        BOOST_CHECK_NE(x[indices[i][index_range()][index_range()]].ptr(),
                y[indices[i][index_range()][index_range()]].ptr());
    }

    tensor<float, host_memory_space> y2(x.copy());
    BOOST_CHECK_NE(x.ptr(), y2.ptr());

    for (int i = 0; i < 4; i++) {
        BOOST_CHECK_NE(x[indices[i][index_range()][index_range()]].ptr(),
                y2[indices[i][index_range()][index_range()]].ptr());
    }

    for (int i = 0; i < 4 * 5 * 6; i++) {
        BOOST_CHECK_EQUAL(x[i], y[i]);
        y[i]++; // change must not change original!
        BOOST_CHECK_NE(x[i], y[i]);
    }
}

BOOST_AUTO_TEST_CASE( tensor_zero_copy_assignment )
{
    tensor<float,host_memory_space> x(extents[4][5][6]);
    for(int i=0;i<4*5*6;i++)
        x[i] = i;
    tensor<float,host_memory_space> y = x;
    for(int i=0;i<4*5*6;i++)
    {
        BOOST_CHECK_EQUAL(x[i], y[i]);
        y[i] = i+1; // change the copy results in change of original!
        BOOST_CHECK_EQUAL(x[i], y[i]);
    }
}

BOOST_AUTO_TEST_CASE( cuv_slice1col ) {
    tensor<float, host_memory_space> y;
    tensor<float, host_memory_space> x(extents[4][6]);

    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 6; ++k) {
            x(i, k) = i + k;
        }
    }

    // accessing strided memory
    y = x[indices[index_range(0,1)][index_range()]];
    for (int i = 0; i < 1; ++i) {
        for (int k = 0; k < 6; ++k) {
            BOOST_CHECK_EQUAL(y(i,k), i+k);
        }
    }
    x[indices[index_range(0,1)][index_range()]] = y; // reassign
}

BOOST_AUTO_TEST_CASE( cuv_slice1row ) {
    tensor<float, host_memory_space> y;
    tensor<float, host_memory_space> x(extents[4][6]);

    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 6; ++k) {
            x(i, k) = i + k;
        }
    }

    // accessing strided memory
    y = x[indices[index_range()][index_range(0,1)]];
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 1; ++k) {
            BOOST_CHECK_EQUAL(y(i,k), i+k);
        }
    }
    x[indices[index_range()][index_range(0,1)]] = y; // reassign
}

BOOST_AUTO_TEST_CASE( tensor_out_of_scope_view )
{
    // subtensor views should persist when original tensor falls out of scope
    tensor<float,host_memory_space> y;
    {
        tensor<float,host_memory_space> x(extents[4][5][6]);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 5; ++j)
                for (int k = 0; k < 6; ++k)
                    x(i,j,k) = i+j+k;
        y = x[indices[index_range(1,3)][index_range()][index_range()]];
    }
    for (int i = 1; i < 3; ++i)
        for (int j = 0; j < 5; ++j)
            for (int k = 0; k < 6; ++k)
                {
                    BOOST_CHECK_EQUAL(y(i-1,j,k), i+j+k);
                }
}

BOOST_AUTO_TEST_CASE( tensor_memcpy2d )
{
    tensor<float,host_memory_space> y;
    tensor<float,host_memory_space> x(extents[4][5][6]);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 5; ++j)
            for (int k = 0; k < 6; ++k)
                x(i,j,k) = i+j+k;

    // accessing strided memory
    y = x[indices[index_range()][index_range()][index_range(0,1)]];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 5; ++j)
            for (int k = 0; k < 1; ++k)
                {
                    BOOST_CHECK_EQUAL(y(i,j,k), i+j+k);
                }
    // copying strided memory
    y = y.copy(); // y in R^(4,5,1)
    for(int k=0;k<y.size();k++) // avoid fill() dependency in this file (speed up compiling...)
        y[k] = 0.f;
    tensor_view<float,host_memory_space> m(x,indices[index_range()][index_range()][index_range(0,1)]);
    m = y;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 5; ++j)
            for (int k = 0; k < 1; ++k)
                {
                    if(k!=0) {
                        BOOST_CHECK_EQUAL(x(i,j,k), i+j+k);
                    }else{
                        BOOST_CHECK_EQUAL(x(i,j,k), 0.f);
                    }
                }
}

template<class V,class M1,class M2>
void test_pushpull_2d()
{
	static const int h=123,w=247;
	tensor<V,M1,row_major> t1;
	tensor<V,M2,row_major> t2(extents[h][w]);
	
	for(int i=0;i<h; i++)
		for(int j=0;j<w; j++){
			t2(i,j) = (float) drand48();
		}
	t1 = t2;
	BOOST_CHECK(equal_shape(t1,t2));
	for(int i=0;i<h; i++)
		for(int j=0;j<w; j++)
			BOOST_CHECK_EQUAL( (V) t1(i,j) , (V) t2(i,j) );
}
template<class V,class M1,class M2>
void test_pushpull_3d()
{
	static const int d=3,h=123,w=247;
	tensor<V,M1,row_major> t1;
	tensor<V,M2,row_major> t2(extents[d][h][w]);
	
	// ***************************************
	// assignment 2D --> 1D
	// ***************************************
	for(int k=0;k<d;k++)
		for(int i=0;i<h; i++)
			for(int j=0;j<w; j++){
				t2(k,i,j) = (float) drand48();
			}
	t1 = t2;
	BOOST_CHECK(equal_shape(t1,t2));
	for (int k = 0; k < d; ++k)
		for(int i=0;i<h; i++)
			for(int j=0;j<w; j++)
				BOOST_CHECK_EQUAL( (V) t1(k,i,j) , (V) t2(k,i,j) );

}

template<class V, class M>
void test_lowdim_views(){
	static const int d=3,h=123,w=247;
	tensor<V,M,row_major>              t1d(extents[d][h][w]);
	tensor<V,M,row_major> t2d(extents[d][h][w]);

	for(int k=0;k<d;k++)
		for(int i=0;i<h; i++)
			for(int j=0;j<w; j++){
				t2d(k,i,j) = (float) drand48();
			}

	// ***************************************
	// 2D View on 3D tensor
	// ***************************************
	for(int k=0;k<d;++k){
		tensor_view<V,M,row_major> view(indices[k][index_range(0,h)][index_range(0,w)], t2d);
		BOOST_CHECK_EQUAL(  view.ndim() , 2);
		BOOST_CHECK_EQUAL(  view.shape(0) , h);
		BOOST_CHECK_EQUAL(  view.shape(1) , w);
		for(int i=0;i<h; i++)
			for(int j=0;j<w; j++)
				BOOST_CHECK_EQUAL( (V) view(i,j) , (V) t2d(k,i,j) );

		// alternative spec
		tensor_view<V,M,row_major> view_(indices[k][index_range()][index_range()<cuv::index(w)], t2d);
		BOOST_CHECK_EQUAL(  view_.ndim() , 2);
		BOOST_CHECK_EQUAL(  view_.shape(0) , h);
		BOOST_CHECK_EQUAL(  view_.shape(1) , w);
		for(int i=0;i<h; i++)
			for(int j=0;j<w; j++)
				BOOST_CHECK_EQUAL( (V) view_(i,j) , (V) t2d(k,i,j) );
	}

	// ***************************************
	// 1D View on 3D tensor
	// ***************************************
	for(int k=0;k<d;++k){
		for (int i = 0; i < h; ++i) {
		       tensor_view<V,M,row_major> view(indices[k][i][index_range(0,w)], t2d);
		       for(int j=0;j<w; j++)
			      BOOST_REQUIRE_EQUAL( (V) view(j) , (V) t2d(k,i,j) );
		}
	}
}

BOOST_AUTO_TEST_CASE( lowdim_views ) {
	test_lowdim_views<float,host_memory_space>();
	test_lowdim_views<float,dev_memory_space>();
}







template<class V, class M>
void test_lowdim_sub_views(){
    {
        static const int d=3,h=123,w=247;
        tensor<V,M,row_major> t2d(extents[d][h][w]);

        for(int k=0;k<d;k++)
            for(int i=0;i<h; i++)
                for(int j=0;j<w; j++){
                    t2d(k,i,j) = (float) drand48();
                }

        // ***************************************
        // 2D View on 3D tensor
        // ***************************************

        {
            tensor_view<V,M,row_major> view(indices[2], t2d);
            BOOST_CHECK_EQUAL(  view.ndim() , 2);
            BOOST_CHECK_EQUAL(  view.shape(0) , h);
            BOOST_CHECK_EQUAL(  view.shape(1) , w);
            BOOST_CHECK_EQUAL(  view.stride(0) , 247);
            BOOST_CHECK_EQUAL(  view.stride(1) , 1);
            for(int i=0;i<h; i++)
                for(int j=0;j<w; j++)
                    BOOST_CHECK_EQUAL( (V) view(i,j) , (V) t2d(2,i,j) );

        }
        {
            tensor_view<V,M,row_major> view(indices[2][5], t2d);
            BOOST_CHECK_EQUAL(  view.ndim() , 1);
            BOOST_CHECK_EQUAL(  view.shape(0) , w);
            for(int j=0;j<w; j++)
                BOOST_CHECK_EQUAL( (V) view(j) , (V) t2d(2,5,j) );

        }
        {
            tensor_view<V,M,row_major> view(indices[cuv::index_range(0,2)], t2d);
            BOOST_CHECK_EQUAL(  view.ndim() , 3);
            BOOST_CHECK_EQUAL(  view.shape(0) , 2);
            BOOST_CHECK_EQUAL(  view.shape(1) , h);
            BOOST_CHECK_EQUAL(  view.shape(2) , w);
            for(int k=0;k<2;k++)
                for(int i=0;i<h; i++)
                    for(int j=0;j<w; j++)
                        BOOST_CHECK_EQUAL( (V) view(k,i,j) , (V) t2d(k,i,j) );
        }
        {
            tensor_view<V,M,row_major> view(indices[cuv::index_range(0,2)][cuv::index_range(0,50)], t2d);
            BOOST_CHECK_EQUAL(  view.ndim() , 3);
            BOOST_CHECK_EQUAL(  view.shape(0) , 2);
            BOOST_CHECK_EQUAL(  view.shape(1) , 50);
            BOOST_CHECK_EQUAL(  view.shape(2) , w);
            for(int k=0;k<2;k++)
                for(int i=0;i<50; i++)
                    for(int j=0;j<w; j++)
                        BOOST_CHECK_EQUAL( (V) view(k,i,j) , (V) t2d(k,i,j) );
        }
        {
            tensor_view<V,M,row_major> view(indices[2][cuv::index_range(0,50)], t2d);
            BOOST_CHECK_EQUAL(  view.ndim() , 2);
            BOOST_CHECK_EQUAL(  view.shape(0) , 50);
            BOOST_CHECK_EQUAL(  view.shape(1) , w);
            for(int i=0;i<50; i++)
                for(int j=0;j<w; j++)
                    BOOST_CHECK_EQUAL( (V) view(i,j) , (V) t2d(2,i,j) );
        }
        {
            tensor_view<V,M,row_major> view(indices[cuv::index_range(0,2)][50], t2d);
            BOOST_CHECK_EQUAL(  view.ndim() , 2);
            BOOST_CHECK_EQUAL(  view.shape(0) , 2);
            BOOST_CHECK_EQUAL(  view.shape(1) , w);
            for(int i=0;i<2; i++)
                for(int j=0;j<w; j++)
                    BOOST_CHECK_EQUAL( (V) view(i,j) , (V) t2d(i,50,j) );
        }
        {
            tensor_view<V,M,row_major> view(indices[cuv::index_range()][cuv::index_range(0,50)], t2d);
            BOOST_CHECK_EQUAL(  view.ndim() , 3);
            BOOST_CHECK_EQUAL(  view.shape(0) , d);
            BOOST_CHECK_EQUAL(  view.shape(1) , 50);
            BOOST_CHECK_EQUAL(  view.shape(2) , w);
            BOOST_CHECK_EQUAL(  view.stride(0) , 123*247);
            BOOST_CHECK_EQUAL(  view.stride(1) , 247);
            BOOST_CHECK_EQUAL(  view.stride(2) , 1);
            for(int k=0;k<d;k++)
                for(int i=0;i<50; i++)
                    for(int j=0;j<w; j++)
                        BOOST_CHECK_EQUAL( (V) view(k,i,j) , (V) t2d(k,i,j) );
        }
    }

        // ***************************************
        // 2D  tensor
        // ***************************************
    {
        static const int d=8,h=16;
        tensor<V,M,row_major> t2d(extents[d][h]);

        for(int k=0;k<d;k++)
            for(int i=0;i<h; i++){
                t2d(k,i) = (float) drand48();
            }

        tensor_view<V,M,row_major> view(indices[cuv::index_range(0,4)], t2d);
        BOOST_CHECK_EQUAL(  view.ndim() , 2);
        BOOST_CHECK_EQUAL(  view.shape(0) , 4);
        BOOST_CHECK_EQUAL(  view.shape(1) , h);
        BOOST_CHECK_EQUAL(  view.stride(0) , 16);
        BOOST_CHECK_EQUAL(  view.stride(1) , 1);
        for(int i=0;i<4; i++)
            for(int j=0;j<h; j++)
                BOOST_CHECK_EQUAL( (V) view(i,j) , (V) t2d(i,j) );

    }

}


BOOST_AUTO_TEST_CASE( lowdim_sub_views ) {
	test_lowdim_sub_views<float,host_memory_space>();
	test_lowdim_sub_views<float,dev_memory_space>();
}



BOOST_AUTO_TEST_CASE( tensor_wrapping ){
    {
        std::vector<float>    v_orig(10, 0.f);
        tensor<float,host_memory_space> v(extents[10], &v_orig[0]);
        tensor<float,host_memory_space> w(extents[10]);
        for(unsigned int i=0;i<10;i++)
            w[i] = 1.f;
    
        // overwrite the wrapped memory (needs copying)
        v = w;
    }
    {
        std::vector<float>    v_orig(10, 0.f);
        tensor<float,host_memory_space> v(extents[10], &v_orig[0]);
        tensor<float,dev_memory_space> w(extents[10]);
        for(unsigned int i=0;i<10;i++)
            w[i] = 1.f;
    
        // overwrite the wrapped memory (needs copying)
        v = w;
    }
}

BOOST_AUTO_TEST_CASE( pushpull_nd )
{
	// same memory space, linear container
	test_pushpull_2d<float,host_memory_space,host_memory_space>();
	test_pushpull_2d<float, dev_memory_space, dev_memory_space>();

	// same memory space, 2d container
	test_pushpull_2d<float,host_memory_space,host_memory_space>();
	test_pushpull_2d<float, dev_memory_space, dev_memory_space>();

	// same memory space, 2d vs. 1d
	test_pushpull_2d<float,host_memory_space,host_memory_space>();
	test_pushpull_2d<float, dev_memory_space, dev_memory_space>();
	test_pushpull_2d<float,host_memory_space,host_memory_space>();
	test_pushpull_2d<float, dev_memory_space, dev_memory_space>();
}

//BOOST_AUTO_TEST_CASE( tensor_value_convert )
//{
//    {
//        tensor<float,host_memory_space> t(100);
//        for(int i=0;i<100;i++) t[i] = (float) i;
//        tensor<int,host_memory_space> t_int;
//        t_int = t;
//        for(int i=0;i<100;i++){
//            BOOST_CHECK_EQUAL((float)t[i], (float)(int)t_int[i]);
//        }
//    }
//    {
//        tensor<float,dev_memory_space> t(100);
//        for(int i=0;i<100;i++) t[i] = (float) i;
//        tensor<int,dev_memory_space> t_int;
//        t_int = t;
//        for(int i=0;i<100;i++){
//            BOOST_CHECK_EQUAL((float)t[i], (float)(int)t_int[i]);
//        }
//    }
//}

template<class V, class M>
void test_resize() {

    // resize with default allocator
    tensor<V, M, row_major> a(100, 100);
    V* p0 = a.ptr();
    a.resize(100, 100);
    BOOST_CHECK_EQUAL(p0, a.ptr());
    // no size change. pointer must not change

    boost::shared_ptr<pooled_cuda_allocator> allocator(new pooled_cuda_allocator("test_resize"));
    {
        tensor<V, M, row_major> a(200, 300, allocator);

        BOOST_CHECK_EQUAL(a.shape(0), 200);
        BOOST_CHECK_EQUAL(a.shape(1), 300);

        BOOST_CHECK_EQUAL(allocator->pool_count(M()), 1);
        BOOST_CHECK_EQUAL(allocator->pool_free_count(M()), 0);
        BOOST_CHECK_EQUAL(allocator->pool_size(M()), 200 * 300 * sizeof(V));

        a.resize(100, 100);

        // make sure the memory is freed before new memory is allocated

        BOOST_CHECK_EQUAL(allocator->pool_count(M()), 1);
        BOOST_CHECK_EQUAL(allocator->pool_free_count(M()), 0);
        BOOST_CHECK_EQUAL(allocator->pool_size(M()), 200 * 300 * sizeof(V));

        BOOST_CHECK_EQUAL(a.shape(0), 100);
        BOOST_CHECK_EQUAL(a.shape(1), 100);
    }

    BOOST_CHECK_EQUAL(allocator->pool_count(M()), 1);
    BOOST_CHECK_EQUAL(allocator->pool_free_count(M()), 1);
}
BOOST_AUTO_TEST_CASE( tensor_resize ) {
    test_resize<float, host_memory_space>();
    test_resize<float, dev_memory_space>();
}

BOOST_AUTO_TEST_SUITE_END()
