#ifndef _UTILS_H_
#define _UTILS_H_

#include <glog/logging.h>
#include <mkldnn.hpp>
#include <iostream>
#include "op_param.h"
#include "omp.h"
using namespace mkldnn;

memory::format get_desired_format(int channel);

template<typename T>
void eltwise_multiply(T* x1, T* x2, T* y, size_t n) {
#pragma omp parallel for schedule(static)
    for (size_t i = 0; i < n; ++i) {
        y[i] = x1[i] * x2[i];
    }
}

//
//// map C type with mkldnn's
//// float -> memory::data_type::f32
//// int -> memory::data_type::s32
//// int16_t -> memory::data_type::s16
//// int8_t -> memory::data_type::s8
//// uint8_t -> memory::data_type::u8
//
template<typename T>
static inline mkldnn::memory::data_type memory_data_type() {
    if (typeid(T) == typeid(float))
        return mkldnn::memory::data_type::f32;
    else if (typeid(T) == typeid(int))
        return mkldnn::memory::data_type::s32;
    else if (typeid(T) == typeid(int16_t))
        return mkldnn::memory::data_type::s16;
    else if (typeid(T) == typeid(int8_t))
        return mkldnn::memory::data_type::s8;
    else if (typeid(T) == typeid(uint8_t))
        return mkldnn::memory::data_type::u8;

    LOG(ERROR) << "Not support type";
    return mkldnn::memory::data_type::data_undef;
}

// utils function conver int/double/bool/dims/ to string
static inline std::string int_to_string(int value) {
    std::ostringstream os;
    os << std::hex << "I" << value << "_";
    return os.str();
}

static inline std::string double_to_string(double value) {
    std::ostringstream os;
    os << "D" << value << "_";
    return os.str();
}

static inline std::string float_to_string(float value) {
    std::ostringstream os;
    os << "F" << value << "_";
    return os.str();
}

static inline std::string bool_to_string(bool value) {
    std::ostringstream os;
    os << "B" << value << "_";
    return os.str();
}

static inline std::string dims_to_string(mkldnn::memory::dims dims) {
   std::ostringstream os;
   os << "DIMS:";
   for (int i = 0; i < dims.size(); i++)
       os << dims[i] << ",";
   os << ";";
   return os.str();
}

static inline std::string long_to_string(size_t value) {
    std::ostringstream os;
    os << std::hex << "L" << value << "_";
    return os.str();
}

static inline mkldnn::algorithm pooling_algo_convert(pooling_param_t::algorithm input) {
    switch(input) {
        case pooling_param_t::algorithm::pooling_max:
            return mkldnn::pooling_max;
        case pooling_param_t::algorithm::pooling_avg:
            return mkldnn::pooling_avg;
        case pooling_param_t::algorithm::pooling_avg_include_padding:
            return mkldnn::pooling_avg_include_padding;
        case pooling_param_t::algorithm::pooling_avg_exclude_padding:
            return mkldnn::pooling_avg_exclude_padding;
        default:
            LOG(ERROR) << "Not a valid pooling algo";
            return mkldnn::pooling_max;
    }
}

static inline mkldnn::algorithm lrn_algo_convert(lrn_param_t::algorithm input) {
    switch(input) {
        case lrn_param_t::algorithm::lrn_across_channels:
            return mkldnn::lrn_across_channels;
        case lrn_param_t::algorithm::lrn_within_channel:
            return mkldnn::lrn_within_channel;
        default:
            LOG(ERROR) << "Not a valid lrn algo";
            return mkldnn::lrn_across_channels;
    }
}

template<typename T, typename U>
inline T div_up(const T a, const U b) {
    assert(b);
    return(a + b - 1) / b;
}
template <typename T, typename U>
inline void balance211(T n, U team, U tid, T &n_start, T &n_end) {
    T n_min = 1;
    T &n_my = n_end;
    if (team <= 1 || n == 0) {
        n_start = 0;
        n_my = n;
    } else if (n_min == 1) {
        // team = T1 + T2
        // n = T1*n1 + T2*n2  (n1 - n2 = 1)
        T n1 = div_up(n, (T)team);
        T n2 = n1 - 1;
        T T1 = n - n2 * (T)team;
        n_my = (T)tid < T1 ? n1 : n2; 
        n_start = (T)tid <= T1 ? tid * n1 : T1 * n1 + ((T)tid - T1) * n2; 
    }   

    n_end += n_start;
}

inline void fast_memcpy(char* data_o, char *data_i, int len)
{
    size_t nelems_float = len / 4;
    size_t nelems_char = len % 4;
    const int block_size = 16; 
    const auto num_blocks_float = nelems_float / block_size;
    const auto rem_elems_float =  nelems_float % block_size;
    float* output_f = (float*)data_o;
    float* input_f = (float*) data_i;
    char* output_c = (char*) data_o;
    char* input_c = (char*) data_i;
#   pragma omp parallel
    {   
        const int ithr = omp_get_thread_num();
        const int nthr = omp_get_num_threads();
        size_t start{0}, end{0};
        balance211(num_blocks_float, nthr, ithr, start, end);  
        start = start * block_size;
        end = end * block_size;
#       pragma omp simd
        for (size_t e = start; e < end; ++e) {
            output_f[e] = input_f[e];
        }   
        if (rem_elems_float != 0 && ithr ==  nthr -1 )  {
            for (int e = nelems_float - rem_elems_float; e < nelems_float; ++e) {
                output_f[e] = input_f[e];
            }   
        }   
        if (nelems_char != 0 && ithr ==  nthr -1){
            for (int e = nelems_float*4; e < len; ++e) {
                output_c[e] = input_c[e];
            }   
        }   
    }   
    return;
}

#endif // _UTILS_H_
