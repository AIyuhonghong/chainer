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


#include <glog/logging.h>
#include <iostream>
#include "common.h"
#include "mkldnn.hpp"
#include "tensor.h"
#include "mem.h"
#include "concat.h"
#include "utils.h"
#include "concat_fwd.h"
#include "prim_factory.h"
#include "reorder_op.h"

using namespace mkldnn;

extern engine cpu_engine;

template<typename T>
Concat<T>::Concat()
{
}

template<typename T>
Concat<T>::~Concat()
{
}

template<typename T>
Tensor *Concat<T>::Forward(
                std::vector<Tensor*> src,
                int axis)
{
    // sanity check
    assert (axis == 1); // currently, only support concat as channel
    assert (src.size() > 0);

    std::vector<mkldnn::memory::format> src_fmts;
    std::vector<mkldnn::memory::format> expected_fmts;
    std::vector<void*> src_datas;
    std::vector<void*> src_reorder;

    std::vector<mkldnn::memory::dims> src_ds;
    mkldnn::memory::dims dst_d;

    //get output channel
    int out_channel = 0;
    for (int i = 0; i < src.size(); i++) {
        //get relate infor from src
        src_fmts.push_back(src[i]->cxx_format());
        src_datas.push_back(src[i]->data());
        src_reorder.push_back(src[i]->data());

        src_ds.push_back(src[i]->cxx_dims());
        out_channel += (src[i]->cxx_dims())[1];
    }
    dst_d = {src_ds[0][0], out_channel, src_ds[0][2], src_ds[0][3]};
    LOG(INFO) << "dst_d={" << dst_d[0] << "," << dst_d[1] << "," << dst_d[2] << "," << dst_d[3] << "}";
    
    // get a concat fwd from primitive pool
    ConcatFwd<T> *concat_forward = NULL;
    concat_forward = ConcatFwdFactory<T>::get(src_ds, dst_d, axis);

    // check wehther fmt is same
    expected_fmts = concat_forward->src_fmts_;
    assert(src_fmts.size() == expected_fmts.size());

    for (int i = 0; i < expected_fmts.size(); i++) {
        if ( src_fmts[i] != expected_fmts[i]) {
            LOG(INFO) << "Concat src fmt not match ("<< i << "):"
                "src_fmt=" << src_fmts[i] <<
                "; expected_fmt="<< expected_fmts[i];
            // From reorder factory to find one reorder
            ReorderOp<T>* reorder_src_op = ReorderFactory<T>::get(src_ds[i], src_fmts[i], expected_fmts[i]);
            src_reorder[i] = new avx::byte[src[i]->len()];
            reorder_src_op->execute(src_datas[i], src_reorder[i]);
        }
    }

    // create tensor based on primitive's dst 
    // assume dst and src have same data type
    // Tensor *dst_tensor = new Tensor(dst_d, src[0]->cxx_data_type(), concat_forward->dst_fmt_, cpu_engine);
    auto data = Allocator::malloc(dst_d, type2size(src[0]->type()), MPOOL_CONCAT_FWD);
    Tensor *dst_tensor = new Tensor(dst_d.size(), dst_d, data,
            (mkldnn_memory_format_t)concat_forward->dst_fmt_,
            src[0]->type());
    
    // do forward
    concat_forward->execute(src_reorder, dst_tensor->data());

    //FIXME here may cause performance issue
    for (int i = 0; i < src_reorder.size(); i++) {
        if (src_reorder[i] != src_datas[i]) {
            // means reorder happen
            delete static_cast<avx::byte *>(src_reorder[i]);
        }
    }

    return dst_tensor;
}


template<typename T>
std::vector<Tensor*> Concat<T>::Backward(
                Tensor *diff_dst,
                std::vector<int> offsets,
                int axis)
{
    //
    assert (axis == 1);
    assert (offsets.size() > 0);

    std::vector<Tensor*> gxs;
    std::vector<void*> gxs_data;

    mkldnn::memory::format expected_dst_fmt; // expected format
    void *diff_dst_data = NULL;
    void *diff_dst_reorder = NULL; 

    // get diff src fmts
    // offset store the offsets of concat
    // Example
    // inputs: [2, 2, 3, 3], [2, 3, 3, 3], [2, 1, 3, 3], [2, 1, 3, 3]
    // outputs: [2, 7, 3, 3]
    // offsets: [2, 5, 6]
    std::vector<mkldnn::memory::dims> diff_src_d;
    mkldnn::memory::dims diff_dst_d = diff_dst->cxx_dims();
    diff_src_d.push_back({diff_dst_d[0], offsets[0], diff_dst_d[2], diff_dst_d[3]});//get first element
    for (int i = 1; i < offsets.size(); i++) {
        int axis_value = offsets[i] - offsets[i-1]; 
        diff_src_d.push_back({diff_dst_d[0], axis_value, diff_dst_d[2], diff_dst_d[3]});
    }
    diff_src_d.push_back({diff_dst_d[0], (diff_dst_d[1]-offsets.back()), diff_dst_d[2], diff_dst_d[3]}); // get last element

    // get a concat bwd from primitive pool
    ConcatBwd<T> *concat_backward = NULL;
    concat_backward = ConcatBwdFactory<T>::get(diff_src_d, diff_dst_d, axis);

    //check whether diff dst fmt is same
    expected_dst_fmt = concat_backward->diff_dst_fmt_;
    diff_dst_data = diff_dst->data();
    if (expected_dst_fmt != diff_dst->cxx_format()) {
        LOG(INFO) << "Concat diff dst fmt not match: diff_dst_fmt="
            << diff_dst->cxx_format() << "; expected fmt = " << expected_dst_fmt;

        // From reorder factory to find one reorder
        ReorderOp<T>* reorder_diff_dst_op = ReorderFactory<T>::get(diff_dst->cxx_dims(), diff_dst->cxx_format(), expected_dst_fmt);
        diff_dst_reorder = new avx::byte[diff_dst->len()];
        reorder_diff_dst_op->execute(diff_dst_data, diff_dst_reorder);
        diff_dst_data = diff_dst_reorder;
    }

    // create diff src tensors to execute concat backward
    assert(diff_src_d.szie() == concat_backward->diff_src_fmts_.size());
    for (int i = 0; i < diff_src_d.size(); i++) {
        // Tensor *diff_src_tensor = new Tensor(diff_src_d[i], diff_dst->cxx_data_type(), concat_backward->diff_src_fmts_[i], cpu_engine);
        auto data = Allocator::malloc(diff_src_d[i], type2size(diff_dst->type()), MPOOL_CONCAT_BWD);
        Tensor *diff_src_tensor = new Tensor(diff_src_d[i].size(), diff_src_d[i], data,
                                        (mkldnn_memory_format_t)concat_backward->diff_src_fmts_[i],
                                        diff_dst->type());
        gxs.push_back(diff_src_tensor);
        gxs_data.push_back(diff_src_tensor->data());
    }

    // do concat backward
    concat_backward->execute(gxs_data, diff_dst_data);

    //
    if (diff_dst_reorder != NULL)
        delete static_cast<avx::byte *>(diff_dst_reorder);

    return gxs;
}

template class Concat<float>;


// vim: et ts=4 sw=4 cindent cino^=l0,\:0,N-s
