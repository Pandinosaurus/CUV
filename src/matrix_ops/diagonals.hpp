#include <basics/dia_matrix.hpp>

namespace cuv{
	
	/**
	 * get the average of all diagonals in a vector
	 * 
	 * @param dst the vector where the results are stored in
	 * @param dia the diagonal matrix where the diagonals are supposed to be summed
	 */
	template<class T, class M, class I>
	void avg_diagonals( cuv::vector<T,M,I>& dst, const cuv::dia_matrix<T,M>& dia );
}
