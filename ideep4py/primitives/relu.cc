/*
 *COPYRIGHT
 *All modification made by Intel Corporation: © 2017 Intel Corporation.
 *Copyright (c) 2015 Preferred Infrastructure, Inc.
 *Copyright (c) 2015 Preferred Networks, Inc.
 *
 *Permission is hereby granted, free of charge, to any person obtaining a copy
 *of this software and associated documentation files (the "Software"), to deal
 *in the Software without restriction, including without limitation the rights
 *to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *copies of the Software, and to permit persons to whom the Software is
 *furnished to do so, subject to the following conditions:
 *
 *The above copyright notice and this permission notice shall be included in
 *all copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *THE SOFTWARE.
 *
 */


#include <mkldnn.hpp>
#include <vector>
#include <memory>
#include "layer.h"
#include "tensor.h"
#include "relu.h"
#include "relu_fwd.h"
#include "relu_bwd.h"
#include "prim_factory.h"
#include "reorder_op.h"

using namespace mkldnn;

const mkldnn::memory::dims NONE_DIMS = {};
extern engine cpu_engine;

template<typename T>
Relu<T>::Relu()
{
}

template<typename T>
Relu<T>::~Relu()
{
}

template<typename T>
Tensor *Relu<T>::Forward(Tensor *src)
{
    //sanity check for data type
    assert(memory_data_type<T>() == src.cxx_data_type());

    // get a relu fwd from primitive pool
    EltwiseFwd<T, float> *relu_fwd = nullptr;
    // FIXME: in this model, every call to relu_fwd will create a new tensor, when to free???
    mkldnn::memory::format src_fmt = src->cxx_format(); // src fmt in tensor
    relu_fwd = EltwiseFwdFactory<T, float>::get(src->dims(), mkldnn::eltwise_relu, src_fmt, 0.0, 0.0);

    // create tensor based on primitive's dst 
    // assume dst and src have same data type
    auto data = Allocator::malloc(src->dims(), type2size(src->type()), MPOOL_RELU_FWD);
    Tensor *dst_tensor = new Tensor(src->ndims(), src->dims(), data,
            (mkldnn_memory_format_t)relu_fwd->dst_fmt_,
            src->type());

    // do forward
    relu_fwd->execute(src->data(), dst_tensor->data());

    return dst_tensor;
}

template<typename T>
Tensor *Relu<T>::Backward(Tensor *src, Tensor *diff_dst)
{
    // sanity check for data type
    assert(memory_data_type<T>() == diff_dst->cxx_data_type());
    assert(src->ndims() == diff_dst->ndims());
    assert(src->size() == diff_dst->size());

    // get a relu bwd data from primitive pool
    EltwiseBwd<T, float> *relu_bwd = nullptr;
    relu_bwd = EltwiseBwdFactory<T, float>::get(diff_dst->dims(), mkldnn::eltwise_relu, diff_dst->cxx_format(), 0.0, 0.0);

    void *src_buf = src->data();

    if (src->cxx_format() != diff_dst->cxx_format()) {
        //LOG(INFO) << "relu bwd data fmt not match, need to reorder";
        //LOG(INFO) << "diff_dst_fmt=" << diff_dst->cxx_format() <<", src format=" << src->cxx_format();
        ReorderOp<T>* reorder_src_op = ReorderFactory<T>::get(src->dims(), src->cxx_format(), diff_dst->cxx_format());
        //src_reorder = new avx::byte[diff_dst->len()];
        auto src_reorder = Allocator::malloc(diff_dst->len(), MPOOL_REORDER);
        reorder_src_op->execute(src_buf, src_reorder.get());
        src_buf = static_cast<void *>(src_reorder.get());
    }

    // create tensor based on selected primitive
    // assume dst and src have same data type
    auto data = Allocator::malloc(src->dims(), type2size(src->type()), MPOOL_RELU_BWD);
    Tensor *diff_src = new Tensor(src->ndims(), src->dims(), data,
                                    (mkldnn_memory_format_t)relu_bwd->src_diff_fmt_,
                                    src->type());
    
    relu_bwd->execute(src_buf, diff_dst->data(), diff_src->data());

    return diff_src;
}

template class Relu<float>;

// vim: et ts=4 sw=4 cindent cino^=l0,\:0,N-s