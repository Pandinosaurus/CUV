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
	tensor<float,column_major,host_memory_space> m(extents[2][3][4]);
	BOOST_CHECK_EQUAL(24,m.size());
	BOOST_CHECK_EQUAL(2ul,m.shape()[0]);
	BOOST_CHECK_EQUAL(3ul,m.shape()[1]);
	BOOST_CHECK_EQUAL(4ul,m.shape()[2]);

	BOOST_CHECK_EQUAL(0ul,m.index_of(extents[0][0][0]));  // column major test
	BOOST_CHECK_EQUAL(1ul,m.index_of(extents[1][0][0]));
	BOOST_CHECK_EQUAL(2ul,m.index_of(extents[0][1][0]));


	// row_major
	tensor<float,row_major,host_memory_space> n(extents[2][3][4]);
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
	tensor<float,column_major,host_memory_space> m(extents[2][3][4]);
	tensor<float,row_major,host_memory_space>    n(extents[2][3][4]);

	tensor<float,column_major,host_memory_space> o(extents[2][3][4]);
	tensor<float,row_major,host_memory_space>    p(extents[2][3][4]);
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

BOOST_AUTO_TEST_SUITE_END()